#!/usr/bin/env bash
# network-connect.sh
# Boot-time connectivity: prefer LTE, fall back to WiFi.
# After this script exits, WiFi remains joinable manually (keyboard/display)
# but will NOT override the LTE default route — it just adds a secondary route.
set -euo pipefail

CONFIG_FILE=/etc/default/network-connect
LTE_WAIT_SEC=90       # seconds to wait for LTE internet after bearer connects
MODEM_REG_WAIT=45     # seconds to wait for cell registration
PING_TARGET=1.1.1.1
LTE_CONN_NAME=lte
LTE_APN=fast.t-mobile.com
LTE_METRIC=100        # lower = preferred (LTE wins)
WIFI_METRIC=600       # higher = secondary (WiFi stays reachable, not default)

if [[ -f "$CONFIG_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$CONFIG_FILE"
fi

log() {
    echo "network-connect: $*"
}

wait_for_nm() {
    local i
    for i in $(seq 1 30); do
        if nmcli general status >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

get_modem_index() {
    mmcli -L 2>/dev/null | sed -n 's/.*Modem\/\([0-9]\+\).*/\1/p' | head -1
}

lte_usb_up() {
    ip link show usb0 >/dev/null 2>&1 || return 1
    # RNDIS/CDC-Ethernet modems (e.g. EG25) report 'state UNKNOWN' not 'state UP'.
    # Check for LOWER_UP flag instead, which means the link is physically active.
    ip link show usb0 | grep -q 'LOWER_UP' || return 1
    ip -4 addr show dev usb0 2>/dev/null | grep -q 'inet ' || return 1
    return 0
}

get_lte_gateway() {
    local gw
    gw=$(ip -4 route show dev usb0 2>/dev/null | awk '/^default / { print $3; exit }')
    if [[ -n "$gw" ]]; then
        echo "$gw"
        return 0
    fi
    # Derive gateway from usb0 IP (RNDIS modems use X.X.X.1)
    if lte_usb_up; then
        ip -4 -o addr show dev usb0 | awk '{split($4,a,"/"); split(a[1],b,"."); printf "%s.%s.%s.1\n", b[1], b[2], b[3]}'
    fi
}

lte_has_internet() {
    lte_usb_up || return 1
    ping -I usb0 -c1 -W3 "$PING_TARGET" >/dev/null 2>&1
}

wifi_has_internet() {
    nmcli -t -f DEVICE,STATE device status | awk -F: '$1=="wlan0" && $2=="connected" { found=1 } END { exit !found }' || return 1
    ping -I wlan0 -c1 -W3 "$PING_TARGET" >/dev/null 2>&1
}

# Set persistent NM connection metrics so that auto-reconnects honour priority
set_route_metrics() {
    local prefer_lte=$1  # "1" = LTE wins, "0" = WiFi wins
    local wifi_metric lte_metric

    if [[ "$prefer_lte" == "1" ]]; then
        wifi_metric=$WIFI_METRIC
        lte_metric=$LTE_METRIC
    else
        wifi_metric=$LTE_METRIC
        lte_metric=$WIFI_METRIC
    fi

    # Stamp every saved WiFi profile
    while IFS=: read -r name type _; do
        [[ "$type" == "802-11-wireless" ]] || continue
        nmcli connection modify "$name" \
            ipv4.route-metric "$wifi_metric" \
            ipv6.route-metric "$wifi_metric" 2>/dev/null || true
    done < <(nmcli -t -f NAME,TYPE connection show)

    # Stamp the LTE connection profile if it exists
    if nmcli -t -f NAME connection show | grep -qx "$LTE_CONN_NAME"; then
        nmcli connection modify "$LTE_CONN_NAME" \
            ipv4.route-metric "$lte_metric" \
            ipv6.route-metric "$lte_metric" 2>/dev/null || true
    fi
}

# Explicitly install a default route via LTE with the correct metric
install_lte_default_route() {
    local lte_gw
    lte_gw=$(get_lte_gateway)
    [[ -n "$lte_gw" ]] || return 1
    ip route replace default via "$lte_gw" dev usb0 metric "$LTE_METRIC" 2>/dev/null || true
    # Push any existing WiFi default route to the secondary metric
    local wifi_gw
    wifi_gw=$(ip -4 route show dev wlan0 2>/dev/null | awk '/^default / { print $3; exit }')
    if [[ -n "$wifi_gw" ]]; then
        ip route replace default via "$wifi_gw" dev wlan0 metric "$WIFI_METRIC" 2>/dev/null || true
    fi
    return 0
}

# Install a default route via WiFi (fallback only — LTE not available)
install_wifi_default_route() {
    local wifi_gw
    wifi_gw=$(ip -4 route show dev wlan0 2>/dev/null | awk '/^default / { print $3; exit }')
    [[ -n "$wifi_gw" ]] || return 1
    ip route replace default via "$wifi_gw" dev wlan0 metric "$LTE_METRIC" 2>/dev/null || true
    return 0
}

# Allow WiFi radio + all saved autoconnect profiles to stay available.
# We do NOT block on them — this just ensures NM keeps scanning so the user
# can connect manually via keyboard/display at any time.
keep_wifi_available() {
    nmcli radio wifi on 2>/dev/null || true
    nmcli device set wlan0 managed yes 2>/dev/null || true
}

wait_for_modem() {
    # ModemManager takes a few seconds after boot to enumerate the EC25.
    # Poll up to 20s before giving up so we don't race past it at cold start.
    local i
    for i in $(seq 1 20); do
        if get_modem_index >/dev/null 2>&1 && [[ -n "$(get_modem_index)" ]]; then
            return 0
        fi
        sleep 1
    done
    return 1
}

connect_lte() {
    local modem state i

    log "Waiting up to 20s for modem to enumerate..."
    if ! wait_for_modem; then
        log "No LTE modem found via mmcli after 20s"
        return 1
    fi

    modem=$(get_modem_index)

    # Enable modem if it is disabled
    state=$(mmcli -m "$modem" 2>/dev/null | awk -F': ' '/^[[:space:]]*state:/{print $2; exit}')
    if [[ "$state" == "disabled" ]]; then
        log "Enabling modem $modem"
        mmcli -m "$modem" --enable 2>/dev/null || true
        sleep 3
    fi

    # Wait for network registration (SIM must find a cell tower)
    if ! mmcli -m "$modem" 2>/dev/null | grep -qE 'state:.*(connected|registered)'; then
        log "Waiting up to ${MODEM_REG_WAIT}s for cell registration..."
        for i in $(seq 1 "$MODEM_REG_WAIT"); do
            if mmcli -m "$modem" 2>/dev/null | grep -qE 'state:.*(connected|registered)'; then
                log "Modem registered after ${i}s"
                break
            fi
            sleep 1
        done
    fi

    # Verify registration succeeded
    if ! mmcli -m "$modem" 2>/dev/null | grep -qE 'state: (connected|registered)'; then
        log "Modem failed to register on network (no signal / SIM issue)"
        return 1
    fi

    # Always force-disconnect existing bearers and reconnect with the correct APN.
    # The modem's built-in default-attach bearer (fast.t-mobile.com) will block
    # the correct Tello APN (wholesale) from working if left active.
    log "Disconnecting any existing bearers..."
    mmcli -m "$modem" --simple-disconnect 2>/dev/null || true
    sleep 2
    log "Connecting LTE bearer (apn=$LTE_APN)"
    mmcli -m "$modem" --simple-connect="apn=${LTE_APN},ip-type=ipv4v6" 2>/dev/null ||
        mmcli -m "$modem" --simple-connect="apn=${LTE_APN}" 2>/dev/null || true
    # Wait for the modem's data path to fully initialize after bearer connect
    log "Waiting 10s for LTE data path to initialize..."
    sleep 10

    # Request IP address on usb0 manually since NetworkManager treats it as unmanaged
    # Prevent dhclient from asynchronously adding a default route (which overrides wlan0)
    local dhclient_conf="/tmp/dhclient-usb0.conf"
    echo "request subnet-mask, broadcast-address, time-offset, domain-name, domain-name-servers, host-name;" > "$dhclient_conf"

    log "Requesting IP on usb0 via dhclient..."
    dhclient -v -cf "$dhclient_conf" usb0 || true

    # Wait for usb0 to come up and test internet safely
    local test_gw
    for i in $(seq 1 "$LTE_WAIT_SEC"); do
        if lte_usb_up; then
            test_gw=$(get_lte_gateway)
            if [[ -n "$test_gw" ]]; then
                # Add a temporary specific route for the ping target so we don't break Tailscale/WiFi
                ip route add "$PING_TARGET" via "$test_gw" dev usb0 2>/dev/null || true
                
                if lte_has_internet; then
                    log "LTE internet confirmed after ${i}s"
                    ip route del "$PING_TARGET" via "$test_gw" dev usb0 2>/dev/null || true
                    install_lte_default_route || true
                    return 0
                fi
                ip route del "$PING_TARGET" via "$test_gw" dev usb0 2>/dev/null || true
            fi
        fi
        sleep 1
    done

    log "LTE bearer connected but could not reach internet (check APN / antenna)"
    # Ensure any bad default route is wiped out so it doesn't break WiFi
    ip route del default dev usb0 2>/dev/null || true
    return 1
}

try_wifi_fallback() {
    local i profile
    local -a profiles=()

    mapfile -t profiles < <(
        nmcli -t -f NAME,TYPE,AUTOCONNECT connection show |
            awk -F: '$2=="802-11-wireless" && $3=="yes" { print $1 }'
    )

    if [[ ${#profiles[@]} -eq 0 ]]; then
        log "No saved WiFi profiles — skipping WiFi fallback"
        return 1
    fi

    log "Attempting WiFi fallback (saved profiles: ${profiles[*]})"

    for i in $(seq 1 30); do
        if wifi_has_internet; then
            return 0
        fi
        if [[ "$i" -le 10 ]]; then
            for profile in "${profiles[@]}"; do
                nmcli connection up "$profile" ifname wlan0 2>/dev/null && break || true
            done
        fi
        sleep 1
    done
    return 1
}

main() {
    log "starting — LTE-first connectivity selection"

    if ! wait_for_nm; then
        log "NetworkManager not ready after 30s"
        exit 1
    fi

    # Always keep WiFi radio alive so the user can connect manually later
    keep_wifi_available

    # ── Primary path: LTE ────────────────────────────────────────────────────
    if connect_lte; then
        log "using LTE as primary internet connection"
        set_route_metrics 1          # stamp NM profiles: LTE=100, WiFi=600
        install_lte_default_route || true
        exit 0
    fi

    # ── Fallback path: WiFi ──────────────────────────────────────────────────
    # LTE is unavailable (no signal, SIM issue, etc.) — try saved WiFi.
    log "LTE unavailable — trying WiFi fallback"
    if try_wifi_fallback; then
        log "using WiFi as fallback internet connection (LTE not available)"
        set_route_metrics 0          # stamp NM profiles: WiFi=100, LTE=600
        install_wifi_default_route || true
        exit 0
    fi

    # ── No internet at all ───────────────────────────────────────────────────
    log "WARNING: no internet connectivity established at boot"
    log "Telemetry will queue locally. Connect WiFi manually if needed."
    # Exit 0 so the service is not marked failed (telemetry still logs to USB)
    exit 0
}

main "$@"
