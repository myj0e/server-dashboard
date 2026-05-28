#include "json_writer.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

char *json_doc_to_string(yyjson_mut_doc *doc, yyjson_mut_val *root) {
    yyjson_mut_doc_set_root(doc, root);
    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG;
    char *json = yyjson_mut_write(doc, flags, NULL);
    if (!json) return NULL;
    size_t len = strlen(json);
    while (len > 0 && (json[len - 1] == '\n' || json[len - 1] == '\r')) {
        json[--len] = '\0';
    }
    return json;
}

yyjson_mut_val *json_build_success(yyjson_mut_doc *doc, yyjson_mut_val *data,
                                   uint64_t sequence, int64_t sampled_at_ms,
                                   uint32_t sample_interval_ms) {
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_val(doc, root, "data", data);

    yyjson_mut_val *meta = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, meta, "sequence", sequence);
    yyjson_mut_obj_add_sint(doc, meta, "sampled_at_ms", sampled_at_ms);
    yyjson_mut_obj_add_uint(doc, meta, "sample_interval_ms", sample_interval_ms);
    yyjson_mut_obj_add_val(doc, root, "meta", meta);

    return root;
}

yyjson_mut_val *json_build_error(yyjson_mut_doc *doc, const char *code, const char *message) {
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, root, "ok", false);
    yyjson_mut_val *err = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, err, "code", code);
    yyjson_mut_obj_add_strcpy(doc, err, "message", message);
    yyjson_mut_obj_add_val(doc, root, "error", err);
    return root;
}

static void format_bytes(char *buf, size_t bufsz, uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int ui = 0;
    double val = (double)bytes;
    while (val >= 1024.0 && ui < 4) {
        val /= 1024.0;
        ui++;
    }
    snprintf(buf, bufsz, "%.2f %s", val, units[ui]);
}

yyjson_mut_val *json_write_config(yyjson_mut_doc *doc, const config_t *cfg) {
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, obj, "listen_host", cfg->listen_host);
    yyjson_mut_obj_add_int(doc, obj, "listen_port", cfg->listen_port);
    yyjson_mut_obj_add_int(doc, obj, "sample_interval_ms", cfg->sample_interval_ms);
    yyjson_mut_obj_add_bool(doc, obj, "enable_gpu", cfg->enable_gpu);
    yyjson_mut_obj_add_bool(doc, obj, "enable_process_ops", cfg->enable_process_ops);
    yyjson_mut_obj_add_bool(doc, obj, "enable_auth", cfg->enable_auth);
    return obj;
}

yyjson_mut_val *json_write_system_summary(yyjson_mut_doc *doc, const system_summary_t *s) {
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_sint(doc, obj, "sampled_at_ms", s->sampled_at_ms);
    yyjson_mut_obj_add_uint(doc, obj, "online_cpus", s->online_cpus);
    yyjson_mut_obj_add_real(doc, obj, "cpu_percent", s->cpu_percent);
    yyjson_mut_obj_add_uint(doc, obj, "total_ram_bytes", s->total_ram_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "used_ram_bytes", s->used_ram_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "total_swap_bytes", s->total_swap_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "used_swap_bytes", s->used_swap_bytes);
    yyjson_mut_obj_add_int(doc, obj, "process_count", s->process_count);
    yyjson_mut_obj_add_int(doc, obj, "running_count", s->running_count);
    yyjson_mut_obj_add_int(doc, obj, "sleeping_count", s->sleeping_count);
    yyjson_mut_obj_add_int(doc, obj, "zombie_count", s->zombie_count);

    char buf[64];
    format_bytes(buf, sizeof(buf), s->total_ram_bytes);
    yyjson_mut_obj_add_strcpy(doc, obj, "total_ram_human", buf);
    format_bytes(buf, sizeof(buf), s->used_ram_bytes);
    yyjson_mut_obj_add_strcpy(doc, obj, "used_ram_human", buf);

    return obj;
}

yyjson_mut_val *json_write_process_info(yyjson_mut_doc *doc, const process_info_t *p) {
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, obj, "pid", p->pid);
    yyjson_mut_obj_add_int(doc, obj, "ppid", p->ppid);
    yyjson_mut_obj_add_uint(doc, obj, "uid", p->uid);
    yyjson_mut_obj_add_strcpy(doc, obj, "user", p->user);
    yyjson_mut_obj_add_strcpy(doc, obj, "name", p->name);

    char state_str[2] = {p->state, '\0'};
    yyjson_mut_obj_add_strcpy(doc, obj, "state", state_str);

    if (p->cmdline) {
        yyjson_mut_obj_add_strcpy(doc, obj, "cmdline", p->cmdline);
    } else {
        yyjson_mut_obj_add_null(doc, obj, "cmdline");
    }

    yyjson_mut_obj_add_real(doc, obj, "cpu_percent", p->cpu_percent);
    yyjson_mut_obj_add_uint(doc, obj, "rss_bytes", p->rss_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "vsize_bytes", p->vsize_bytes);
    yyjson_mut_obj_add_int(doc, obj, "thread_count", p->thread_count);
    yyjson_mut_obj_add_int(doc, obj, "priority", p->priority);
    yyjson_mut_obj_add_int(doc, obj, "nice", p->nice_val);
    yyjson_mut_obj_add_sint(doc, obj, "start_time_ms", p->start_time_ms);
    yyjson_mut_obj_add_sint(doc, obj, "running_duration_ms", p->running_duration_ms);

    char buf[64];
    format_bytes(buf, sizeof(buf), p->rss_bytes);
    yyjson_mut_obj_add_strcpy(doc, obj, "rss_human", buf);
    format_bytes(buf, sizeof(buf), p->vsize_bytes);
    yyjson_mut_obj_add_strcpy(doc, obj, "vsize_human", buf);

    if (p->has_io) {
        yyjson_mut_obj_add_uint(doc, obj, "read_bytes", p->read_bytes);
        yyjson_mut_obj_add_uint(doc, obj, "write_bytes", p->write_bytes);
    } else {
        yyjson_mut_obj_add_null(doc, obj, "read_bytes");
        yyjson_mut_obj_add_null(doc, obj, "write_bytes");
    }
    yyjson_mut_obj_add_bool(doc, obj, "has_io", p->has_io);

    return obj;
}

