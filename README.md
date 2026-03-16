# OAI-RAN-Network

A modified OpenAirInterface 5G NR gNB extended with a custom E2 agent for O-RAN control and network slicing support, deployable on the Colosseum wireless network emulator or on any LXC-capable host.

The system comprises three components:

| Component | Directory | Description |
|-----------|-----------|-------------|
| LXC Container Setup | `src/` | Scripts to provision the LXC container, install dependencies, and compile the gNB |
| OAI gNB | `oai_ran/` | Modified OAI gNB with E2 agent and network slicing, plus UE launch scripts |
| E2 Simulator | `o-ran-e2sim/` | E2AP/SCTP simulator bridging the nearRT-RIC and the OAI gNB over UDP |

---

## Deployment Sequence

The three components are set up and launched in the following order:

**Step 1 — Provision the container**

Download the base LXC image, initialize the container, and compile the gNB binaries. This is a one-time operation per machine.

→ See [`src/README.md`](src/README.md)

---

**Step 2 — Launch the gNB**

Start `nr-softmodem` inside the container. The launch script configures the network interface, injects the IP into the configuration file, derives the gNB identity, and starts the process.

→ See [`oai_ran/README.md`](oai_ran/README.md)

---

**Step 3 — Launch the E2 simulator**

Start `kpm_sim` to connect the gNB to the nearRT-RIC over E2AP/SCTP. The simulator relays per-UE KPIs to the RIC and forwards control commands (PRB caps, slice ratios) back to the gNB over UDP.

→ See [`o-ran-e2sim/README.md`](o-ran-e2sim/README.md)

---

## Deployment Modes

Both **rfsim** (no hardware, local machine) and **USRP / Colosseum** (real hardware) modes are supported. The mode is specified as an argument at each launch step.

---

## Upstream Sources

- [wineslab/ORANSlice](https://github.com/wineslab/ORANSlice) — OAI gNB baseline with slicing support
- [wineslab/o-ran-e2sim](https://github.com/wineslab/o-ran-e2sim) — E2 simulator base (`x5g-e2sim` branch)
- [OpenAirInterface5G](https://gitlab.eurecom.fr/oai/openairinterface5g/) — OAI develop branch
