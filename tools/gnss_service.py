#!/usr/bin/env python3
import json
import os
import re
import subprocess
import time
from pathlib import Path

OUT_PATH = Path("/run/can_telem/gnss.json")
OUT_PATH.parent.mkdir(parents=True, exist_ok=True)


def run_cmd(args):
    try:
        cp = subprocess.run(args, capture_output=True, text=True, timeout=8, check=False)
        return cp.returncode, cp.stdout + cp.stderr
    except Exception:
        return 1, ""


def find_modem_id():
    rc, out = run_cmd(["mmcli", "-L"])
    if rc != 0:
        return None
    m = re.search(r"/Modem/(\d+)", out)
    return m.group(1) if m else None


def enable_location(modem_id):
    run_cmd(["mmcli", "-m", modem_id, "--location-enable-gps-nmea"])
    run_cmd(["mmcli", "-m", modem_id, "--location-enable-gps-raw"])
    run_cmd(["mmcli", "-m", modem_id, "--location-enable-3gpp"])


def parse_location(text):
    def num_after(label):
        m = re.search(label + r"\s*:\s*([-+]?\d+(?:\.\d+)?)", text, flags=re.IGNORECASE)
        return float(m.group(1)) if m else None

    lat = num_after("latitude")
    lon = num_after("longitude")
    elev = num_after("altitude")
    return lat, lon, elev


def write_fix(lat, lon, elev):
    payload = {
        "lat": lat,
        "lon": lon,
        "elev": elev if elev is not None else 0.0,
        "timestamp_ns": time.time_ns(),
    }
    tmp = OUT_PATH.with_suffix(".tmp")
    tmp.write_text(json.dumps(payload), encoding="utf-8")
    os.replace(tmp, OUT_PATH)


def main():
    modem_id = None
    while True:
        if modem_id is None:
            modem_id = find_modem_id()
            if modem_id:
                enable_location(modem_id)
            else:
                time.sleep(2)
                continue

        rc, out = run_cmd(["mmcli", "-m", modem_id, "--location-get"])
        if rc != 0:
            modem_id = None
            time.sleep(1)
            continue
        lat, lon, elev = parse_location(out)
        if lat is not None and lon is not None:
            write_fix(lat, lon, elev)
        time.sleep(1)


if __name__ == "__main__":
    main()
