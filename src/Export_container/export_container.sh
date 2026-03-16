
# === export_container.sh ===
# Usage: ./export_container.sh <container-name> [image-alias] [ssh-key-path-inside-container]

CONTAINER="$1"
IMAGE_ALIAS="${2:-$CONTAINER}"
KEY_PATH_IN_CONTAINER="$3"
EXPORT_PATH=~/myimages
EXPORT_FILE="$EXPORT_PATH/$IMAGE_ALIAS.tar.gz"

if [ -z "$CONTAINER" ]; then
  echo "❌ Usage: $0 <container-name> [image-alias] [ssh-key-path-inside-container]"
  exit 1
fi

if ! lxc list --format csv | cut -d',' -f1 | grep -q "^$CONTAINER$"; then
  echo "❌ Error: Container '$CONTAINER' not found."
  exit 1
fi

# Step 1: Clean up SSH keys inside container if path provided
if [ -n "$KEY_PATH_IN_CONTAINER" ]; then
  echo "🧹 Removing SSH keys from '$CONTAINER' at path '$KEY_PATH_IN_CONTAINER'..."
  lxc exec "$CONTAINER" -- rm -rf "$KEY_PATH_IN_CONTAINER"
  echo "✅ SSH keys removed."
fi

# Step 2: Stop container
STATE=$(lxc info "$CONTAINER" | grep "^Status:" | awk '{print $2}')
if [ "$STATE" != "Stopped" ]; then
  echo "⏹️  Stopping container '$CONTAINER'..."
  lxc stop "$CONTAINER" || exit 1
fi

# Step 3: Publish and export
if ! lxc image list --format csv | cut -d',' -f1 | grep -q "^$IMAGE_ALIAS$"; then
  echo "📦 Publishing container as image alias '$IMAGE_ALIAS'..."
  lxc publish "$CONTAINER" --alias "$IMAGE_ALIAS" || exit 1
else
  echo "✅ Image alias '$IMAGE_ALIAS' already exists."
fi

mkdir -p "$EXPORT_PATH"
echo "📤 Exporting image to $EXPORT_FILE"
rm -f "$EXPORT_FILE"
lxc image export "$IMAGE_ALIAS" "$EXPORT_PATH/$IMAGE_ALIAS" || exit 1
chmod 755 "$EXPORT_FILE"

echo "✅ Export complete: $EXPORT_FILE"
