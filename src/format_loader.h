#ifndef CAN_TELEM_FORMAT_LOADER_H
#define CAN_TELEM_FORMAT_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIG_NAME_MAX      64
#define SIG_UNITS_MAX     16
#define SIG_CATEGORY_MAX  48
#define SIG_DB_KEY_MAX    96
#define SIG_TABLE_BUCKETS 1024

typedef enum {
    T_BOOL,
    T_UINT8,
    T_UINT16,
    T_UINT32,
    T_UINT64,
    T_INT32,
    T_FLOAT,
} sig_type_t;

typedef enum {
    SIG_SOURCE_CAN = 0,
    SIG_SOURCE_DB  = 1,
} sig_source_t;

typedef struct {
    char       name[SIG_NAME_MAX];
    uint8_t    num_bytes;
    sig_type_t type;
    char       units[SIG_UNITS_MAX];
    double     nom_min;
    double     nom_max;
    char       category[SIG_CATEGORY_MAX];
    uint32_t   can_id;
    uint16_t   bit_offset;
    bool       placeholder;
    sig_source_t source;
    char       db_key[SIG_DB_KEY_MAX];
    bool       tx_on_change;
    uint32_t   tx_min_interval_ms;
} signal_def_t;

typedef struct sig_node {
    signal_def_t     sig;
    struct sig_node *next;
} sig_node_t;

typedef struct {
    sig_node_t *buckets[SIG_TABLE_BUCKETS];
    size_t      count;
    size_t      placeholder_count;
} signal_table_t;

/*
 * Parse the JSON file at `path` and populate `table`.
 * Returns 0 on success, non-zero on failure (errors printed to stderr).
 * On success, caller must release with signal_table_free().
 */
int signal_table_load(const char *path, signal_table_t *table);

void signal_table_free(signal_table_t *table);

/*
 * Return the head of the bucket list containing all signals that share
 * the given 11-bit CAN ID. The caller walks node->next and filters by
 * node->sig.can_id == can_id (because multiple IDs hash to one bucket).
 */
const sig_node_t *signal_table_lookup(const signal_table_t *table,
                                      uint32_t can_id);

#endif
