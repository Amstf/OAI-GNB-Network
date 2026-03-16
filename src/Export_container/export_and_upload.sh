

# === export_and_upload.sh ===
# Usage: ./export_and_upload.sh <container-name> [image-alias] [ssh-key-path-inside-container] [remote-user] [export-path] [gw-server] [file-proxy] [team-nas-path]

CONTAINER="$1"
IMAGE_ALIAS="${2:-$CONTAINER}"
KEY_PATH_INSIDE="$3"
REMOTE_USER="$4"
EXPORT_PATH="$5"
GW_SERVER="$6"
FILE_PROXY="$7"
TEAM_NAS_PATH="$8"

if [ -z "$CONTAINER" ]; then
  echo "❌ Usage: $0 <container-name> [image-alias] [ssh-key-path-inside-container] [remote-user] [export-path] [gw-server] [file-proxy] [team-nas-path]"
  exit 1
fi

echo "🛠️  Step 1: Exporting container '$CONTAINER' as image '$IMAGE_ALIAS'..."
./export_container.sh "$CONTAINER" "$IMAGE_ALIAS" "$KEY_PATH_INSIDE" || {
  echo "❌ Export step failed."; exit 1;
}

echo "📤 Step 2: Uploading image '$IMAGE_ALIAS' to Colosseum..."
./upload_image.sh "$IMAGE_ALIAS" "$REMOTE_USER" "$EXPORT_PATH" "$GW_SERVER" "$FILE_PROXY" "$TEAM_NAS_PATH" || {
  echo "❌ Upload step failed."; exit 1;
}

echo "🎉 Done: Container '$CONTAINER' exported and uploaded as '$IMAGE_ALIAS'"
