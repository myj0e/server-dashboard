#include "sampler.h"
#include <unistd.h>

static void *sampler_thread_fn(void *arg) {
    sampler_t *s = (sampler_t *)arg;

    while (s->running) {
        monitor_snapshot_t *snap = snapshot_store_acquire_next(s->store);
        if (!snap) {
            usleep((useconds_t)s->config->sample_interval_ms * 1000);
            continue;
        }

        int64_t now = current_time_ms();
        snap->sampled_at_ms = now;
        snap->sample_interval_ms = (uint32_t)s->config->sample_interval_ms;

        /* collect system summary + processes */
        snap->processes.sampled_at_ms = now;
        process_probe_collect(s->proc_probe, &snap->processes,
                              &snap->summary, &snap->process_status, s->config);

        /* collect GPU data */
        gpu_probe_collect(s->gpu_probe, &snap->gpus, &snap->gpu_processes,
                          &snap->processes, &snap->gpu_status);

        /* publish snapshot */
        snapshot_store_publish(s->store, NULL);

        usleep((useconds_t)s->config->sample_interval_ms * 1000);
    }

    return NULL;
}

int sampler_start(sampler_t *s) {
    s->running = true;
    int rc = pthread_create(&s->thread, NULL, sampler_thread_fn, s);
    if (rc != 0) {
        fprintf(stderr, "Error: failed to create sampler thread: %s\n", strerror(rc));
        return -1;
    }
    return 0;
}

void sampler_stop(sampler_t *s) {
    s->running = false;
    pthread_join(s->thread, NULL);
}
