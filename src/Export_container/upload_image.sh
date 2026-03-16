
# Usage: ./upload_image.sh <image-alias> [remote-user] [export-path] [gw-server] [file-proxy] [team-nas-path]

IMAGE_ALIAS="$1"
REMOTE_USER="${2:-alimustapha}"
EXPORT_PATH="${3:-$HOME/myimages}"
GW_SERVER="${4:-172.16.1.200}"
FILE_PROXY="${5:-file-proxy}"
TEAM_NAS_PATH="${6:-/share/nas/netmon5g/images}"

FILE="$EXPORT_PATH/$IMAGE_ALIAS.tar.gz"
REMOTE_FILE_PATH="$TEAM_NAS_PATH/$IMAGE_ALIAS.tar.gz"

if [ -z "$IMAGE_ALIAS" ]; then
  echo "❌ Usage: $0 <image-alias> [remote-user] [export-path] [gw-server] [file-proxy] [team-nas-path]"
  exit 1
fi

if [ ! -f "$FILE" ]; then
  echo "❌ Error: Exported image '$FILE' not found."
  exit 1
fi

echo "📡 Uploading '$FILE' to Colosseum file-proxy as '$REMOTE_USER'..."
rsync -av --progress -e "ssh -J $REMOTE_USER@$GW_SERVER" \
  "$FILE" "$REMOTE_USER@$FILE_PROXY:$TEAM_NAS_PATH/" || {
    echo "❌ Upload failed."; exit 1;
  }

echo "🔐 Setting permissions to 755 on remote file..."
ssh -J "$REMOTE_USER@$GW_SERVER" "$REMOTE_USER@$FILE_PROXY" chmod 755 "$REMOTE_FILE_PATH" || {
  echo "⚠️ Failed to set remote file permissions."
  exit 1
}

echo "✅ Upload complete and permissions set: $REMOTE_FILE_PATH"
