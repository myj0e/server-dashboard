#include "gpu_probe_nvml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAS_NVML
#define NVML_NO_UNVERSIONED_FUNC_DEFS
#include "nvml.h"
#define MAX_GPU_COUNT 32
#define MAX_GPU_PROCESSES 256
#endif

struct gpu_probe {
    bool enabled;
    bool initialized;
#ifdef HAS_NVML
    unsigned int device_count;
    char driver_version[64];
    char nvml_version[64];
    nvmlDevice_t handles[MAX_GPU_COUNT];
#endif
};

gpu_probe_t *gpu_probe_create(void) {
    gpu_probe_t *p = calloc(1, sizeof(*p));
    return p;
}

void gpu_probe_destroy(gpu_probe_t *p) {
    if (!p) return;
#ifdef HAS_NVML
    if (p->initialized) {
        nvmlShutdown();
    }
#endif
    free(p);
}

int gpu_probe_init(gpu_probe_t *p, const config_t *cfg) {
    p->enabled = cfg->enable_gpu;
    if (!cfg->enable_gpu) return 0;

#ifdef HAS_NVML
    nvmlReturn_t ret = nvmlInit_v2();
    if (ret != NVML_SUCCESS) {
        fprintf(stderr, "Warning: NVML init failed: %s\n", nvmlErrorString(ret));
        p->initialized = false;
        return -1;
    }

    p->initialized = true;

    char buf[64];
    ret = nvmlSystemGetDriverVersion(buf, sizeof(buf));
    if (ret == NVML_SUCCESS)
        strncpy(p->driver_version, buf, sizeof(p->driver_version) - 1);
    else
        strncpy(p->driver_version, "unknown", sizeof(p->driver_version) - 1);

    ret = nvmlSystemGetNVMLVersion(buf, sizeof(buf));
    if (ret == NVML_SUCCESS)
        strncpy(p->nvml_version, buf, sizeof(p->nvml_version) - 1);
    else
        strncpy(p->nvml_version, "unknown", sizeof(p->nvml_version) - 1);

    ret = nvmlDeviceGetCount_v2(&p->device_count);
    if (ret != NVML_SUCCESS) {
        fprintf(stderr, "Warning: nvmlDeviceGetCount failed: %s\n", nvmlErrorString(ret));
        p->device_count = 0;
        return -1;
    }

    if (p->device_count > MAX_GPU_COUNT) p->device_count = MAX_GPU_COUNT;

    for (unsigned int i = 0; i < p->device_count; i++) {
        ret = nvmlDeviceGetHandleByIndex_v2(i, &p->handles[i]);
        if (ret != NVML_SUCCESS) {
            fprintf(stderr, "Warning: cannot get handle for GPU %u: %s\n",
                    i, nvmlErrorString(ret));
            p->handles[i] = (nvmlDevice_t)NULL;
        }
    }

    printf("NVML: %u GPU(s) found, driver %s, NVML %s\n",
           p->device_count, p->driver_version, p->nvml_version);
    return 0;
#else
    p->initialized = false;
    return -1;
#endif
}

#ifdef HAS_NVML

