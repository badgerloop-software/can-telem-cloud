# can_telem

A small C utility that reads CAN frames from a Linux SocketCAN interface,
matches each frame's 11-bit ID against the signal definitions in
[`format.json`](format.json), decodes every matching field, and appends the
value to a per-signal CSV log.

The JSON schema is the one documented in [`format_exp.md`](format_exp.md):

```
"name": [<num bytes>, "data_type", "units",
         <nominal min>, <nominal max>,
         "Category/Subsystem", "CAN ID (hex)", <bit offset>,
         "source?", "db_key?", "tx_mode?", <tx_min_interval_ms?>]
```

The first 8 fields are unchanged and required. Optional fields (9-12):
- `source`: `"can"` (default) or `"db"`
- `db_key`: DB row key used when `source="db"` (default: signal name)
- `tx_mode`: currently supports `"on_change"` only
- `tx_min_interval_ms`: minimum interval between TX updates for this signal

## Data flow

At startup, optional [`can_telem.conf`](can_telem.conf.example) (and `-c`) plus CLI flags set paths, the CAN interface, and optional InfluxDB Cloud credentials. [`format.json`](format.json) is parsed into a hash table keyed by CAN ID so each incoming frame can be matched to one or more signal definitions.

While running, every decoded sample follows **two independent paths**: full-rate append to local CSV files, and (if enabled) in-memory aggregation for batched cloud upload.

Diagram (monospace; use a fixed-width font in the editor or GitHub raw view):

```
                         STARTUP
    +------------------+  +------------------+  +------------------+
    | can_telem.conf   |  | CLI -i -f -o -c   |  | INFLUX_TOKEN     |
    | (optional file)  |  | overrides file   |  | env (optional)   |
    +--------+---------+  +--------+---------+  +--------+---------+
             |                     |                     |
             +---------------------+---------------------+
                                   |
                                   v
                        +------------------------+
                        | Parse config, init     |
                        | Influx + libcurl if on |
                        +-----------+------------+
                                    |
              +---------------------+---------------------+
              |                                           |
              v                                           v
   +------------------------+              +------------------------+
   | Load format.json       |              | Open SocketCAN       |
   | Build hash: CAN ID ->  |              | bind to interface    |
   | list of signal defs    |              | (e.g. can0)          |
   +------------+-----------+              +------------+-----------+
                |                                       |
                +---------------------+-------------------+
                                      |
                                      v
                               ( enter main loop )

                         RUNTIME (each iteration)
                        +------------------------+
                        | poll or read           |
                        | one CAN frame          |
                        +-----------+------------+
                                    |
                                    v
                        +------------------------+
                        | Match CAN ID in table  |
                        | For each signal: decode|
                        +-----------+------------+
                                    |
                    +---------------+---------------+
                    |                               |
                    v                               v
         +------------------------+    +------------------------+
         | CSV writer             |    | Influx accumulator     |
         | 1 row per decode       |    | (disabled = skip all)  |
         | full bus rate to disk  |    | sum+count or bool OR   |
         +------------+-----------+    +------------+-------------+
                      |                            |
                      v                            |
         +------------------------+                |
         | logs/<signal>.csv      |                |
         +------------------------+                |
                                                   |
                                                   |  (decodes update agg)
                                                   v
                        +------------------------+
                        | Tick: every N ms or    |
                        | poll wake (Influx on) |
                        | flush agg to LP body   |
                        +-----------+------------+
                                    |
                                    v
                        +------------------------+
                        | Line protocol body    |
                        +-----------+------------+
                                    |
                                    v
                        +------------------------+
                        | HTTPS POST            |
                        | InfluxDB Cloud v2     |
                        +------------------------+
```

**Aggregation (cloud only):** between each successful POST, non-boolean samples contribute to a running sum and count so the next point is the **mean** over that window; boolean samples are combined with **OR** (true if any sample was true). CSV rows are **not** averaged—each decode appends one line with its own timestamp.

## Build

Requires a Linux kernel with SocketCAN headers, a C11 compiler, and
**libcurl** with TLS (Debian / Raspberry Pi OS: `sudo apt install libcurl4-openssl-dev`).

