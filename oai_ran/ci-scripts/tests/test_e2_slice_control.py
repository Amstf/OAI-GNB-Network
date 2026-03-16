#!/usr/bin/env python3
"""Send a SlicingControlM message to the local E2 agent and report the outcome."""

import argparse
import os
import socket
import sys
import time

# The generated protobuf module lives alongside the C sources.
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROTO_DIR = os.path.join(REPO_ROOT, "openair2", "E2_AGENT", "oai-oran-protolib", "builds")
if PROTO_DIR not in sys.path:
    sys.path.insert(0, PROTO_DIR)

try:
    import ran_messages_pb2 as ran_pb
except ImportError as err:  # pragma: no cover
    sys.stderr.write(f"Failed to import ran_messages_pb2: {err}\n")
    sys.exit(1)


def parse_sd(value: str) -> int:
    if value.lower().startswith("0x"):
        return int(value, 16)
    return int(value, 0)


def build_message(sst: int, sd: int, min_ratio: int, max_ratio: int) -> bytes:
    message = ran_pb.RANMessage()
    message.msg_type = ran_pb.RAN_MESSAGE_TYPE_CONTROL

    control = message.ran_control_request
    entry = control.target_param_map.add()
    entry.key = ran_pb.RAN_PARAMETER_SLICING_CONTROL
    entry.slicing_ctrl.sst = sst
    entry.slicing_ctrl.sd = sd
    entry.slicing_ctrl.min_ratio = min_ratio
    entry.slicing_ctrl.max_ratio = max_ratio

    return message.SerializeToString()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sst", type=int, help="Slice/service type")
    parser.add_argument("sd", type=parse_sd, help="Slice differentiator (hex or int)")
    parser.add_argument("min_ratio", type=int, help="Minimum PRB ratio (0-100)")
    parser.add_argument("max_ratio", type=int, help="Maximum PRB ratio (0-100)")
    parser.add_argument("--host", default="127.0.0.1", help="E2 agent host (default: %(default)s)")
    parser.add_argument("--port", type=int, default=6655, help="E2 agent port (default: %(default)s)")
    parser.add_argument("--timeout", type=float, default=1.0, help="Socket timeout in seconds")
    args = parser.parse_args()

    payload = build_message(args.sst, args.sd, args.min_ratio, args.max_ratio)

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(args.timeout)
        target = (args.host, args.port)
        sock.sendto(payload, target)
        time.sleep(0.05)

    print(
        "Sent slicing control to %s:%d (sst=%d sd=0x%06x min=%d max=%d)"
        % (args.host, args.port, args.sst, args.sd, args.min_ratio, args.max_ratio)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
