# Colosseum / LXC Container Setup

Scripts for automating the full LXC container lifecycle when deploying the OAI RAN on the [Colosseum](https://www.colosseum.net) wireless network emulator, or on any LXC-capable host. The tooling covers base image acquisition, container initialization, dependency installation, and binary compilation. Both **rfsim** (no hardware) and **USRP / Colosseum** deployment modes are supported; the mode is specified as an argument at invocation.

> After setup, refer to **[`../oai_ran/README.md`](../oai_ran/README.md)** for instructions on running the gNB and UE.

---

## Directory Structure

```
src/
├── setup_container/              # Scripts to download, import, and prepare an LXC container
│   ├── setup_ran_container.sh        # Main orchestration script
│   ├── download_image.sh             # Downloads the base image from Colosseum storage
│   ├── import_and_launch.sh          # Imports the image into LXC and starts the container
│   └── set_lxc_network.sh            # Configures network inside the container
├── Export_container/             # Scripts to snapshot and upload a container to Colosseum
│   ├── export_container.sh           # Exports the running container as a .tar.gz image
│   ├── upload_image.sh               # Uploads the exported image to Colosseum storage
│   └── export_and_upload.sh          # Runs export then upload in one step
└── setup_Github/                 # SSH key generation helper for GitHub access
    └── generate_github_keys.sh
```

---

## Prerequisites

LXC must be configured on the host before using these scripts. For installation and setup instructions, refer to `<add-your-lxc-setup-doc-here>`.

---

## Pre-step — GitHub SSH Key Setup (one-time)

Before running the container setup script, generate and register an SSH key for GitHub access.

**1. Run the key generator:**

```bash
cd src/setup_Github
./generate_github_keys.sh
```

When prompted, enter a GitHub username, email, passphrase (optional), and target directory (default: `~/.ssh`). The script prints the public key on completion.

**2. Add the public key to GitHub:**
- Go to [https://github.com/settings/keys](https://github.com/settings/keys)
- Click **New SSH key** and paste the printed key.

**3. Verify authentication:**

```bash
ssh -i ~/.ssh/github-keys -T git@github.com
```

Expected output:
```
Hi <your-username>! You've successfully authenticated, but GitHub does not provide shell access.
```

---

## Setting Up the Container — `setup_ran_container.sh`

`setup_ran_container.sh` automates the full pipeline from a base LXC image to a compiled, ready-to-run gNB environment. Run it from the `src/setup_container/` directory.

### Usage

```bash
cd src/setup_container
./setup_ran_container.sh \
    <image-name.tar.gz> \
    <rfsim|usrp> \
    [alias] \
    [container-name] \
    [remote-user] \
    [ssh-key-path]
```

| Argument | Required | Description |
|----------|----------|-------------|
| `image-name.tar.gz` | yes | Filename of the base LXC image on Colosseum storage |
| `rfsim` or `usrp` | yes | Build mode: RF simulator (no hardware) or USRP (Colosseum) |
| `alias` | no | LXC image alias — defaults to filename without `.tar.gz` |
| `container-name` | no | LXC container name — defaults to `<alias>-cont` |
| `remote-user` | no | Colosseum username — defaults to `alimustapha` |
| `ssh-key-path` | no | Path to the GitHub SSH private key (host-side) to copy into the container |

### Example

```bash
./setup_ran_container.sh base-2204.tar.gz usrp ran-image ran-cont myuser ~/.ssh/github-keys
```

### Execution Steps

The script runs **5 sequential steps**.

---

**[1/5] Download the image** — `download_image.sh`

Downloads the base LXC image from the Colosseum shared NAS (`/share/nas/common`), proxied through the Colosseum gateway (`gw.colosseum.net`). If the image is already present locally, this step is skipped. The image is saved under `./images/`.

---

**[2/5] Import and launch the container** — `import_and_launch.sh`

Imports the downloaded image into LXC under the given alias, then initializes and starts a container from it using the `bigpool` storage pool. Both steps are idempotent — if the image alias or container already exists, they are skipped.

---

**[3/5] Configure the network** — `set_lxc_network.sh`

Attaches the `lxdbr1` bridge to the container's `eth0` interface, brings it up, and obtains a DHCP lease. Writes a static DNS configuration to prevent DHCP from overwriting `/etc/resolv.conf`. Verifies internet connectivity before exiting.

---

**[4/5] Push SSH key** *(only if `ssh-key-path` is provided)*

Copies the GitHub SSH key pair into `/root/.ssh/` inside the container as `id_rsa` / `id_rsa.pub`, sets correct permissions, adds `github.com` to known hosts, and runs an authentication test.

---

**[5/5] Clone, install, and build** — runs entirely inside the container

- Clones the `OAI-RAN-Network` repository from GitHub
- Installs all required system packages (build tools, protobuf libraries, network utilities)
- Clones and builds `protobuf-c` from source (required for the custom E2 agent)
- Installs all OAI-specific build dependencies via `build_oai -I`, including UHD drivers, the ASN.1 compiler, and other OAI toolchain prerequisites; this may take several minutes
- Builds the gNB and UE binaries using the RF simulator driver (`-w SIMU`) in **rfsim** mode, or the USRP driver (`-w USRP`) in **usrp** mode
- In **rfsim** mode only: copies `oai-gnb.conf` and `nrUE_slice1.conf` into the build output directory

Compiled binaries are written to:
```
/root/OAI-RAN-Network/oai_ran/cmake_targets/ran_build/build/
```

---

## Exporting a Container — `Export_container/`

A working container can be snapshotted and pushed back to Colosseum storage for reuse or sharing.

| Script | Description |
|--------|-------------|
| `export_container.sh` | Stops the container, publishes it as an LXC image, and exports it to `~/myimages/<alias>.tar.gz`. Optionally removes a private SSH key from inside the image before export to avoid credential leakage. |
| `upload_image.sh` | Uploads the exported `.tar.gz` to the Colosseum shared NAS via the gateway jump host. |
| `export_and_upload.sh` | Runs export then upload in one step. |

### Usage

```bash
cd src/Export_container

# Export only
./export_container.sh <container-name> [image-alias] [ssh-key-path-inside-container]

# Export and upload in one step
./export_and_upload.sh <container-name> [image-alias] [ssh-key-path-inside-container] [remote-user] [export-path] [gw-server] [file-proxy] [team-nas-path]
```