[cJSON](https://github.com/DaveGamble/cJSON) is vendored in `third_party/`.

```
make
```

Produces the `can_telem` binary in the project root.

## Usage

```
./can_telem [-c <file>] [-i <iface>] [-f <format.json>] [-o <outdir>] [-h]
  -c   path to a key=value config file (optional; see below)
  -i   CAN interface name          (default: can0)
  -f   path to signal format JSON  (default: ./format.json)
  -o   output directory for CSVs   (default: ./logs)
  -h   help
```

### Config file

If `./can_telem.conf` exists in the current working directory, it is read
automatically. You can also pass an explicit path with `-c /path/to/file`.

The file is plain text, one `key = value` per line (spaces around `=` are
optional). Blank lines and lines starting with `#` are ignored.

| Key | Meaning |
|-----|---------|
| `output_dir` | Directory where per-signal CSV files are written |
| `format_file` | Path to `format.json` (or equivalent) |
| `can_interface` | SocketCAN interface name (alias: `interface`) |
| `influx_enabled` | `true` / `false` — enable InfluxDB Cloud uploads |
| `influx_url` | Cloud **base URL** only (e.g. `https://…cloud2.influxdata.com`, no `/api/…` path) |
| `influx_org` | Organization name (Load Data → API Tokens in the Cloud UI) |
| `influx_bucket` | Bucket name |
| `influx_token` | API token with write access; may be omitted if `INFLUX_TOKEN` is set in the environment |
| `influx_upload_interval_ms` | Minimum time between batched writes (default `1000`). Non-boolean signals use the **mean** of all decoded samples in the window; booleans use **OR** (true if any sample was true). |
| `influx_measurement` | Influx line-protocol measurement name: letters, digits, underscore only (default `can_telem`) |
| `db_enabled` | `true` / `false` — enable DB-driven CAN transmit |
| `db_path` | SQLite DB path (required when `db_enabled=true`) |
| `db_table` | Table name for signal values (default `signal_values`) |
| `db_key_column` | Key column name (default `signal_key`) |
| `db_value_column` | Value column name (default `signal_value`) |
| `db_poll_interval_ms` | DB polling interval in ms (default `200`) |
| `db_can_interface` | CAN interface for TX (default: same as `can_interface`) |

Command-line options always override values from the config file.

When Influx is enabled, the CAN reader uses `poll()` with a 200 ms timeout so
upload intervals still fire if the bus goes quiet; with Influx disabled the
socket read stays fully blocking as before.

Copy [`can_telem.conf.example`](can_telem.conf.example) to `can_telem.conf`
next to the binary (or your working directory) and edit the paths.

Each decoded signal is written to `<outdir>/<signal_name>.csv` with columns:

```
timestamp_ns,value,raw_hex
```

The directory is created on startup if it does not already exist. Files are
opened in append mode, so repeated runs accumulate rows in the same CSV.

Send `SIGINT` (Ctrl-C) or `SIGTERM` to stop the reader and flush/close all
open CSV files cleanly.

## Quick smoke test with a virtual CAN bus

```
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

./can_telem -i vcan0 -f format.json -o logs &

# Send a frame on CAN ID 0x101 with an 8-byte payload. This ID has
# pack_current, pack_voltage, soc, and soh defined in format.json.
cansend vcan0 101#0000FA00640000000

ls logs/   # expect pack_current.csv, pack_voltage.csv, soc.csv, soh.csv
```

## Notes and caveats

- Decoding assumes little-endian (Intel) byte order, which matches the
  SocketCAN convention. Motorola/big-endian support can be added as a
  per-signal flag later.
- Signals declared with CAN ID `"FFF"` in `format.json` are treated as
  unassigned placeholders; they are loaded for inspection but never
  matched against incoming frames.
- `format.json` contains a handful of duplicate signal names
  (`dcdc_deg`, `use_supp`, `use_dcdc`, `regen_brake`). Both definitions
  are loaded; rows for the shared name are appended to a single CSV.
- DB-backed signals (`source="db"`) can now be polled from a SQLite table and
  transmitted to CAN when values change.
