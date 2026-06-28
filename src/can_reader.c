#define _GNU_SOURCE

#include "can_reader.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "decoder.h"

static int rx_allowed(const config_file_t *cf, const char *sig_name) {
    if (!cf || !cf->has_rx_signals || cf->rx_signals[0] == '\0') return 1;
    return config_list_contains(cf->rx_signals, sig_name);
}

static const signal_def_t *find_signal_by_name(const signal_table_t *table, const char *name) {
    if (!table || !name || !name[0]) return NULL;
    for (size_t b = 0; b < SIG_TABLE_BUCKETS; ++b) {
        for (const sig_node_t *n = table->buckets[b]; n; n = n->next) {
            if (strcmp(n->sig.name, name) == 0) return &n->sig;
        }
    }
    return NULL;
}

static void inject_one_signal(writer_t *w,
                              influx_ctx_t *influx,
                              serial_radio_ctx_t *radio,
                              const config_file_t *cf,
                              const signal_def_t *sig,
                              double value) {
    if (!sig || !rx_allowed(cf, sig->name)) return;
    decoded_value_t dv;
    memset(&dv, 0, sizeof dv);
    dv.value = value;
    strcpy(dv.raw_hex, "gnss");
    writer_append(w, sig, &dv);
    if (influx) influx_accumulate(influx, sig, &dv);
    serial_radio_accumulate(radio, sig, &dv);
}

static void maybe_inject_gnss(const signal_table_t *table,
                              writer_t *w,
                              influx_ctx_t *influx,
                              serial_radio_ctx_t *radio,
                              gnss_reader_t *gnss,
                              const config_file_t *cf,
                              const signal_def_t *lat_sig,
                              const signal_def_t *lon_sig,
                              const signal_def_t *elev_sig) {
    (void)table;
    if (!gnss || !gnss->enabled) return;
    (void)gnss_reader_tick(gnss);

    double lat = 0.0, lon = 0.0, elev = 0.0;
    if (!gnss_reader_get_fix(gnss, &lat, &lon, &elev)) return;

    inject_one_signal(w, influx, radio, cf, lat_sig, lat);
    inject_one_signal(w, influx, radio, cf, lon_sig, lon);
    inject_one_signal(w, influx, radio, cf, elev_sig, elev);
}

static void maybe_inject_status(writer_t *w,
                                influx_ctx_t *influx,
                                serial_radio_ctx_t *radio,
                                const config_file_t *cf,
                                const signal_def_t *lte_sig,
                                const signal_def_t *radio_sig) {
    static struct timespec last_check = {0};
    static bool lte_up = false;
    static bool radio_up = false;
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    // Poll operstate only once every 2 seconds
    if (last_check.tv_sec == 0 || (now.tv_sec - last_check.tv_sec) >= 2) {
        last_check = now;
        
        // Check LTE status
        lte_up = false;
        FILE *f = fopen("/sys/class/net/usb0/operstate", "r");
        if (f) {
            char buf[32];
            if (fgets(buf, sizeof buf, f)) {
                if (strncmp(buf, "up", 2) == 0) {
                    lte_up = true;
                }
            }
            fclose(f);
        }
        
        // Check Radio status (initialized and descriptor active)
        radio_up = (radio && radio->enabled && radio->fd >= 0);
    }
    
    if (lte_sig) {
        inject_one_signal(w, influx, radio, cf, lte_sig, lte_up ? 1.0 : 0.0);
    }
    if (radio_sig) {
        inject_one_signal(w, influx, radio, cf, radio_sig, radio_up ? 1.0 : 0.0);
    }
}

int can_reader_open(const char *ifname) {
    if (!ifname) return -1;

    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        fprintf(stderr, "can_reader: socket: %s\n", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "can_reader: SIOCGIFINDEX %s: %s\n",
                ifname, strerror(errno));
        close(s);
        return -1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "can_reader: bind %s: %s\n", ifname, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

int can_reader_loop(int fd,
                    const signal_table_t *table,
                    writer_t *w,
                    influx_ctx_t *influx,
                    serial_radio_ctx_t *radio,
                    gnss_reader_t *gnss,
                    const config_file_t *cf,
                    volatile sig_atomic_t *running) {
    if (fd < 0 || !table || !w || !running) return -1;

    const char *lat_name = (cf && cf->has_gnss_lat_signal && cf->gnss_lat_signal[0])
        ? cf->gnss_lat_signal : "lat";
    const char *lon_name = (cf && cf->has_gnss_lon_signal && cf->gnss_lon_signal[0])
        ? cf->gnss_lon_signal : "lon";
    const char *elev_name = (cf && cf->has_gnss_elev_signal && cf->gnss_elev_signal[0])
        ? cf->gnss_elev_signal : "elev";
    const signal_def_t *lat_sig = find_signal_by_name(table, lat_name);
    const signal_def_t *lon_sig = find_signal_by_name(table, lon_name);
    const signal_def_t *elev_sig = find_signal_by_name(table, elev_name);

    const signal_def_t *lte_sig = find_signal_by_name(table, "lte_status");
    const signal_def_t *radio_sig = find_signal_by_name(table, "serial_status");

    struct can_frame frame;
    while (*running) {
        int poll_ms = 200;
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int pr = poll(&pfd, 1, poll_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "can_reader: poll: %s\n", strerror(errno));
            return -1;
        }
        if (pr == 0) {
            maybe_inject_gnss(table, w, influx, radio, gnss, cf, lat_sig, lon_sig, elev_sig);
            maybe_inject_status(w, influx, radio, cf, lte_sig, radio_sig);
            writer_tick(w);
            if (influx) influx_tick(influx);
            serial_radio_tick(radio);
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "can_reader: poll: socket error\n");
            return -1;
        }

        ssize_t n = read(fd, &frame, sizeof frame);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "can_reader: read: %s\n", strerror(errno));
            return -1;
        }
        if (n < (ssize_t)sizeof frame) {
            fprintf(stderr, "can_reader: short read %zd bytes\n", n);
            continue;
        }
        if (frame.can_id & CAN_ERR_FLAG) continue;
        uint32_t id = frame.can_id & CAN_EFF_MASK;
        if (!(frame.can_id & CAN_EFF_FLAG)) id &= CAN_SFF_MASK;

        const sig_node_t *node = signal_table_lookup(table, id);
        for (; node; node = node->next) {
            if (node->sig.can_id != id) continue;
            if (node->sig.placeholder) continue;
            if (!rx_allowed(cf, node->sig.name)) continue;

            decoded_value_t dv;
            if (decoder_extract(&node->sig, frame.data,
                                frame.can_dlc, &dv) != 0) {
                continue;
            }
            writer_append(w, &node->sig, &dv);
            if (influx) influx_accumulate(influx, &node->sig, &dv);
            serial_radio_accumulate(radio, &node->sig, &dv);
        }

        maybe_inject_gnss(table, w, influx, radio, gnss, cf, lat_sig, lon_sig, elev_sig);
        maybe_inject_status(w, influx, radio, cf, lte_sig, radio_sig);
        writer_tick(w);
        if (influx) influx_tick(influx);
        serial_radio_tick(radio);
    }
    return 0;
}
