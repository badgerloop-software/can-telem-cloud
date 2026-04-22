#ifndef CAN_TELEM_DB_WATCHER_H
#define CAN_TELEM_DB_WATCHER_H

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "config.h"
#include "format_loader.h"

typedef struct db_watcher db_watcher_t;

/*
 * Starts a background thread that polls DB-backed signals and transmits CAN
 * updates when values change. Returns 0 on success.
 * Returns 1 when disabled/no DB-backed signals are configured.
 * Returns -1 on hard startup errors.
 */
int db_watcher_start(db_watcher_t *ctx,
                     const config_file_t *cf,
                     const signal_table_t *table,
                     const char *default_can_iface,
                     volatile sig_atomic_t *running);

/*
 * Stops the thread and releases resources.
 */
void db_watcher_stop(db_watcher_t *ctx);

/*
 * Opaque definition exposed for stack allocation in main.
 */
struct db_sig_state {
    const signal_def_t *sig;
    double last_value;
    bool has_last;
    uint64_t last_tx_ns;
    uint64_t retry_after_ns;
};

struct db_frame_cache {
    uint32_t can_id;
    uint8_t dlc;
    uint8_t data[8];
};

struct db_watcher {
    bool enabled;
    volatile sig_atomic_t *running;
    char db_path[CONFIG_VALUE_MAX];
    char can_iface[CONFIG_VALUE_MAX];
    char table_name[DB_IDENTIFIER_MAX];
    char key_column[DB_IDENTIFIER_MAX];
    char value_column[DB_IDENTIFIER_MAX];
    uint32_t poll_interval_ms;
    int tx_fd;
    struct sqlite3 *db;
    struct db_sig_state *signals;
    size_t signal_count;
    struct db_frame_cache *frames;
    size_t frame_count;
    pthread_t thread;
};

#endif
