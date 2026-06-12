#ifndef CAN_TELEM_CONFIG_H
#define CAN_TELEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_VALUE_MAX       512
#define CONFIG_SIGNAL_LIST_MAX 4096
#define INFLUX_ORG_MAX         256
#define INFLUX_BUCKET_MAX      256
#define INFLUX_TOKEN_MAX       2048
#define INFLUX_MEASUREMENT_MAX 128
#define DB_IDENTIFIER_MAX      64

typedef struct {
    char output_dir[CONFIG_VALUE_MAX];
    char format_file[CONFIG_VALUE_MAX];
    char can_interface[CONFIG_VALUE_MAX];
    bool has_output_dir;
    bool has_format_file;
    bool has_can_interface;

    bool influx_enabled;
    bool has_influx_enabled;
    char influx_url[CONFIG_VALUE_MAX];
    bool has_influx_url;
    char influx_org[INFLUX_ORG_MAX];
    bool has_influx_org;
    char influx_bucket[INFLUX_BUCKET_MAX];
    bool has_influx_bucket;
    char influx_token[INFLUX_TOKEN_MAX];
    bool has_influx_token;
    uint32_t influx_upload_interval_ms;
    bool has_influx_upload_interval_ms;
    char influx_measurement[INFLUX_MEASUREMENT_MAX];
    bool has_influx_measurement;

    bool db_enabled;
    bool has_db_enabled;
    char db_path[CONFIG_VALUE_MAX];
    bool has_db_path;
    char db_table[DB_IDENTIFIER_MAX];
    bool has_db_table;
    char db_key_column[DB_IDENTIFIER_MAX];
    bool has_db_key_column;
    char db_value_column[DB_IDENTIFIER_MAX];
    bool has_db_value_column;
    uint32_t db_poll_interval_ms;
    bool has_db_poll_interval_ms;
    char db_can_interface[CONFIG_VALUE_MAX];
    bool has_db_can_interface;

    /* --- Serial radio --- */
    bool     radio_enabled;
    bool     has_radio_enabled;
    char     radio_device[CONFIG_VALUE_MAX];
    bool     has_radio_device;
    uint32_t radio_baud;
    bool     has_radio_baud;
    uint32_t radio_flush_interval_ms;
    bool     has_radio_flush_interval_ms;

    /* --- Role filters --- */
    char rx_signals[CONFIG_SIGNAL_LIST_MAX];
    bool has_rx_signals;
    char tx_signals[CONFIG_SIGNAL_LIST_MAX];
    bool has_tx_signals;

    /* --- GNSS --- */
    bool     gnss_enabled;
    bool     has_gnss_enabled;
    uint32_t gnss_poll_interval_ms;
    bool     has_gnss_poll_interval_ms;
    char     gnss_cache_path[CONFIG_VALUE_MAX];
    bool     has_gnss_cache_path;
    char     gnss_lat_signal[CONFIG_VALUE_MAX];
    bool     has_gnss_lat_signal;
    char     gnss_lon_signal[CONFIG_VALUE_MAX];
    bool     has_gnss_lon_signal;
    char     gnss_elev_signal[CONFIG_VALUE_MAX];
    bool     has_gnss_elev_signal;
} config_file_t;

/*
 * Parse simple key=value config.
 */
int config_file_load(const char *path, config_file_t *out);

/*
 * Returns 1 if `name` is present as a comma-separated token in `csv_list`,
 * otherwise 0. Whitespace around tokens is ignored.
 */
int config_list_contains(const char *csv_list, const char *name);

#endif /* CAN_TELEM_CONFIG_H */
