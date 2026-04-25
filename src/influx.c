#define _GNU_SOURCE

#include "influx.h"

#include <curl/curl.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t hash_name(const char *s) {
    size_t h = 5381;
    for (; *s; ++s) h = ((h << 5) + h) + (unsigned char)*s;
    return h;
}

static void strip_trailing_slash(char *url) {
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        url[--n] = '\0';
    }
}

/* Influx line protocol: escape tag value characters , = space \ */
static size_t lp_escape_tag_value(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        if (o + 2 >= outsz) return 0;
        unsigned char c = *p;
        if (c == ',' || c == '=' || c == ' ' || c == '\\') {
            out[o++] = '\\';
        }
        if (c == '\n' || c == '\r') return 0;
        out[o++] = (char)c;
    }
    if (o >= outsz) return 0;
    out[o] = '\0';
    return o;
}

static int build_write_url(CURL *curl,
                           const char *base_url,
                           const char *org,
                           const char *bucket,
                           char **out_url) {
    char base[CONFIG_VALUE_MAX];
    if (strlen(base_url) >= sizeof base) return -1;
    memcpy(base, base_url, strlen(base_url) + 1);
    strip_trailing_slash(base);

    char *org_e   = curl_easy_escape(curl, org, 0);
    char *bucket_e = curl_easy_escape(curl, bucket, 0);
    if (!org_e || !bucket_e) {
        if (org_e) curl_free(org_e);
        if (bucket_e) curl_free(bucket_e);
        return -1;
    }
    int n = asprintf(out_url, "%s/api/v2/write?org=%s&bucket=%s&precision=ns",
                     base, org_e, bucket_e);
    curl_free(org_e);
    curl_free(bucket_e);
    return n < 0 ? -1 : 0;
}

static int64_t timespec_diff_ms(const struct timespec *a,
                                const struct timespec *b) {
    return (int64_t)(a->tv_sec - b->tv_sec) * 1000LL +
           (int64_t)(a->tv_nsec - b->tv_nsec) / 1000000LL;
}

static int measurement_valid(const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        unsigned char c = *p;
        if (!isalnum(c) && c != '_') return 0;
    }
    return s[0] != '\0';
}

static int is_fault_signal(const char *name) {
    if (!name || !name[0]) return 0;
    char lower[SIG_NAME_MAX * 2];
    size_t n = strlen(name);
    if (n >= sizeof lower) n = sizeof lower - 1;
    for (size_t i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)name[i]);
    lower[n] = '\0';
    return strstr(lower, "fault") != NULL;
}

