#!/bin/bash

# Default values
FILE=""
ALIAS=""
CONTAINER_NAME=""
DEFAULT_DIR="./images"
STORAGE_POOL="bigpool"  # <--- Add this line
usage() {
  echo "Usage: $0 -f <image-file-path> [-a <image-alias>] [-c <container-name>]"
  exit 1
}

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    -f|--file) FILE="$2"; shift 2 ;;
    -a|--alias) ALIAS="$2"; shift 2 ;;
    -c|--container) CONTAINER_NAME="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1"; usage ;;
  esac
done

# Validate
if [ -z "$FILE" ]; then echo "❌ Error: image file path is required."; usage; fi
BASENAME=$(basename "$FILE")
ALIAS="${ALIAS:-${BASENAME%.tar.gz}}"
CONTAINER_NAME="${CONTAINER_NAME:-${ALIAS}-cont}"

# Import or use existing image
EXISTING_FINGERPRINT=$(lxc image info "$ALIAS" 2>/dev/null | grep '^Fingerprint:' | awk '{print $2}')

if [ -n "$EXISTING_FINGERPRINT" ]; then
  echo "⚠️  Image alias '$ALIAS' already exists with fingerprint: $EXISTING_FINGERPRINT"
else
  echo "📦 Importing image as alias '$ALIAS'..."
  lxc image import "$FILE" --alias "$ALIAS" || {
    echo "❌ Failed to import image."; exit 1;
  }
fi

# Check container
if lxc list --format csv | cut -d',' -f1 | grep -q "^$CONTAINER_NAME$"; then
  echo "✅ Container '$CONTAINER_NAME' already exists. Skipping launch."
else
  echo "🚀 Launching container '$CONTAINER_NAME'..."
  lxc init "$ALIAS" "$CONTAINER_NAME" -s "$STORAGE_POOL" || {
    echo "❌ Failed to launch container."; exit 1;
  }

  echo "🚀 Starting container '$CONTAINER_NAME'..."
  lxc start "$CONTAINER_NAME" || {
    echo "❌ Failed to start container."; exit 1;
  }
fi

echo "✅ Container '$CONTAINER_NAME' is ready."
