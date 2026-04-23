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

static const char CSV_HEADER[] = "timestamp_ns,value,raw_hex\n";
static const char UNKNOWN_CSV_HEADER[] = "timestamp_ns,dlc,data_hex\n";
static const char UNKNOWN_DIR_NAME[] = "unknown_ids";
static const uint8_t MAX_CAN_DLC = 8;
enum {
    UNKNOWN_PATH_SUFFIX_MAX = 16, /* '/' + 8 hex chars + ".csv" + '\0' */
    UNKNOWN_HEX_BUFFER_SIZE = (8 * 2) + 1
};

/* djb2 string hash */
static size_t hash_name(const char *s) {
    size_t h = 5381;
    for (; *s; ++s) h = ((h << 5) + h) + (unsigned char)*s;
    return h;
}

static int mkdir_p(const char *path) {
    /* Accept an existing directory, create a single-level directory otherwise.
     * We don't walk components because the plan only needs one level. */
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

int writer_init(writer_t *w, const char *out_dir) {
    if (!w || !out_dir) return -1;
    memset(w, 0, sizeof *w);

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
    int dlen = snprintf(w->unknown_dir, sizeof w->unknown_dir,
                        "%s/%s", w->out_dir, UNKNOWN_DIR_NAME);
    if (dlen < 0 || (size_t)dlen >= sizeof w->unknown_dir) {
        fprintf(stderr, "writer: unknown output path too long\n");
        return -1;
    }
    if (mkdir_p(w->unknown_dir) != 0) {
        fprintf(stderr, "writer: mkdir %s: %s\n",
                w->unknown_dir, strerror(errno));
        return -1;
    }
    return 0;
}

static FILE *lookup_or_open(writer_t *w, const char *name) {
    size_t idx = hash_name(name) % WRITER_CACHE_SIZE;
    for (size_t i = 0; i < WRITER_CACHE_SIZE; ++i) {
        size_t probe = (idx + i) % WRITER_CACHE_SIZE;
        writer_entry_t *e = &w->entries[probe];
        if (e->file == NULL && e->name[0] == '\0') {
            /* Empty slot: open file here. */
            char path[sizeof w->out_dir + SIG_NAME_MAX + 8];
            int len = snprintf(path, sizeof path, "%s/%s.csv", w->out_dir, name);
            if (len < 0 || (size_t)len >= sizeof path) {
                fprintf(stderr, "writer: path too long for %s\n", name);
                return NULL;
            }
            struct stat st;
            int newly_created = (stat(path, &st) != 0);
            FILE *f = fopen(path, "a");
            if (!f) {
                fprintf(stderr, "writer: fopen %s: %s\n", path, strerror(errno));
                return NULL;
            }
            if (newly_created) {
                fwrite(CSV_HEADER, 1, sizeof CSV_HEADER - 1, f);
            }
            size_t nn = strlen(name);
            if (nn >= sizeof e->name) nn = sizeof e->name - 1;
            memcpy(e->name, name, nn);
            e->name[nn] = '\0';
            e->file = f;
            w->open_count++;
            return f;
        }
        if (e->name[0] != '\0' && strcmp(e->name, name) == 0) {
            return (FILE *)e->file;
        }
    }
    fprintf(stderr, "writer: cache full, cannot open %s\n", name);
    return NULL;
}

int writer_append(writer_t *w,
                  const signal_def_t *sig,
                  const decoded_value_t *dv) {
    if (!w || !sig || !dv) return -1;
    FILE *f = lookup_or_open(w, sig->name);
    if (!f) return -1;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long ns = (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;

    if (fprintf(f, "%lld,%.9g,%s\n", ns, dv->value, dv->raw_hex) < 0) {
        fprintf(stderr, "writer: fprintf %s: %s\n", sig->name, strerror(errno));
        return -1;
    }
    fflush(f);
    return 0;
}

int writer_append_unknown(writer_t *w,
                          uint32_t can_id,
                          const uint8_t *payload,
                          uint8_t dlc) {
    if (!w || !payload) return -1;

    if (dlc > MAX_CAN_DLC) dlc = MAX_CAN_DLC;

    char path[sizeof(w->unknown_dir) + UNKNOWN_PATH_SUFFIX_MAX];
    int len = snprintf(path, sizeof path, "%s/%08X.csv", w->unknown_dir, can_id);
    if (len < 0 || (size_t)len >= sizeof path) {
        fprintf(stderr, "writer: unknown path too long for %08X\n", can_id);
        return -1;
    }

    struct stat st;
    errno = 0;
    int st_rc = stat(path, &st);
    int newly_created = (st_rc != 0 && errno == ENOENT);
    if (st_rc != 0 && errno != ENOENT) {
        fprintf(stderr, "writer: stat %s: %s\n", path, strerror(errno));
        return -1;
    }
    FILE *f = fopen(path, "a");
    if (!f) {
        fprintf(stderr, "writer: fopen %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (newly_created) {
        fwrite(UNKNOWN_CSV_HEADER, 1, sizeof UNKNOWN_CSV_HEADER - 1, f);
    }

    static const char HEX[] = "0123456789ABCDEF";
    char hex[UNKNOWN_HEX_BUFFER_SIZE];
    for (uint8_t i = 0; i < dlc; ++i) {
        hex[(size_t)(i * 2)] = HEX[(payload[i] >> 4) & 0x0F];
        hex[(size_t)(i * 2) + 1] = HEX[payload[i] & 0x0F];
    }
    hex[(size_t)dlc * 2] = '\0';

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long ns = (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;

    int rc = 0;
    if (fprintf(f, "%lld,%u,%s\n", ns, (unsigned)dlc, hex) < 0) {
        fprintf(stderr, "writer: fprintf unknown %08X: %s\n",
                can_id, strerror(errno));
        rc = -1;
    } else {
        fflush(f);
    }
    fclose(f);
    return rc;
}

void writer_close(writer_t *w) {
    if (!w) return;
    for (size_t i = 0; i < WRITER_CACHE_SIZE; ++i) {
        if (w->entries[i].file) {
            fclose((FILE *)w->entries[i].file);
            w->entries[i].file = NULL;
            w->entries[i].name[0] = '\0';
        }
    }
    w->open_count = 0;
}