static void collect_gpu_device(gpu_probe_t *p, unsigned int idx,
                               nvmlDevice_t handle, gpu_info_t *gi) {
    memset(gi, 0, sizeof(*gi));
    gi->index = idx;

    char buf[96];
    nvmlReturn_t ret = nvmlDeviceGetUUID(handle, buf, sizeof(buf));
    if (ret == NVML_SUCCESS) strncpy(gi->uuid, buf, sizeof(gi->uuid) - 1);
    else snprintf(gi->uuid, sizeof(gi->uuid), "GPU-%u", idx);

    ret = nvmlDeviceGetName(handle, buf, sizeof(buf));
    if (ret == NVML_SUCCESS) strncpy(gi->name, buf, sizeof(gi->name) - 1);
    else snprintf(gi->name, sizeof(gi->name), "Unknown GPU %u", idx);

    nvmlUtilization_t util;
    ret = nvmlDeviceGetUtilizationRates(handle, &util);
    if (ret == NVML_SUCCESS) {
        gi->gpu_util_percent = util.gpu;
        gi->mem_util_percent = util.memory;
    }

    nvmlMemory_t mem;
    ret = nvmlDeviceGetMemoryInfo(handle, &mem);
    if (ret == NVML_SUCCESS) {
        gi->mem_total_bytes = mem.total;
        gi->mem_used_bytes = mem.used;
        gi->mem_free_bytes = mem.free;
    }

    unsigned int temp;
    ret = nvmlDeviceGetTemperature(handle, NVML_TEMPERATURE_GPU, &temp);
    if (ret == NVML_SUCCESS) gi->temperature_c = temp;

    unsigned int power;
    ret = nvmlDeviceGetPowerUsage(handle, &power);
    if (ret == NVML_SUCCESS) {
        gi->power_mw = power;
        gi->has_power = true;
    }

    unsigned int power_limit;
    ret = nvmlDeviceGetEnforcedPowerLimit(handle, &power_limit);
    if (ret == NVML_SUCCESS) {
        gi->power_limit_mw = power_limit;
        gi->has_power_limit = true;
    }

    unsigned int fan_speed;
    ret = nvmlDeviceGetFanSpeed(handle, &fan_speed);
    if (ret == NVML_SUCCESS) {
        gi->fan_speed_percent = fan_speed;
        gi->has_fan_speed = true;
    }

    strncpy(gi->driver_version, p->driver_version, sizeof(gi->driver_version) - 1);
    strncpy(gi->nvml_version, p->nvml_version, sizeof(gi->nvml_version) - 1);
}

