#!/bin/bash

# setup_ran_container.sh — Setup and build OAI RAN inside a container
# Usage:
# ./setup_ran_container.sh <image-name.tar.gz> <build-type: rfsim|usrp> [alias] [container-name] [remote-user] [ssh-key-path]

IMAGE_NAME="$1"
BUILD_TYPE="$2"
ALIAS="${3:-${IMAGE_NAME%.tar.gz}}"
CONTAINER="${4:-${ALIAS}-cont}"
REMOTE_USER="${5:-alimustapha}"
SSH_KEY_PATH="$6"

if [ -z "$IMAGE_NAME" ] || [ -z "$BUILD_TYPE" ]; then
  echo "Usage: $0 <image-name.tar.gz> <build-type: rfsim|usrp> [alias] [container-name] [remote-user] [ssh-key-path]"
  exit 1
fi

if [[ "$BUILD_TYPE" != "rfsim" && "$BUILD_TYPE" != "usrp" ]]; then
  echo "❌ Invalid build type: $BUILD_TYPE. Must be 'rfsim' or 'usrp'."
  exit 1
fi

echo "🤩 Starting setup for:"
echo "  📦 Image:      $IMAGE_NAME"
echo "  ⚙️  Build type: $BUILD_TYPE"
echo "  🏷️  Alias:      $ALIAS"
echo "  🐧 Container:  $CONTAINER"
echo "  🌐 Remote User: $REMOTE_USER"
echo "  🔐 SSH Key:    ${SSH_KEY_PATH:-None provided}"
echo ""

# Download image
echo "▶️  [1/5] Downloading image..."
./download_image.sh "$IMAGE_NAME" "$REMOTE_USER" || { echo "❌ Download failed."; exit 1; }

# Import & launch
echo "▶️  [2/5] Importing & launching container..."
./import_and_launch.sh -f "./images/$IMAGE_NAME" -a "$ALIAS" -c "$CONTAINER" || { echo "❌ Import/launch failed."; exit 1; }

# Set network
echo "▶️  [3/5] Setting up network..."
./set_lxc_network.sh "$CONTAINER" || { echo "❌ Network setup failed."; exit 1; }

# Push SSH key
if [ -n "$SSH_KEY_PATH" ]; then
  echo "▶️  [4/5] Pushing SSH key..."
  BASENAME=$(basename "$SSH_KEY_PATH")
  lxc exec "$CONTAINER" -- mkdir -p /root/.ssh
  lxc file push "$SSH_KEY_PATH" "$CONTAINER"/root/.ssh/id_rsa
  lxc file push "$SSH_KEY_PATH.pub" "$CONTAINER"/root/.ssh/id_rsa.pub
  lxc exec "$CONTAINER" -- chmod 600 /root/.ssh/id_rsa
  lxc exec "$CONTAINER" -- chmod 644 /root/.ssh/id_rsa.pub
  lxc exec "$CONTAINER" -- bash -c "ssh-keyscan github.com >> /root/.ssh/known_hosts"
  lxc exec "$CONTAINER" -- ssh -i /root/.ssh/id_rsa -T git@github.com || echo "⚠️  SSH test might fail if no shell access."
fi

# Build steps
echo "▶️  [5/5] Building OAI RAN..."

echo "📥 Cloning OAI-RAN-Network..."
lxc exec "$CONTAINER" -- git clone git@github.com:Amstf/OAI-RAN-Network.git

echo "🔧 Installing dependencies..."
lxc exec "$CONTAINER" -- apt-get update
lxc exec "$CONTAINER" -- apt-get install -y autoconf automake libtool curl make g++ pkg-config \
  libprotobuf-dev protobuf-compiler libprotoc-dev protobuf-compiler net-tools iputils-ping

echo "📦 Cloning and building protobuf-c..."
lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network && git clone https://github.com/protobuf-c/protobuf-c"
lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network/protobuf-c && ./autogen.sh && ./configure && make && make install && ldconfig"

echo "⚙️  Installing OAI deps..."
lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network/oai_ran/cmake_targets && ./build_oai -I"

echo "📄 You can tail ASN1C log if needed:"
echo "    lxc exec $CONTAINER -- tail -f /root/OAI-RAN-Network/oai_ran/cmake_targets/log/asn1c_install_log.txt"

if [ "$BUILD_TYPE" = "rfsim" ]; then
  echo "📡 Building gNB and nrUE with SIMU..."
  lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network/oai_ran/cmake_targets && ./build_oai -w SIMU --ninja --gNB"
  lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network/oai_ran/cmake_targets && ./build_oai -w SIMU --ninja --nrUE"

  echo "📄 Copying RFSIM config files into build directory..."
  lxc exec "$CONTAINER" -- bash -c "cp /root/OAI-RAN-Network/oai_ran/nrUE_slice1.conf /root/OAI-RAN-Network/oai_ran/cmake_targets/ran_build/build/"
  lxc exec "$CONTAINER" -- bash -c "cp /root/OAI-RAN-Network/oai_ran/oai-gnb.conf /root/OAI-RAN-Network/oai_ran/cmake_targets/ran_build/build/"
else
  echo "📡 Building gNB and nrUE with USRP..."
  lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network/oai_ran/cmake_targets && ./build_oai -w USRP --ninja --gNB"
  lxc exec "$CONTAINER" -- bash -c "cd /root/OAI-RAN-Network/oai_ran/cmake_targets && ./build_oai -w USRP --ninja --nrUE"
fi

echo "✅ All steps completed successfully!"
