# OAI-HANDOVER: Overview

Technical overview of the custom components built on top of the OpenAirInterface RAN codebase — covering `oai_ran` and `o-ran-e2sim`.

---

## Original Baseline

The `oai_ran` codebase is derived from the **OAI NR gNB "Slice" branch**, which already included:
- O-RAN Network Slicing support in the NR MAC scheduler
- A standard OAI **FlexRIC E2 agent** (located in `openair2/E2AP/`) for connecting to a nearRT-RIC via the standard E2AP/SCTP interface, with KPM and RC service model support

The modifications described below build **on top of** that Slice baseline.

---

## Part 1 — `oai_ran` Modifications

### 1. New Custom E2 Agent (`openair2/E2_AGENT/`)

A completely new, custom E2 agent was introduced **alongside** the existing FlexRIC agent. While the FlexRIC agent handles the standard O-RAN E2AP/SCTP path, this custom agent provides a **lighter UDP+Protobuf channel** for direct, low-latency communication between the gNB and the E2 Simulator.

**New files introduced:**

| File | Description |
|------|-------------|
| `openair2/E2_AGENT/e2_agent_app.h` | Data structures, port definitions, function declarations |
| `openair2/E2_AGENT/e2_agent_app.c` | Agent initialization, UDP socket setup, ITTI task loop, heartbeat thread |
| `openair2/E2_AGENT/e2_message_handlers.h` | Message handler function declarations |
| `openair2/E2_AGENT/e2_message_handlers.c` | Core message routing, RAN parameter read/write, per-UE metric collection (~1000 lines) |
| `openair2/E2_AGENT/e2_agent_logging.h` | Logging macros with fallback to `GNB_APP` when T tracer IDs are missing |
| `openair2/E2_AGENT/CMakeLists.txt` | Builds `e2_agent` as a static library, links `protobuf-c` and `pthread` |
| `openair2/E2_AGENT/oai-oran-protolib/ran_messages.proto` | Protobuf schema for all E2 messages (see §2 below) |
| `openair2/E2_AGENT/oai-oran-protolib/builds/ran_messages.pb-c.c/h` | Generated C code from the proto file |

**How it works:**

- `e2_agent_init()` — allocates the agent context, opens two UDP sockets:
  - **Inbound** on port **6655** — receives messages from the E2 Simulator
  - **Outbound** targeting port **6600** — sends responses back to the E2 Simulator
  - Initializes a shared databank (`e2_agent_db`) protected by a mutex
  - Starts a **heartbeat thread** (logs every 3 seconds)
  - Registers an **ITTI task** (`TASK_E2_AGENT`) for the main receive loop

- `e2_agent_task()` — the main loop: blocks on `recvfrom()`, then calls `handle_master_message()` for every received packet

- `handle_master_message()` — decodes the incoming protobuf `RAN_message` and routes to:
  - `handle_indication_request()` → reads current RAN state, builds and sends a `RAN_indication_response`
  - `handle_control()` → applies xApp control actions to the RAN

**Per-UE metric collection** (`get_ue_list()` / `build_ue_entry()`):

For each connected UE, the agent collects from the MAC layer:
- RNTI, GBR flag, MAC buffer occupation
- DL/UL cumulative bytes and error counters
- Instantaneous BLER and most recent MCS (DL and UL)
- 1-second running average TBS (`tbs_avg_dl/ul`)
- RSRP (averaged: `cumul_rsrp / num_rsrp_meas`), power headroom (PH), max TX power (PCMAX)
- Slice info: NSSAI SST and SD

**Custom KPI metrics computed by the agent:**

1. **`avg_prbs_dl/ul`** — average PRBs used per slot, including retransmissions:
   ```
   slots_elapsed = elapsed_seconds × (numb_slots_frame × 100)
   avg_prbs = (Δtotal_rbs + Δtotal_rbs_retx) / slots_elapsed
   ```
   Uses delta-based calculation (snapshot at query time minus last snapshot); resets on counter wraparound.

