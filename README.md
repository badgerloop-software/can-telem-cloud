# can-telem-cloud

A lightweight C daemon for Raspberry Pi that reads raw CAN frames from a SocketCAN interface, decodes every signal defined in a JSON format file, and fans the data out to three independent sinks simultaneously:

| Sink | What it does |
|------|-------------|
| **CSV logger** | Appends every decoded sample to a per-signal `.csv` file on a USB drive |
| **InfluxDB** | Batches samples and uploads to InfluxDB Cloud on a configurable interval |
| **Serial radio** | Periodically serializes the latest value of every active signal and writes it to a UART radio (e.g. RFD900A) for wireless ground-station reception |

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                          STARTUP                                     │
│                                                                      │
│  can_telem.conf ──┐                                                  │
│  CLI flags     ───┼──► Parse config & credentials                   │
│  INFLUX_TOKEN  ───┘         │                                        │
│                             ├──► Load format.json → signal hash table│
│                             ├──► Open SocketCAN (e.g. can0)          │
│                             ├──► Init CSV writer  → /mnt/usb/        │
│                             ├──► Init InfluxDB    → libcurl          │
│                             └──► Init serial radio → /dev/ttyUSB0   │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│                        RUNTIME LOOP (poll)                           │
│                                                                      │
│   ┌───────────┐    ┌──────────────────┐                              │
│   │  CAN frame│───►│ decoder_extract() │                             │
│   │  arrives  │    └────────┬─────────┘                              │
│   └───────────┘             │                                        │
│                    ┌────────┴──────────────────┐                     │
│                    │                           │                     │
│                    ▼                           ▼                     │
│          ┌──────────────────┐       ┌─────────────────────┐         │
│          │  writer_append() │       │  influx_accumulate() │         │
│          │  → CSV row on    │       │  serial_radio_       │         │
│          │    USB drive     │       │    accumulate()      │         │
│          └──────────────────┘       └──────────┬──────────┘         │
│                                                │                     │
│   ┌───────────┐                                │                     │
│   │poll() 200 │    ┌───────────────────────────┘                     │
│   │ms timeout │───►│                                                 │
│   └───────────┘    ├──► influx_tick()                                │
│                    │    Flush batch → InfluxDB Cloud (HTTPS/libcurl) │
│                    │                                                  │
│                    └──► serial_radio_tick()                          │
│                         Flush latest values → /dev/ttyUSB0           │
│                         Format: <ts_ns>,<signal>,<value>\n           │
└──────────────────────────────────────────────────────────────────────┘
```

### Data flow detail

```
                          CAN frame (SocketCAN)
                                  │
                    ┌─────────────▼──────────────┐
                    │  Lookup CAN ID in hash table│
                    │  → list of signal_def_t     │
                    └─────────────┬──────────────┘
                                  │  (one frame can carry N signals)
                     ┌────────────┼─────────────┐
                     ▼            ▼             ▼
              ┌──────────┐  ┌──────────┐  ┌──────────┐
              │  writer  │  │  influx  │  │  radio   │
              │ (always) │  │(optional)│  │(optional)│
              └────┬─────┘  └────┬─────┘  └────┬─────┘
                   │             │              │
            per-signal     batched mean    last value
             CSV append    → InfluxDB     → UART radio
           /mnt/usb/*.csv  every N sec   every N ms
```

---

## Hardware Setup

### Raspberry Pi 4 (driverio board)

| Component | Details |
|-----------|---------|
| CAN hat | MCP2515 on `can0`, 500 kbps |
| USB log drive | `ext4` mounted at `/mnt/usb` |
| LTE modem | Quectel EG25-G on `/dev/ttyUSB1–4`; NTP via `systemd-timesyncd` for accurate timestamps |
| Serial radio | RFD900A on `/dev/ttyUSB0` (Silicon Labs CP2102); 115200 baud, NET\_ID 420 |

### Bring up CAN interface

```bash
sudo ip link set can0 up type can bitrate 500000
```

### Mount USB drive

```bash
sudo mkdir -p /mnt/usb
sudo mount /dev/sda1 /mnt/usb
```

Add to `/etc/fstab` for persistence:

```
UUID=<your-uuid>  /mnt/usb  ext4  defaults,noatime  0  2
```

---

## Building

```bash
sudo apt install libcurl4-openssl-dev libsqlite3-dev
make
```

The binary is `./can_telem`.

---

## Configuration

Copy and edit the example:

```bash
cp can_telem.conf.example can_telem.conf
```

### Full config reference

```ini
# ── Core ────────────────────────────────────────────────────────────
can_interface          = can0
format_file            = /home/sunpi/can-telem-cloud/format.json
output_dir             = /mnt/usb

# ── InfluxDB Cloud (optional) ───────────────────────────────────────
influx_url             = https://us-east-1-1.aws.cloud2.influxdata.com
influx_org             = your-org
influx_bucket          = telemetry
influx_token           = your-token
influx_flush_interval  = 5

# ── Serial radio (optional) ─────────────────────────────────────────
radio_enabled          = true
radio_device           = /dev/ttyUSB0
radio_baud             = 115200
radio_flush_interval_ms = 1000
```

### CLI flags

```
can_telem [-c config] [-i interface] [-f format.json] [-o output_dir]
```

CLI flags override values in the config file.

---

## Signal format file

`format.json` (provided by the `sc-data-format` submodule) defines every signal:

```json
"signal_name": [<bytes>, "type", "units",
                <min>, <max>, "Category", "0xID",
                <bit_offset>, "source?", "db_key?",
                "tx_mode?", <tx_min_interval_ms?>]
```

---

## CSV output

One file per signal at `<output_dir>/<signal_name>.csv`:

```
timestamp_ns,value,raw_hex
1777013464503432008,3.412,3dad5b40
1777013464612834001,3.413,52ae5b40
```

`timestamp_ns` is a Unix nanosecond timestamp from `CLOCK_REALTIME` (synced via NTP over LTE).

---

## Serial radio output

The radio module flushes the **latest decoded value** for every active signal once per `radio_flush_interval_ms`. Each flush writes one line per signal to the serial port:

```
<timestamp_ns>,<signal_name>,<value>
```

Example flush frame (1-second window):

```
1777013464503432008,cell_group1_voltage,3.412
1777013464503432008,bms_input_voltage,19.2
1777013464503432008,bps_fault,0
```

### RFD900A setup notes

| Parameter | Value |
|-----------|-------|
| Baud (serial) | 115200 |
| Air data rate | 96 kbps |
| NET\_ID | 420 |
| MAVLINK mode | 0 (raw transparent) |
| RTSCTS | 0 |

Both ends must have identical settings. To verify:

```bash
# Enter AT command mode (1 s silence → +++ → 1 s silence)
stty -F /dev/ttyUSB0 115200 raw -echo cs8 -parenb -cstopb -crtscts
# then send: +++  (wait)  ATI5  (shows all settings)  ATO  (return to transparent)
```

---

## Running

```bash
# foreground
sudo ./can_telem -c can_telem.conf

# background (persistent across SSH sessions)
nohup sudo ./can_telem -c can_telem.conf > /tmp/can_telem.log 2>&1 &
```

---

## Source layout

```
can-telem-cloud/
├── src/
│   ├── main.c            — entry point, config init, signal handling
│   ├── config.[ch]       — config file parser
│   ├── format_loader.[ch]— format.json parser → signal hash table
│   ├── decoder.[ch]      — bit-exact CAN signal decoder
│   ├── can_reader.[ch]   — SocketCAN poll loop, fan-out to all sinks
│   ├── writer.[ch]       — CSV append sink
│   ├── influx.[ch]       — InfluxDB Cloud batch upload sink
│   ├── serial_radio.[ch] — UART radio sink (RFD900A / CP2102)
│   ├── encoder.[ch]      — CAN signal encoder (for TX path)
│   └── db_watcher.[ch]   — SQLite DB watcher for TX signals
├── third_party/
│   └── cJSON.[ch]        — JSON parser
├── format.json           — (git submodule: sc-data-format)
├── can_telem.conf.example
└── Makefile
```
