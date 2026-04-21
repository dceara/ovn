#!/bin/bash

# FDP-3476 Reproducer: L3 EVPN with indirect next hops.
#
# Bug: northd called find_route_outport() with force_out_port=false when
# processing Learned_Route entries.  This rejects any route whose nexthop
# is not in a configured LRP subnet.  In L3 EVPN (IP-VRF / type-5) the
# nexthop of a learned route is the remote peer's underlay IP (e.g.,
# 20.0.0.2), which is not in the LRP's configured subnet -- an
# "indirect next hop".  The route was silently dropped and never reached
# the pipeline.
#
# Topology (2 containers on a shared L2 network):
#
#  h1 (OVN host)                          h2 (external host)
#  +---------------------------------+    +-----------------------------+
#  | OVN northd + controller + FRR   |    | FRR only                   |
#  |                                 |    |                             |
#  |  lr (dynamic-routing, vrf=10)   |    |  vrf10                     |
#  |    lrp-ext: 10.255.255.1/24     |    |    br-10: 42.42.0.1/16     |
#  |    lrp-int: 30.0.0.1/24        |    |    vxlan-10 (VNI 10)       |
#  |                                 |    |                             |
#  |  ls-ext (localnet, EVPN VNI=10) |    |  ext-host netns:           |
#  |  ls-int                         |    |    42.42.0.100/16           |
#  |    workload-int: 30.0.0.42/24   |    |    gw 42.42.0.1            |
#  |                                 |    |                             |
#  |  vrf10 + br-10 + vxlan-10       |    +-----------------------------+
#  +---------------------------------+
#
# What happens:
#   1. OVN on h1 advertises 30.0.0.0/24 (connected on lrp-int) via EVPN
#      type-5 into vrf10.
#   2. h2 learns the route: 30.0.0.0/24 via <h1-br-10-mac> through VXLAN.
#   3. h2 has connected route 42.42.0.0/16 in vrf10 (SVI on br-10).
#      FRR on h2 advertises it as EVPN type-5.
#   4. h1's ovn-controller learns it: Learned_Route(42.42.0.0/16, nexthop
#      20.0.0.2) on lrp-ext.
#   5. BUG: nexthop 20.0.0.2 is NOT in lrp-ext's subnet 10.255.255.0/24.
#      Without the fix northd drops the route; the return path from
#      workload-int (30.0.0.42) to ext-host (42.42.0.100) is broken.
#   6. Ping from ext-host to workload-int fails (request arrives via OVN's
#      advertised route, but the reply has no route back).
#
# With the fix (force_out_port=true) northd accepts the learned route,
# generates a flow, and the ping succeeds.

set -ex

image=ovn-bgp-test:dev
net=evpn-l3-fdp3476

podman build -t $image -f Dockerfile ../../../

h1=fdp3476-host1
h2=fdp3476-host2

vni=10
vrf=vrf$vni

function cleanup() {
    set +e
    podman exec $h1 systemctl stop ovn-controller
    podman exec $h1 systemctl stop ovn-northd
    podman exec $h1 systemctl stop openvswitch

    for host in $h1 $h2; do
        podman stop $host
        podman rm -f $host
    done
    podman network rm $net
}

trap "cleanup" EXIT

###############################################################################
# Create network and containers
###############################################################################
podman network create --internal --ipam-driver=none $net

podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net --mac-address=00:00:00:00:00:01 --name $h1 $image
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net --mac-address=00:00:00:00:00:02 --name $h2 $image

# BGP peering IPs on the underlay.
podman exec $h2 ip a a dev eth0 20.0.0.2/8

###############################################################################
# Start OVN on h1
###############################################################################
echo "=== Starting OVN on h1 ==="
podman exec $h1 systemctl start openvswitch
podman exec $h1 ovs-vsctl set open . external_ids:system-id=$h1
podman exec $h1 systemctl start ovn-northd
podman exec $h1 systemctl start ovn-controller

podman exec $h1 ovs-vsctl set open .         \
  external-ids:ovn-remote=tcp:127.0.0.1:6642 \
  external-ids:ovn-encap-type=geneve         \
  external-ids:ovn-encap-ip=127.0.0.1
podman exec $h1 ovn-sbctl set-connection ptcp:6642

###############################################################################
# Configure OVN topology on h1
###############################################################################
echo "=== Configuring OVN topology ==="

# Logical router with dynamic routing enabled, VRF ID matching the VNI.
podman exec $h1 ovn-nbctl lr-add lr \
  -- set logical_router lr          \
        options:chassis=$h1         \
        options:dynamic-routing=true \
        options:dynamic-routing-vrf-id=$vni

# External switch (localnet + EVPN).
podman exec $h1 ovn-nbctl ls-add ls-ext

