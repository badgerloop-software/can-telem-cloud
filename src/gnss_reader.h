#ifndef CAN_TELEM_GNSS_READER_H
#define CAN_TELEM_GNSS_READER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "config.h"

typedef struct {
    bool enabled;
    char cache_path[CONFIG_VALUE_MAX];
    uint32_t poll_interval_ms;

    struct timespec last_poll_mono;
    bool have_last_poll;

    struct timespec cache_mtime;
    bool have_cache_mtime;

    bool have_fix;
    double lat;
    double lon;
    double elev;

    bool   have_rssi;
    double rssi_dbm;   /* dBm, e.g. -85.0; only valid when have_rssi=true */
} gnss_reader_t;

int gnss_reader_init(gnss_reader_t *ctx, const config_file_t *cf);
int gnss_reader_tick(gnss_reader_t *ctx); /* 1=new fix, 0=no update, -1=error */
int gnss_reader_get_fix(const gnss_reader_t *ctx, double *lat, double *lon, double *elev);
int gnss_reader_get_rssi(const gnss_reader_t *ctx, double *rssi_dbm);
void gnss_reader_shutdown(gnss_reader_t *ctx);

#endif
