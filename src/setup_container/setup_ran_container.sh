#!/bin/bash

# setup_ran_container.sh — Setup and build OAI RAN inside a container
# Usage:
#   ./setup_ran_container.sh <image-name.tar.gz> <build-type: rfsim|usrp> [alias] [container-name] [remote-user] [ssh-key-path] [local-repo-path]
#   ./setup_ran_container.sh <image-name.tar.gz> <build-type: rfsim|usrp> [alias] [container-name] [remote-user] [ssh-key-path] --clone

# ─── Parse arguments ────────────────────────────────────────────────────────

IMAGE_NAME="$1"
BUILD_TYPE="$2"
ALIAS="${3:-${IMAGE_NAME%.tar.gz}}"
CONTAINER="${4:-${ALIAS}-cont}"
REMOTE_USER="${5:-alimustapha}"
SSH_KEY_PATH="$6"
REPO_OR_FLAG="$7"

USE_CLONE=false
LOCAL_REPO_PATH=""

if [ "$REPO_OR_FLAG" = "--clone" ]; then
  USE_CLONE=true
elif [ -n "$REPO_OR_FLAG" ]; then
  LOCAL_REPO_PATH="$REPO_OR_FLAG"
else
  # Default: use local repo relative to this script
  LOCAL_REPO_PATH="$(cd "$(dirname "$0")/../.." && pwd)"
fi

REPO_URL="git@github.com:Amstf/OAI-GNB-Network.git"
DEST_PATH="/root/OAI-GNB-Network"

# ─── Validate input ──────────────────────────────────────────────────────────

if [ -z "$IMAGE_NAME" ] || [ -z "$BUILD_TYPE" ]; then
  echo "Usage: $0 <image-name.tar.gz> <build-type: rfsim|usrp> [alias] [container-name] [remote-user] [ssh-key-path] [local-repo-path|--clone]"
  exit 1
fi

if [[ "$BUILD_TYPE" != "rfsim" && "$BUILD_TYPE" != "usrp" ]]; then
  echo "❌ Invalid build type: $BUILD_TYPE. Must be 'rfsim' or 'usrp'."
  exit 1
fi

if [ "$USE_CLONE" = false ] && [ ! -d "$LOCAL_REPO_PATH" ]; then
  echo "❌ Local repo path not found: $LOCAL_REPO_PATH"
  echo "   Either fix the path, or pass --clone to clone from GitHub instead."
  exit 1
fi

# ─── Display setup summary ───────────────────────────────────────────────────

echo "🤩 Starting setup for:"
echo "  📦 Image:       $IMAGE_NAME"
echo "  ⚙️  Build type:  $BUILD_TYPE"
echo "  🏷️  Alias:       $ALIAS"
echo "  🐧 Container:   $CONTAINER"
echo "  🌐 Remote User: $REMOTE_USER"
echo "  🔐 SSH Key:     ${SSH_KEY_PATH:-None provided}"
if [ "$USE_CLONE" = true ]; then
  echo "  📥 Repo mode:   Clone from GitHub ($REPO_URL)"
else
  echo "  📁 Repo mode:   Copy from local ($LOCAL_REPO_PATH)"
fi
echo ""

# ─── Step 1: Download image ──────────────────────────────────────────────────

echo "▶️  [1/5] Downloading image..."
./download_image.sh "$IMAGE_NAME" "$REMOTE_USER" || { echo "❌ Download failed."; exit 1; }

# ─── Step 2: Import & Launch ─────────────────────────────────────────────────

echo "▶️  [2/5] Importing & launching container..."
./import_and_launch.sh -f "./images/$IMAGE_NAME" -a "$ALIAS" -c "$CONTAINER" || { echo "❌ Import/launch failed."; exit 1; }

# ─── Step 3: Set network ─────────────────────────────────────────────────────

echo "▶️  [3/5] Setting up network..."
./set_lxc_network.sh "$CONTAINER" || { echo "❌ Network setup failed."; exit 1; }

# ─── Step 4: Push SSH key ────────────────────────────────────────────────────

