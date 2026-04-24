#ifndef CAN_TELEM_CONFIG_H
#define CAN_TELEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_VALUE_MAX       512
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
} config_file_t;

/*
 * Parse a simple key=value config file (UTF-8, one assignment per line).
 * Lines starting with # and blank lines are ignored. Leading/trailing
 * whitespace on keys and values is stripped.
 *
 * Recognized keys (case-insensitive):
 *   output_dir                 — directory for per-signal CSV files
 *   format_file                — path to format.json
 *   can_interface, interface   — SocketCAN interface name (e.g. can0)
 *   influx_enabled             — true/false (default false if omitted)
 *   influx_url                 — InfluxDB Cloud base URL (no trailing path)
 *   influx_org                 — organization name
 *   influx_bucket              — bucket name
 *   influx_token               — API token (optional; INFLUX_TOKEN env if empty)
 *   influx_upload_interval_ms  — cloud flush period (default 1000)
 *   influx_measurement         — line-protocol measurement name [A-Za-z0-9_]+
 *   db_enabled                 — true/false (default false)
 *   db_path                    — sqlite3 file path for DB sourced signals
 *   db_table                   — table containing signal rows (default signal_values)
 *   db_key_column              — key/name column (default signal_key)
 *   db_value_column            — value column (default signal_value)
 *   db_poll_interval_ms        — DB polling period in ms (default 200)
 *   db_can_interface           — CAN interface for TX (default can_interface)
 *   radio_enabled              — true/false (default false)
 *   radio_device               — serial device path (default /dev/ttyUSB0)
 *   radio_baud                 — baud rate: 1200/2400/4800/9600/19200/38400/57600/115200
 *   radio_flush_interval_ms    — serial flush period in ms (default 1000)
 *
 * Returns 0 on success, -1 on failure (missing file when path was
 * required, or I/O error).
 */
int config_file_load(const char *path, config_file_t *out);

#endif /* CAN_TELEM_CONFIG_H */