# NOTE: lrp-ext has an IP in 10.255.255.0/24 which is deliberately NOT in
# the 20.0.0.0/8 underlay subnet.  This means the EVPN type-5 learned route
# for 42.42.0.0/16 with nexthop 20.0.0.2 is "indirect" -- the nexthop is
# not in any LRP subnet, which is the condition that triggers the bug.
#
# Ideally lrp-ext would be fully unnumbered (MAC-only, just LLA) to make
# the indirect nexthop condition even more obvious, but an unnumbered LRP
# triggers a separate known bug (https://redhat.atlassian.net/browse/FDP-3545)
# where northd generates "reg5 = (null)" in the lflow.  Use an unrelated
# IP as a workaround.
podman exec $h1 ovn-nbctl lrp-add lr lrp-ext 00:00:00:00:01:01 10.255.255.1/24 \
  -- lrp-set-options lrp-ext                                                    \
        dynamic-routing-maintain-vrf=false                                      \
        dynamic-routing-redistribute=connected

podman exec $h1 ovn-nbctl lsp-add ls-ext ls-ext-lr \
  -- lsp-set-type ls-ext-lr router                 \
  -- lsp-set-options ls-ext-lr router-port=lrp-ext  \
  -- lsp-set-addresses ls-ext-lr router

podman exec $h1 ovn-nbctl lsp-add ls-ext ls-ext-ln \
  -- lsp-set-type ls-ext-ln localnet               \
  -- lsp-set-addresses ls-ext-ln unknown            \
  -- lsp-set-options ls-ext-ln network_name=phys

# EVPN configuration on the external switch.
podman exec $h1 ovn-nbctl set logical-switch ls-ext \
    other_config:dynamic-routing-vni=$vni           \
    other_config:dynamic-routing-redistribute=fdb,ip \
    other_config:dynamic-routing-bridge-ifname=br-$vni \
    other_config:dynamic-routing-vxlan-ifname=vxlan-$vni \
    other_config:dynamic-routing-advertise-ifname=lo-$vni

# Localnet bridge: bridge eth0 into br-ex.
podman exec $h1 ovs-vsctl add-br br-ex
podman exec $h1 ovs-vsctl set open . external-ids:ovn-bridge-mappings=phys:br-ex
podman exec $h1 ovs-vsctl add-port br-ex eth0
podman exec $h1 ip addr add 20.0.0.1/8 dev br-ex
podman exec $h1 ip link set up br-ex
podman exec $h1 ovs-vsctl set open . external_ids:ovn-evpn-local-ip="20.0.0.1"
podman exec $h1 ovs-vsctl set open . external_ids:ovn-evpn-vxlan-ports=4789

# Internal switch + workload.
podman exec $h1 ovn-nbctl ls-add ls-int
podman exec $h1 ovn-nbctl lrp-add lr lrp-int 00:00:00:00:01:02 30.0.0.1/24 \
  -- lrp-set-options lrp-int dynamic-routing-redistribute=connected        \
        dynamic-routing-no-learning=true

podman exec $h1 ovn-nbctl lsp-add ls-int ls-int-lr \
  -- lsp-set-type ls-int-lr router                 \
  -- lsp-set-options ls-int-lr router-port=lrp-int  \
  -- lsp-set-addresses ls-int-lr router

# Create the OVN workload: a netns at 30.0.0.42 behind the logical router.
podman exec $h1 ovs-vsctl add-port br-int workload-int \
  -- set interface workload-int type=internal           \
  -- set interface workload-int external_ids:iface-id=workload-int
podman exec $h1 ip netns add workload-int
podman exec $h1 ip link set dev workload-int netns workload-int
podman exec $h1 ip netns exec workload-int ip link set workload-int address 00:00:00:00:42:42
podman exec $h1 ip netns exec workload-int ip addr add dev workload-int 30.0.0.42/24
podman exec $h1 ip netns exec workload-int ip link set dev workload-int up
podman exec $h1 ip netns exec workload-int ip route add default via 30.0.0.1
podman exec $h1 ovn-nbctl lsp-add ls-int workload-int \
  -- lsp-set-addresses workload-int "00:00:00:00:42:42 30.0.0.42"

echo "Sleeping for a bit to let OVN settle..."
sleep 5

###############################################################################
# Verify underlay connectivity
###############################################################################
echo "=== Checking underlay connectivity ==="
podman exec $h2 ping -c 1 20.0.0.1

###############################################################################
# Start FRR on both hosts
###############################################################################
echo "=== Starting FRR ==="
for host in $h1 $h2; do
    podman exec $host sed -i 's/bgpd=no/bgpd=yes/g' /etc/frr/daemons
    podman exec $host systemctl start frr
done

