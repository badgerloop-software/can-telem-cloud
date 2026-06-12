#ifndef CAN_TELEM_CAN_READER_H
#define CAN_TELEM_CAN_READER_H

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "format_loader.h"
#include "gnss_reader.h"
#include "influx.h"
#include "serial_radio.h"
#include "writer.h"

int can_reader_open(const char *ifname);

int can_reader_loop(int fd,
                    const signal_table_t *table,
                    writer_t *w,
                    influx_ctx_t *influx,
                    serial_radio_ctx_t *radio,
                    gnss_reader_t *gnss,
                    const config_file_t *cf,
                    volatile sig_atomic_t *running);

#endif
