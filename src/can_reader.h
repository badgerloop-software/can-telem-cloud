#ifndef CAN_TELEM_CAN_READER_H
#define CAN_TELEM_CAN_READER_H

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include "format_loader.h"
#include "influx.h"
#include "writer.h"

/*
 * Open a raw SocketCAN socket bound to `ifname`.
 * Returns a file descriptor >= 0 on success, or -1 on error.
 */
int can_reader_open(const char *ifname);

/*
 * Blocking receive loop: reads frames from `fd`, matches each ID against
 * `table`, decodes every matching signal (skipping placeholders with
 * can_id==0xFFF) and appends the value via the writer.
 * If `influx` is non-NULL and enabled, updates Influx aggregators and may
 * flush to the cloud on a timer (see config).
 * Runs until `*running` becomes 0.
 */
int can_reader_loop(int fd,
                    const signal_table_t *table,
                    writer_t *w,
                    influx_ctx_t *influx,
                    volatile sig_atomic_t *running);

#endif
