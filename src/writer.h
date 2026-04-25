#ifndef CAN_TELEM_WRITER_H
#define CAN_TELEM_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "decoder.h"
#include "format_loader.h"

#define WRITER_SNAPSHOT_FILE "telemetry_snapshot.csv"
#define WRITER_DEFAULT_SNAPSHOT_INTERVAL_MS 500u

typedef struct {
    char            out_dir[512];
    FILE           *f;
    unsigned        snapshot_interval_ms;
    struct timespec last_flush_mono;
    bool            have_last_flush;

    size_t          col_count;
    char          **col_names;  /* sorted, stable column order */
    double         *col_values; /* latest value per column */
    bool           *col_seen;   /* whether a value has been seen yet */
} writer_t;

/*
 * Initialize a unified snapshot CSV writer in `out_dir`.
 * The header is: timestamp_ns,<all_signal_names...>
 * Signal columns are built from `table` (excluding placeholders).
 */
int writer_init(writer_t *w, const char *out_dir, const signal_table_t *table);

/*
 * Update the in-memory latest value for one signal.
 * Does not immediately write to disk.
 */
int writer_append(writer_t *w,
                  const signal_def_t *sig,
                  const decoded_value_t *dv);

/*
 * Periodically flush one wide snapshot row:
 * timestamp_ns,<value_for_sig1>,<value_for_sig2>,...
 */
void writer_tick(writer_t *w);

/*
 * Flush and close writer resources.
 */
void writer_close(writer_t *w);

#endif
