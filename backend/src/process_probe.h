#ifndef PROCESS_PROBE_H
#define PROCESS_PROBE_H

#include "snapshot.h"
#include "config.h"

typedef struct process_probe process_probe_t;

process_probe_t *process_probe_create(void);
void process_probe_destroy(process_probe_t *p);

int process_probe_collect(process_probe_t *p,
                          process_list_t *out_list,
                          system_summary_t *out_summary,
                          probe_status_info_t *status,
                          const config_t *cfg);

#endif