if [ -n "$SSH_KEY_PATH" ]; then
  echo "▶️  [4/5] Pushing SSH key..."
  lxc exec "$CONTAINER" -- mkdir -p /root/.ssh
  lxc file push "$SSH_KEY_PATH"     "$CONTAINER"/root/.ssh/id_rsa
  lxc file push "$SSH_KEY_PATH.pub" "$CONTAINER"/root/.ssh/id_rsa.pub
  lxc exec "$CONTAINER" -- chmod 600 /root/.ssh/id_rsa
  lxc exec "$CONTAINER" -- chmod 644 /root/.ssh/id_rsa.pub
  lxc exec "$CONTAINER" -- bash -c "ssh-keyscan github.com >> /root/.ssh/known_hosts"

  echo "🧪 Testing SSH connection to GitHub from inside container..."
  lxc exec "$CONTAINER" -- ssh -i /root/.ssh/id_rsa -T git@github.com
  SSH_EXIT=$?

  if [ "$SSH_EXIT" -eq 1 ]; then
    echo "✅ SSH authentication to GitHub succeeded."
  elif [ "$SSH_EXIT" -ne 0 ]; then
    echo "⚠️  SSH test failed. Check key permissions or GitHub settings."
  fi
else
  echo "⚠️  No SSH key path provided. Skipping SSH key setup."
  if [ "$USE_CLONE" = true ]; then
    echo "❌ --clone requires an SSH key. Provide one as argument 6."
    exit 1
  fi
fi

# ─── Step 5: Get the repository ──────────────────────────────────────────────

echo "▶️  [5/5] Setting up OAI-GNB-Network repository..."

if [ "$USE_CLONE" = true ]; then

  echo "  📥 Cloning from GitHub: $REPO_URL"
  lxc exec "$CONTAINER" -- bash -c "git clone $REPO_URL $DEST_PATH" || {
    echo "❌ Failed to clone repository."; exit 1;
  }
  echo "✅ Repository cloned to $CONTAINER:$DEST_PATH"

else

  echo "  📁 Copying from local: $LOCAL_REPO_PATH"
  lxc file push --recursive "$LOCAL_REPO_PATH" "$CONTAINER"/root/ || {
    echo "❌ Failed to copy OAI-GNB-Network into container."; exit 1;
  }
  echo "✅ Repository copied to $CONTAINER:$DEST_PATH"

fi

# ─── Step 5b: Install dependencies ──────────────────────────────────────────

echo "🔧 Installing dependencies..."
lxc exec "$CONTAINER" -- apt-get update
lxc exec "$CONTAINER" -- apt-get install -y autoconf automake libtool curl make g++ pkg-config \
  libprotobuf-dev protobuf-compiler libprotoc-dev net-tools iputils-ping \
  libjson-c-dev

echo "📦 Cloning and building protobuf-c..."
lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH && git clone https://github.com/protobuf-c/protobuf-c"
lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH/protobuf-c && ./autogen.sh && ./configure && make && make install && ldconfig"

echo "⚙️  Installing OAI deps..."
lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH/oai_ran/cmake_targets && ./build_oai -I"

echo "📄 You can tail ASN1C log if needed:"
echo "    lxc exec $CONTAINER -- tail -f $DEST_PATH/oai_ran/cmake_targets/log/asn1c_install_log.txt"

# ─── Step 5c: Build OAI RAN ──────────────────────────────────────────────────

if [ "$BUILD_TYPE" = "rfsim" ]; then
  echo "📡 Building gNB and nrUE with SIMU..."
  lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH/oai_ran/cmake_targets && ./build_oai -w SIMU --ninja --gNB"
  lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH/oai_ran/cmake_targets && ./build_oai -w SIMU --ninja --nrUE"

  echo "📄 Copying RFSIM config files into build directory..."
  lxc exec "$CONTAINER" -- bash -c "cp $DEST_PATH/oai_ran/nrUE_slice1.conf $DEST_PATH/oai_ran/cmake_targets/ran_build/build/"
  lxc exec "$CONTAINER" -- bash -c "cp $DEST_PATH/oai_ran/oai-gnb.conf $DEST_PATH/oai_ran/cmake_targets/ran_build/build/"
else
  echo "📡 Building gNB and nrUE with USRP..."
  lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH/oai_ran/cmake_targets && ./build_oai -w USRP --ninja --gNB"
  lxc exec "$CONTAINER" -- bash -c "cd $DEST_PATH/oai_ran/cmake_targets && ./build_oai -w USRP --ninja --nrUE"
fi

# ─── Done ────────────────────────────────────────────────────────────────────

echo ""
echo "✅ All steps completed successfully for container '$CONTAINER'"
echo "   Repo location: $DEST_PATH"
