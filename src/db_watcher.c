#define _POSIX_C_SOURCE 200809L

#include "db_watcher.h"

#include <ctype.h>
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <math.h>
#include <net/if.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "encoder.h"

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (dst_sz == 0) return;
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int valid_sql_identifier(const char *s) {
    if (!s || !s[0]) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (const char *p = s + 1; *p; ++p) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    }
    return 1;
}

static int parse_bool_text(const unsigned char *txt, double *out) {
    if (!txt || !out) return -1;
    if (strcasecmp((const char *)txt, "true") == 0 ||
        strcmp((const char *)txt, "1") == 0) {
        *out = 1.0;
        return 0;
    }
    if (strcasecmp((const char *)txt, "false") == 0 ||
        strcmp((const char *)txt, "0") == 0) {
        *out = 0.0;
        return 0;
    }
    return -1;
}

static int parse_db_value(const signal_def_t *sig,
                          sqlite3_stmt *stmt,
                          double *out) {
    int t = sqlite3_column_type(stmt, 0);
    if (t == SQLITE_NULL) return -1;
    if (sig->type == T_BOOL) {
        if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) {
            *out = sqlite3_column_double(stmt, 0) != 0.0 ? 1.0 : 0.0;
            return 0;
        }
        if (t == SQLITE_TEXT) {
            return parse_bool_text(sqlite3_column_text(stmt, 0), out);
        }
        return -1;
    }

    if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) {
        *out = sqlite3_column_double(stmt, 0);
        return isfinite(*out) ? 0 : -1;
    }
    if (t == SQLITE_TEXT) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (!txt) return -1;
        errno = 0;
        char *end = NULL;
        double v = strtod((const char *)txt, &end);
        if (errno != 0 || end == (const char *)txt || *end != '\0' || !isfinite(v))
            return -1;
        *out = v;
        return 0;
    }
    return -1;
}

static int values_equal(double a, double b) {
    double diff = fabs(a - b);
    double scale = fmax(1.0, fmax(fabs(a), fabs(b)));
    return diff <= (1e-9 * scale);
}

static int open_can_tx(const char *ifname) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        fprintf(stderr, "db_watcher: socket: %s\n", strerror(errno));
        return -1;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    size_t ifn = strnlen(ifname, IFNAMSIZ);
    if (ifn == IFNAMSIZ) {
        fprintf(stderr, "db_watcher: interface name too long: %s\n", ifname);
        close(s);
        return -1;
    }
    memcpy(ifr.ifr_name, ifname, ifn);
    ifr.ifr_name[ifn] = '\0';
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "db_watcher: SIOCGIFINDEX %s: %s\n",
                ifname, strerror(errno));
        close(s);
        return -1;
    }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "db_watcher: bind %s: %s\n", ifname, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static int ensure_db_open(db_watcher_t *ctx) {
    if (ctx->db) return 0;
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(ctx->db_path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_watcher: sqlite open %s: %s\n",
                ctx->db_path, db ? sqlite3_errmsg(db) : "unknown");
        if (db) sqlite3_close(db);
        return -1;
    }
    ctx->db = db;
    return 0;
}

static void close_db(db_watcher_t *ctx) {
    if (!ctx->db) return;
    sqlite3_close((sqlite3 *)ctx->db);
    ctx->db = NULL;
}

