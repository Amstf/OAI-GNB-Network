#!/bin/bash

# Prompt for GitHub username and email
read -p "Enter your GitHub username: " GITHUB_USER
read -p "Enter your GitHub email: " GITHUB_EMAIL

KEY_NAME="github-keys"
read -p "Enter passphrase for SSH key (default: none): " SSH_PASSPHRASE
read -p "Enter directory to save SSH key (default: ~/.ssh): " SSH_DIR
SSH_DIR="${SSH_DIR:-$HOME/.ssh}"
KEY_PATH="$SSH_DIR/$KEY_NAME"

# Create SSH directory if it doesn't exist
mkdir -p "$SSH_DIR"
chmod 700 "$SSH_DIR"

# Check if key already exists
if [ -f "$KEY_PATH" ]; then
  echo "⚠️ SSH key '$KEY_NAME' already exists at path: $KEY_PATH. Skipping key generation."
else
  # Generate SSH key with optional passphrase
  echo "🔑 Generating SSH key named '$KEY_NAME' at path: $KEY_PATH..."
  ssh-keygen -t rsa -b 4096 -C "$GITHUB_EMAIL" -f "$KEY_PATH" -N "${SSH_PASSPHRASE:-}" || {
    echo "❌ Failed to generate SSH key."; exit 1;
  }
fi

# Display public key for GitHub
PUBLIC_KEY="$(cat "$KEY_PATH.pub")"
echo ""
echo "📌 ======== ACTION REQUIRED ======== 📌"
echo ""
echo "✅ Copy the following public SSH key and add it to your GitHub account:"
echo "   - Open https://github.com/settings/keys"
echo "   - Click 'New SSH key'"
echo "   - Paste the following key:"
echo ""
echo "$PUBLIC_KEY"
echo ""
echo "⚠️ Once added, you can use this key to clone your repositories using SSH."
echo ""
echo "Test SSH connection to GitHub with:"
echo "ssh -i $KEY_PATH -T git@github.com"
echo ""
echo "===================================="
