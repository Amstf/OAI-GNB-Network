#!/bin/bash

# Usage: ./download_image.sh <image-name.tar.gz> <remote-user>

IMAGE_NAME="$1"
REMOTE_USER="$2"
GW_SERVER="gw.colosseum.net"
FILE_PROXY="file-proxy"
REMOTE_IMAGE_DIR="/share/nas/common"
LOCAL_DIR="./images"

if [ -z "$IMAGE_NAME" ] || [ -z "$REMOTE_USER" ]; then
  echo "❌ Error: Missing arguments."
  echo "Usage: $0 <image-name.tar.gz> <remote-user>"
  exit 1
fi

mkdir -p "$LOCAL_DIR"
cd "$LOCAL_DIR" || { echo "❌ Failed to enter $LOCAL_DIR"; exit 1; }

# Check if already downloaded
if [ -f "$IMAGE_NAME" ]; then
  echo "✅ Image '$IMAGE_NAME' already exists locally. Skipping download."
  exit 0
fi

# Download image only
rsync -vP -e "ssh -J $REMOTE_USER@$GW_SERVER" \
  "$REMOTE_USER@$FILE_PROXY:$REMOTE_IMAGE_DIR/$IMAGE_NAME" . || {
  echo "❌ Failed to download image from remote server."; exit 1;
}

echo "✅ Download complete."
