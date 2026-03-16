#!/bin/bash

# ========= Defaults =========
A_VALUE=2200
UE_SCAN_CARRIER=false
MODE="usrp"            # usrp|rfsim
GNB_IP=""              # for rfsim
BASE_CONF_DIR="${HOME}/OAI-HANDOVER/oai_ran"
CONF_BASENAME="nrUE_slice1.conf"   # default if not specified

usage() {
  echo "Usage: $0 [-m usrp|rfsim] [-A carrier_freq] [-g gnb_ip] [-c conf_name.conf] [--ue-scan-carrier True|False] [--conf=conf_name.conf]"
  exit 1
}

# -------- Parse short options --------
while getopts "A:m:g:c:" opt; do
  case $opt in
    A) A_VALUE="$OPTARG" ;;
    m) MODE="$OPTARG" ;;
    g) GNB_IP="$OPTARG" ;;
    c) CONF_BASENAME="$(basename "$OPTARG")" ;;   # only filename
    *) usage ;;
  esac
done
shift $((OPTIND -1))

# -------- Parse long options --------
for arg in "$@"; do
  case $arg in
    --ue-scan-carrier=*)
      UE_SCAN_CARRIER="${arg#*=}"
      shift
      ;;
    --conf=*)
      CONF_BASENAME="$(basename "${arg#*=}")"
      shift
      ;;
  esac
done

# -------- Resolve & validate conf path --------
CONF_FILE="${BASE_CONF_DIR}/${CONF_BASENAME}"
if [[ ! -f "$CONF_FILE" ]]; then
  echo "❌ Config not found: $CONF_FILE"
  echo "   (Base dir: $BASE_CONF_DIR)  Use -c/--conf to pick a file in that directory."
  exit 1
fi

# -------- Env & build dir --------
source oaienv
cd cmake_targets/ran_build/build/ || exit 1

# -------- Derive IMSI suffix from iface (example kept from your script) --------
ue_id=$(ip -4 -o addr show col0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | awk -F. '{print $4}')
imsi_string=${ue_id:1:2}

# -------- Inject rfsimulator block if needed --------
if [[ "$MODE" == "rfsim" ]]; then
  if [[ -z "$GNB_IP" ]]; then
    echo "❌ ERROR: provide -g <gnb_ip> for rfsim mode"
    exit 1
  fi

  # Remove any existing rfsimulator block (key to closing brace)
  sed -i '/^[[:space:]]*rfsimulator[[:space:]]*=/,/};/d' "$CONF_FILE"

  # Append fresh block
  cat <<EOR >> "$CONF_FILE"

rfsimulator = {
  serveraddr = "$GNB_IP";   # gNB LXC IP
  serverport = "4043";      # must match gNB port
  options = ();             # e.g., "chanmod"
  modelname = "AWGN";       # default channel model
};
EOR

  echo "✅ Updated rfsimulator in $(basename "$CONF_FILE") with gNB IP $GNB_IP"
fi

# -------- Build command --------
if [[ "$MODE" == "usrp" ]]; then
  CMD="numactl --cpunodebind=1 --membind=1 ./nr-uesoftmodem \
  -A $A_VALUE \
  --ue-fo-compensation \
  --dlsch-parallel 8 \
  --sa \
  -O \"$CONF_FILE\" \
  --usrp-args \"addr=192.168.40.2\" \
  -E \
  --numerology 1 \
  -r 106 \
  --band 78 \
  -C 3619200000 \
  --nokrnmod 1 \
  --ue-txgain 0 \
  --clock-source 1 \
  --time-source 1"
elif [[ "$MODE" == "rfsim" ]]; then
  CMD="sudo -E ./nr-uesoftmodem \
  -r 106 \
  --numerology 1 \
  --band 78 \
  -C 3619200000 \
  --rfsim \
  -O \"$CONF_FILE\""
else
  usage
fi

# Optional scan
if [[ "$UE_SCAN_CARRIER" =~ ^([Tt]rue)$ ]]; then
  CMD="$CMD --ue-scan-carrier"
fi

echo "🚀 Starting UE in $MODE mode using $(basename "$CONF_FILE")"
eval "$CMD"