yyjson_mut_val *json_write_process_list(yyjson_mut_doc *doc, const process_list_t *pl) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < pl->count; i++) {
        yyjson_mut_val *item = json_write_process_info(doc, &pl->items[i]);
        yyjson_mut_arr_append(arr, item);
    }
    return arr;
}

yyjson_mut_val *json_write_gpu_info(yyjson_mut_doc *doc, const gpu_info_t *gi) {
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, obj, "index", gi->index);
    yyjson_mut_obj_add_strcpy(doc, obj, "uuid", gi->uuid);
    yyjson_mut_obj_add_strcpy(doc, obj, "name", gi->name);
    yyjson_mut_obj_add_uint(doc, obj, "gpu_util_percent", gi->gpu_util_percent);
    yyjson_mut_obj_add_uint(doc, obj, "mem_util_percent", gi->mem_util_percent);
    yyjson_mut_obj_add_uint(doc, obj, "mem_total_bytes", gi->mem_total_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "mem_used_bytes", gi->mem_used_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "mem_free_bytes", gi->mem_free_bytes);
    yyjson_mut_obj_add_uint(doc, obj, "temperature_c", gi->temperature_c);
    yyjson_mut_obj_add_uint(doc, obj, "power_mw", gi->power_mw);
    yyjson_mut_obj_add_uint(doc, obj, "power_limit_mw", gi->power_limit_mw);
    yyjson_mut_obj_add_uint(doc, obj, "fan_speed_percent", gi->fan_speed_percent);
    yyjson_mut_obj_add_bool(doc, obj, "has_power", gi->has_power);
    yyjson_mut_obj_add_bool(doc, obj, "has_power_limit", gi->has_power_limit);
    yyjson_mut_obj_add_bool(doc, obj, "has_fan_speed", gi->has_fan_speed);
    yyjson_mut_obj_add_strcpy(doc, obj, "driver_version", gi->driver_version);
    yyjson_mut_obj_add_strcpy(doc, obj, "nvml_version", gi->nvml_version);

    char buf[64];
    format_bytes(buf, sizeof(buf), gi->mem_total_bytes);
    yyjson_mut_obj_add_strcpy(doc, obj, "mem_total_human", buf);
    format_bytes(buf, sizeof(buf), gi->mem_used_bytes);
    yyjson_mut_obj_add_strcpy(doc, obj, "mem_used_human", buf);

    return obj;
}

yyjson_mut_val *json_write_gpu_list(yyjson_mut_doc *doc, const gpu_list_t *gl) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < gl->count; i++) {
        yyjson_mut_val *item = json_write_gpu_info(doc, &gl->items[i]);
        yyjson_mut_arr_append(arr, item);
    }
    return arr;
}

yyjson_mut_val *json_write_gpu_process_list(yyjson_mut_doc *doc, const gpu_process_list_t *gpl) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < gpl->count; i++) {
        gpu_process_info_t *gp = &gpl->items[i];
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, obj, "gpu_index", gp->gpu_index);
        yyjson_mut_obj_add_strcpy(doc, obj, "gpu_uuid", gp->gpu_uuid);
        yyjson_mut_obj_add_int(doc, obj, "pid", gp->pid);
        yyjson_mut_obj_add_uint(doc, obj, "gpu_memory_bytes", gp->gpu_memory_bytes);
        yyjson_mut_obj_add_strcpy(doc, obj, "process_type", gp->process_type);
        yyjson_mut_obj_add_bool(doc, obj, "enriched", gp->enriched);
        if (gp->enriched) {
            yyjson_mut_obj_add_strcpy(doc, obj, "user", gp->user);
            yyjson_mut_obj_add_strcpy(doc, obj, "name", gp->name);
            yyjson_mut_obj_add_strcpy(doc, obj, "cmdline", gp->cmdline);
            yyjson_mut_obj_add_real(doc, obj, "cpu_percent", gp->cpu_percent);
            yyjson_mut_obj_add_uint(doc, obj, "rss_bytes", gp->rss_bytes);
            yyjson_mut_obj_add_uint(doc, obj, "vsize_bytes", gp->vsize_bytes);
        }
        yyjson_mut_arr_append(arr, obj);
    }
    return arr;
}

yyjson_mut_val *json_write_probe_status(yyjson_mut_doc *doc, const probe_status_info_t *ps) {
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    const char *status_str;
    switch (ps->status) {
    case PROBE_OK:         status_str = "ok"; break;
    case PROBE_DISABLED:   status_str = "disabled"; break;
    case PROBE_ERROR:      status_str = "error"; break;
    case PROBE_WARMING_UP: status_str = "warming_up"; break;
    default:               status_str = "unknown"; break;
    }
    yyjson_mut_obj_add_strcpy(doc, obj, "status", status_str);
    yyjson_mut_obj_add_strcpy(doc, obj, "error_message", ps->error_message);
    yyjson_mut_obj_add_sint(doc, obj, "last_ok_sample_ms", ps->last_ok_sample_ms);
    return obj;
}
