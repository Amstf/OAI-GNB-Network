#! /usr/bin/env python3
import logging, scapy.config, scapy.layers.l2, scapy.route
import socket, math, errno, os, getopt, sys, subprocess

logging.basicConfig(format='%(message)s', level=logging.DEBUG)
logger = logging.getLogger(__name__)

SUBNET_CIDR = "192.168.70.128/26"
CN_IP = "192.168.70.129"

def long2net(arg):
    if (arg <= 0 or arg >= 0xFFFFFFFF):
        raise ValueError("illegal netmask value", hex(arg))
    return 32 - int(round(math.log(0xFFFFFFFF - arg, 2)))

def to_CIDR_notation(bytes_network, bytes_netmask):
    network = scapy.utils.ltoa(bytes_network)
    netmask = long2net(bytes_netmask)
    net = f"{network}/{netmask}"
    if netmask < 16:
        return None
    return net

def ip_route_exists(prefix):
    return subprocess.run(
        ["ip", "route", "show", "match", prefix],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode == 0

def ip_route_add(prefix, gw, dev):
    return subprocess.run(
        ["ip", "route", "replace", prefix, "via", gw, "dev", dev]
    ).returncode == 0

def ip_route_del(prefix):
    # Only delete if it exists to avoid SIOCDELRT noise
    if ip_route_exists(prefix):
        return subprocess.run(["ip", "route", "del", prefix]).returncode == 0
    return True

def ping_once(ip):
    # -c 1 (one probe), -W 1 (1s timeout) on Linux
    return subprocess.run(
        ["ping", "-c", "1", "-W", "1", ip],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode == 0

def scan_and_try(interface, net, out_dev):
    try:
        ans, _ = scapy.layers.l2.arping(net, iface=interface, timeout=5, verbose=False)
        for _, r in ans.res:
            gw = r.sprintf("%ARP.psrc%")
            # Try to install route via discovered gateway on the *selected* device
            if ip_route_add(SUBNET_CIDR, gw, out_dev):
                if ping_once(CN_IP):
                    logger.info(f"IP address of host running CN is {gw}")
                    return True
                else:
                    ip_route_del(SUBNET_CIDR)
    except socket.error as e:
        if e.errno == errno.EPERM:
            logger.error(f"{e.strerror}. Did you run as root?")
        else:
            raise
    return False

def main(interface_to_scan=None):
    if os.geteuid() != 0:
        print('You need to be root to run this script', file=sys.stderr)
        sys.exit(1)

    # If the route already works, exit early
    if ping_once(CN_IP):
        logger.info("Route to CN host exists!")
        return

    # Clean stale route silently
    ip_route_del(SUBNET_CIDR)

    for network, netmask, _, iface, address, _ in scapy.config.conf.route.routes:
        if interface_to_scan and iface != interface_to_scan:
            continue
        if network == 0 or iface == 'lo' or address in ('127.0.0.1', '0.0.0.0'):
            continue
        if netmask <= 0 or netmask == 0xFFFFFFFF:
            continue
        if not interface_to_scan and (iface.startswith('docker') or iface.startswith('br-') or iface.startswith('tun')):
            logger.warning(f"Skipping interface '{iface}'")
            continue

        net = to_CIDR_notation(network, netmask)
        if not net:
            continue

        found = ping_once(CN_IP)
        while not found:
            if scan_and_try(iface, net, interface_to_scan or iface):
                logger.info("Route to core network added!")
                return
            else:
                logger.info("Route to core network not found. Retrying...")
                # Optional: add a small sleep to avoid busy looping
                # time.sleep(1)

def usage():
    print(f"Usage: {sys.argv[0]} [-i <interface>]")

if __name__ == "__main__":
    try:
        opts, _ = getopt.getopt(sys.argv[1:], 'hi:', ['help', 'interface='])
    except getopt.GetoptError as err:
        print(str(err)); usage(); sys.exit(2)

    interface = None
    for o, a in opts:
        if o in ('-h', '--help'):
            usage(); sys.exit()
        elif o in ('-i', '--interface'):
            interface = a

    main(interface_to_scan=interface)
