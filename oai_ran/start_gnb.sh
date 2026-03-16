#!/bin/bash
CONF_FILE=~/OAI-HANDOVER/oai_ran/oai-gnb.conf
CN_IP=192.168.70.129

usage() {
    echo "Usage: $0 -a <rfsim|usrp>"
    exit 1
}

# -------- Helpers --------
reachable_via_iface() {
    local ip="$1" iface="$2"
    # 1) quick reachability
    if ! ping -c1 -W1 "$ip" >/dev/null 2>&1; then
        return 1
    fi
    # 2) kernel path uses iface?
    ip route get "$ip" 2>/dev/null | grep -q "dev $iface" || return 1
    return 0
}

ensure_col0() {
    # if col0 is required but missing, create macvlan on eth0
    if ! ip link show col0 >/dev/null 2>&1; then
        ip link add link eth0 name col0 type macvlan mode passthru 2>/dev/null || true
        ip link set col0 up 2>/dev/null || true
    fi
}

# -------- Functions --------
cn_route() {
    local iface=$1
    # Skip if already good
    if reachable_via_iface "$CN_IP" "$iface"; then
        echo "✅ CN $CN_IP already reachable via $iface — skipping route setup"
        return 0
    fi
    # If user selected usrp/col0 but col0 isn't present, make it exist
    if [[ "$iface" == "col0" ]]; then
        ensure_col0
    fi
    echo "⚡ Setting up CN route on interface: $iface"
    python3 ./utils/set_up_to_cn.py -i "$iface"
}

set_ip_in_conf() {
    local iface=$1
    ip_addr=$(ip -f inet addr show "$iface" | grep -Po 'inet \K[\d.]+')
    if [ -z "$ip_addr" ]; then
        echo "❌ Could not get IP for $iface"
        exit 1
    fi
    sed -i "/GNB_IPV4_ADDRESS_FOR_NG_AMF/ c \        GNB_IPV4_ADDRESS_FOR_NG_AMF = \"$ip_addr\/24\";" $CONF_FILE
    sed -i "/GNB_IPV4_ADDRESS_FOR_NGU/ c \        GNB_IPV4_ADDRESS_FOR_NGU = \"$ip_addr\/24\";" $CONF_FILE
    echo "✅ Updated NG IPs in $CONF_FILE to $ip_addr/24 for $iface"
}

set_gnb_id_names_cellid() {
    local iface="$1"
    local ip last id name digits

    [[ -f "$CONF_FILE" ]] || { echo "❌ Config not found: $CONF_FILE"; return 1; }

    ip=$(ip -4 -o addr show "$iface" | awk '{print $4}' | cut -d/ -f1) || return 1
    last="${ip##*.}"
    digits=$(tr -d '.' <<<"$ip")
    id="$last"
    name="gNB_OAI_${id}"

    if [[ -z "$ip" || -z "$last" || -z "$digits" || ! "$id" =~ ^[0-9]+$ ]]; then
        echo "❌ Failed to derive IDs from interface '$iface' (ip='$ip')." >&2
        return 1
    fi

    sed -i.bak -E \
        -e "s/^([[:space:]]*gNB_ID[[:space:]]*=[[:space:]]*).*/\1${id};/" \
        -e "s/^(Active_gNBs[[:space:]]*=[[:space:]]*\\([[:space:]]*\")[^\"]*(\"[[:space:]]*\\)[[:space:]]*;)/\1${name}\2/" \
        -e "s/^([[:space:]]*gNB_name[[:space:]]*=[[:space:]]*\")[^\"]*(\"[[:space:]]*;)/\1${name}\2/" \
        -e "s/^([[:space:]]*nr_cellid[[:space:]]*=[[:space:]]*).*/\1${digits};/" \
        -e "s/^([[:space:]]*physCellId[[:space:]]*=[[:space:]]*).*/\1${id};/" \
        "$CONF_FILE"

    echo "✅ Updated $(basename "$CONF_FILE") :
       gNB_ID=${id}
       Active_gNBs=\"${name}\"
       gNB_name=\"${name}\"
       nr_cellid=${digits}L
       physCellId=${id}   (from ${ip})"
}

# -------- Parse arguments --------
while getopts a: flag; do
    case "${flag}" in
        a) arch=${OPTARG};;   # rfsim | usrp
        *) usage;;
    esac
done

[[ -z "$arch" ]] && usage

# -------- Mode handling --------
case $arch in
  rfsim)
    iface="eth0"
    run_cmd="./nr-softmodem --rfsim --sa -O $CONF_FILE"
    ;;
  usrp)
    iface="col0"
    run_cmd="numactl --cpunodebind=1 --membind=1 ./nr-softmodem -O $CONF_FILE --sa -E --continuous-tx --usrp-tx-thread-config 1 --mtmode 1 2>&1 | tee ../../../../mylogs/GNB-$(date +\"%m%d%H%M\").log"
    ;;
  *) usage;;
esac

# -------- Run flow --------
echo "📡 Running gNB as donor ($arch)"
cn_route "$iface"
set_ip_in_conf "$iface"
set_gnb_id_names_cellid "$iface"

source oaienv
cd cmake_targets/ran_build/build/

echo "🚀 Starting gNB with command:"
echo "    $run_cmd"
eval $run_cmd