# Configure BGP on h1 (OVN host).
echo "configure
  vrf $vrf
   vni $vni
  exit-vrf
  !
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000
    bgp log-neighbor-changes
    neighbor 20.0.0.2 remote-as 65000
    !
    address-family l2vpn evpn
     neighbor 20.0.0.2 activate
     advertise-all-vni
     advertise-svi-ip
    exit-address-family
  exit
  !
  router bgp 65000 vrf $vrf
   !
   address-family ipv4 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family l2vpn evpn
    advertise ipv4 unicast
   exit-address-family
  exit
  !
  do copy running-config startup-config" | podman exec -i $h1 vtysh

# Configure BGP on h2 (external host).
echo "configure
  vrf $vrf
   vni $vni
  exit-vrf
  !
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000
    bgp log-neighbor-changes
    neighbor 20.0.0.1 remote-as 65000
    !
    address-family l2vpn evpn
     neighbor 20.0.0.1 activate
     advertise-all-vni
     advertise-svi-ip
    exit-address-family
  exit
  !
  router bgp 65000 vrf $vrf
   !
   address-family ipv4 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family l2vpn evpn
    advertise ipv4 unicast
   exit-address-family
  exit
  !
  do copy running-config startup-config" | podman exec -i $h2 vtysh

# Restart FRR to pick up the VRF/VNI association.
for host in $h1 $h2; do
    podman exec $host systemctl restart frr
done

echo "Sleeping to let FRR start..."
sleep 10

###############################################################################
# Create EVPN VTEPs (VRF + bridge + VXLAN)
###############################################################################
echo "=== Configuring VTEPs ==="

# h1: ovn-controller creates vxlan_sys_4789 for EVPN; use it as the parent
# device so FRR reads the correct VXLAN dst port.  Use a different dstport
# for the FRR-facing VXLAN device so it doesn't conflict.
podman exec $h1 ip link add $vrf type vrf table $vni
podman exec $h1 ip link set $vrf up

podman exec $h1 ip link add br-$vni type bridge
podman exec $h1 ip link set br-$vni master $vrf addrgenmode none
# Use the same MAC as lrp-ext so remote peers resolve the nexthop correctly.
podman exec $h1 ip link set br-$vni address 00:00:00:00:01:01

dstport=$((60000 + $vni))
podman exec $h1 ip link add vxlan-$vni type vxlan dev vxlan_sys_4789 \
    id $vni dstport $dstport local 20.0.0.1 nolearning
podman exec $h1 ip link set vxlan-$vni master br-$vni

podman exec $h1 ip link set vxlan-$vni up
podman exec $h1 ip link set br-$vni up

# Dummy loopback for advertising local MACs.
podman exec $h1 ip link add name lo-$vni type dummy
podman exec $h1 ip link set lo-$vni master br-$vni
podman exec $h1 ip link set lo-$vni up

# h2: standard VXLAN VTEP (no OVN, no vxlan_sys_4789 parent).
podman exec $h2 ip link add $vrf type vrf table $vni
podman exec $h2 ip link set $vrf up

podman exec $h2 ip link add br-$vni type bridge
podman exec $h2 ip link set br-$vni type bridge vlan_filtering 0
podman exec $h2 ip link set br-$vni master $vrf addrgenmode none
podman exec $h2 ip link set br-$vni address 00:02:42:42:42:$vni

podman exec $h2 ip link add vxlan-$vni type vxlan id $vni dstport 4789 \
    local 20.0.0.2 nolearning
podman exec $h2 ip link set vxlan-$vni master br-$vni addrgenmode none
podman exec $h2 ip link set vxlan-$vni type bridge_slave \
    neigh_suppress on learning off

podman exec $h2 ip link set vxlan-$vni up
podman exec $h2 ip link set br-$vni up

# Dummy loopback for advertising local MACs.
podman exec $h2 ip link add name lo-$vni type dummy
podman exec $h2 ip link set lo-$vni master br-$vni
podman exec $h2 ip link set lo-$vni up

###############################################################################
# Configure the external host on h2
###############################################################################
echo "=== Configuring external host on h2 ==="

# Add an SVI on br-10 in the VRF -- this creates the connected route
# 42.42.0.0/16 that FRR will advertise as EVPN type-5.
podman exec $h2 ip addr add 42.42.0.1/16 dev br-$vni

# Create the external host as a netns connected to br-10 via a veth pair.
podman exec $h2 ip netns add ext-host
podman exec $h2 ip link add ext-host-pair type veth peer ext-host
podman exec $h2 ip link set ext-host-pair up
podman exec $h2 ip link set ext-host-pair master br-$vni
podman exec $h2 ip link set netns ext-host dev ext-host
podman exec $h2 ip netns exec ext-host ip link set ext-host address 00:02:42:42:00:42
podman exec $h2 ip netns exec ext-host ip addr add 42.42.0.100/16 dev ext-host
podman exec $h2 ip netns exec ext-host ip link set ext-host up
podman exec $h2 ip netns exec ext-host ip route add default via 42.42.0.1

