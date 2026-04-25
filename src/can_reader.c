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

#define PLACEHOLDER_CAN_ID 0xFFFu

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
                    volatile sig_atomic_t *running) {
    if (fd < 0 || !table || !w || !running) return -1;

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

            decoded_value_t dv;
            if (decoder_extract(&node->sig, frame.data,
                                frame.can_dlc, &dv) != 0) {
                continue;
            }
            writer_append(w, &node->sig, &dv);
            if (influx) influx_accumulate(influx, &node->sig, &dv);
            serial_radio_accumulate(radio, &node->sig, &dv);
        }
        writer_tick(w);
        if (influx) influx_tick(influx);
        serial_radio_tick(radio);
    }

    return 0;
}
