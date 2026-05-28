#include "api_handlers.h"
#include "json_writer.h"
#include "ops_stub.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool mg_str_eq(struct mg_str s, const char *lit) {
    size_t len = strlen(lit);
    return s.len == len && memcmp(s.buf, lit, len) == 0;
}

static bool mg_str_startswith(struct mg_str s, const char *prefix) {
    size_t len = strlen(prefix);
    return s.len >= len && memcmp(s.buf, prefix, len) == 0;
}

static void send_json(struct mg_connection *c, int code,
                      yyjson_mut_doc *doc, yyjson_mut_val *root) {
    char *json = json_doc_to_string(doc, root);
    if (!json) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\","
                      "\"message\":\"JSON serialization failed\"}}\n");
        return;
    }
    mg_http_reply(c, code, "Content-Type: application/json\r\n", "%s\n", json);
    free(json);
}

static void send_error(struct mg_connection *c, int code,
                       const char *err_code, const char *message) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = json_build_error(doc, err_code, message);
    send_json(c, code, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/config ---------- */

static void handle_config(struct mg_connection *c, const config_t *cfg) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = json_write_config(doc, cfg);
    /* config endpoint has no meta (no snapshot yet) */
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_val(doc, root, "data", data);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/system/summary ---------- */

static void handle_system_summary(struct mg_connection *c, snapshot_store_t *store) {
    const monitor_snapshot_t *snap = snapshot_store_acquire_read(store);
    if (!snap) {
        snapshot_store_release_read(store);
        send_error(c, 503, "SNAPSHOT_NOT_READY", "No snapshot available");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, data, "summary", json_write_system_summary(doc, &snap->summary));
    yyjson_mut_obj_add_val(doc, data, "process_probe",
                           json_write_probe_status(doc, &snap->process_status));
    yyjson_mut_obj_add_val(doc, data, "gpu_probe",
                           json_write_probe_status(doc, &snap->gpu_status));

    yyjson_mut_val *root = json_build_success(doc, data, snap->sequence,
                                              snap->sampled_at_ms, snap->sample_interval_ms);
    snapshot_store_release_read(store);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/processes ---------- */

static void handle_processes(struct mg_connection *c, snapshot_store_t *store) {
    const monitor_snapshot_t *snap = snapshot_store_acquire_read(store);
    if (!snap) {
        snapshot_store_release_read(store);
        send_error(c, 503, "SNAPSHOT_NOT_READY", "No snapshot available");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, data, "processes",
                           json_write_process_list(doc, &snap->processes));
    yyjson_mut_obj_add_val(doc, data, "process_probe",
                           json_write_probe_status(doc, &snap->process_status));

    yyjson_mut_val *root = json_build_success(doc, data, snap->sequence,
                                              snap->sampled_at_ms, snap->sample_interval_ms);
    snapshot_store_release_read(store);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/processes/{pid} ---------- */

static void handle_process_by_pid(struct mg_connection *c, snapshot_store_t *store,
                                  const char *pid_str) {
    int pid = atoi(pid_str);
    if (pid <= 0) {
        send_error(c, 400, "INVALID_PID", "Invalid PID");
        return;
    }

    const monitor_snapshot_t *snap = snapshot_store_acquire_read(store);
    if (!snap) {
        snapshot_store_release_read(store);
        send_error(c, 503, "SNAPSHOT_NOT_READY", "No snapshot available");
        return;
    }

    const process_info_t *found = NULL;
    for (size_t i = 0; i < snap->processes.count; i++) {
        if (snap->processes.items[i].pid == pid) {
            found = &snap->processes.items[i];
            break;
        }
    }

    if (!found) {
        snapshot_store_release_read(store);
        send_error(c, 404, "PROCESS_NOT_FOUND", "Process not found");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, data, "process", json_write_process_info(doc, found));

    yyjson_mut_val *root = json_build_success(doc, data, snap->sequence,
                                              snap->sampled_at_ms, snap->sample_interval_ms);
    snapshot_store_release_read(store);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/gpus ---------- */

static void handle_gpus(struct mg_connection *c, snapshot_store_t *store) {
    const monitor_snapshot_t *snap = snapshot_store_acquire_read(store);
    if (!snap) {
        snapshot_store_release_read(store);
        send_error(c, 503, "SNAPSHOT_NOT_READY", "No snapshot available");
        return;
    }

    if (snap->gpu_status.status == PROBE_DISABLED) {
        snapshot_store_release_read(store);
        send_error(c, 200, "GPU_DISABLED", "GPU monitoring is disabled by configuration");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, data, "gpus", json_write_gpu_list(doc, &snap->gpus));
    yyjson_mut_obj_add_val(doc, data, "gpu_probe",
                           json_write_probe_status(doc, &snap->gpu_status));

    yyjson_mut_val *root = json_build_success(doc, data, snap->sequence,
                                              snap->sampled_at_ms, snap->sample_interval_ms);
    snapshot_store_release_read(store);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/gpus/{gpu_id} ---------- */

static void handle_gpu_by_id(struct mg_connection *c, snapshot_store_t *store,
                             const char *id_str) {
    unsigned int gpu_id = (unsigned int)atoi(id_str);

    const monitor_snapshot_t *snap = snapshot_store_acquire_read(store);
    if (!snap) {
        snapshot_store_release_read(store);
        send_error(c, 503, "SNAPSHOT_NOT_READY", "No snapshot available");
        return;
    }

    if (snap->gpu_status.status == PROBE_DISABLED) {
        snapshot_store_release_read(store);
        send_error(c, 200, "GPU_DISABLED", "GPU monitoring is disabled by configuration");
        return;
    }

    const gpu_info_t *found = NULL;
    for (size_t i = 0; i < snap->gpus.count; i++) {
        if (snap->gpus.items[i].index == gpu_id) {
            found = &snap->gpus.items[i];
            break;
        }
    }

    if (!found) {
        snapshot_store_release_read(store);
        send_error(c, 404, "GPU_NOT_FOUND", "GPU not found");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, data, "gpu", json_write_gpu_info(doc, found));

    yyjson_mut_val *root = json_build_success(doc, data, snap->sequence,
                                              snap->sampled_at_ms, snap->sample_interval_ms);
    snapshot_store_release_read(store);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- GET /api/v1/gpu-processes ---------- */

static void handle_gpu_processes(struct mg_connection *c, snapshot_store_t *store) {
    const monitor_snapshot_t *snap = snapshot_store_acquire_read(store);
    if (!snap) {
        snapshot_store_release_read(store);
        send_error(c, 503, "SNAPSHOT_NOT_READY", "No snapshot available");
        return;
    }

    if (snap->gpu_status.status == PROBE_DISABLED) {
        snapshot_store_release_read(store);
        send_error(c, 200, "GPU_DISABLED", "GPU monitoring is disabled by configuration");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, data, "gpu_processes",
                           json_write_gpu_process_list(doc, &snap->gpu_processes));
    yyjson_mut_obj_add_val(doc, data, "gpu_probe",
                           json_write_probe_status(doc, &snap->gpu_status));

    yyjson_mut_val *root = json_build_success(doc, data, snap->sequence,
                                              snap->sampled_at_ms, snap->sample_interval_ms);
    snapshot_store_release_read(store);
    send_json(c, 200, doc, root);
    yyjson_mut_doc_free(doc);
}

/* ---------- POST /api/v1/processes (501) ---------- */

static void handle_not_implemented(struct mg_connection *c) {
    mg_http_reply(c, 501, "Content-Type: application/json\r\n",
                  "{\"ok\":false,\"error\":{\"code\":\"NOT_IMPLEMENTED\","
                  "\"message\":\"Process operations are not available in this version\"}}\n");
}

/* ---------- Router ---------- */

void api_handle_request(struct mg_connection *c, struct mg_http_message *hm,
                        snapshot_store_t *store, const config_t *cfg) {
    struct mg_str uri = hm->uri;
    struct mg_str method = hm->method;

    if (!mg_str_startswith(uri, "/api/v1/")) {
        send_error(c, 404, "NOT_FOUND", "Not found");
        return;
    }

    /* POST process operations -> 501 */
    if (mg_str_eq(method, "POST") && mg_str_startswith(uri, "/api/v1/processes/")) {
        handle_not_implemented(c);
        return;
    }

    if (!mg_str_eq(method, "GET")) {
        send_error(c, 405, "METHOD_NOT_ALLOWED", "Method not allowed");
        return;
    }

    /* GET /api/v1/config */
    if (mg_str_eq(uri, "/api/v1/config")) {
        handle_config(c, cfg);
        return;
    }

    /* GET /api/v1/system/summary */
    if (mg_str_eq(uri, "/api/v1/system/summary")) {
        handle_system_summary(c, store);
        return;
    }

    /* GET /api/v1/processes */
    if (mg_str_eq(uri, "/api/v1/processes")) {
        handle_processes(c, store);
        return;
    }

    /* GET /api/v1/processes/{pid} */
    if (mg_str_startswith(uri, "/api/v1/processes/")) {
        const char *pid_str = uri.buf + strlen("/api/v1/processes/");
        handle_process_by_pid(c, store, pid_str);
        return;
    }

    /* GET /api/v1/gpu-processes (must be before gpus to avoid prefix match) */
    if (mg_str_eq(uri, "/api/v1/gpu-processes")) {
        handle_gpu_processes(c, store);
        return;
    }

    /* GET /api/v1/gpus/{gpu_id} */
    if (mg_str_startswith(uri, "/api/v1/gpus/")) {
        const char *id_str = uri.buf + strlen("/api/v1/gpus/");
        handle_gpu_by_id(c, store, id_str);
        return;
    }

    /* GET /api/v1/gpus */
    if (mg_str_eq(uri, "/api/v1/gpus")) {
        handle_gpus(c, store);
        return;
    }

    send_error(c, 404, "NOT_FOUND", "Not found");
}
