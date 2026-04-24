#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void trim_inplace(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static int parse_bool(const char *val, bool *out) {
    if (strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0) {
        *out = true;
        return 0;
    }
    if (strcasecmp(val, "false") == 0 || strcasecmp(val, "no") == 0 ||
        strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int set_field(config_file_t *out, const char *key, const char *val) {
    if (strcasecmp(key, "output_dir") == 0) {
        if (strlen(val) >= sizeof out->output_dir) {
            fprintf(stderr, "config: output_dir value too long\n");
            return -1;
        }
        strcpy(out->output_dir, val);
        out->has_output_dir = true;
        return 0;
    }
    if (strcasecmp(key, "format_file") == 0) {
        if (strlen(val) >= sizeof out->format_file) {
            fprintf(stderr, "config: format_file value too long\n");
            return -1;
        }
        strcpy(out->format_file, val);
        out->has_format_file = true;
        return 0;
    }
    if (strcasecmp(key, "can_interface") == 0 ||
        strcasecmp(key, "interface") == 0) {
        if (strlen(val) >= sizeof out->can_interface) {
            fprintf(stderr, "config: can_interface value too long\n");
            return -1;
        }
        strcpy(out->can_interface, val);
        out->has_can_interface = true;
        return 0;
    }
    if (strcasecmp(key, "influx_enabled") == 0) {
        bool b;
        if (parse_bool(val, &b) != 0) {
            fprintf(stderr, "config: influx_enabled: expected boolean\n");
            return -1;
        }
        out->influx_enabled     = b;
        out->has_influx_enabled = true;
        return 0;
    }
    if (strcasecmp(key, "influx_url") == 0) {
        if (strlen(val) >= sizeof out->influx_url) {
            fprintf(stderr, "config: influx_url value too long\n");
            return -1;
        }
        strcpy(out->influx_url, val);
        out->has_influx_url = true;
        return 0;
    }
    if (strcasecmp(key, "influx_org") == 0) {
        if (strlen(val) >= sizeof out->influx_org) {
            fprintf(stderr, "config: influx_org value too long\n");
            return -1;
        }
        strcpy(out->influx_org, val);
        out->has_influx_org = true;
        return 0;
    }
    if (strcasecmp(key, "influx_bucket") == 0) {
        if (strlen(val) >= sizeof out->influx_bucket) {
            fprintf(stderr, "config: influx_bucket value too long\n");
            return -1;
        }
        strcpy(out->influx_bucket, val);
        out->has_influx_bucket = true;
        return 0;
    }
    if (strcasecmp(key, "influx_token") == 0) {
        if (strlen(val) >= sizeof out->influx_token) {
            fprintf(stderr, "config: influx_token value too long\n");
            return -1;
        }
        strcpy(out->influx_token, val);
        out->has_influx_token = true;
        return 0;
    }
    if (strcasecmp(key, "influx_upload_interval_ms") == 0) {
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 10);
        if (errno != 0 || end == val || *end != '\0' || v > 86400000UL) {
            fprintf(stderr,
                    "config: influx_upload_interval_ms: invalid "
                    "(use 1..86400000 ms)\n");
            return -1;
        }
        out->influx_upload_interval_ms     = (uint32_t)v;
        out->has_influx_upload_interval_ms = true;
        return 0;
    }
    if (strcasecmp(key, "influx_measurement") == 0) {
        if (strlen(val) >= sizeof out->influx_measurement) {
            fprintf(stderr, "config: influx_measurement value too long\n");
            return -1;
        }
        strcpy(out->influx_measurement, val);
        out->has_influx_measurement = true;
        return 0;
    }
    if (strcasecmp(key, "db_enabled") == 0) {
        bool b;
        if (parse_bool(val, &b) != 0) {
            fprintf(stderr, "config: db_enabled: expected boolean\n");
            return -1;
        }
        out->db_enabled = b;
        out->has_db_enabled = true;
        return 0;
    }
    if (strcasecmp(key, "db_path") == 0) {
        if (strlen(val) >= sizeof out->db_path) {
            fprintf(stderr, "config: db_path value too long\n");
            return -1;
        }
        strcpy(out->db_path, val);
        out->has_db_path = true;
        return 0;
    }
    if (strcasecmp(key, "db_table") == 0) {
        if (strlen(val) >= sizeof out->db_table) {
            fprintf(stderr, "config: db_table value too long\n");
            return -1;
        }
        strcpy(out->db_table, val);
        out->has_db_table = true;
        return 0;
    }
    if (strcasecmp(key, "db_key_column") == 0) {
        if (strlen(val) >= sizeof out->db_key_column) {
            fprintf(stderr, "config: db_key_column value too long\n");
            return -1;
        }
        strcpy(out->db_key_column, val);
        out->has_db_key_column = true;
        return 0;
    }
    if (strcasecmp(key, "db_value_column") == 0) {
        if (strlen(val) >= sizeof out->db_value_column) {
            fprintf(stderr, "config: db_value_column value too long\n");
            return -1;
        }
        strcpy(out->db_value_column, val);
        out->has_db_value_column = true;
        return 0;
    }
    if (strcasecmp(key, "db_poll_interval_ms") == 0) {
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 10);
        if (errno != 0 || end == val || *end != '\0' || v > 60000UL) {
            fprintf(stderr,
                    "config: db_poll_interval_ms: invalid (use 1..60000 ms)\n");
            return -1;
        }
        out->db_poll_interval_ms = (uint32_t)v;
        out->has_db_poll_interval_ms = true;
        return 0;
    }
    if (strcasecmp(key, "db_can_interface") == 0) {
        if (strlen(val) >= sizeof out->db_can_interface) {
            fprintf(stderr, "config: db_can_interface value too long\n");
            return -1;
        }
        strcpy(out->db_can_interface, val);
        out->has_db_can_interface = true;
        return 0;
    }
    /* --- Serial radio --- */
    if (strcasecmp(key, "radio_enabled") == 0) {
        bool b;
        if (parse_bool(val, &b) != 0) {
            fprintf(stderr, "config: radio_enabled: expected boolean\n");
            return -1;
        }
        out->radio_enabled     = b;
        out->has_radio_enabled = true;
        return 0;
    }
    if (strcasecmp(key, "radio_device") == 0) {
        if (strlen(val) >= sizeof out->radio_device) {
            fprintf(stderr, "config: radio_device value too long\n");
            return -1;
        }
        strcpy(out->radio_device, val);
        out->has_radio_device = true;
        return 0;
    }
    if (strcasecmp(key, "radio_baud") == 0) {
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 10);
        if (errno != 0 || end == val || *end != '\0' || v > 4000000UL) {
            fprintf(stderr, "config: radio_baud: invalid baud rate\n");
            return -1;
        }
        out->radio_baud     = (uint32_t)v;
        out->has_radio_baud = true;
        return 0;
    }
    if (strcasecmp(key, "radio_flush_interval_ms") == 0) {
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(val, &end, 10);
        if (errno != 0 || end == val || *end != '\0' || v > 86400000UL) {
            fprintf(stderr,
                    "config: radio_flush_interval_ms: invalid "
                    "(use 1..86400000 ms)\n");
            return -1;
        }
        out->radio_flush_interval_ms     = (uint32_t)v;
        out->has_radio_flush_interval_ms = true;
        return 0;
    }
    fprintf(stderr, "config: unknown key '%s' (ignored)\n", key);
    return 0;
}

int config_file_load(const char *path, config_file_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "config: open %s: %s\n", path, strerror(errno));
        return -1;
    }

    char line[1024];
    unsigned lineno = 0;
    while (fgets(line, sizeof line, f)) {
        ++lineno;
        size_t len = strlen(line);
        if (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        trim_inplace(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "config: %s:%u: expected key=value\n",
                    path, lineno);
            fclose(f);
            return -1;
        }
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_inplace(key);
        trim_inplace(val);
        if (key[0] == '\0') {
            fprintf(stderr, "config: %s:%u: empty key\n", path, lineno);
            fclose(f);
            return -1;
        }
        if (set_field(out, key, val) != 0) {
            fclose(f);
            return -1;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "config: read %s: %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}