static struct db_frame_cache *frame_for_can_id(db_watcher_t *ctx, uint32_t id) {
    for (size_t i = 0; i < ctx->frame_count; ++i) {
        if (ctx->frames[i].can_id == id) return &ctx->frames[i];
    }
    return NULL;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static int send_signal(db_watcher_t *ctx, struct db_sig_state *st, double value) {
    struct db_frame_cache *fc = frame_for_can_id(ctx, st->sig->can_id);
    if (!fc) return -1;
    if (encoder_insert(st->sig, value, fc->data, fc->dlc) != 0) {
        fprintf(stderr, "db_watcher: skip %s: value rejected by encoder/range\n",
                st->sig->name);
        return -1;
    }
    struct can_frame frame;
    memset(&frame, 0, sizeof frame);
    frame.can_id = st->sig->can_id & CAN_SFF_MASK;
    frame.can_dlc = fc->dlc;
    memcpy(frame.data, fc->data, fc->dlc);
    ssize_t n = write(ctx->tx_fd, &frame, sizeof frame);
    if (n != (ssize_t)sizeof frame) {
        fprintf(stderr, "db_watcher: CAN write %s: %s\n",
                st->sig->name, n < 0 ? strerror(errno) : "short write");
        return -1;
    }
    st->last_tx_ns = monotonic_ns();
    return 0;
}

static void *watcher_thread(void *arg) {
    db_watcher_t *ctx = (db_watcher_t *)arg;
    char sql[256];
    snprintf(sql, sizeof sql,
             "SELECT %s FROM %s WHERE %s = ?1 LIMIT 1",
             ctx->value_column, ctx->table_name, ctx->key_column);

    while (*(ctx->running)) {
        if (ensure_db_open(ctx) != 0) {
            sleep_ms(ctx->poll_interval_ms);
            continue;
        }

        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2((sqlite3 *)ctx->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "db_watcher: prepare query failed: %s\n",
                    sqlite3_errmsg((sqlite3 *)ctx->db));
            close_db(ctx);
            sleep_ms(ctx->poll_interval_ms);
            continue;
        }

        for (size_t i = 0; i < ctx->signal_count && *(ctx->running); ++i) {
            struct db_sig_state *st = &ctx->signals[i];
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            sqlite3_bind_text(stmt, 1, st->sig->db_key, -1, SQLITE_STATIC);

            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                double value = 0.0;
                if (parse_db_value(st->sig, stmt, &value) == 0) {
                    if (!st->has_last) {
                        st->last_value = value;
                        st->has_last = true;
                    } else if (!values_equal(st->last_value, value)) {
                        uint64_t now = monotonic_ns();
                        uint64_t min_ns = (uint64_t)st->sig->tx_min_interval_ms * 1000000ull;
                        uint64_t fail_backoff_ns = (uint64_t)(ctx->poll_interval_ms > 100
                            ? ctx->poll_interval_ms : 100u) * 1000000ull;
                        if (now >= st->retry_after_ns && now - st->last_tx_ns >= min_ns) {
                            if (send_signal(ctx, st, value) == 0) {
                                st->retry_after_ns = 0;
                            } else {
                                st->last_tx_ns = now;
                                st->retry_after_ns = now + fail_backoff_ns;
                            }
                        }
                        st->last_value = value;
                    }
                }
            } else if (rc != SQLITE_DONE) {
                fprintf(stderr, "db_watcher: query failed for %s: %s\n",
                        st->sig->db_key, sqlite3_errmsg((sqlite3 *)ctx->db));
                close_db(ctx);
                break;
            }
        }
        sqlite3_finalize(stmt);
        sleep_ms(ctx->poll_interval_ms);
    }
    return NULL;
}

static uint8_t compute_dlc_for_signal(const signal_def_t *sig) {
    if (sig->type == T_BOOL) {
        uint16_t end_bit = (uint16_t)(sig->bit_offset + 1);
        uint8_t dlc = (uint8_t)((end_bit + 7u) / 8u);
        return dlc == 0 ? 1 : dlc;
    }
    uint16_t end_byte = (uint16_t)(sig->bit_offset / 8u + sig->num_bytes);
    if (end_byte == 0) end_byte = 1;
    return (uint8_t)end_byte;
}

