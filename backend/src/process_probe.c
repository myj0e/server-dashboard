#include "process_probe.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <dirent.h>

#define UID_CACHE_SIZE      256
#define MAX_PREV_PIDS       65536
#define PROC_STAT_BUF_SIZE  2048
#define CMDLINE_BUF_SIZE    8192

typedef struct {
    int pid;
    uint64_t start_time_ticks;
    uint64_t prev_ticks;
} prev_pid_entry_t;

typedef struct {
    uint32_t uid;
    char name[64];
    bool valid;
} uid_cache_entry_t;

struct process_probe {
    uint64_t prev_total_cpu_ticks;
    uint64_t prev_total_idle_ticks;
    prev_pid_entry_t *prev_pids;
    size_t prev_pid_capacity;
    size_t prev_pid_count;
    uid_cache_entry_t uid_cache[UID_CACHE_SIZE];
    uint64_t sample_count;
    long clock_ticks_per_sec;
    long online_cpus;
    long page_size;
};

process_probe_t *process_probe_create(void) {
    process_probe_t *p = calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->prev_pid_capacity = MAX_PREV_PIDS;
    p->prev_pids = calloc(p->prev_pid_capacity, sizeof(prev_pid_entry_t));
    if (!p->prev_pids) {
        free(p);
        return NULL;
    }

    p->clock_ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (p->clock_ticks_per_sec <= 0) p->clock_ticks_per_sec = 100;

    p->online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (p->online_cpus <= 0) p->online_cpus = 1;

    p->page_size = sysconf(_SC_PAGESIZE);
    if (p->page_size <= 0) p->page_size = 4096;

    return p;
}

void process_probe_destroy(process_probe_t *p) {
    if (!p) return;
    free(p->prev_pids);
    free(p);
}

/* ---------- UID cache ---------- */

static const char *resolve_username(process_probe_t *p, uint32_t uid) {
    for (int i = 0; i < UID_CACHE_SIZE; i++) {
        if (p->uid_cache[i].valid && p->uid_cache[i].uid == uid) {
            return p->uid_cache[i].name;
        }
    }

    struct passwd pwd_buf;
    struct passwd *result = NULL;
    char buf[4096];
    int rc = getpwuid_r(uid, &pwd_buf, buf, sizeof(buf), &result);

    if (rc == 0 && result != NULL && result->pw_name != NULL) {
        int slot = (int)(uid % UID_CACHE_SIZE);
        p->uid_cache[slot].uid = uid;
        p->uid_cache[slot].valid = true;
        strncpy(p->uid_cache[slot].name, result->pw_name, sizeof(p->uid_cache[slot].name) - 1);
        return p->uid_cache[slot].name;
    }

    return NULL;
}

/* ---------- Previous PID tracking ---------- */

/* Look up the previous ticks for a PID. Returns NULL if no previous data exists
   (new PID or PID reused with different start time). */
static uint64_t *lookup_prev_ticks(process_probe_t *p, int pid, uint64_t start_time_ticks) {
    for (size_t i = 0; i < p->prev_pid_count; i++) {
        if (p->prev_pids[i].pid == pid) {
            if (p->prev_pids[i].start_time_ticks == start_time_ticks) {
                return &p->prev_pids[i].prev_ticks;
            }
            /* PID reused with different start time - reset to new process */
            p->prev_pids[i].start_time_ticks = start_time_ticks;
            p->prev_pids[i].prev_ticks = 0;
            return NULL;
        }
    }
    return NULL;
}

/* Store current ticks for next sample's delta calculation. */
static void store_prev_ticks(process_probe_t *p, int pid, uint64_t start_time_ticks,
                             uint64_t current_ticks) {
    for (size_t i = 0; i < p->prev_pid_count; i++) {
        if (p->prev_pids[i].pid == pid &&
            p->prev_pids[i].start_time_ticks == start_time_ticks) {
            p->prev_pids[i].prev_ticks = current_ticks;
            return;
        }
    }
    if (p->prev_pid_count < p->prev_pid_capacity) {
        size_t idx = p->prev_pid_count++;
        p->prev_pids[idx].pid = pid;
        p->prev_pids[idx].start_time_ticks = start_time_ticks;
        p->prev_pids[idx].prev_ticks = current_ticks;
    }
}

/* ---------- /proc/stat reading ---------- */

static bool read_cpu_ticks(uint64_t *total, uint64_t *idle) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return false;

    char line[1024];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    uint64_t user, nice, system, idle_val, iowait, irq, softirq, steal, guest, guest_nice;
    int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle_val, &iowait, &irq, &softirq,
                   &steal, &guest, &guest_nice);
    if (n < 4) return false;

    *total = user + nice + system + idle_val + iowait + irq + softirq;
    if (n >= 8) *total += steal;
    if (n >= 9) *total += guest;
    if (n >= 10) *total += guest_nice;

    *idle = idle_val + iowait;
    return true;
}

