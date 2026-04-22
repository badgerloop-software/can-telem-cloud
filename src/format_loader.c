#include "format_loader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

static const struct {
    const char *name;
    sig_type_t  type;
} TYPE_MAP[] = {
    {"bool",   T_BOOL},
    {"uint8",  T_UINT8},
    {"uint16", T_UINT16},
    {"uint32", T_UINT32},
    {"uint64", T_UINT64},
    {"int32",  T_INT32},
    {"float",  T_FLOAT},
};

static int parse_type(const char *s, sig_type_t *out) {
    for (size_t i = 0; i < sizeof TYPE_MAP / sizeof TYPE_MAP[0]; ++i) {
        if (strcmp(s, TYPE_MAP[i].name) == 0) {
            *out = TYPE_MAP[i].type;
            return 0;
        }
    }
    return -1;
}

static int parse_source(const char *s, sig_source_t *out) {
    if (strcasecmp(s, "can") == 0) {
        *out = SIG_SOURCE_CAN;
        return 0;
    }
    if (strcasecmp(s, "db") == 0) {
        *out = SIG_SOURCE_DB;
        return 0;
    }
    return -1;
}

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (dst_sz == 0) return;
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "format_loader: open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static int validate_size_for_type(sig_type_t t, uint8_t nb) {
    switch (t) {
        case T_BOOL:   return nb == 1;
        case T_UINT8:  return nb == 1 || nb == 2;  /* format.json uses 1 or 2 for uint8/16 */
        case T_UINT16: return nb == 1 || nb == 2;
        case T_UINT32: return nb == 4;
        case T_UINT64: return nb == 8;
        case T_INT32:  return nb == 4;
        case T_FLOAT:  return nb == 1 || nb == 2 || nb == 4 || nb == 8;
    }
    return 0;
}

