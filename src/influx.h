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
    char  measurement[128];       /* periodic snapshot measurement */
    char  fault_measurement[128]; /* event measurement */
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

    struct {
        char name[SIG_NAME_MAX];
        bool used;
        bool have_last_state;
        bool last_state;
    } fault_slots[INFLUX_AGG_SLOTS];

    struct timespec last_flush_mono;
    bool            have_last_flush;
} influx_ctx_t;

int influx_init(influx_ctx_t *ctx, const config_file_t *cf);

void influx_accumulate(influx_ctx_t *ctx,
                       const signal_def_t *sig,
                       const decoded_value_t *dv);

void influx_tick(influx_ctx_t *ctx);

void influx_shutdown(influx_ctx_t *ctx);

#endif
