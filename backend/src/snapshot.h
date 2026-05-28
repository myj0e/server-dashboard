#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include "platform.h"

/* ---------- probe status ---------- */

typedef enum {
    PROBE_OK = 0,
    PROBE_DISABLED,
    PROBE_ERROR,
    PROBE_WARMING_UP
} probe_status_t;

typedef struct {
    probe_status_t status;
    char error_message[256];
    int64_t last_ok_sample_ms;
} probe_status_info_t;

/* ---------- system summary ---------- */

typedef struct {
    int64_t sampled_at_ms;
    unsigned int online_cpus;
    double cpu_percent;
    uint64_t total_ram_bytes;
    uint64_t used_ram_bytes;
    uint64_t total_swap_bytes;
    uint64_t used_swap_bytes;
    int process_count;
    int running_count;
    int sleeping_count;
    int zombie_count;
} system_summary_t;

/* ---------- process info ---------- */

typedef struct {
    int pid;
    int ppid;
    uint32_t uid;
    char user[64];
    char name[256];
    char state;
    char *cmdline;
    double cpu_percent;
    uint64_t rss_bytes;
    uint64_t vsize_bytes;
    int thread_count;
    int priority;
    int nice_val;
    uint64_t start_time_ticks;
    int64_t start_time_ms;
    int64_t running_duration_ms;
    uint64_t read_bytes;
    uint64_t write_bytes;
    bool has_io;
} process_info_t;

typedef struct {
    size_t count;
    size_t capacity;
    process_info_t *items;
    int64_t sampled_at_ms;
} process_list_t;

/* ---------- gpu info ---------- */

typedef struct {
    unsigned int index;
    char uuid[96];
    char name[128];
    unsigned int gpu_util_percent;
    unsigned int mem_util_percent;
    uint64_t mem_total_bytes;
    uint64_t mem_used_bytes;
    uint64_t mem_free_bytes;
    unsigned int temperature_c;
    unsigned int power_mw;
    unsigned int power_limit_mw;
    unsigned int fan_speed_percent;
    bool has_power;
    bool has_power_limit;
    bool has_fan_speed;
    char driver_version[64];
    char nvml_version[64];
} gpu_info_t;

typedef struct {
    size_t count;
    size_t capacity;
    gpu_info_t *items;
} gpu_list_t;

/* ---------- gpu process info ---------- */

typedef struct {
    unsigned int gpu_index;
    char gpu_uuid[96];
    int pid;
    uint64_t gpu_memory_bytes;
    char process_type[32];
    bool enriched;
    char user[64];
    char name[256];
    char cmdline[4096];
    double cpu_percent;
    uint64_t rss_bytes;
    uint64_t vsize_bytes;
} gpu_process_info_t;

typedef struct {
    size_t count;
    size_t capacity;
    gpu_process_info_t *items;
} gpu_process_list_t;

/* ---------- top-level snapshot ---------- */

typedef struct {
    uint64_t sequence;
    int64_t sampled_at_ms;
    uint32_t sample_interval_ms;
    system_summary_t summary;
    process_list_t processes;
    gpu_list_t gpus;
    gpu_process_list_t gpu_processes;
    probe_status_info_t process_status;
    probe_status_info_t gpu_status;
} monitor_snapshot_t;

/* ---------- snapshot store ---------- */

typedef struct {
    pthread_rwlock_t rwlock;
    monitor_snapshot_t *current;
    monitor_snapshot_t *next;
} snapshot_store_t;

int  snapshot_store_init(snapshot_store_t *store);
void snapshot_store_destroy(snapshot_store_t *store);

monitor_snapshot_t *snapshot_store_acquire_next(snapshot_store_t *store);
void snapshot_store_publish(snapshot_store_t *store, monitor_snapshot_t *old_snapshot);

const monitor_snapshot_t *snapshot_store_acquire_read(snapshot_store_t *store);
void snapshot_store_release_read(snapshot_store_t *store);

/* allocate and free snapshots */
monitor_snapshot_t *snapshot_alloc(void);
void snapshot_free(monitor_snapshot_t *s);

#endif