int signal_table_load(const char *path, signal_table_t *table) {
    memset(table, 0, sizeof *table);

    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return -1;

    cJSON *root = cJSON_ParseWithLength(buf, len);
    free(buf);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        fprintf(stderr, "format_loader: JSON parse error near: %s\n",
                err ? err : "(unknown)");
        return -1;
    }
    if (!cJSON_IsObject(root)) {
        fprintf(stderr, "format_loader: top-level JSON must be an object\n");
        cJSON_Delete(root);
        return -1;
    }

    for (cJSON *item = root->child; item != NULL; item = item->next) {
        const char *name = item->string;
        if (!name) continue;
        int arr_sz = cJSON_GetArraySize(item);
        if (!cJSON_IsArray(item) || arr_sz < 8 || arr_sz > 12) {
            fprintf(stderr, "format_loader: skipping %s: expected 8..12-element array\n",
                    name);
            continue;
        }
        cJSON *c_nbytes    = cJSON_GetArrayItem(item, 0);
        cJSON *c_type      = cJSON_GetArrayItem(item, 1);
        cJSON *c_units     = cJSON_GetArrayItem(item, 2);
        cJSON *c_nmin      = cJSON_GetArrayItem(item, 3);
        cJSON *c_nmax      = cJSON_GetArrayItem(item, 4);
        cJSON *c_category  = cJSON_GetArrayItem(item, 5);
        cJSON *c_canid     = cJSON_GetArrayItem(item, 6);
        cJSON *c_bitoff    = cJSON_GetArrayItem(item, 7);
        cJSON *c_source    = arr_sz > 8  ? cJSON_GetArrayItem(item, 8)  : NULL;
        cJSON *c_db_key    = arr_sz > 9  ? cJSON_GetArrayItem(item, 9)  : NULL;
        cJSON *c_tx_mode   = arr_sz > 10 ? cJSON_GetArrayItem(item, 10) : NULL;
        cJSON *c_tx_min_ms = arr_sz > 11 ? cJSON_GetArrayItem(item, 11) : NULL;

        if (!cJSON_IsNumber(c_nbytes) ||
            !cJSON_IsString(c_type) ||
            !cJSON_IsString(c_units) ||
            !cJSON_IsNumber(c_nmin) ||
            !cJSON_IsNumber(c_nmax) ||
            !cJSON_IsString(c_category) ||
            !cJSON_IsString(c_canid) ||
            !cJSON_IsNumber(c_bitoff)) {
            fprintf(stderr, "format_loader: skipping %s: wrong element types\n",
                    name);
            continue;
        }

        signal_def_t sig;
        memset(&sig, 0, sizeof sig);
        safe_copy(sig.name, sizeof sig.name, name);
        sig.num_bytes = (uint8_t)c_nbytes->valueint;

        if (parse_type(c_type->valuestring, &sig.type) != 0) {
            fprintf(stderr, "format_loader: skipping %s: unknown type %s\n",
                    name, c_type->valuestring);
            continue;
        }
        if (!validate_size_for_type(sig.type, sig.num_bytes)) {
            fprintf(stderr,
                    "format_loader: skipping %s: size %u invalid for type %s\n",
                    name, sig.num_bytes, c_type->valuestring);
            continue;
        }

        safe_copy(sig.units,    sizeof sig.units,    c_units->valuestring);
        safe_copy(sig.category, sizeof sig.category, c_category->valuestring);
        sig.nom_min = c_nmin->valuedouble;
        sig.nom_max = c_nmax->valuedouble;

        const char *hex = c_canid->valuestring;
        errno = 0;
        char *end = NULL;
        unsigned long id = strtoul(hex, &end, 16);
        if (errno != 0 || end == hex || *end != '\0') {
            fprintf(stderr,
                    "format_loader: skipping %s: bad CAN ID '%s'\n", name, hex);
            continue;
        }
        sig.can_id     = (uint32_t)id;
        sig.bit_offset = (uint16_t)c_bitoff->valueint;
        sig.placeholder = (strcasecmp(hex, "FFF") == 0);
        sig.source = SIG_SOURCE_CAN;
        sig.tx_on_change = false;
        sig.tx_min_interval_ms = 0;
        safe_copy(sig.db_key, sizeof sig.db_key, name);

        if (c_source) {
            if (!cJSON_IsString(c_source) ||
                parse_source(c_source->valuestring, &sig.source) != 0) {
                fprintf(stderr, "format_loader: skipping %s: bad source\n", name);
                continue;
            }
        }
        if (c_db_key) {
            if (!cJSON_IsString(c_db_key)) {
                fprintf(stderr, "format_loader: skipping %s: db_key must be string\n",
                        name);
                continue;
            }
            safe_copy(sig.db_key, sizeof sig.db_key, c_db_key->valuestring);
        }
        if (c_tx_mode) {
            if (!cJSON_IsString(c_tx_mode) ||
                strcasecmp(c_tx_mode->valuestring, "on_change") != 0) {
                fprintf(stderr, "format_loader: skipping %s: tx_mode must be on_change\n",
                        name);
                continue;
            }
            sig.tx_on_change = true;
        }
        if (c_tx_min_ms) {
            if (!cJSON_IsNumber(c_tx_min_ms) || c_tx_min_ms->valuedouble < 0 ||
                c_tx_min_ms->valuedouble > 86400000.0) {
                fprintf(stderr,
                        "format_loader: skipping %s: tx_min_interval_ms invalid\n",
                        name);
                continue;
            }
            sig.tx_min_interval_ms = (uint32_t)c_tx_min_ms->valuedouble;
        }
        if (sig.source == SIG_SOURCE_DB) {
            sig.tx_on_change = true;
            if (sig.db_key[0] == '\0') {
                fprintf(stderr, "format_loader: skipping %s: empty db_key\n", name);
                continue;
            }
        }

        sig_node_t *node = malloc(sizeof *node);
        if (!node) {
            fprintf(stderr, "format_loader: out of memory\n");
            cJSON_Delete(root);
            signal_table_free(table);
            return -1;
        }
        node->sig = sig;
        size_t bucket = sig.can_id & (SIG_TABLE_BUCKETS - 1);
        node->next = table->buckets[bucket];
        table->buckets[bucket] = node;
        table->count++;
        if (sig.placeholder) table->placeholder_count++;
    }

    cJSON_Delete(root);
    return 0;
}

void signal_table_free(signal_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < SIG_TABLE_BUCKETS; ++i) {
        sig_node_t *n = table->buckets[i];
        while (n) {
            sig_node_t *next = n->next;
            free(n);
            n = next;
        }
        table->buckets[i] = NULL;
    }
    table->count = 0;
    table->placeholder_count = 0;
}

const sig_node_t *signal_table_lookup(const signal_table_t *table,
                                      uint32_t can_id) {
    if (!table) return NULL;
    return table->buckets[can_id & (SIG_TABLE_BUCKETS - 1)];
}
