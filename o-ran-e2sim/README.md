# O-RAN E2 Simulator

Connects to a nearRT-RIC over the O-RAN E2 interface (SCTP / E2AP) and communicates with the OAI gNB over local UDP using Protocol Buffers. `OAI_PROTOBUF=1` is always enabled in this setup.

---

## Directory Structure

```
o-ran-e2sim/
├── build_e2sim.sh
├── run_e2sim.sh
├── src/                                  # Core E2AP / SCTP simulator library
└── e2sm_examples/kpm_e2sm/
    └── src/kpm/
        ├── bs_connector.cpp/hpp          # UDP bridge to OAI gNB
        ├── kpm_callbacks.cpp/hpp         # RIC subscription/control callbacks
        ├── encode_kpm.cpp/hpp            # ASN.1 KPM indication encoding
        ├── e2sim_debug.hpp               # Runtime debug gating (E2SIM_DEBUG)
        └── kpm_callbacks.hpp             # Contains OAI_PROTOBUF flag
```

---

## Build

```bash
./build_e2sim.sh -i   # install dependencies and build (first run)
./build_e2sim.sh      # subsequent builds
```

The `-i` flag installs system packages (`gcc-9`, `g++-9`, `cmake`, `libsctp-dev`, `libboost-all-dev`, etc.), configures `gcc-9`/`g++-9` as the default compiler via `update-alternatives`, and builds the JSON library from source.

Requirements: `cmake 3.14+`, `gcc-9` / `g++-9`.

The `OAI_PROTOBUF` flag is set at the top of `build_e2sim.sh`:

```bash
OAI_PROTOBUF=1   # enables the OAI gNB UDP bridge — do not modify
```

Compiled binary:

```
e2sm_examples/kpm_e2sm/build/src/kpm/kpm_sim
```

---

## Running — `run_e2sim.sh`

```bash
./run_e2sim.sh <e2term_ip> <e2term_port> [gnb_id] [--debug]
```

| Argument | Required | Description |
|----------|----------|-------------|
| `e2term_ip` | yes | IP address of the nearRT-RIC SCTP endpoint |
| `e2term_port` | yes | SCTP port of the nearRT-RIC E2 termination |
| `gnb_id` | no | 4-character gNB identifier, e.g. `a223`. Value must match `gNB_ID` in `oai-gnb.conf`. If omitted, the compiled default (`0xb5c67788`) is used. |
| `--debug` | no | Enable verbose debug logging |

Example:

```bash
./run_e2sim.sh <e2term_ip> <e2term_port> a223 --debug
```

---

## Logging

| Mode | How to enable | Output |
|------|---------------|--------|
| Normal | default | Error-level messages only |
| Debug | `--debug` | Full verbose trace covering subscription handling, UDP send/receive, protobuf decode, and KPM encoding |

`--debug` sets `E2SIM_DEBUG=1` before launching the binary. All verbose output is gated behind this variable via `E2SIM_DBG_FPRINTF` in `e2sim_debug.hpp`, incurring no overhead when disabled.

To invoke the binary directly with debug output and a specific gNB ID:

```bash
GNB_ID=a223 E2SIM_DEBUG=1 ./e2sm_examples/kpm_e2sm/build/src/kpm/kpm_sim <e2term_ip> <e2term_port>
```

---

## Sources

**Repository**
- [wineslab/o-ran-e2sim](https://github.com/wineslab/o-ran-e2sim) — upstream base (`x5g-e2sim` branch)

**Reference**

A. Lacava, M. Polese, R. Sivaraj, R. Soundrarajan, B. S. Bhati, T. Singh, T. Zugno, F. Cuomo, T. Melodia, "Programmable and Customized Intelligence for Traffic Steering in 5G Networks Using Open RAN Architectures," arXiv:2209.14171, Oct. 2022.
