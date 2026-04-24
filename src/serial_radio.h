#ifndef CAN_TELEM_SERIAL_RADIO_H
#define CAN_TELEM_SERIAL_RADIO_H

#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "decoder.h"
#include "format_loader.h"

#define RADIO_SLOTS 512

typedef struct {
    bool     enabled;
    int      fd;               /* serial file descriptor, -1 when closed */
    unsigned interval_ms;
    struct timespec last_flush_mono;
    bool     have_last_flush;
    struct {
        char   name[SIG_NAME_MAX];
        bool   used;
        double value;
    } slots[RADIO_SLOTS];
} serial_radio_ctx_t;

/*
 * Open the serial device and configure baud rate via termios.
 * If cf->radio_enabled is false, sets ctx->enabled=false and returns 0.
 * Returns 0 on success, -1 on failure.
 */
int serial_radio_init(serial_radio_ctx_t *ctx, const config_file_t *cf);

/*
 * Store the latest decoded value for this signal (linear-probe hash).
 * No-op when ctx is NULL or not enabled.
 */
void serial_radio_accumulate(serial_radio_ctx_t *ctx,
                             const signal_def_t *sig,
                             const decoded_value_t *dv);

/*
 * If the flush interval has elapsed, write one line per updated signal to
 * the serial fd in the format:
 *   <timestamp_ns>,<signal_name>,<value>\n
 * Call this from the CAN receive loop alongside influx_tick().
 */
void serial_radio_tick(serial_radio_ctx_t *ctx);

/*
 * Close the serial fd. Safe to call when not enabled.
 */
void serial_radio_shutdown(serial_radio_ctx_t *ctx);

#endif /* CAN_TELEM_SERIAL_RADIO_H */
