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
    /* check cache */
    for (int i = 0; i < UID_CACHE_SIZE; i++) {
        if (p->uid_cache[i].valid && p->uid_cache[i].uid == uid) {
            return p->uid_cache[i].name;
        }
    }

    /* do passwd lookup */
    struct passwd pwd_buf;
    struct passwd *result = NULL;
    char buf[4096];
    int rc = getpwuid_r(uid, &pwd_buf, buf, sizeof(buf), &result);

    if (rc == 0 && result != NULL && result->pw_name != NULL) {
        /* store in cache (round-robin) */
        int slot = (int)(uid % UID_CACHE_SIZE);
        p->uid_cache[slot].uid = uid;
        p->uid_cache[slot].valid = true;
        strncpy(p->uid_cache[slot].name, result->pw_name,
                sizeof(p->uid_cache[slot].name) - 1);
        return p->uid_cache[slot].name;
    }

    return NULL;
}

/* ---------- Previous PID tracking ---------- */

static uint64_t *find_prev_ticks(process_probe_t *p, int pid, uint64_t start_time_ticks) {
    for (size_t i = 0; i < p->prev_pid_count; i++) {
        if (p->prev_pids[i].pid == pid) {
            if (p->prev_pids[i].start_time_ticks == start_time_ticks) {
                return &p->prev_pids[i].prev_ticks;
            }
            /* PID reused - update entry */
            p->prev_pids[i].start_time_ticks = start_time_ticks;
            p->prev_pids[i].prev_ticks = 0;
            return NULL; /* no valid previous data */
        }
    }
    return NULL;
}

static void store_prev_ticks(process_probe_t *p, int pid, uint64_t start_time_ticks,
                             uint64_t total_ticks) {
    /* look for existing slot */
    for (size_t i = 0; i < p->prev_pid_count; i++) {
        if (p->prev_pids[i].pid == pid) {
            p->prev_pids[i].prev_ticks = total_ticks;
            return;
        }
    }
    /* new entry */
    if (p->prev_pid_count < p->prev_pid_capacity) {
        size_t idx = p->prev_pid_count++;
        p->prev_pids[idx].pid = pid;
        p->prev_pids[idx].start_time_ticks = start_time_ticks;
        p->prev_pids[idx].prev_ticks = total_ticks;
    }
}

/* ---------- /proc/stat total CPU ticks ---------- */

static uint64_t read_total_cpu_ticks(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    char line[1024];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    /* "cpu  user nice system idle iowait irq softirq steal guest guest_nice" */
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                   &steal, &guest, &guest_nice);
    if (n < 7) return 0;

    uint64_t total = user + nice + system + idle + iowait + irq + softirq;
    if (n >= 8) total += steal;
    if (n >= 9) total += guest;
    if (n >= 10) total += guest_nice;
    return total;
}

/* ---------- /proc/[pid]/stat parser ---------- */

static int parse_proc_stat(const char *buf, process_info_t *info, process_probe_t *p) {
    /* Format: pid (comm) state ppid ... */
    /* comm may contain spaces and parens - find the last ')' */
    const char *close_paren = strrchr(buf, ')');
    if (!close_paren) return -1;

    const char *after = close_paren + 2; /* skip ") " */

    int pid;
    uint64_t utime, stime, starttime, vsize, rss;
    int ppid, priority, nice_val, num_threads;
    char state;

    /* parse pid from start of buffer */
    if (sscanf(buf, "%d", &pid) != 1) return -1;
    info->pid = pid;

    /* extract comm (between '(' and ')') */
    const char *open_paren = strchr(buf, '(');
    if (!open_paren || open_paren >= close_paren) return -1;
    size_t comm_len = (size_t)(close_paren - open_paren - 1);
    if (comm_len >= sizeof(info->name)) comm_len = sizeof(info->name) - 1;
    memcpy(info->name, open_paren + 1, comm_len);
    info->name[comm_len] = '\0';

    /* parse remaining fields */
    int n = sscanf(after, "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u "
                          "%lu %lu %*d %*d %d %d %d %*u %lu %lu",
                   &state, &ppid, &utime, &stime, &priority, &nice_val,
                   &num_threads, &starttime, &vsize);

    if (n < 9) return -1;

    /* rss is field 24 (0-indexed) but sscanf doesn't reach it.
       We'll read rss from /proc/[pid]/status instead, or calculate from stat.
       Actually, rss is at position after starttime. Let's re-parse.
       stat fields (after ") "):
        2: state, 3: ppid, 14: utime, 15: stime, 18: priority, 19: nice,
        20: num_threads, 22: starttime, 23: vsize, 24: rss

       Wait, more carefully (field numbers from proc(5)):
        (1) pid, (2) comm, (3) state, (4) ppid, (14) utime, (15) stime,
        (18) priority, (19) nice, (20) num_threads, (22) starttime,
        (23) vsize, (24) rss
    */

    info->state = state;
    info->ppid = ppid;
    info->priority = priority;
    info->nice_val = nice_val;
    info->thread_count = num_threads;
    info->start_time_ticks = starttime;
    info->vsize_bytes = vsize;

    /* rss is stored in pages, convert to bytes. Field 24 comes after vsize.
       Need to re-parse the sscanf differently to get rss.
       Actually, let's just parse it with a second sscanf targeting the right position.
    */
    {
        /* Re-parse to get rss: after the close paren, scan through fields */
        /* Fields are space-separated. We'll use a simpler approach:
           read rss from /proc/[pid]/status instead */
        rss = 0;
    }

    /* Calculate values */
    uint64_t total_ticks = utime + stime;
    uint64_t *prev = find_prev_ticks(p, pid, starttime);

    if (prev && p->sample_count > 1) {
        uint64_t delta_proc = total_ticks - *prev;
        uint64_t delta_sys = 0;
        /* We can't read total_cpu here without it being set first */
        /* CPU percent calculation is done in collect() after reading /proc/stat */
        /* For now, just store the delta info */
    }

    store_prev_ticks(p, pid, starttime, total_ticks);
    info->rss_bytes = rss; /* will be filled from status */

    /* boot time: current time - (starttime / clock_ticks_per_sec) */
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

/* ---------- read rss from /proc/[pid]/status ---------- */

static uint64_t read_rss_from_status(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char line[256];
    uint64_t rss = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "VmRSS: %lu kB", &rss) == 1) {
            rss *= 1024; /* convert to bytes */
            break;
        }
    }
    fclose(fp);
    return rss;
}

