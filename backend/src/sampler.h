#ifndef SAMPLER_H
#define SAMPLER_H

#include "platform.h"
#include "snapshot.h"
#include "config.h"
#include "process_probe.h"
#include "gpu_probe_nvml.h"

typedef struct {
    volatile bool running;
    pthread_t thread;
    snapshot_store_t *store;
    const config_t *config;
    process_probe_t *proc_probe;
    gpu_probe_t *gpu_probe;
} sampler_t;

int  sampler_start(sampler_t *s);
void sampler_stop(sampler_t *s);

#endif