/* ---------- /proc/[pid]/stat parser ---------- */

/* Parse /proc/[pid]/stat into process_info. Returns 0 on success, -1 on failure.
   Also returns total_ticks (utime + stime) for CPU delta calculation. */
static int parse_proc_stat(const char *buf, process_info_t *info,
                           process_probe_t *p, uint64_t *out_total_ticks) {
    const char *close_paren = strrchr(buf, ')');
    if (!close_paren) return -1;

    const char *after = close_paren + 2; /* skip ") " */

    int pid;
    if (sscanf(buf, "%d", &pid) != 1) return -1;
    info->pid = pid;

    /* extract comm */
    const char *open_paren = strchr(buf, '(');
    if (!open_paren || open_paren >= close_paren) return -1;
    size_t comm_len = (size_t)(close_paren - open_paren - 1);
    if (comm_len >= sizeof(info->name)) comm_len = sizeof(info->name) - 1;
    memcpy(info->name, open_paren + 1, comm_len);
    info->name[comm_len] = '\0';

    /* Parse: state(3), ppid(4), skip to utime(14), stime(15),
       priority(18), nice(19), num_threads(20), starttime(22), vsize(23) */
    uint64_t utime, stime, starttime, vsize;
    int ppid, priority, nice_val, num_threads;
    char state;

    int n = sscanf(after, "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u "
                          "%lu %lu %*d %*d %d %d %d %*u %lu %lu",
                   &state, &ppid, &utime, &stime, &priority, &nice_val,
                   &num_threads, &starttime, &vsize);

    if (n < 9) return -1;

    info->state = state;
    info->ppid = ppid;
    info->priority = priority;
    info->nice_val = nice_val;
    info->thread_count = num_threads;
    info->start_time_ticks = starttime;
    info->vsize_bytes = vsize;

    *out_total_ticks = utime + stime;

    /* calculate start_time and running_duration */
    int64_t uptime_ms = 0;
    {
        FILE *uf = fopen("/proc/uptime", "r");
        if (uf) {
            double uptime_sec;
            if (fscanf(uf, "%lf", &uptime_sec) == 1) {
                uptime_ms = (int64_t)(uptime_sec * 1000.0);
            }
            fclose(uf);
        }
    }
    int64_t elapsed_ms = (int64_t)((double)starttime / p->clock_ticks_per_sec * 1000.0);
    info->start_time_ms = current_time_ms() - uptime_ms + elapsed_ms;
    info->running_duration_ms = uptime_ms - elapsed_ms;
    if (info->running_duration_ms < 0) info->running_duration_ms = 0;

    return 0;
}

/* ---------- read rss / uid / cmdline / io ---------- */

static uint64_t read_rss_from_status(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char line[256];
    uint64_t rss = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "VmRSS: %lu kB", &rss) == 1) {
            rss *= 1024;
            break;
        }
    }
    fclose(fp);
    return rss;
}

static uint32_t read_uid_from_status(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char line[256];
    uint32_t uid = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            if (sscanf(line + 4, "%u", &uid) == 1) break;
        }
    }
    fclose(fp);
    return uid;
}

static char *read_cmdline(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    char *buf = malloc(CMDLINE_BUF_SIZE);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t total = fread(buf, 1, CMDLINE_BUF_SIZE - 1, fp);
    fclose(fp);

    if (total == 0) {
        free(buf);
        return NULL;
    }

    buf[total] = '\0';
    for (size_t i = 0; i < total; i++) {
        if (buf[i] == '\0') buf[i] = ' ';
    }
    while (total > 0 && buf[total - 1] == ' ') {
        buf[--total] = '\0';
    }

    return buf;
}

static bool read_io(int pid, uint64_t *read_bytes, uint64_t *write_bytes) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[256];
    bool got_read = false, got_write = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "read_bytes:", 11) == 0) {
            *read_bytes = strtoull(line + 12, NULL, 10);
            got_read = true;
        } else if (strncmp(line, "write_bytes:", 12) == 0) {
            *write_bytes = strtoull(line + 13, NULL, 10);
            got_write = true;
        }
    }
    fclose(fp);
    return got_read && got_write;
}

/* ---------- Main collect function ---------- */

