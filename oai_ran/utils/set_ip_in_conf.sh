#!/bin/bash

# Check for input argument
if [ -z "$1" ]; then
  echo "Usage: $0 <path-to-gnb-config-file>"
  exit 1
fi

CONF_FILE="$1"

# Get IP of col0 interface
col0_ip=$(ip -f inet addr show col0 | grep -Po 'inet \K[\d.]+')

echo ""
echo "IP address of col0 interface is: ${col0_ip}"
echo "Updating gNB config file: $CONF_FILE"

# Update NG-AMF IP
sed -i "/GNB_IPV4_ADDRESS_FOR_NG_AMF/ c \        GNB_IPV4_ADDRESS_FOR_NG_AMF              = \"$col0_ip\/24\";" "$CONF_FILE"

# Update NGU IP
sed -i "/GNB_IPV4_ADDRESS_FOR_NGU/ c \        GNB_IPV4_ADDRESS_FOR_NGU                 = \"$col0_ip\/24\";" "$CONF_FILE"

echo "gNB config file updated successfully!"
echo ""