static int influx_post_body(influx_ctx_t *ctx, const char *body, size_t len) {
    curl_easy_setopt((CURL *)ctx->curl, CURLOPT_URL, ctx->write_url);
    curl_easy_setopt((CURL *)ctx->curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt((CURL *)ctx->curl, CURLOPT_POSTFIELDSIZE, (long)len);
    CURLcode cr = curl_easy_perform((CURL *)ctx->curl);
    if (cr != CURLE_OK) {
        fprintf(stderr, "influx: curl_easy_perform: %s\n", curl_easy_strerror(cr));
        return -1;
    }
    long http = 0;
    curl_easy_getinfo((CURL *)ctx->curl, CURLINFO_RESPONSE_CODE, &http);
    if (http != 200 && http != 204) {
        fprintf(stderr, "influx: HTTP %ld (expected 204)\n", http);
        return -1;
    }
    return 0;
}

static void influx_write_fault_event(influx_ctx_t *ctx,
                                     const char *sig_name,
                                     bool state,
                                     int64_t ts_ns) {
    if (!ctx || !ctx->enabled || !sig_name) return;
    char esc_name[SIG_NAME_MAX * 2];
    if (lp_escape_tag_value(sig_name, esc_name, sizeof esc_name) == 0) return;
    char line[512];
    int n = snprintf(line, sizeof line,
                     "%s,fault_name=%s state=%s %lld\n",
                     ctx->fault_measurement,
                     esc_name,
                     state ? "true" : "false",
                     (long long)ts_ns);
    if (n <= 0 || (size_t)n >= sizeof line) return;
    (void)influx_post_body(ctx, line, (size_t)n);
}

static void agg_reset_slot(influx_ctx_t *ctx, size_t i) {
    ctx->slots[i].used      = false;
    ctx->slots[i].bool_any = false;
    ctx->slots[i].bool_seen = false;
    ctx->slots[i].sum      = 0.0;
    ctx->slots[i].count    = 0;
    ctx->slots[i].name[0]  = '\0';
}

static void influx_flush(influx_ctx_t *ctx) {
    if (!ctx->enabled || !ctx->curl) return;

    struct timespec wall;
    clock_gettime(CLOCK_REALTIME, &wall);
    int64_t ts_ns = (int64_t)wall.tv_sec * 1000000000LL + wall.tv_nsec;

    size_t cap = 32768;
    char *body = malloc(cap);
    if (!body) {
        fprintf(stderr, "influx: out of memory for write body\n");
        goto reset_slots;
    }
    size_t off = 0;
    int lines = 0;

    for (size_t i = 0; i < INFLUX_AGG_SLOTS; ++i) {
        if (!ctx->slots[i].used) continue;

        char esc_name[SIG_NAME_MAX * 2];
        if (lp_escape_tag_value(ctx->slots[i].name, esc_name, sizeof esc_name) == 0) {
            fprintf(stderr, "influx: signal name too long or invalid: %s\n",
                    ctx->slots[i].name);
            continue;
        }

        char line[512];
        int n;
        if (ctx->slots[i].type == T_BOOL) {
            if (!ctx->slots[i].bool_seen) continue;
            bool v = ctx->slots[i].bool_any;
            n = snprintf(line, sizeof line,
                         "%s,signal=%s value=%s %" PRId64 "\n",
                         ctx->measurement, esc_name, v ? "true" : "false", ts_ns);
        } else {
            if (ctx->slots[i].count == 0) continue;
            double mean = ctx->slots[i].sum / (double)ctx->slots[i].count;
            n = snprintf(line, sizeof line,
                         "%s,signal=%s value=%.9g %" PRId64 "\n",
                         ctx->measurement, esc_name, mean, ts_ns);
        }
        if (n < 0 || (size_t)n >= sizeof line) {
            fprintf(stderr, "influx: line overflow for %s\n", ctx->slots[i].name);
            continue;
        }
        if (off + (size_t)n + 1 > cap) {
            size_t ncap = cap * 2;
            while (off + (size_t)n + 1 > ncap) ncap *= 2;
            char *nb = realloc(body, ncap);
            if (!nb) {
                fprintf(stderr, "influx: realloc body failed\n");
                free(body);
                goto reset_slots;
            }
            body = nb;
            cap = ncap;
        }
        memcpy(body + off, line, (size_t)n);
        off += (size_t)n;
        lines++;
    }

    if (lines == 0) {
        free(body);
        goto reset_slots;
    }

    (void)influx_post_body(ctx, body, off);

    free(body);

reset_slots:
    for (size_t i = 0; i < INFLUX_AGG_SLOTS; ++i) {
        if (ctx->slots[i].used) agg_reset_slot(ctx, i);
    }
}

int influx_init(influx_ctx_t *ctx, const config_file_t *cf) {
    memset(ctx, 0, sizeof *ctx);
    if (!cf || !cf->influx_enabled) return 0;

    if (!cf->has_influx_url || cf->influx_url[0] == '\0') {
        fprintf(stderr, "influx: influx_enabled but influx_url is missing\n");
        return -1;
    }
    if (!cf->has_influx_org || cf->influx_org[0] == '\0') {
        fprintf(stderr, "influx: influx_enabled but influx_org is missing\n");
        return -1;
    }
    if (!cf->has_influx_bucket || cf->influx_bucket[0] == '\0') {
        fprintf(stderr, "influx: influx_enabled but influx_bucket is missing\n");
        return -1;
    }

    const char *token = NULL;
    if (cf->has_influx_token && cf->influx_token[0] != '\0')
        token = cf->influx_token;
    else
        token = getenv("INFLUX_TOKEN");
    if (!token || token[0] == '\0') {
        fprintf(stderr,
                "influx: set influx_token in config or export INFLUX_TOKEN\n");
        return -1;
    }

    unsigned interval = cf->has_influx_upload_interval_ms
                            ? cf->influx_upload_interval_ms
                            : 1000u;
    if (interval == 0) interval = 1000u;
    ctx->interval_ms = interval;

    if (cf->has_influx_measurement && cf->influx_measurement[0] != '\0') {
        strncpy(ctx->measurement, cf->influx_measurement,
                sizeof ctx->measurement - 1);
        ctx->measurement[sizeof ctx->measurement - 1] = '\0';
    } else {
        strcpy(ctx->measurement, "telemetry_snapshot");
    }
    strcpy(ctx->fault_measurement, "fault_events");
    if (!measurement_valid(ctx->measurement) || !measurement_valid(ctx->fault_measurement)) {
        fprintf(stderr, "influx: invalid measurement name\n");
        return -1;
    }

    CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (g != CURLE_OK) {
        fprintf(stderr, "influx: curl_global_init failed\n");
        return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "influx: curl_easy_init failed\n");
        curl_global_cleanup();
        return -1;
    }

    ctx->write_url = NULL;
    if (build_write_url(curl, cf->influx_url, cf->influx_org, cf->influx_bucket,
                        &ctx->write_url) != 0) {
        fprintf(stderr, "influx: failed to build write URL\n");
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return -1;
    }

    char auth[6144];
    int alen = snprintf(auth, sizeof auth, "Authorization: Token %s", token);
    if (alen < 0 || (size_t)alen >= sizeof auth) {
        fprintf(stderr, "influx: token too long for Authorization header\n");
        goto fail_after_curl;
    }

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, auth);
    hdrs = curl_slist_append(hdrs, "Content-Type: text/plain; charset=utf-8");
    if (!hdrs) {
        fprintf(stderr, "influx: curl_slist_append failed\n");
        goto fail_after_curl;
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    ctx->curl     = curl;
    ctx->headers  = hdrs;
    ctx->enabled  = true;

    fprintf(stderr,
            "influx: enabled, interval=%ums, measurement=%s, fault_measurement=%s, url=%s\n",
            ctx->interval_ms, ctx->measurement, ctx->fault_measurement, ctx->write_url);
    return 0;

fail_after_curl:
    free(ctx->write_url);
    ctx->write_url = NULL;
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return -1;
}