int process_probe_collect(process_probe_t *p, process_list_t *out_list,
                          system_summary_t *out_summary,
                          probe_status_info_t *status, const config_t *cfg) {
    (void)cfg;
    int64_t now = current_time_ms();
    out_summary->sampled_at_ms = now;
    out_list->sampled_at_ms = now;

    out_summary->online_cpus = (unsigned int)p->online_cpus;
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out_summary->total_ram_bytes = si.totalram * (uint64_t)si.mem_unit;
        out_summary->used_ram_bytes = (si.totalram - si.freeram) * (uint64_t)si.mem_unit;
        out_summary->total_swap_bytes = si.totalswap * (uint64_t)si.mem_unit;
        out_summary->used_swap_bytes = (si.totalswap - si.freeswap) * (uint64_t)si.mem_unit;
    }

    /* Read CPU ticks for delta calculation */
    uint64_t cur_total_ticks = 0, cur_idle_ticks = 0;
    if (!read_cpu_ticks(&cur_total_ticks, &cur_idle_ticks)) {
        status->status = PROBE_ERROR;
        strncpy(status->error_message, "cannot read /proc/stat", 255);
        return -1;
    }

    uint64_t sys_delta = 0;
    uint64_t idle_delta = 0;
    if (p->sample_count > 0 && cur_total_ticks > p->prev_total_cpu_ticks) {
        sys_delta = cur_total_ticks - p->prev_total_cpu_ticks;
        idle_delta = cur_idle_ticks - p->prev_total_idle_ticks;
    }

    /* System CPU percent */
    if (sys_delta > 0) {
        out_summary->cpu_percent = (double)(sys_delta - idle_delta) / (double)sys_delta * 100.0;
    } else {
        out_summary->cpu_percent = 0.0;
    }

    /* Collect processes */
    out_list->count = 0;
    int running = 0, sleeping = 0, zombie = 0;

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        status->status = PROBE_ERROR;
        strncpy(status->error_message, "cannot open /proc", 255);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;

        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        /* Ensure capacity */
        if (out_list->count >= out_list->capacity) {
            size_t new_cap = out_list->capacity * 2;
            process_info_t *new_items = realloc(out_list->items,
                                                 new_cap * sizeof(process_info_t));
            if (!new_items) break;
            memset(new_items + out_list->capacity, 0,
                   (new_cap - out_list->capacity) * sizeof(process_info_t));
            out_list->items = new_items;
            out_list->capacity = new_cap;
        }

        process_info_t *info = &out_list->items[out_list->count];
        memset(info, 0, sizeof(*info));

        /* Read /proc/[pid]/stat */
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

        FILE *fp = fopen(stat_path, "r");
        if (!fp) continue;

        char stat_buf[PROC_STAT_BUF_SIZE];
        size_t len = fread(stat_buf, 1, sizeof(stat_buf) - 1, fp);
        fclose(fp);

        if (len == 0) continue;
        stat_buf[len] = '\0';

        uint64_t cur_total_ticks_proc = 0;
        if (parse_proc_stat(stat_buf, info, p, &cur_total_ticks_proc) != 0) continue;

        /* Lookup previous ticks BEFORE storing new ones */
        uint64_t *prev_ticks = lookup_prev_ticks(p, pid, info->start_time_ticks);

        /* Calculate per-process CPU percent using delta */
        if (prev_ticks && p->sample_count > 0 && sys_delta > 0) {
            uint64_t delta = cur_total_ticks_proc > *prev_ticks ?
                             cur_total_ticks_proc - *prev_ticks : 0;
            info->cpu_percent = (double)delta / (double)sys_delta
                              * (double)p->online_cpus * 100.0;
        } else {
            info->cpu_percent = 0.0;
        }

        /* Store current ticks for next sample */
        store_prev_ticks(p, pid, info->start_time_ticks, cur_total_ticks_proc);

        /* Read UID */
        info->uid = read_uid_from_status(pid);
        const char *username = resolve_username(p, info->uid);
        if (username) {
            strncpy(info->user, username, sizeof(info->user) - 1);
        } else {
            snprintf(info->user, sizeof(info->user), "%u", info->uid);
        }

        /* Read RSS */
        info->rss_bytes = read_rss_from_status(pid);

        /* Read cmdline */
        info->cmdline = read_cmdline(pid);

        /* Read IO */
        uint64_t read_bytes = 0, write_bytes = 0;
        info->has_io = read_io(pid, &read_bytes, &write_bytes);
        if (info->has_io) {
            info->read_bytes = read_bytes;
            info->write_bytes = write_bytes;
        }

        /* Count state */
        switch (info->state) {
        case 'R': running++; break;
        case 'S': case 'D': sleeping++; break;
        case 'Z': zombie++; break;
        }

        out_list->count++;
    }
    closedir(proc_dir);

    out_summary->process_count = (int)out_list->count;
    out_summary->running_count = running;
    out_summary->sleeping_count = sleeping;
    out_summary->zombie_count = zombie;

    /* Save current ticks for next sample */
    p->prev_total_cpu_ticks = cur_total_ticks;
    p->prev_total_idle_ticks = cur_idle_ticks;
    p->sample_count++;

    if (status->status == PROBE_WARMING_UP && p->sample_count > 1) {
        status->status = PROBE_OK;
        status->error_message[0] = '\0';
    }
    status->last_ok_sample_ms = now;

    return (int)out_list->count;
}
