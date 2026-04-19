#ifndef CAN_TELEM_INFLUX_H
#define CAN_TELEM_INFLUX_H

#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "decoder.h"
#include "format_loader.h"

#define INFLUX_AGG_SLOTS 512

typedef struct influx_ctx {
    bool enabled;
    void *curl; /* CURL * */
    void *headers; /* struct curl_slist * */
    char *write_url;
    char  measurement[128];
    unsigned interval_ms;

    struct {
        char       name[SIG_NAME_MAX];
        sig_type_t type;
        bool       used;
        bool       bool_any;
        bool       bool_seen;
        double     sum;
        uint64_t   count;
    } slots[INFLUX_AGG_SLOTS];

    struct timespec last_flush_mono;
    bool            have_last_flush;
} influx_ctx_t;

/*
 * If `cf->influx_enabled` is false, initializes ctx with enabled=false and
 * returns 0. Otherwise validates URL/org/bucket/token, initializes libcurl,
 * and prepares the write URL. Token is taken from config if set, else
 * getenv("INFLUX_TOKEN").
 *
 * Returns 0 on success, -1 on configuration or curl init failure.
 */
int influx_init(influx_ctx_t *ctx, const config_file_t *cf);

void influx_accumulate(influx_ctx_t *ctx,
                       const signal_def_t *sig,
                       const decoded_value_t *dv);

/*
 * Flush aggregated points to InfluxDB if the upload interval has elapsed.
 * Call periodically from the CAN receive loop (or elsewhere).
 */
void influx_tick(influx_ctx_t *ctx);

/*
 * Send any remaining aggregated data and release libcurl resources.
 * Safe to call when ctx->enabled is false (no-op).
 */
void influx_shutdown(influx_ctx_t *ctx);

#endif