2. **`avg_tbs_per_prb_dl/ul`** — bytes-per-PRB efficiency ratio:
   ```
   avg_tbs_per_prb = Δbytes / max(Δrbs_including_retx, 1)
   ```

**Control parameters the agent can apply:**

- `MAX_PRB` → calls `apply_max_cell_prb()` → stores in `e2_agent_db->max_prb` (enforced in the MAC scheduler, see §4)
- `SLICING_CONTROL` → calls `apply_slicing_ctrl()` → updates the min/max PRB ratio for a given NSSAI slice (SST/SD)

---

### 2. Protobuf Message Schema (`ran_messages.proto`)

A custom Protocol Buffers v2 schema defines the full message interface between the gNB and the E2 Simulator.

**Message types:**
```protobuf
enum RAN_message_type { SUBSCRIPTION=1; INDICATION_REQUEST=2; INDICATION_RESPONSE=3; CONTROL=4; }
```

**RAN parameters exposed:**
```protobuf
enum RAN_parameter {
  GNB_ID = 1;      // gNB identifier (read)
  UE_LIST = 3;     // per-UE metrics list (read)
  SCHED_INFO_ = 4; // scheduler info
  SCHED_CONTROL = 5;
  MAX_PRB = 6;     // cell-level PRB cap (write)
  USE_TRUE_GBR = 7;
  SLICING_CONTROL = 8; // per-slice min/max ratio (write)
}
```

**Per-UE message (`ue_info_m`)** — 25 optional fields:
- Scheduling: `tbs_avg_dl/ul`, `tbs_dl/ul_toapply`, `is_GBR`, `dl_mac_buffer_occupation`
- PRB/efficiency metrics: `avg_prbs_dl/ul`, `avg_tbs_per_prb_dl/ul`
- Radio: `avg_rsrp`, `ph`, `pcmax`, `dl/ul_mcs`
- Error/quality: `dl/ul_total_bytes`, `dl/ul_errors`, `dl/ul_bler`
- Slice context: `nssai_sST`, `nssai_sD`

**Control messages:**
- `sched_control_m` — carries `max_cell_allocable_prbs`
- `slicing_control_m` — carries `sst`, `sd`, `min_ratio`, `max_ratio`

---

### 3. Modified: `executables/nr-softmodem.c`

The main gNB entry point was modified to initialize the custom E2 agent after the standard FlexRIC agent startup:

```c
#include "openair2/E2_AGENT/e2_agent_app.h"
#ifdef E2_AGENT
#include "openair2/E2AP/flexric/src/agent/e2_agent_api.h"
#include "openair2/E2AP/RAN_FUNCTION/init_ran_func.h"

// initialize_agent() → starts FlexRIC E2 agent (existing path)
// then:
e2_agent_init();  // starts the new custom UDP+Protobuf agent
#endif
```

This means both the standard FlexRIC agent and the custom UDP agent run concurrently.

---

### 4. Modified: MAC Scheduler — PRB Cap Enforcement

**Files modified:**
- `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch.c`
- `openair2/LAYER2/NR_MAC_gNB/nr_mac_slice_sched_utils.c`

Both the DL scheduling unit and the slice budget preparation functions were modified to read `e2_agent_db->max_prb` and cap total allocatable PRBs at that value:

```c
// In dl_sched_unit() — gNB_scheduler_dlsch.c:930
int capped_total = total_prbs;
#ifdef E2_AGENT
if (e2_agent_db != NULL) {
  pthread_mutex_lock(&e2_agent_db->mutex);
  const int max_prb = e2_agent_db->max_prb;
  pthread_mutex_unlock(&e2_agent_db->mutex);
  if (max_prb >= 0 && capped_total > max_prb)
    capped_total = max_prb;
}
#endif
// proportionally scale per-slice budgets down to capped_total
```

```c
// In nr_mac_prepare_slice_budgets() — nr_mac_slice_sched_utils.c:14
int capped_total = total_prbs;
#ifdef E2_AGENT
if (e2_agent_db != NULL) {
  pthread_mutex_lock(&e2_agent_db->mutex);
  const int max_prb = e2_agent_db->max_prb;
  pthread_mutex_unlock(&e2_agent_db->mutex);
  if (max_prb >= 0 && capped_total > max_prb)
    capped_total = max_prb;
}
#endif
total_prbs = capped_total;
```