# Advertise the external host's MAC via EVPN type-2 so h1 can resolve it.
podman exec $h2 bridge fdb append 00:02:42:42:00:42 dev lo-$vni master static
podman exec $h2 ip neigh replace dev br-$vni 42.42.0.100 \
    lladdr 00:02:42:42:00:42 nud noarp

###############################################################################
# OVN workload route advertisement
###############################################################################
# OVN with dynamic-routing-redistribute=connected on lrp-int will install
# a blackhole route for 30.0.0.0/24 in vrf10.  FRR picks it up and advertises
# it as EVPN type-5.
#
# However, ovn-controller may take a moment to install the route.  We also
# need to advertise the lrp-ext MAC on the VNI bridge so that h2 can resolve
# the nexthop MAC for return traffic to 30.0.0.0/24.
podman exec $h1 ip neigh add dev br-$vni 20.0.0.1 \
    lladdr 00:00:00:00:01:01 nud noarp || true
podman exec $h1 bridge fdb add 00:00:00:00:01:01 dev lo-$vni master static || true

echo "=== Waiting for EVPN convergence ==="
# Wait until ovn-controller learns the remote VTEP (20.0.0.2) and the
# ARP entry for it appears on br-10.
max_wait=60
for i in $(seq 1 $max_wait); do
    if podman exec $h1 ovn-appctl evpn/remote-vtep-list 2>/dev/null \
            | grep -q "20.0.0.2" && \
       podman exec $h1 ip neigh show dev br-$vni 20.0.0.2 2>/dev/null \
            | grep -q "lladdr"; then
        echo "Remote VTEP and ARP for 20.0.0.2 learned after ${i}s."
        break
    fi

    if [ "$i" -eq "$max_wait" ]; then
        echo "WARNING: timed out waiting for EVPN convergence after ${max_wait}s."
        podman exec $h1 ovn-appctl evpn/remote-vtep-list || true
        podman exec $h1 ip neigh show dev br-$vni || true
    fi

    sleep 1
done

###############################################################################
# Diagnostics
###############################################################################
echo "=== BGP neighbor status ==="
podman exec $h1 vtysh -c "show bgp neighbors" | grep -A2 "BGP neighbor is" || true
podman exec $h2 vtysh -c "show bgp neighbors" | grep -A2 "BGP neighbor is" || true

echo "=== VRF routes on h1 ==="
podman exec $h1 ip route show table $vni || true

echo "=== VRF routes on h2 ==="
podman exec $h2 ip route show table $vni || true

echo "=== EVPN type-5 routes on h1 ==="
podman exec $h1 vtysh -c "show bgp l2vpn evpn route type prefix" || true

echo "=== EVPN type-5 routes on h2 ==="
podman exec $h2 vtysh -c "show bgp l2vpn evpn route type prefix" || true

echo "=== Learned_Route table in OVN SB ==="
podman exec $h1 ovn-sbctl list Learned_Route || true

echo "=== OVN lflows for lr_in_ip_routing ==="
podman exec $h1 ovn-sbctl lflow-list lr | grep lr_in_ip_routing || true

###############################################################################
# Verify the bug / fix
###############################################################################
echo ""
echo "======================================================================="
echo "  FDP-3476 verification"
echo "======================================================================="
echo ""
echo "The learned route for 42.42.0.0/16 should appear in the Learned_Route"
echo "table above, and a corresponding lflow should exist in lr_in_ip_routing."
echo ""
echo "Without the fix: the lflow is MISSING (northd drops the route because"
echo "nexthop 20.0.0.2 is not in lrp-ext's subnet 10.255.255.0/24)."
echo ""
echo "With the fix: the lflow is PRESENT and the ping below succeeds."
echo ""

echo "=== Ping from ext-host (42.42.0.100) to OVN workload (30.0.0.42) ==="
if podman exec $h2 ip netns exec ext-host ping -c 3 -W 5 30.0.0.42; then
    echo ""
    echo "SUCCESS: Ping from external host to OVN workload works!"
    echo "The fix for FDP-3476 is effective."
else
    echo ""
    echo "FAILURE: Ping from external host to OVN workload failed."
    echo "This is the expected result WITHOUT the FDP-3476 fix."
    echo "(northd dropped the learned route due to indirect next hop)"
fi

echo ""
echo "=== Ping from OVN workload (30.0.0.42) to ext-host (42.42.0.100) ==="
if podman exec $h1 ip netns exec workload-int ping -c 3 -W 5 42.42.0.100; then
    echo ""
    echo "SUCCESS: Ping from OVN workload to external host works!"
else
    echo ""
    echo "FAILURE: Ping from OVN workload to external host failed."
    echo "This confirms the bug: OVN has no route for 42.42.0.0/16."
fi

sleep infinity
