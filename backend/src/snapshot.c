#include "snapshot.h"
#include <stdlib.h>
#include <string.h>

monitor_snapshot_t *snapshot_alloc(void) {
    monitor_snapshot_t *s = calloc(1, sizeof(monitor_snapshot_t));
    if (!s) return NULL;
    s->processes.capacity = 512;
    s->processes.items = calloc(s->processes.capacity, sizeof(process_info_t));
    s->gpus.capacity = 8;
    s->gpus.items = calloc(s->gpus.capacity, sizeof(gpu_info_t));
    s->gpu_processes.capacity = 64;
    s->gpu_processes.items = calloc(s->gpu_processes.capacity, sizeof(gpu_process_info_t));
    if (!s->processes.items || !s->gpus.items || !s->gpu_processes.items) {
        snapshot_free(s);
        return NULL;
    }
    return s;
}

void snapshot_free(monitor_snapshot_t *s) {
    if (!s) return;
    if (s->processes.items) {
        for (size_t i = 0; i < s->processes.count; i++) {
            free(s->processes.items[i].cmdline);
        }
        free(s->processes.items);
    }
    free(s->gpus.items);
    free(s->gpu_processes.items);
    free(s);
}

int snapshot_store_init(snapshot_store_t *store) {
    memset(store, 0, sizeof(*store));
    if (pthread_rwlock_init(&store->rwlock, NULL) != 0) return -1;
    store->current = snapshot_alloc();
    store->next = snapshot_alloc();
    if (!store->current || !store->next) {
        snapshot_store_destroy(store);
        return -1;
    }
    store->current->sequence = 0;
    store->current->process_status.status = PROBE_WARMING_UP;
    strncpy(store->current->process_status.error_message, "waiting for first sample", 255);
    return 0;
}

void snapshot_store_destroy(snapshot_store_t *store) {
    if (!store) return;
    pthread_rwlock_destroy(&store->rwlock);
    snapshot_free(store->current);
    snapshot_free(store->next);
    store->current = NULL;
    store->next = NULL;
}

monitor_snapshot_t *snapshot_store_acquire_next(snapshot_store_t *store) {
    /* caller gets write access to store->next for building */
    return store->next;
}

void snapshot_store_publish(snapshot_store_t *store, monitor_snapshot_t *old_snapshot) {
    monitor_snapshot_t *new_cur = store->next;

    /* allocate a fresh next snapshot for the next cycle */
    store->next = snapshot_alloc();
    if (!store->next) {
        /* allocation failed - keep old next so it will be retried */
        store->next = new_cur;
        return;
    }

    new_cur->sequence = store->current ? store->current->sequence + 1 : 1;

    pthread_rwlock_wrlock(&store->rwlock);
    monitor_snapshot_t *old = store->current;
    store->current = new_cur;
    pthread_rwlock_unlock(&store->rwlock);

    /* free the old snapshot outside the lock */
    if (old) snapshot_free(old);
    (void)old_snapshot;
}

const monitor_snapshot_t *snapshot_store_acquire_read(snapshot_store_t *store) {
    pthread_rwlock_rdlock(&store->rwlock);
    return store->current;
}

void snapshot_store_release_read(snapshot_store_t *store) {
    pthread_rwlock_unlock(&store->rwlock);
}