All E2 agent hooks in the scheduler are guarded with `#ifdef E2_AGENT` so the build remains backward-compatible.

---

### 5. Modified: Logging Infrastructure

**`common/utils/LOG/log.h`** — Added `E2_AGENT` as a registered logging component (one line, alongside existing components like `GNB_APP`, `MAC`, etc.).

**`common/utils/ocp_itti/intertask_interface.h`** — Added `TASK_E2_AGENT` with priority 200 to the ITTI task definitions. This is the task that runs the E2 agent's main UDP receive loop.

**`openair2/E2_AGENT/e2_agent_logging.h`** — Provides `E2_LOG_E/W/I/D` macros. Falls back to the `GNB_APP` component when the T tracer database has not been regenerated with the new E2_AGENT trace IDs, ensuring the code always compiles.

---

### 6. Modified: Top-level `CMakeLists.txt` and `openair2/CMakeLists.txt`

- The top-level build system was updated to expose an `E2_AGENT` CMake flag
- `openair2/CMakeLists.txt` conditionally includes the `E2_AGENT` subdirectory:
  ```cmake
  if(E2_AGENT)
    add_subdirectory(E2_AGENT)
  endif()
  ```
- The `e2_agent` static library is then linked into the gNB executable when the flag is set

---

## Part 2 — `o-ran-e2sim` Modifications

### Overview

The `o-ran-e2sim` directory contains a fork of the **O-RAN SC E2 Simulator** (originally by AT&T/Nokia, Apache 2.0), which provides a software E2 node that connects to a nearRT-RIC via SCTP/E2AP. The main customization is the addition of an **OAI Protobuf path** for bridging to the custom gNB E2 agent.

The E2 Simulator sits between:
```
[nearRT-RIC xApp]  ←—SCTP/E2AP/ASN.1—→  [E2Sim]  ←—UDP/Protobuf—→  [gNB E2 Agent]
```

---

### Key Files and Their Role

#### `e2sm_examples/kpm_e2sm/src/kpm/bs_connector.cpp` / `.hpp`

This is the **gNB bridge** — the component that interfaces the E2 Simulator with the actual gNB.

**`handleTimer()`** — called when a RIC subscription request is received. Depending on the `OAI_PROTOBUF` compile flag, it spawns one of two threads:

- **Legacy path** (`OAI_PROTOBUF=0`): `periodicDataReport()` — reads slice metrics from a local CSV/JSON file and encodes them into ASN.1 KPM indications for the RIC
- **OAI custom path** (`OAI_PROTOBUF=1`): `periodicDataReportOaiProtobuf()` — communicates with the gNB's custom E2 agent via UDP

**`periodicDataReportOaiProtobuf()`** — the custom OAI communication loop:

1. Opens two UDP sockets:
   - **Out socket** → sends to `127.0.0.1:6655` (gNB E2 agent inbound port)
   - **In socket** → binds to `127.0.0.1:6600` (gNB E2 agent outbound port)

2. In a loop (until subscription cancelled):
   ```
   → Send indication_request_buffer (protobuf) to gNB on port 6655
   → Wait 500ms
   → Receive protobuf response from gNB on port 6600
   → Decode protobuf response
   → Encode as ASN.1 KPM indication and send to RIC via SCTP
   → Sleep for subscription timer duration
   ```

The `indication_request_buffer` is the serialized `RAN_indication_request` protobuf received from the RIC subscription, forwarded verbatim to the gNB.

---

#### `e2sm_examples/kpm_e2sm/src/kpm/kpm_callbacks.cpp` / `.hpp`

Registers callback handlers for different RAN function IDs:

```cpp
e2sim.register_sm_callback(0,   &callback_kpm_subscription_request); // KPM RAN function 0
e2sim.register_sm_callback(1,   &callback_kpm_subscription_request); // KPM RAN function 1
e2sim.register_sm_callback(300, &callback_kpm_control);              // Control RAN function
```

- **`callback_kpm_subscription_request`** — handles incoming RIC subscription requests, calls `handleTimer()` to start periodic reporting
- **`callback_kpm_control`** — handles RIC control messages

---

#### `e2sm_examples/kpm_e2sm/src/kpm/encode_kpm.cpp` / `.hpp` (~2200 lines)

Handles all **ASN.1 KPM E2SM encoding**:
- `encode_kpm_function_description()` — encodes the KPM RAN Function Description sent to the RIC at setup
- `encode_and_send_ric_indication_report_metrics_buffer()` — takes raw metric data (either JSON or protobuf-decoded) and encodes a full KPM indication message (header + body) for transmission to the RIC via SCTP

---

#### `e2sm_examples/kpm_e2sm/src/kpm/e2sim_debug.hpp`

Introduces a runtime debug gating mechanism:

```cpp
#define E2SIM_DBG_FPRINTF(stream, ...) \
  do { if (getenv("E2SIM_DEBUG")) fprintf(stream, __VA_ARGS__); } while(0)
```

Verbose logging is zero-cost when the `E2SIM_DEBUG` environment variable is not set, and fully enabled at runtime without recompilation.

---

#### `e2sm_examples/kpm_e2sm/src/kpm/csv_reader.c` / `.h`

Reads slice performance metrics from a local CSV/JSON file at `/playpen/src/reports.json`. Used by the legacy (non-OAI) data reporting path. Example JSON format:

```json
{
  "timestamp": 1602706183796,
  "slice_id": 0,
  "dl_bytes": 53431,
  "dl_thr_mbps": 2.39,
  "ratio_granted_req_prb": 0.02,
  "slice_prb": 6,
  "dl_pkts": 200
}
```

---

#### `e2sm_examples/kpm_e2sm/src/kpm/srs_connector.cpp` / `viavi_connector.hpp`

Additional data source connectors (legacy paths):
- `srs_connector.cpp` — bridge to a srsRAN-based SCOPE RAN
- `viavi_connector.hpp` — support for VIAVI test equipment (port 3001)

These are the pre-existing E2Sim connectors; the OAI integration is the new addition.

---

#### `build_e2sim.sh` / `run_e2sim.sh`

- `build_e2sim.sh` — build script accepting an optional `-i` flag to also install dependencies
- `run_e2sim.sh` — launcher that accepts:
  ```
  ./run_e2sim.sh <e2term-address> <e2term-port> [gnb-id]
  ```
  The optional `gnb-id` parameter (4-byte hex) allows distinguishing multiple gNB instances in multi-gNB deployments.

---

## Summary: Overall Data Flow

```
[nearRT-RIC xApp]
      │
      │  E2AP / SCTP (standard O-RAN)
      ▼
[o-ran-e2sim (kpm_e2sm)]
      │  kpm_callbacks.cpp → handleTimer()
      │
      │  OAI_PROTOBUF=1 path:
      │  UDP port 6655 → RAN_indication_request (protobuf)
      ▼
[oai_ran — E2_AGENT (e2_agent_app.c)]
      │  e2_agent_task() → handle_master_message()
      │  → get_ue_list() reads MAC stats
      │  → computes avg_prbs, avg_tbs_per_prb
      │  UDP port 6600 ← RAN_indication_response (protobuf)
      ▼
[o-ran-e2sim (bs_connector.cpp)]
      │  encode_and_send_ric_indication_report_metrics_buffer()
      │  ASN.1 KPM Indication / SCTP
      ▼
[nearRT-RIC xApp — KPI analytics]
```

Control flow (xApp → gNB):
```
[xApp] → RIC Control Request (E2AP)
[E2Sim] → RAN_control_request (UDP 6655, protobuf)
[E2_AGENT] → apply_max_cell_prb() / apply_slicing_ctrl()
[MAC Scheduler] → PRB cap / slice ratio enforced
```
