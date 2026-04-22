#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "can_reader.h"
#include "config.h"
#include "db_watcher.h"
#include "format_loader.h"
#include "influx.h"
#include "writer.h"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "can_telem - SocketCAN telemetry reader\n"
        "\n"
        "Usage: %s [-c <file>] [-i <iface>] [-f <format.json>] [-o <outdir>] [-h]\n"
        "  -c   path to config file (key=value). If omitted, ./can_telem.conf is\n"
        "       read when that file exists.\n"
        "  -i   CAN interface name          (default: can0)\n"
        "  -f   path to signal format JSON  (default: ./format.json)\n"
        "  -o   output directory for CSVs   (default: ./logs)\n"
        "  -h   show this help\n"
        "\n"
        "CLI flags override values from the config file.\n"
        "InfluxDB Cloud (optional) is configured in the same file; see README.\n",
        argv0);
}

int main(int argc, char **argv) {
    const char *iface   = "can0";
    const char *fmtpath = "./format.json";
    const char *outdir  = "./logs";

    char store_iface[CONFIG_VALUE_MAX];
    char store_fmt[CONFIG_VALUE_MAX];
    char store_out[CONFIG_VALUE_MAX];

    bool cli_i = false, cli_f = false, cli_o = false;
    const char *config_arg = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "c:i:f:o:h")) != -1) {
        switch (opt) {
            case 'c': config_arg = optarg; break;
            case 'i': iface = optarg; cli_i = true; break;
            case 'f': fmtpath = optarg; cli_f = true; break;
            case 'o': outdir = optarg; cli_o = true; break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 2;
        }
    }

    config_file_t cf;
    memset(&cf, 0, sizeof cf);

    const char *cfg_path = NULL;
    if (config_arg) {
        cfg_path = config_arg;
    } else {
        struct stat st;
        if (stat("./can_telem.conf", &st) == 0 && S_ISREG(st.st_mode))
            cfg_path = "./can_telem.conf";
    }

    if (cfg_path) {
        if (config_file_load(cfg_path, &cf) != 0) return 1;
        fprintf(stderr, "can_telem: loaded config from %s\n", cfg_path);
        if (cf.has_can_interface && !cli_i) {
            memcpy(store_iface, cf.can_interface, sizeof store_iface);
            store_iface[sizeof store_iface - 1] = '\0';
            iface = store_iface;
        }
        if (cf.has_format_file && !cli_f) {
            memcpy(store_fmt, cf.format_file, sizeof store_fmt);
            store_fmt[sizeof store_fmt - 1] = '\0';
            fmtpath = store_fmt;
        }
        if (cf.has_output_dir && !cli_o) {
            memcpy(store_out, cf.output_dir, sizeof store_out);
            store_out[sizeof store_out - 1] = '\0';
            outdir = store_out;
        }
    }

    signal_table_t table;
    if (signal_table_load(fmtpath, &table) != 0) {
        fprintf(stderr, "can_telem: failed to load format %s\n", fmtpath);
        return 1;
    }
    fprintf(stderr,
            "can_telem: loaded %zu signals (%zu placeholders) from %s\n",
            table.count, table.placeholder_count, fmtpath);

    writer_t w;
    if (writer_init(&w, outdir) != 0) {
        signal_table_free(&table);
        return 1;
    }

    influx_ctx_t influx;
    memset(&influx, 0, sizeof influx);
    if (influx_init(&influx, &cf) != 0) {
        writer_close(&w);
        signal_table_free(&table);
        return 1;
    }
    influx_ctx_t *pinflux = influx.enabled ? &influx : NULL;

    int fd = can_reader_open(iface);
    if (fd < 0) {
        influx_shutdown(&influx);
        writer_close(&w);
        signal_table_free(&table);
        return 1;
    }
    fprintf(stderr, "can_telem: listening on %s, writing to %s/\n",
            iface, outdir);

    db_watcher_t dbw;
    memset(&dbw, 0, sizeof dbw);
    int db_rc = db_watcher_start(&dbw, &cf, &table, iface, &g_running);
    if (db_rc < 0) {
        close(fd);
        influx_shutdown(&influx);
        writer_close(&w);
        signal_table_free(&table);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int rc = can_reader_loop(fd, &table, &w, pinflux, &g_running);

    fprintf(stderr, "can_telem: shutting down\n");
    close(fd);
    db_watcher_stop(&dbw);
    influx_shutdown(&influx);
    writer_close(&w);
    signal_table_free(&table);
    return rc == 0 ? 0 : 1;
}
