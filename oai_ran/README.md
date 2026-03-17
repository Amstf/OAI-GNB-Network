# OAI RAN — Custom gNB with E2 Agent and Network Slicing

A modified version of the OpenAirInterface 5G NR gNB, extended with a custom **E2 agent** for O-RAN control and **network slicing** support. Two deployment modes are supported:

- **rfsim** — RF simulator on a local machine (no hardware required)
- **USRP / Colosseum** — real hardware deployment on the [Colosseum](https://www.colosseum.net) wireless network emulator

---

## Table of Contents

1. [Repository Layout](#1-repository-layout)
2. [Configuration Overview](#2-configuration-overview)
3. [Running the gNB — `start_gnb.sh`](#3-running-the-gnb--start_gnbsh)
4. [Running the UE — `start_ue.sh`](#4-running-the-ue--start_uesh)
5. [Network Interface Summary](#5-network-interface-summary)
6. [E2 Agent](#6-e2-agent)
7. [Sources](#7-sources)

---

## 1. Repository Layout

```
oai_ran/
│
│   # ── Custom configuration and launch scripts (added on top of OAI) ──
├── oai-gnb.conf              # Main gNB configuration file
├── rrmPolicy.json            # Slice PRB ratio policy (used by the MAC scheduler)
├── nrUE_slice1.conf          # UE configuration — slice SST=1 SD=0xFFFFFF
├── nrUE_slice2.conf          # UE configuration — slice SST=1 SD=0x000002
├── nrUE1_slice1.conf         # Alternative UE configuration
├── start_gnb.sh              # Main gNB launch script (rfsim / USRP)
├── start_ue.sh               # UE launch script (rfsim / USRP)
├── CONF/
│   └── neighbour_gnb_223_21.conf   # Bidirectional neighbour list (included by oai-gnb.conf)
├── utils/
│   ├── set_up_to_cn.py       # Sets up the IP route to the 5G core
│   └── set_ip_in_conf.sh     # Injects the col0 IP into a configuration file
│
│   # ── Standard OAI source tree ──
├── CMakeLists.txt            # Top-level CMake build file
├── cmake_targets/            # Build utilities (build_oai script lives here)
├── common/                   # Common OAI utilities and logging
├── executables/              # Top-level entry points: nr-softmodem, nr-uesoftmodem
├── openair1/                 # Layer 1 — NR Rel-15 PHY
├── openair2/                 # Layer 2 — NR MAC/RLC/PDCP/RRC/F1AP/E1AP + E2AP
│   └── E2_AGENT/             # Custom E2 agent (UDP + Protobuf) — see §6
├── openair3/                 # Layer 3 — NGAP / GTP
├── radio/                    # Radio drivers: USRP, RFsim, FHI 7.2, …
├── nfapi/                    # MAC–PHY (n)FAPI interface
├── doc/                      # OAI documentation
├── docker/                   # Dockerfiles for Ubuntu / RHEL
├── ci-scripts/               # CI meta-scripts
├── charts/                   # Helm charts
├── openshift/                # OpenShift deployment files
└── tools/                    # Developer tools (code formatting, analysis)
```

> **Container and Colosseum setup:** For instructions on provisioning an LXC container, building the project, and deploying on Colosseum, refer to [`../src/README.md`](../src/README.md).

---

## 2. Configuration Overview

### 2.1 `oai-gnb.conf` — Main gNB Configuration

Single configuration file read by `nr-softmodem`. Key sections:

**Identity**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `gNB_ID` | `223` | gNB identifier — auto-set by `start_gnb.sh` from the last IP octet |
| `gNB_name` | `gNB_OAI_223` | Display name — auto-derived |
| `nr_cellid` | `1015612223` | NR Cell Identity — derived from the full IP (dots removed) |
| `physCellId` | `223` | Physical Cell ID — same as the last IP octet |
| `tracking_area_code` | `0x0001` | TAC |
| `mcc` / `mnc` | `001` / `01` | PLMN identity |

> `start_gnb.sh` updates all fields above at launch time based on the interface IP. Manual editing of these fields is not required under normal deployment.



**AMF / Core Network**

```
amf_ip_address = 192.168.70.132
GNB_INTERFACE_NAME_FOR_NG_AMF = "col0"    # or "eth0" in rfsim mode
GNB_IPV4_ADDRESS_FOR_NG_AMF   = "<auto-set by start_gnb.sh>"
```





### 2.2 `rrmPolicy.json` — Slice PRB Ratios

Read by the MAC scheduler at runtime. Can also be updated dynamically via the E2 agent's `SLICING_CONTROL` parameter.

```json
{
  "rrmPolicyRatio": [
    { "sst": 1, "sd": "FFFFFF", "dedicated_ratio": 5, "min_ratio": 10, "max_ratio": 100 },
    { "sst": 1, "sd": "000002", "dedicated_ratio": 5, "min_ratio": 10, "max_ratio": 100 }
  ]
}
```

| Field | Description |
|-------|-------------|
| `sst` / `sd` | Slice identifier matching `oai-gnb.conf` |
| `min_ratio` | Minimum guaranteed PRB share (%) |
| `max_ratio` | Maximum PRB share (%) |
| `dedicated_ratio` | Dedicated PRB share (%) |

---

### 2.3 `nrUE_slice1.conf` / `nrUE_slice2.conf` / — UE Configurations

Each file defines the IMSI, authentication keys, DNN, and S-NSSAI for a UE. Example:

```
uicc0 = {
  imsi = "001010000010776";
  key  = "fec86ba6eb707ed08905757b1bb44b8f";
  opc  = "C42449363BBAD02B66D16BC975D77CC1";
  dnn  = "oai";
  nssai_sst = 1;
  nssai_sd  = 0xFFFFFF;
}
```

In **rfsim** mode, `start_ue.sh` appends an `rfsimulator` block pointing at the gNB IP address.

---

### 2.4 `CONF/neighbour_gnb_223_21.conf` — Neighbour gNB List

Loaded via `@include` at the top of `oai-gnb.conf`. Defines the bidirectional neighbour relationship between gNB `223` and gNB `21`: each cell's `nr_cellid`, `gNB_ID`, PCI, ARFCN, PLMN, and SCTP endpoint. Both gNB instances reference this file.

---

## 3. Running the gNB — `start_gnb.sh`

### Usage

```bash
cd ~/OAI-HANDOVER/oai_ran
./start_gnb.sh -a <rfsim|usrp>
```

### Execution Steps

`start_gnb.sh` prepares the environment then launches `nr-softmodem` in the following order:

**1 — Select interface**

| Mode | Interface |
|------|-----------|
| `rfsim` | `eth0` |
| `usrp` | `col0` (created as a macvlan over `eth0` if not present) |

**2 — CN route** — Verifies that `192.168.70.129` is reachable via the selected interface; executes `utils/set_up_to_cn.py -i <iface>` if not.

**3 — Inject IP** — Reads the interface IP and writes it into `GNB_IPV4_ADDRESS_FOR_NG_AMF` and `GNB_IPV4_ADDRESS_FOR_NGU` in `oai-gnb.conf`.

**4 — Derive gNB identity** — Derived from the interface IP (e.g. `10.156.12.223`):

| Field | Derivation | Example |
|-------|-----------|---------|
| `gNB_ID` / `physCellId` | Last octet | `223` |
| `gNB_name` / `Active_gNBs` | `gNB_OAI_<last>` | `gNB_OAI_223` |
| `nr_cellid` | Dots removed | `1015612223` |

Fields are patched in-place into `oai-gnb.conf` (a `.bak` backup is created).

**5 — Launch `nr-softmodem`**

rfsim:
```bash
./nr-softmodem --rfsim --sa -O oai-gnb.conf
```

USRP / Colosseum:
```bash
numactl --cpunodebind=1 --membind=1 ./nr-softmodem \
    -O oai-gnb.conf --sa -E --continuous-tx \
    --usrp-tx-thread-config 1 --mtmode 1 \
    2>&1 | tee mylogs/GNB-<timestamp>.log
```

---

## 4. Running the UE — `start_ue.sh`

### Usage

```bash
# USRP mode (default)
./start_ue.sh -m usrp -c nrUE_slice1.conf

# RF Simulator mode — gNB LXC IP required
./start_ue.sh -m rfsim -g <gnb_ip> -c nrUE_slice1.conf
```

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `-m usrp\|rfsim` | Deployment mode | `usrp` |
| `-A <carrier_freq>` | Carrier frequency offset passed to `nr-uesoftmodem` | `2200` |
| `-g <gnb_ip>` | gNB IP address (required for rfsim) | — |
| `-c <conf.conf>` | UE configuration file (filename only) | `nrUE_slice1.conf` |
| `--ue-scan-carrier=True` | Enable carrier scan | off |

### Execution

**rfsim:** Appends an `rfsimulator` block to the UE configuration file pointing at the specified gNB IP, then launches `nr-uesoftmodem --rfsim`.

**usrp:** Launches `nr-uesoftmodem` with NUMA pinning, USRP address `192.168.40.2`, Band 78 / 30 kHz SCS / 106 PRBs / 3619.2 MHz. Derives a unique IMSI suffix from the last octet of the `col0` IP address.

---

## 5. Network Interface Summary

| Mode | gNB Interface | Used for |
|------|--------------|----------|
| `rfsim` | `eth0` | LXC bridge network; gNB and UE communicate over it |
| `usrp` (Colosseum) | `col0` | Colosseum RF emulation network; also used for NG and NGU |

The 5G core AMF IP is `192.168.70.132` by default in `oai-gnb.conf`.

---

## 6. E2 Agent

Custom E2 agent located at `openair2/E2_AGENT/`, running alongside the standard OAI FlexRIC agent. Communicates with the nearRT-RIC xApp over **UDP** using **Protocol Buffers**:

- Listens on **UDP 6655**, responds on **UDP 6600**
- Reports per-UE KPIs: RNTI, BLER, MCS, avg PRBs, avg TBS/PRB, RSRP, buffer occupancy, slice info
- Accepts control parameters: PRB cap (`MAX_PRB`), slice PRB ratios (`SLICING_CONTROL`)

---

## 7. Sources

**Repositories**
- [wineslab/ORANSlice](https://github.com/wineslab/ORANSlice/tree/main/oai_ran) — upstream `oai_ran` baseline, itself rebased on the OpenAirInterface [develop branch](https://gitlab.eurecom.fr/oai/openairinterface5g/)

**Reference**

H. Cheng, S. D'Oro, R. Gangula, S. Velumani, D. Villa, L. Bonati, M. Polese, G. Arrobo, C. Maciocco, T. Melodia, "ORANSlice: An Open-Source 5G Network Slicing Platform for O-RAN," in *Proc. ACM MobiCom '24*, Washington, D.C., USA, Nov. 2024.
