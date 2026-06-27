#define _POSIX_C_SOURCE 200809L

#include "writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char *MONTHS[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static int mkdir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static int64_t mono_diff_ms(const struct timespec *a,
                            const struct timespec *b) {
    return (int64_t)(a->tv_sec - b->tv_sec) * 1000LL +
           (int64_t)(a->tv_nsec - b->tv_nsec) / 1000000LL;
}

static int64_t wall_clock_ms(const struct timespec *wall) {
    return (int64_t)wall->tv_sec * 1000LL + (int64_t)wall->tv_nsec / 1000000LL;
}

static int cmp_strptr(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

static ssize_t column_index(const writer_t *w, const char *name) {
    for (size_t i = 0; i < w->col_count; ++i) {
        if (strcmp(w->col_names[i], name) == 0) return (ssize_t)i;
    }
    return -1;
}

static void format_day_key(const struct tm *tm, char *buf, size_t sz) {
    snprintf(buf, sz, "%02d-%s-%04d",
             tm->tm_mday,
             MONTHS[tm->tm_mon],
             tm->tm_year + 1900);
}

static int build_snapshot_path(const writer_t *w,
                               const struct tm *tm,
                               char *path,
                               size_t path_sz) {
    char day_key[16];
    format_day_key(tm, day_key, sizeof day_key);
    return snprintf(path, path_sz, "%s/%s/" WRITER_SNAPSHOT_BASENAME "_%02d-%02d-%02d.csv",
                    w->out_dir, day_key,
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static int ensure_day_directory(const writer_t *w, const char *day_key) {
    char day_path[640];
    int n = snprintf(day_path, sizeof day_path, "%s/%s", w->out_dir, day_key);
    if (n < 0 || (size_t)n >= sizeof day_path) return -1;
    return mkdir_p(day_path);
}

static int writer_write_header(writer_t *w) {
    if (!w || !w->f) return -1;

    if (fprintf(w->f, "timestamp_ms") < 0) return -1;
    for (size_t c = 0; c < w->col_count; ++c) {
        if (fprintf(w->f, ",%s", w->col_names[c]) < 0) return -1;
    }
    if (fprintf(w->f, "\n") < 0) return -1;
    return fflush(w->f) == 0 ? 0 : -1;
}

static int writer_open_snapshot(writer_t *w, const struct tm *tm) {
    char day_key[16];
    char path[sizeof w->snapshot_path];

    format_day_key(tm, day_key, sizeof day_key);
    if (ensure_day_directory(w, day_key) != 0) {
        fprintf(stderr, "writer: mkdir %s/%s: %s\n",
                w->out_dir, day_key, strerror(errno));
        return -1;
    }

    if (build_snapshot_path(w, tm, path, sizeof path) < 0
        || (size_t)snprintf(w->snapshot_path, sizeof w->snapshot_path, "%s", path)
           >= sizeof w->snapshot_path) {
        fprintf(stderr, "writer: snapshot path too long\n");
        return -1;
    }

    if (w->f) {
        fflush(w->f);
        fclose(w->f);
        w->f = NULL;
    }

    w->f = fopen(w->snapshot_path, "a");
    if (!w->f) {
        fprintf(stderr, "writer: fopen %s: %s\n", w->snapshot_path, strerror(errno));
        return -1;
    }

    memcpy(w->current_day, day_key, sizeof w->current_day);
    w->have_last_flush = false;

    if (writer_write_header(w) != 0) {
        fprintf(stderr, "writer: failed to write header to %s\n", w->snapshot_path);
        fclose(w->f);
        w->f = NULL;
        return -1;
    }

    fprintf(stderr, "writer: logging snapshots to %s\n", w->snapshot_path);
    return 0;
}

static int writer_ensure_open_for_now(writer_t *w) {
    struct timespec wall;
    struct tm local_tm;

    clock_gettime(CLOCK_REALTIME, &wall);
    if (!localtime_r(&wall.tv_sec, &local_tm)) return -1;

    char day_key[16];
    format_day_key(&local_tm, day_key, sizeof day_key);
    if (w->f && strcmp(w->current_day, day_key) == 0) return 0;

    return writer_open_snapshot(w, &local_tm);
}

int writer_init(writer_t *w, const char *out_dir, const signal_table_t *table) {
    if (!w || !out_dir || !table) return -1;
    memset(w, 0, sizeof *w);
    w->snapshot_interval_ms = WRITER_DEFAULT_SNAPSHOT_INTERVAL_MS;

    size_t n = strlen(out_dir);
    if (n >= sizeof w->out_dir) {
        fprintf(stderr, "writer: output path too long\n");
        return -1;
    }
    memcpy(w->out_dir, out_dir, n + 1);

    if (mkdir_p(w->out_dir) != 0) {
        fprintf(stderr, "writer: mkdir %s: %s\n", w->out_dir, strerror(errno));
        return -1;
    }

    size_t cols = 0;
    for (size_t b = 0; b < SIG_TABLE_BUCKETS; ++b) {
        for (const sig_node_t *node = table->buckets[b]; node; node = node->next) {
            cols++;
        }
    }
    if (cols == 0) {
        fprintf(stderr, "writer: no non-placeholder signals in table\n");
        return -1;
    }

    w->col_count = cols;
    w->col_names = calloc(cols, sizeof *w->col_names);
    w->col_values = calloc(cols, sizeof *w->col_values);
    w->col_seen = calloc(cols, sizeof *w->col_seen);
    if (!w->col_names || !w->col_values || !w->col_seen) {
        fprintf(stderr, "writer: memory allocation failed\n");
        writer_close(w);
        return -1;
    }

    size_t i = 0;
    for (size_t b = 0; b < SIG_TABLE_BUCKETS; ++b) {
        for (const sig_node_t *node = table->buckets[b]; node; node = node->next) {
            w->col_names[i] = strdup(node->sig.name);
            if (!w->col_names[i]) {
                fprintf(stderr, "writer: strdup failed\n");
                writer_close(w);
                return -1;
            }
            i++;
        }
    }

    qsort(w->col_names, w->col_count, sizeof *w->col_names, cmp_strptr);

    size_t uniq = 0;
    for (size_t k = 0; k < w->col_count; ++k) {
        if (uniq == 0 || strcmp(w->col_names[k], w->col_names[uniq - 1]) != 0) {
            w->col_names[uniq++] = w->col_names[k];
        } else {
            free(w->col_names[k]);
        }
    }
    w->col_count = uniq;

    if (writer_ensure_open_for_now(w) != 0) {
        writer_close(w);
        return -1;
    }

    return 0;
}

int writer_append(writer_t *w,
                  const signal_def_t *sig,
                  const decoded_value_t *dv) {
    if (!w || !sig || !dv || !w->f) return -1;
    ssize_t idx = column_index(w, sig->name);
    if (idx < 0) return 0;
    w->col_values[idx] = dv->value;
    w->col_seen[idx] = true;
    return 0;
}

void writer_tick(writer_t *w) {
    if (!w) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!w->have_last_flush) {
        w->last_flush_mono = now;
        w->have_last_flush = true;
        return;
    }
    if (mono_diff_ms(&now, &w->last_flush_mono) < (int64_t)w->snapshot_interval_ms)
        return;

    if (writer_ensure_open_for_now(w) != 0) return;

    struct timespec wall;
    clock_gettime(CLOCK_REALTIME, &wall);
    int64_t ts_ms = wall_clock_ms(&wall);

    if (fprintf(w->f, "%lld", (long long)ts_ms) < 0) return;
    for (size_t c = 0; c < w->col_count; ++c) {
        if (!w->col_seen[c]) {
            if (fprintf(w->f, ",") < 0) return;
        } else {
            if (fprintf(w->f, ",%.9g", w->col_values[c]) < 0) return;
        }
    }
    if (fprintf(w->f, "\n") < 0) return;
    fflush(w->f);
    w->last_flush_mono = now;
}

void writer_close(writer_t *w) {
    if (!w) return;
    if (w->f) {
        fflush(w->f);
        fclose(w->f);
        w->f = NULL;
    }
    if (w->col_names) {
        for (size_t i = 0; i < w->col_count; ++i) {
            free(w->col_names[i]);
        }
        free(w->col_names);
        w->col_names = NULL;
    }
    free(w->col_values);
    w->col_values = NULL;
    free(w->col_seen);
    w->col_seen = NULL;
    w->col_count = 0;
    w->have_last_flush = false;
    w->current_day[0] = '\0';
    w->snapshot_path[0] = '\0';
}