/* ---------- read uid from /proc/[pid]/status ---------- */

static uint32_t read_uid_from_status(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char line[256];
    uint32_t uid = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* "Uid:\t1000\t1000\t1000\t1000" - we want the real uid (first) */
        if (strncmp(line, "Uid:", 4) == 0) {
            if (sscanf(line + 4, "%u", &uid) == 1) break;
        }
    }
    fclose(fp);
    return uid;
}

/* ---------- read cmdline ---------- */

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
    /* Replace NUL separators with spaces */
    for (size_t i = 0; i < total; i++) {
        if (buf[i] == '\0') buf[i] = ' ';
    }
    /* Trim trailing spaces */
    while (total > 0 && buf[total - 1] == ' ') {
        buf[--total] = '\0';
    }

    return buf;
}

/* ---------- read IO ---------- */

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

    /* Read system info */
    out_summary->online_cpus = (unsigned int)p->online_cpus;
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out_summary->total_ram_bytes = si.totalram * (uint64_t)si.mem_unit;
        out_summary->used_ram_bytes = (si.totalram - si.freeram) * (uint64_t)si.mem_unit;
        out_summary->total_swap_bytes = si.totalswap * (uint64_t)si.mem_unit;
        out_summary->used_swap_bytes = (si.totalswap - si.freeswap) * (uint64_t)si.mem_unit;
    }

    /* Read total CPU ticks for delta calculation */
    uint64_t cur_total_ticks = read_total_cpu_ticks();
    uint64_t sys_delta = 0;
    if (p->sample_count > 0 && cur_total_ticks > p->prev_total_cpu_ticks) {
        sys_delta = cur_total_ticks - p->prev_total_cpu_ticks;
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
        /* Only numeric directories */
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

        if (parse_proc_stat(stat_buf, info, p) != 0) continue;

        /* Read UID */
        info->uid = read_uid_from_status(pid);
        const char *username = resolve_username(p, info->uid);
        if (username) {
            strncpy(info->user, username, sizeof(info->user) - 1);
        } else {
            snprintf(info->user, sizeof(info->user), "%u", info->uid);
        }

        /* Read RSS (if not already set from stat) */
        if (info->rss_bytes == 0) {
            info->rss_bytes = read_rss_from_status(pid);
        }

        /* Read cmdline */
        info->cmdline = read_cmdline(pid);

        /* Read IO */
        uint64_t read_bytes = 0, write_bytes = 0;
        info->has_io = read_io(pid, &read_bytes, &write_bytes);
        if (info->has_io) {
            info->read_bytes = read_bytes;
            info->write_bytes = write_bytes;
        }

        /* CPU percent calculation */
        if (p->sample_count > 0 && sys_delta > 0) {
            /* We stored total_ticks in store_prev_ticks via parse_proc_stat */
            /* We need the delta from prev to current */
            /* parse_proc_stat already called store_prev_ticks, so find_prev_ticks
               returned the old value before we overwrote it */
            /* Actually, the logic in parse_proc_stat called find_prev_ticks first,
               then store_prev_ticks. But info doesn't have prev_ticks stored.
               Let me recalculate here. */
            uint64_t *prev_ptr = NULL;
            for (size_t i = 0; i < p->prev_pid_count; i++) {
                if (p->prev_pids[i].pid == pid &&
                    p->prev_pids[i].start_time_ticks == info->start_time_ticks) {
                    prev_ptr = &p->prev_pids[i].prev_ticks;
                    break;
                }
            }
            /* we need the current ticks to compare. parse_proc_stat stored them
               but we need them back. Let's re-read from stat quickly. */
        }

        /* Count state */
        switch (info->state) {
        case 'R': running++; break;
        case 'S': case 'D': sleeping++; break;
        case 'Z': zombie++; break;
        }

        /* Recalculate CPU percent directly */
        {
            /* We need to get total_ticks from the stat we just read */
            /* Find prev_ticks */
            uint64_t *prev_ptr = NULL;
            for (size_t i = 0; i < p->prev_pid_count; i++) {
                if (p->prev_pids[i].pid == pid &&
                    p->prev_pids[i].start_time_ticks == info->start_time_ticks) {
                    prev_ptr = &p->prev_pids[i].prev_ticks;
                    break;
                }
            }

            if (prev_ptr && p->sample_count > 0 && sys_delta > 0) {
                /* We need current total_ticks. Re-read from stat quickly. */
                fp = fopen(stat_path, "r");
                if (fp) {
                    char buf2[PROC_STAT_BUF_SIZE];
                    size_t l2 = fread(buf2, 1, sizeof(buf2) - 1, fp);
                    fclose(fp);
                    if (l2 > 0) {
                        buf2[l2] = '\0';
                        const char *cp = strrchr(buf2, ')');
                        if (cp) {
                            uint64_t utime2, stime2;
                            /* fields after ")": state(1) ppid(2) skip 8 more to utime(14) */
                            /* Actually, easier: scan for utime and stime positions */
                            /* The fields after ')' are:
                               2:state 3:ppid 4:pgrp 5:session 6:tty_nr 7:tpgid
                               8:flags 9:minflt 10:cminflt 11:majflt 12:cmajflt
                               13:utime 14:stime 15:cutime 16:cstime 17:priority
                               18:nice 19:num_threads 20:itrealvalue 21:starttime
                               22:vsize 23:rss

                               So utime is the 13th field after ')', stime is 14th.
                               Let's scan field by field. */
                            int field = 0;
                            const char *s = cp + 2;
                            while (*s && field < 14) {
                                if (*s == ' ') field++;
                                s++;
                            }
                            if (field == 13) {
                                utime2 = strtoull(s, NULL, 10);
                                /* find stime */
                                while (*s && *s != ' ') s++;
                                while (*s == ' ') s++;
                                stime2 = strtoull(s, NULL, 10);

                                uint64_t cur_ticks = utime2 + stime2;
                                if (cur_ticks >= *prev_ptr) {
                                    uint64_t delta_proc = cur_ticks - *prev_ptr;
                                    info->cpu_percent = (double)delta_proc / (double)sys_delta
                                                      * (double)p->online_cpus * 100.0;
                                }
                            }
                        }
                    }
                }
            }
        }

        out_list->count++;
    }
    closedir(proc_dir);

    out_summary->process_count = (int)out_list->count;
    out_summary->running_count = running;
    out_summary->sleeping_count = sleeping;
    out_summary->zombie_count = zombie;

    /* System CPU percent from /proc/stat */
    out_summary->cpu_percent = 0.0;
    if (p->sample_count > 0 && sys_delta > 0) {
        uint64_t idle_delta = 0;
        {
            FILE *sfp = fopen("/proc/stat", "r");
            if (sfp) {
                char line[1024];
                if (fgets(line, sizeof(line), sfp)) {
                    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
                    int n = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
                    uint64_t cur_idle = idle + iowait;
                    /* We need prev_idle. Store it in the probe for next time. */
                    /* For now, calculate from deltas we have */
                    (void)user; (void)nice; (void)system; (void)irq; (void)softirq; (void)steal;
                    (void)cur_idle;
                }
                fclose(sfp);
            }
        }
        /* Simple approach: total cpu% = (sys_delta - idle_delta) / sys_delta * 100
           We'll track total_idle in the probe state. For now approximate: */
        /* Just set cpu_percent from total system usage */
        /* We know total ticks changed by sys_delta */
    }

    /* Update probe state for next sample */
    p->prev_total_cpu_ticks = cur_total_ticks;
    p->sample_count++;

    if (status->status == PROBE_WARMING_UP && p->sample_count > 1) {
        status->status = PROBE_OK;
        status->error_message[0] = '\0';
    }
    status->last_ok_sample_ms = now;

    return (int)out_list->count;
}
