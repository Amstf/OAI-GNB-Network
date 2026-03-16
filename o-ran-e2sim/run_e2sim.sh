#!/bin/bash
# Call as ./run_e2sim.sh e2term_ip e2term_port [gnb_id] [--debug]

set -e

if [ $# -lt 2 ]; then
  echo "Usage: $0 <e2term_ip> <e2term_port> [gnb_id] [--debug]" >&2
  exit 1
fi

E2TERM_IP="$1"
E2TERM_PORT="$2"
shift 2

GNB_ARG=""
DEBUG_FLAG=0

for arg in "$@"; do
  case "$arg" in
    --debug)
      DEBUG_FLAG=1
      ;;
    *)
      if [ -z "$GNB_ARG" ]; then
        GNB_ARG="$arg"
      else
        echo "Error: unexpected argument '$arg'." >&2
        echo "Usage: $0 <e2term_ip> <e2term_port> [gnb_id] [--debug]" >&2
        exit 1
      fi
      ;;
  esac
done

if [ -n "$GNB_ARG" ]; then
  if [ ${#GNB_ARG} -lt 4 ]; then
    echo "Error: gnb_id must be at least 4 characters (four bytes)." >&2
    exit 1
  fi

  export GNB_ID="${GNB_ARG:0:4}"
  gnb_hex=$(printf "%s" "$GNB_ID" | od -An -tx1 | tr -d ' \n')
  echo "Using gNB ID '${GNB_ID}' (hex: ${gnb_hex:-N/A})"
else
  echo "GNB_ID not provided; simulator will use the compiled default (0xb5c67788)."
fi

if [ "$DEBUG_FLAG" -eq 1 ]; then
  export E2SIM_DEBUG=1
  echo "E2SIM debug logging enabled."
else
  export E2SIM_DEBUG=0
  echo "E2SIM debug logging disabled (error logs only)."
fi

./e2sm_examples/kpm_e2sm/build/src/kpm/kpm_sim "$E2TERM_IP" "$E2TERM_PORT"
