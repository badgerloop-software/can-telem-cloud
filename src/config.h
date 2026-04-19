#ifndef CAN_TELEM_CONFIG_H
#define CAN_TELEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_VALUE_MAX       512
#define INFLUX_ORG_MAX         256
#define INFLUX_BUCKET_MAX      256
#define INFLUX_TOKEN_MAX       2048
#define INFLUX_MEASUREMENT_MAX 128

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
 *
 * Returns 0 on success, -1 on failure (missing file when path was
 * required, or I/O error). Errors are printed to stderr.
 */
int config_file_load(const char *path, config_file_t *out);

#endif