int db_watcher_start(db_watcher_t *ctx,
                     const config_file_t *cf,
                     const signal_table_t *table,
                     const char *default_can_iface,
                     volatile sig_atomic_t *running) {
    if (!ctx || !cf || !table || !default_can_iface || !running) return -1;
    memset(ctx, 0, sizeof *ctx);
    ctx->tx_fd = -1;
    ctx->running = running;

    if (!cf->has_db_enabled || !cf->db_enabled) return 1;
    if (!cf->has_db_path || cf->db_path[0] == '\0') {
        fprintf(stderr, "db_watcher: db_enabled=true requires db_path\n");
        return -1;
    }

    safe_copy(ctx->db_path, sizeof ctx->db_path, cf->db_path);
    safe_copy(ctx->table_name, sizeof ctx->table_name,
              cf->has_db_table ? cf->db_table : "signal_values");
    safe_copy(ctx->key_column, sizeof ctx->key_column,
              cf->has_db_key_column ? cf->db_key_column : "signal_key");
    safe_copy(ctx->value_column, sizeof ctx->value_column,
              cf->has_db_value_column ? cf->db_value_column : "signal_value");
    safe_copy(ctx->can_iface, sizeof ctx->can_iface,
              cf->has_db_can_interface ? cf->db_can_interface : default_can_iface);
    ctx->poll_interval_ms = cf->has_db_poll_interval_ms
        ? cf->db_poll_interval_ms : 200;
    if (ctx->poll_interval_ms == 0) ctx->poll_interval_ms = 200;

    if (!valid_sql_identifier(ctx->table_name) ||
        !valid_sql_identifier(ctx->key_column) ||
        !valid_sql_identifier(ctx->value_column)) {
        fprintf(stderr, "db_watcher: db_table/db_key_column/db_value_column must be [A-Za-z_][A-Za-z0-9_]*\n");
        return -1;
    }

    for (size_t b = 0; b < SIG_TABLE_BUCKETS; ++b) {
        for (const sig_node_t *n = table->buckets[b]; n; n = n->next) {
            if (n->sig.source == SIG_SOURCE_DB && !n->sig.placeholder && n->sig.tx_on_change) {
                ctx->signal_count++;
            }
        }
    }
    if (ctx->signal_count == 0) return 1;

    ctx->signals = calloc(ctx->signal_count, sizeof *ctx->signals);
    if (!ctx->signals) return -1;

    size_t idx = 0;
    for (size_t b = 0; b < SIG_TABLE_BUCKETS; ++b) {
        for (const sig_node_t *n = table->buckets[b]; n; n = n->next) {
            if (n->sig.source == SIG_SOURCE_DB && !n->sig.placeholder && n->sig.tx_on_change) {
                ctx->signals[idx].sig = &n->sig;
                idx++;
            }
        }
    }

    ctx->frames = calloc(ctx->signal_count, sizeof *ctx->frames);
    if (!ctx->frames) {
        free(ctx->signals);
        return -1;
    }
    for (size_t i = 0; i < ctx->signal_count; ++i) {
        uint32_t can_id = ctx->signals[i].sig->can_id;
        struct db_frame_cache *fc = frame_for_can_id(ctx, can_id);
        if (!fc) {
            fc = &ctx->frames[ctx->frame_count++];
            fc->can_id = can_id;
            fc->dlc = 1;
            memset(fc->data, 0, sizeof fc->data);
        }
        uint8_t sig_dlc = compute_dlc_for_signal(ctx->signals[i].sig);
        if (sig_dlc > fc->dlc) fc->dlc = sig_dlc;
    }

    ctx->tx_fd = open_can_tx(ctx->can_iface);
    if (ctx->tx_fd < 0) {
        free(ctx->frames);
        free(ctx->signals);
        return -1;
    }

    if (pthread_create(&ctx->thread, NULL, watcher_thread, ctx) != 0) {
        fprintf(stderr, "db_watcher: pthread_create failed\n");
        close(ctx->tx_fd);
        ctx->tx_fd = -1;
        free(ctx->frames);
        free(ctx->signals);
        return -1;
    }
    ctx->enabled = true;
    fprintf(stderr, "db_watcher: enabled with %zu DB-backed signals\n", ctx->signal_count);
    return 0;
}

void db_watcher_stop(db_watcher_t *ctx) {
    if (!ctx) return;
    if (ctx->enabled) pthread_join(ctx->thread, NULL);
    if (ctx->tx_fd >= 0) close(ctx->tx_fd);
    ctx->tx_fd = -1;
    close_db(ctx);
    free(ctx->signals);
    free(ctx->frames);
    ctx->signals = NULL;
    ctx->frames = NULL;
    ctx->signal_count = 0;
    ctx->frame_count = 0;
    ctx->enabled = false;
}
