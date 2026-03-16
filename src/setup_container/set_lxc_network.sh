#!/bin/bash

show_help() {
  echo "Usage: $0 <container-name>"
  exit 1
}

[ "$1" == "-h" ] || [ "$1" == "--help" ] && show_help
[ -z "$1" ] && echo "❌ Error: Container name required." && show_help

CONTAINER="$1"
BRIDGE="lxdbr1"

# Check container exists
if ! lxc list --format csv | cut -d',' -f1 | grep -q "^$CONTAINER$"; then
  echo "❌ Error: Container '$CONTAINER' does not exist."
  exit 1
fi

echo "🔧 Fixing network for container: $CONTAINER"

# Attach bridge
lxc network attach "$BRIDGE" "$CONTAINER" eth0 2>/dev/null || echo "⚠️ Bridge might already be attached."

# Bring interface up + request IP
lxc exec "$CONTAINER" -- bash -c "ip link set eth0 up && dhclient eth0" || {
  echo "❌ Failed to bring up eth0."; exit 1;
}

# DNS fix
lxc exec "$CONTAINER" -- bash -c '
  rm -f /etc/resolv.conf &&
  printf "nameserver 8.8.8.8\nnameserver 1.1.1.1\n" >/etc/resolv.conf &&
  chattr +i /etc/resolv.conf   # remove if you *do* want DHCP to change it later
'

# Test
echo "✅ IP configuration:"
lxc exec "$CONTAINER" -- ip a show eth0

echo "🌐 Testing connectivity..."
lxc exec "$CONTAINER" -- ping -c 3 8.8.8.8 || {
  echo "❌ Ping failed. Internet may still be down."; exit 1;
}

echo "🎉 Network fix complete for: $CONTAINER"
