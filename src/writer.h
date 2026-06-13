#ifndef CAN_TELEM_WRITER_H
#define CAN_TELEM_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "decoder.h"
#include "format_loader.h"

#define WRITER_SNAPSHOT_BASENAME "telemetry_snapshot"
#define WRITER_DEFAULT_SNAPSHOT_INTERVAL_MS 500u

typedef struct {
    char            out_dir[512];
    char            current_day[16];   /* e.g. 12-Jun-2026 */
    char            snapshot_path[640]; /* full path to active CSV */
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
 * Creates {out_dir}/{DD-Mon-YYYY}/telemetry_snapshot_HH-MM-SS.csv
 * Header matches sc2-mobile-app wide export: timestamp_ms,<sorted_signal_names...>
 */
int writer_init(writer_t *w, const char *out_dir, const signal_table_t *table);

int writer_append(writer_t *w,
                  const signal_def_t *sig,
                  const decoded_value_t *dv);

void writer_tick(writer_t *w);

void writer_close(writer_t *w);

#endif
