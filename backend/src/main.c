#include "platform.h"
#include "config.h"
#include "snapshot.h"
#include "http_server.h"
#include "sampler.h"
#include "process_probe.h"
#include "gpu_probe_nvml.h"

static volatile sig_atomic_t g_shutdown = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_shutdown = 1;
}

int main(int argc, char **argv) {
    /* 1. Load configuration */
    config_t cfg;
    if (config_load(&cfg, argc, argv) != 0) {
        return 1;
    }
    printf("Configuration loaded from: %s\n", cfg.config_path);
    printf("  listen: %s:%d\n", cfg.listen_host, cfg.listen_port);
    printf("  sample_interval_ms: %d\n", cfg.sample_interval_ms);
    printf("  enable_gpu: %s\n", cfg.enable_gpu ? "true" : "false");

    /* 2. Initialize snapshot store */
    snapshot_store_t store;
    if (snapshot_store_init(&store) != 0) {
        fprintf(stderr, "Error: failed to initialize snapshot store\n");
        return 1;
    }

    /* 3. Initialize probes */
    process_probe_t *proc_probe = process_probe_create();
    if (!proc_probe) {
        fprintf(stderr, "Error: failed to create process probe\n");
        snapshot_store_destroy(&store);
        return 1;
    }

    gpu_probe_t *gpu_probe = gpu_probe_create();
    if (!gpu_probe) {
        fprintf(stderr, "Error: failed to create GPU probe\n");
        process_probe_destroy(proc_probe);
        snapshot_store_destroy(&store);
        return 1;
    }
    gpu_probe_init(gpu_probe, &cfg);

    /* 4. Start sampler thread */
    sampler_t sampler = {
        .running = false,
        .store = &store,
        .config = &cfg,
        .proc_probe = proc_probe,
        .gpu_probe = gpu_probe,
    };
    if (sampler_start(&sampler) != 0) {
        gpu_probe_destroy(gpu_probe);
        process_probe_destroy(proc_probe);
        snapshot_store_destroy(&store);
        return 1;
    }
    printf("Sampler thread started\n");

    /* 5. Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 6. Start HTTP server (runs in main thread) */
    http_server_t http_srv;
    printf("Starting HTTP server...\n");
    http_server_start(&http_srv, &cfg, &store, &g_shutdown);

    /* 7. Shutdown */
    printf("Shutting down...\n");
    sampler_stop(&sampler);
    gpu_probe_destroy(gpu_probe);
    process_probe_destroy(proc_probe);
    snapshot_store_destroy(&store);

    printf("Done.\n");
    return 0;
}