void influx_accumulate(influx_ctx_t *ctx,
                       const signal_def_t *sig,
                       const decoded_value_t *dv) {
    if (!ctx || !ctx->enabled || !sig || !dv) return;

    size_t idx = hash_name(sig->name) % INFLUX_AGG_SLOTS;
    size_t i;
    for (i = 0; i < INFLUX_AGG_SLOTS; ++i) {
        size_t probe = (idx + i) % INFLUX_AGG_SLOTS;
        if (!ctx->slots[probe].used) {
            idx = probe;
            break;
        }
        if (strcmp(ctx->slots[probe].name, sig->name) == 0) {
            idx = probe;
            break;
        }
    }
    if (i == INFLUX_AGG_SLOTS) {
        fprintf(stderr, "influx: aggregation table full, drop %s\n", sig->name);
        return;
    }

    if (!ctx->slots[idx].used) {
        ctx->slots[idx].used = true;
        size_t nl = strlen(sig->name);
        if (nl >= sizeof ctx->slots[idx].name)
            nl = sizeof ctx->slots[idx].name - 1;
        memcpy(ctx->slots[idx].name, sig->name, nl);
        ctx->slots[idx].name[nl] = '\0';
        ctx->slots[idx].type = sig->type;
    }

    if (sig->type == T_BOOL) {
        ctx->slots[idx].bool_seen = true;
        if (dv->value != 0.0) ctx->slots[idx].bool_any = true;
    } else {
        ctx->slots[idx].sum += dv->value;
        ctx->slots[idx].count++;
    }

    if (is_fault_signal(sig->name)) {
        size_t fidx = hash_name(sig->name) % INFLUX_AGG_SLOTS;
        size_t fi;
        for (fi = 0; fi < INFLUX_AGG_SLOTS; ++fi) {
            size_t probe = (fidx + fi) % INFLUX_AGG_SLOTS;
            if (!ctx->fault_slots[probe].used) {
                fidx = probe;
                break;
            }
            if (strcmp(ctx->fault_slots[probe].name, sig->name) == 0) {
                fidx = probe;
                break;
            }
        }
        if (fi < INFLUX_AGG_SLOTS) {
            if (!ctx->fault_slots[fidx].used) {
                ctx->fault_slots[fidx].used = true;
                size_t nl = strlen(sig->name);
                if (nl >= sizeof ctx->fault_slots[fidx].name)
                    nl = sizeof ctx->fault_slots[fidx].name - 1;
                memcpy(ctx->fault_slots[fidx].name, sig->name, nl);
                ctx->fault_slots[fidx].name[nl] = '\0';
            }
            bool state = (dv->value != 0.0);
            if (!ctx->fault_slots[fidx].have_last_state) {
                ctx->fault_slots[fidx].have_last_state = true;
                ctx->fault_slots[fidx].last_state = state;
                if (state) {
                    struct timespec wall;
                    clock_gettime(CLOCK_REALTIME, &wall);
                    int64_t ts_ns = (int64_t)wall.tv_sec * 1000000000LL + wall.tv_nsec;
                    influx_write_fault_event(ctx, sig->name, state, ts_ns);
                }
            } else if (ctx->fault_slots[fidx].last_state != state) {
                ctx->fault_slots[fidx].last_state = state;
                struct timespec wall;
                clock_gettime(CLOCK_REALTIME, &wall);
                int64_t ts_ns = (int64_t)wall.tv_sec * 1000000000LL + wall.tv_nsec;
                influx_write_fault_event(ctx, sig->name, state, ts_ns);
            }
        }
    }
}

void influx_tick(influx_ctx_t *ctx) {
    if (!ctx || !ctx->enabled) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!ctx->have_last_flush) {
        ctx->last_flush_mono   = now;
        ctx->have_last_flush   = true;
        return;
    }

    int64_t dt = timespec_diff_ms(&now, &ctx->last_flush_mono);
    if (dt >= (int64_t)ctx->interval_ms) {
        influx_flush(ctx);
        ctx->last_flush_mono = now;
    }
}

void influx_shutdown(influx_ctx_t *ctx) {
    if (!ctx) return;
    if (!ctx->enabled) {
        memset(ctx, 0, sizeof *ctx);
        return;
    }

    influx_flush(ctx);

    if (ctx->headers) {
        curl_slist_free_all((struct curl_slist *)ctx->headers);
        ctx->headers = NULL;
    }
    if (ctx->curl) {
        curl_easy_cleanup((CURL *)ctx->curl);
        ctx->curl = NULL;
    }
    free(ctx->write_url);
    ctx->write_url = NULL;
    curl_global_cleanup();

    memset(ctx, 0, sizeof *ctx);
}