static unsigned int collect_gpu_processes_for_device(gpu_probe_t *p,
                                                      nvmlDevice_t handle,
                                                      unsigned int gpu_idx,
                                                      const char *gpu_uuid,
                                                      gpu_process_info_t *out,
                                                      unsigned int max_out) {
    unsigned int count = 0;

    unsigned int info_count = max_out - count;
    if (info_count == 0) return count;

    nvmlProcessInfo_t proc_infos[MAX_GPU_PROCESSES];

    nvmlReturn_t ret = nvmlDeviceGetComputeRunningProcesses_v3(handle, &info_count, proc_infos);
    if (ret == NVML_SUCCESS) {
        for (unsigned int i = 0; i < info_count && count < max_out; i++) {
            gpu_process_info_t *gp = &out[count];
            memset(gp, 0, sizeof(*gp));
            gp->gpu_index = gpu_idx;
            strncpy(gp->gpu_uuid, gpu_uuid, sizeof(gp->gpu_uuid) - 1);
            gp->pid = (int)proc_infos[i].pid;
            gp->gpu_memory_bytes = proc_infos[i].usedGpuMemory;
            strncpy(gp->process_type, "C", sizeof(gp->process_type) - 1);
            count++;
        }
    }

    info_count = max_out - count;
    if (info_count > 0) {
        ret = nvmlDeviceGetGraphicsRunningProcesses_v3(handle, &info_count, proc_infos);
        if (ret == NVML_SUCCESS) {
            for (unsigned int i = 0; i < info_count && count < max_out; i++) {
                bool exists = false;
                for (unsigned int j = 0; j < count; j++) {
                    if (out[j].pid == (int)proc_infos[i].pid) {
                        size_t len = strlen(out[j].process_type);
                        if (len < sizeof(out[j].process_type) - 2) {
                            out[j].process_type[len] = '+';
                            out[j].process_type[len + 1] = 'G';
                            out[j].process_type[len + 2] = '\0';
                        }
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    gpu_process_info_t *gp = &out[count];
                    memset(gp, 0, sizeof(*gp));
                    gp->gpu_index = gpu_idx;
                    strncpy(gp->gpu_uuid, gpu_uuid, sizeof(gp->gpu_uuid) - 1);
                    gp->pid = (int)proc_infos[i].pid;
                    gp->gpu_memory_bytes = proc_infos[i].usedGpuMemory;
                    strncpy(gp->process_type, "G", sizeof(gp->process_type) - 1);
                    count++;
                }
            }
        }
    }

    return count;
    (void)p;
}

#endif /* HAS_NVML */

static void enrich_gpu_process(gpu_process_info_t *gp, const process_list_t *proc_list) {
    gp->enriched = false;
    gp->user[0] = '\0';
    gp->name[0] = '\0';
    gp->cmdline[0] = '\0';

    for (size_t i = 0; i < proc_list->count; i++) {
        if (proc_list->items[i].pid == gp->pid) {
            process_info_t *pi = &proc_list->items[i];
            gp->enriched = true;
            strncpy(gp->user, pi->user, sizeof(gp->user) - 1);
            strncpy(gp->name, pi->name, sizeof(gp->name) - 1);
            if (pi->cmdline)
                strncpy(gp->cmdline, pi->cmdline, sizeof(gp->cmdline) - 1);
            gp->cpu_percent = pi->cpu_percent;
            gp->rss_bytes = pi->rss_bytes;
            gp->vsize_bytes = pi->vsize_bytes;
            return;
        }
    }
}

int gpu_probe_collect(gpu_probe_t *p, gpu_list_t *out_gpus,
                      gpu_process_list_t *out_gpu_procs,
                      const process_list_t *proc_list,
                      probe_status_info_t *status) {
    out_gpus->count = 0;
    out_gpu_procs->count = 0;

    if (!p->enabled) {
        status->status = PROBE_DISABLED;
        strncpy(status->error_message, "GPU monitoring is disabled", 255);
        return 0;
    }

#ifdef HAS_NVML
    if (!p->initialized) {
        status->status = PROBE_ERROR;
        strncpy(status->error_message, "NVML initialization failed", 255);
        return 0;
    }

    for (unsigned int i = 0; i < p->device_count; i++) {
        if (p->handles[i] == (nvmlDevice_t)NULL) continue;
        if (out_gpus->count >= out_gpus->capacity) {
            size_t new_cap = out_gpus->capacity * 2;
            gpu_info_t *new_items = realloc(out_gpus->items, new_cap * sizeof(gpu_info_t));
            if (!new_items) break;
            memset(new_items + out_gpus->capacity, 0,
                   (new_cap - out_gpus->capacity) * sizeof(gpu_info_t));
            out_gpus->items = new_items;
            out_gpus->capacity = new_cap;
        }
        collect_gpu_device(p, i, p->handles[i], &out_gpus->items[out_gpus->count]);
        out_gpus->count++;
    }

    for (unsigned int i = 0; i < p->device_count; i++) {
        if (p->handles[i] == (nvmlDevice_t)NULL) continue;
        const char *uuid = out_gpus->items[i].uuid;
        unsigned int remaining = (unsigned int)(out_gpu_procs->capacity - out_gpu_procs->count);
        if (remaining == 0) {
            size_t new_cap = out_gpu_procs->capacity * 2;
            gpu_process_info_t *new_items = realloc(out_gpu_procs->items,
                                                      new_cap * sizeof(gpu_process_info_t));
            if (!new_items) break;
            memset(new_items + out_gpu_procs->capacity, 0,
                   (new_cap - out_gpu_procs->capacity) * sizeof(gpu_process_info_t));
            out_gpu_procs->items = new_items;
            out_gpu_procs->capacity = new_cap;
            remaining = (unsigned int)(out_gpu_procs->capacity - out_gpu_procs->count);
        }
        unsigned int added = collect_gpu_processes_for_device(
            p, p->handles[i], i, uuid,
            &out_gpu_procs->items[out_gpu_procs->count], remaining);
        out_gpu_procs->count += added;
    }

    for (size_t i = 0; i < out_gpu_procs->count; i++)
        enrich_gpu_process(&out_gpu_procs->items[i], proc_list);

    status->status = PROBE_OK;
    status->error_message[0] = '\0';
    status->last_ok_sample_ms = current_time_ms();
#else
    status->status = PROBE_ERROR;
    strncpy(status->error_message,
            "GPU monitoring unavailable: nvml.h not found at build time", 255);
#endif
    return 0;
}
