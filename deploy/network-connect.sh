#!/usr/bin/env bash
set -euo pipefail

CONFIG_FILE=/etc/default/network-connect
WIFI_WAIT_SEC=45
LTE_WAIT_SEC=60
PING_TARGET=1.1.1.1
LTE_CONN_NAME=lte
LTE_APN=fast.t-mobile.com
WIFI_METRIC=100
LTE_METRIC=600

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

wifi_has_internet() {
    if ! nmcli -t -f DEVICE,STATE device status | awk -F: '$1=="wlan0" && $2=="connected" { found=1 } END { exit !found }'; then
        return 1
    fi
    ping -I wlan0 -c1 -W3 "$PING_TARGET" >/dev/null 2>&1
}

lte_has_internet() {
    if ! nmcli -t -f DEVICE,STATE device status | awk -F: '$1=="usb0" && $2=="connected" { found=1 } END { exit !found }'; then
        return 1
    fi
    ping -I usb0 -c1 -W3 "$PING_TARGET" >/dev/null 2>&1
}

set_route_metrics() {
    local prefer_wifi=$1
    local wifi_metric lte_metric

    if [[ "$prefer_wifi" == "1" ]]; then
        wifi_metric=$WIFI_METRIC
        lte_metric=$LTE_METRIC
    else
        wifi_metric=$LTE_METRIC
        lte_metric=$WIFI_METRIC
    fi

    while IFS=: read -r name type _; do
        [[ "$type" == "802-11-wireless" ]] || continue
        nmcli connection modify "$name" ipv4.route-metric "$wifi_metric" ipv6.route-metric "$wifi_metric" 2>/dev/null || true
    done < <(nmcli -t -f NAME,TYPE connection show)

    if nmcli -t -f NAME connection show | grep -qx "$LTE_CONN_NAME"; then
        nmcli connection modify "$LTE_CONN_NAME" ipv4.route-metric "$lte_metric" ipv6.route-metric "$lte_metric" 2>/dev/null || true
    fi
}

apply_default_route() {
    local prefer_wifi=$1
    local wifi_gw lte_gw

    wifi_gw=$(ip -4 route show dev wlan0 2>/dev/null | awk '/^default / { print $3; exit }')
    lte_gw=$(ip -4 route show dev usb0 2>/dev/null | awk '/^default / { print $3; exit }')

    if [[ "$prefer_wifi" == "1" && -n "$wifi_gw" ]]; then
        ip route replace default via "$wifi_gw" dev wlan0 metric "$WIFI_METRIC"
        if [[ -n "$lte_gw" ]]; then
            ip route del default via "$lte_gw" dev usb0 metric "$LTE_METRIC" 2>/dev/null || true
            ip route del default dev usb0 2>/dev/null || true
        fi
        return 0
    fi

    if [[ -n "$lte_gw" ]]; then
        ip route replace default via "$lte_gw" dev usb0 metric "$WIFI_METRIC"
        if [[ -n "$wifi_gw" ]]; then
            ip route replace default via "$wifi_gw" dev wlan0 metric "$LTE_METRIC" 2>/dev/null || true
        fi
        return 0
    fi

    return 1
}

try_wifi() {
    local i profile active_wifi
    local -a profiles=()

    nmcli radio wifi on 2>/dev/null || true
    nmcli device set wlan0 managed yes 2>/dev/null || true

    mapfile -t profiles < <(
        nmcli -t -f NAME,TYPE,AUTOCONNECT connection show |
            awk -F: '$2=="802-11-wireless" && $3=="yes" { print $1 }'
    )

    for i in $(seq 1 "$WIFI_WAIT_SEC"); do
        if wifi_has_internet; then
            return 0
        fi

        if [[ "$i" -le 10 ]]; then
            for profile in "${profiles[@]}"; do
                if nmcli connection up "$profile" ifname wlan0 2>/dev/null; then
                    break
                fi
            done
        fi
        sleep 1
    done
    return 1
}

connect_lte() {
    local modem state i

    modem=$(get_modem_index)
    if [[ -z "$modem" ]]; then
        log "No LTE modem found"
        return 1
    fi

    state=$(mmcli -m "$modem" 2>/dev/null | awk -F': ' '/^[[:space:]]*state:/{print $2; exit}')
    if [[ "$state" == "disabled" ]]; then
        log "Enabling modem $modem"
        mmcli -m "$modem" --enable 2>/dev/null || true
        sleep 2
    fi

    if ! mmcli -m "$modem" 2>/dev/null | grep -qE 'state: (connected|registered)'; then
        log "Waiting for modem registration"
        for i in $(seq 1 30); do
            if mmcli -m "$modem" 2>/dev/null | grep -qE 'state: (connected|registered)'; then
                break
            fi
            sleep 1
        done
    fi

    if ! mmcli -m "$modem" -b 0 2>/dev/null | grep -q 'connected: yes'; then
        log "Connecting LTE bearer (apn=$LTE_APN)"
        mmcli -m "$modem" --simple-connect="apn=${LTE_APN},ip-type=ipv4v6" 2>/dev/null ||
            mmcli -m "$modem" --simple-connect="apn=${LTE_APN}" 2>/dev/null || true
    fi

    nmcli connection up "$LTE_CONN_NAME" ifname usb0 2>/dev/null ||
        nmcli device connect usb0 2>/dev/null || true

    for i in $(seq 1 "$LTE_WAIT_SEC"); do
        if lte_has_internet; then
            return 0
        fi
        sleep 1
    done
    return 1
}

main() {
    log "starting connectivity selection"

    if ! wait_for_nm; then
        log "NetworkManager not ready"
        exit 1
    fi

    if try_wifi; then
        log "using saved WiFi network"
        set_route_metrics 1
        active_wifi=$(nmcli -t -f NAME,TYPE connection show --active | awk -F: '$2=="802-11-wireless"{print $1; exit}')
        if [[ -n "${active_wifi:-}" ]]; then
            nmcli connection up "$active_wifi" 2>/dev/null || true
        fi
        apply_default_route 1 || true
        exit 0
    fi

    log "no saved WiFi available, connecting LTE"
    if connect_lte; then
        log "using LTE"
        set_route_metrics 0
        nmcli connection up "$LTE_CONN_NAME" 2>/dev/null || true
        apply_default_route 0 || true
        exit 0
    fi

    log "failed to establish internet connectivity"
    exit 1
}

main "$@"
