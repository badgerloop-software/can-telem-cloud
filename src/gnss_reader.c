#define _POSIX_C_SOURCE 200809L

#include "gnss_reader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"

static int64_t mono_diff_ms(const struct timespec *a,
                            const struct timespec *b) {
    return (int64_t)(a->tv_sec - b->tv_sec) * 1000LL +
           (int64_t)(a->tv_nsec - b->tv_nsec) / 1000000LL;
}

static int mtime_changed(const struct timespec *a, const struct timespec *b) {
    return a->tv_sec != b->tv_sec || a->tv_nsec != b->tv_nsec;
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = calloc((size_t)n + 1, 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

static int json_get_num(cJSON *obj, const char *k1, const char *k2, double *out) {
    cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, k1);
    if ((!n || !cJSON_IsNumber(n)) && k2) n = cJSON_GetObjectItemCaseSensitive(obj, k2);
    if (n && cJSON_IsNumber(n)) {
        *out = n->valuedouble;
        return 0;
    }
    return -1;
}

int gnss_reader_init(gnss_reader_t *ctx, const config_file_t *cf) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof *ctx);

    bool enabled = false;
    if (cf && cf->has_gnss_enabled) enabled = cf->gnss_enabled;
    ctx->enabled = enabled;
    if (!ctx->enabled) return 0;

    const char *cache_path = "/run/can_telem/gnss.json";
    if (cf && cf->has_gnss_cache_path && cf->gnss_cache_path[0] != '\0')
        cache_path = cf->gnss_cache_path;
    if (strlen(cache_path) >= sizeof ctx->cache_path) {
        fprintf(stderr, "gnss_reader: cache path too long\n");
        return -1;
    }
    strcpy(ctx->cache_path, cache_path);

    uint32_t poll_ms = 1000;
    if (cf && cf->has_gnss_poll_interval_ms && cf->gnss_poll_interval_ms != 0)
        poll_ms = cf->gnss_poll_interval_ms;
    if (poll_ms < 100) poll_ms = 100;
    ctx->poll_interval_ms = poll_ms;

    fprintf(stderr, "gnss_reader: enabled, path=%s, interval=%ums\n",
            ctx->cache_path, ctx->poll_interval_ms);
    return 0;
}

int gnss_reader_tick(gnss_reader_t *ctx) {
    if (!ctx || !ctx->enabled) return 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (ctx->have_last_poll &&
        mono_diff_ms(&now, &ctx->last_poll_mono) < (int64_t)ctx->poll_interval_ms) {
        return 0;
    }
    ctx->last_poll_mono = now;
    ctx->have_last_poll = true;

    struct stat st;
    if (stat(ctx->cache_path, &st) != 0) return 0;

    if (ctx->have_cache_mtime && !mtime_changed(&st.st_mtim, &ctx->cache_mtime)) return 0;

    char *txt = read_text_file(ctx->cache_path);
    if (!txt) return -1;

    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (!root) return -1;

    double lat = 0.0, lon = 0.0, elev = 0.0;
    int have_lat = (json_get_num(root, "lat", "latitude", &lat) == 0);
    int have_lon = (json_get_num(root, "lon", "longitude", &lon) == 0);
    (void)json_get_num(root, "elev", "altitude", &elev);
    cJSON_Delete(root);

    if (!have_lat || !have_lon) return 0;

    ctx->lat = lat;
    ctx->lon = lon;
    ctx->elev = elev;
    ctx->have_fix = true;
    ctx->cache_mtime = st.st_mtim;
    ctx->have_cache_mtime = true;
    return 1;
}

int gnss_reader_get_fix(const gnss_reader_t *ctx, double *lat, double *lon, double *elev) {
    if (!ctx || !ctx->enabled || !ctx->have_fix) return 0;
    if (lat) *lat = ctx->lat;
    if (lon) *lon = ctx->lon;
    if (elev) *elev = ctx->elev;
    return 1;
}

void gnss_reader_shutdown(gnss_reader_t *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof *ctx);
}
