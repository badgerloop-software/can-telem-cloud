#define _GNU_SOURCE

#include "serial_radio.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static size_t hash_name(const char *s) {
    size_t h = 5381;
    for (; *s; ++s) h = ((h << 5) + h) + (unsigned char)*s;
    return h;
}

static speed_t baud_to_speed(unsigned baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B0;
    }
}

static int64_t mono_diff_ms(const struct timespec *a,
                             const struct timespec *b) {
    return (int64_t)(a->tv_sec  - b->tv_sec)  * 1000LL +
           (int64_t)(a->tv_nsec - b->tv_nsec) / 1000000LL;
}

int serial_radio_init(serial_radio_ctx_t *ctx, const config_file_t *cf) {
    memset(ctx, 0, sizeof *ctx);
    ctx->fd = -1;

    if (!cf || !cf->radio_enabled) return 0;

    const char *device = (cf->has_radio_device && cf->radio_device[0] != '\0')
                         ? cf->radio_device : "/dev/ttyUSB0";

    unsigned baud = (cf->has_radio_baud && cf->radio_baud != 0)
                    ? cf->radio_baud : 9600u;

    unsigned interval = (cf->has_radio_flush_interval_ms &&
                         cf->radio_flush_interval_ms != 0)
                        ? cf->radio_flush_interval_ms : 1000u;
    ctx->interval_ms = interval;

    speed_t speed = baud_to_speed(baud);
    if (speed == B0) {
        fprintf(stderr, "serial_radio: unsupported baud rate %u\n", baud);
        return -1;
    }

    int fd = open(device, O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "serial_radio: open %s: %s\n", device, strerror(errno));
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "serial_radio: tcgetattr %s: %s\n",
                device, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tty);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    tty.c_cflag |=  CLOCAL | CREAD;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "serial_radio: tcsetattr %s: %s\n",
                device, strerror(errno));
        close(fd);
        return -1;
    }

    ctx->fd      = fd;
    ctx->enabled = true;

    fprintf(stderr,
            "serial_radio: enabled, device=%s, baud=%u, interval=%ums\n",
            device, baud, interval);
    return 0;
}

void serial_radio_accumulate(serial_radio_ctx_t *ctx,
                             const signal_def_t *sig,
                             const decoded_value_t *dv) {
    if (!ctx || !ctx->enabled || !sig || !dv) return;

    size_t idx = hash_name(sig->name) % RADIO_SLOTS;
    for (size_t i = 0; i < RADIO_SLOTS; ++i) {
        size_t probe = (idx + i) % RADIO_SLOTS;
        if (!ctx->slots[probe].used) {
            size_t nn = strlen(sig->name);
            if (nn >= SIG_NAME_MAX) nn = SIG_NAME_MAX - 1;
            memcpy(ctx->slots[probe].name, sig->name, nn);
            ctx->slots[probe].name[nn] = '\0';
            ctx->slots[probe].value    = dv->value;
            ctx->slots[probe].used     = true;
            return;
        }
        if (strcmp(ctx->slots[probe].name, sig->name) == 0) {
            ctx->slots[probe].value = dv->value;
            return;
        }
    }
    fprintf(stderr, "serial_radio: slot table full, dropping %s\n", sig->name);
}

void serial_radio_tick(serial_radio_ctx_t *ctx) {
    if (!ctx || !ctx->enabled || ctx->fd < 0) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (ctx->have_last_flush) {
        if (mono_diff_ms(&now, &ctx->last_flush_mono) < (int64_t)ctx->interval_ms)
            return;
    }
    ctx->last_flush_mono = now;
    ctx->have_last_flush = true;

    struct timespec wall;
    clock_gettime(CLOCK_REALTIME, &wall);
    int64_t ts_ns = (int64_t)wall.tv_sec * 1000000000LL +
                    (int64_t)wall.tv_nsec;

    for (size_t i = 0; i < RADIO_SLOTS; ++i) {
        if (!ctx->slots[i].used) continue;

        char line[SIG_NAME_MAX + 64];
        int n = snprintf(line, sizeof line,
                         "%" PRId64 ",%s,%.9g\n",
                         ts_ns, ctx->slots[i].name, ctx->slots[i].value);
        if (n > 0 && (size_t)n < sizeof line) {
            ssize_t w = write(ctx->fd, line, (size_t)n);
            if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "serial_radio: write: %s\n", strerror(errno));
            }
        }

        ctx->slots[i].used     = false;
        ctx->slots[i].name[0]  = '\0';
    }
}

void serial_radio_shutdown(serial_radio_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->fd >= 0) {
        tcdrain(ctx->fd);
        close(ctx->fd);
        ctx->fd = -1;
    }
    ctx->enabled = false;
}
