#ifndef GPU_PROBE_NVML_H
#define GPU_PROBE_NVML_H

#include "snapshot.h"
#include "config.h"

typedef struct gpu_probe gpu_probe_t;

gpu_probe_t *gpu_probe_create(void);
void gpu_probe_destroy(gpu_probe_t *p);

int gpu_probe_init(gpu_probe_t *p, const config_t *cfg);
int gpu_probe_collect(gpu_probe_t *p,
                      gpu_list_t *out_gpus,
                      gpu_process_list_t *out_gpu_procs,
                      const process_list_t *proc_list,
                      probe_status_info_t *status);

#endif
