#!/bin/bash
set -ex

# HOST 1 is the one running OVN.
# HOST 2 and 3 run FRR directly in the container.

image=ovn-bgp-test:dev
net=evpn-l2-net

podman build -t $image -f Dockerfile ../../../

h1=evpn-host1
h2=evpn-host2
h3=evpn-host3
eth=eth0
bgp_eth=lsp-bgp

function cleanup() {
    set +e
    podman exec $h1 systemctl stop ovn-controller
    podman exec $h1 systemctl stop ovn-northd
    podman exec $h1 systemctl stop openvswitch

    for host in $h1 $h2 $h3; do
        podman stop $host
        podman rm -f $host
    done
    podman network rm $net
}

trap "cleanup" EXIT

# private l2 network
podman network create --internal --ipam-driver=none $net

# hosts
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net --mac-address=00:00:00:00:00:01 --name $h1 $image
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net --mac-address=00:00:00:00:00:02 --name $h2 $image
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net --mac-address=00:00:00:00:00:03 --name $h3 $image

# Configure BGP IPs
podman exec $h2 ip a a dev $eth 20.0.0.2/8
podman exec $h3 ip a a dev $eth 20.0.0.3/8

echo Provisioning OVN...
podman exec $h1 systemctl start openvswitch
podman exec $h1 ovs-vsctl set open . external_ids:system-id=$h1
podman exec $h1 systemctl start ovn-northd
podman exec $h1 systemctl start ovn-controller

# Configure OVN:
podman exec $h1 ovs-vsctl set open .         \
  external-ids:ovn-remote=tcp:127.0.0.1:6642 \
  external-ids:ovn-encap-type=geneve         \
  external-ids:ovn-encap-ip=127.0.0.1
podman exec $h1 ovn-sbctl set-connection ptcp:6642

# Configure a GW router with external and internal switches connected.
podman exec $h1 ovn-nbctl lr-add lr \
  -- set logical_router lr options:chassis=$h1

podman exec $h1 ovn-nbctl ls-add ls-ext
podman exec $h1 ovn-nbctl lrp-add lr lrp-ext 00:00:00:00:01:01 20.0.0.1/8
podman exec $h1 ovn-nbctl lsp-add ls-ext ls-ext-lr \
  -- lsp-set-type ls-ext-lr router \
  -- lsp-set-options ls-ext-lr router-port=lrp-ext \
  -- lsp-set-addresses ls-ext-lr router
podman exec $h1 ovn-nbctl lsp-add ls-ext ls-ext-ln \
  -- lsp-set-type ls-ext-ln localnet \
  -- lsp-set-addresses ls-ext-ln unknown \
  -- lsp-set-options ls-ext-ln network_name=phys

# Configure the localnet bridge/interface.
podman exec $h1 ovs-vsctl add-br br-ex
podman exec $h1 ovs-vsctl set open . external-ids:ovn-bridge-mappings=phys:br-ex
podman exec $h1 ovs-vsctl add-port br-ex $eth
podman exec $h1 ip addr add 20.0.0.1/8 dev br-ex
podman exec $h1 ip link set up br-ex

# Add internal switch.
podman exec $h1 ovn-nbctl ls-add ls-int
podman exec $h1 ovn-nbctl lrp-add lr lrp-int 00:00:00:00:01:02 30.0.0.1/8
podman exec $h1 ovn-nbctl lsp-add ls-int ls-int-lr \
  -- lsp-set-type ls-int-lr router \
  -- lsp-set-options ls-int-lr router-port=lrp-int \
  -- lsp-set-addresses ls-int-lr router


# Configure an OVN workload attached to ls-ext.
podman exec $h1 ovs-vsctl add-port br-int workload \
  -- set interface workload type=internal \
  -- set interface workload external_ids:iface-id=workload
podman exec $h1 ip netns add workload
podman exec $h1 ip link set dev workload netns workload
podman exec $h1 ip netns exec workload ip link set workload address 00:00:00:00:01:42
podman exec $h1 ip netns exec workload ip a a dev workload 42.42.1.15/16
podman exec $h1 ip netns exec workload ip link set dev workload up
podman exec $h1 ovn-nbctl lsp-add ls-ext workload \
  -- lsp-set-addresses workload "00:00:00:00:01:42 42.42.1.15/16"

echo Sleeping for a bit...
sleep 5

echo Checking connectivity...
podman exec $h2 ping -c 1 20.0.0.1
podman exec $h3 ping -c 1 20.0.0.1
podman exec $h3 ping -c 1 20.0.0.2

# start frr
for host in $h1 $h2 $h3; do
    podman exec $host sed -i 's/bgpd=no/bgpd=yes/g' /etc/frr/daemons
    podman exec $host systemctl start frr
done

# configure BGP on host 1
echo "configure
  router bgp 65000
    bgp router-id 20.0.0.1
    bgp cluster-id 20.0.0.1
    bgp log-neighbor-changes
    no bgp default ipv4-unicast
    neighbor 20.0.0.2 remote-as 65000
    neighbor 20.0.0.3 remote-as 65000
    !
    address-family l2vpn evpn
     neighbor 20.0.0.2 activate
     neighbor 20.0.0.3 activate
     advertise-all-vni
    exit-address-family
    !
  !
  do copy running-config startup-config" | podman exec -i $h1 vtysh

# configure BGP on host 2
echo "configure
  router bgp 65000
    bgp router-id 20.0.0.2
    bgp cluster-id 20.0.0.2
    bgp log-neighbor-changes
    no bgp default ipv4-unicast
    neighbor 20.0.0.1 remote-as 65000
    neighbor 20.0.0.3 remote-as 65000
    !
    address-family l2vpn evpn
     neighbor 20.0.0.1 activate
     neighbor 20.0.0.3 activate
     advertise-all-vni
    exit-address-family
    !
  !
  do copy running-config startup-config" | podman exec -i $h2 vtysh

# configure BGP on host 3
echo "configure
  router bgp 65000
    bgp router-id 20.0.0.3
    bgp cluster-id 20.0.0.3
    bgp log-neighbor-changes
    no bgp default ipv4-unicast
    neighbor 20.0.0.1 remote-as 65000
    neighbor 20.0.0.2 remote-as 65000
    !
    address-family l2vpn evpn
     neighbor 20.0.0.1 activate
     neighbor 20.0.0.2 activate
     advertise-all-vni
    exit-address-family
    !
  !
  do copy running-config startup-config" | podman exec -i $h3 vtysh

echo Creating VTEPs...

podman exec $h1 ovs-vsctl add-port br-int vxlan-ovs \
    -- set interface vxlan-ovs type=vxlan \
        options:local_ip=flow options:remote_ip=flow options:key=flow \
        options:dst_port=4789

# - host1
for vni in 10 20; do
    # Setup a VRF to interact with OVN:
    podman exec $h1 ip link add vrf$vni type vrf table $vni
    podman exec $h1 ip link set vrf$vni up

    # Add VNI bridge.
    podman exec $h1 ip link add br-$vni type bridge
    podman exec $h1 ip link set br-$vni master vrf$vni addrgenmode none
    podman exec $h1 ip link set dev br-$vni up

    # Add VXLAN VTEP for the VNI.
    # Use a dstport different than the one used by OVS.
    # This is fine because we don't actually want traffic to pass through vxlan-$vni.
    # FRR should read the dstport from the linked vxlan_sys_4789 device.
    dstport=$((60000 + $vni))
    podman exec $h1 ip link add vxlan-$vni type vxlan dev vxlan_sys_4789 id $vni dstport $dstport local 20.0.0.1 nolearning
    podman exec $h1 ip link set dev vxlan-$vni up
    podman exec $h1 ip link set vxlan-$vni master br-$vni

    # Add a dummy loopback to the VNI bridge to be used for advertising local
    # MACs.
    podman exec $h1 ip link add name lo-$vni type dummy
    podman exec $h1 ip link set lo-$vni master br-$vni
    podman exec $h1 ip link set lo-$vni up
done

# - host2
for vni in 10 20; do
    # Add VNI bridge.
    podman exec $h2 ip link add br-$vni type bridge
    podman exec $h2 ip link set dev br-$vni up

    # Add VXLAN VTEP for the VNI.
    podman exec $h2 ip link add vxlan-$vni type vxlan id $vni dstport 4789 local 20.0.0.2 nolearning
    podman exec $h2 ip link set dev vxlan-$vni up
    podman exec $h2 ip link set vxlan-$vni master br-$vni

    # Add a dummy loopback to the VNI bridge to be used for advertising local
    # MACs.
    podman exec $h2 ip link add name lo-$vni type dummy
    podman exec $h2 ip link set lo-$vni master br-$vni
    podman exec $h2 ip link set lo-$vni up
done

# - host3
for vni in 10 20; do
    # Add VNI bridge.
    podman exec $h3 ip link add br-$vni type bridge
    podman exec $h3 ip link set dev br-$vni up

    # Add VXLAN VTEP for the VNI.
    podman exec $h3 ip link add vxlan-$vni type vxlan id $vni dstport 4789 local 20.0.0.3 nolearning
    podman exec $h3 ip link set dev vxlan-$vni up
    podman exec $h3 ip link set vxlan-$vni master br-$vni

    # Add a dummy loopback to the VNI bridge to be used for advertising local
    # MACs.
    podman exec $h3 ip link add name lo-$vni type dummy
    podman exec $h3 ip link set lo-$vni master br-$vni
    podman exec $h3 ip link set lo-$vni up
done

echo Sleeping for a bit....
sleep 10

echo Creating workloads...

# Add a "workload" on host1, simulate OVN adding it.
for vni in 10 20; do
    # Advertise MAC (type-2 EVPN route).
    podman exec $h1 bridge fdb add 00:01:84:84:84:$vni dev lo-$vni master static

    # Advertise MAC + IP (type-2 EVPN route).
    podman exec $h1 ip neigh add dev br-$vni 42.42.1.$vni lladdr 00:01:42:42:00:$vni nud noarp
    podman exec $h1 bridge fdb add 00:01:42:42:00:$vni dev lo-$vni master static
done

# Add a "workload" on host2, simulate OVN adding it.
for vni in 10 20; do
    # Advertise MAC (type-2 EVPN route).
    podman exec $h2 bridge fdb add 00:02:84:84:84:$vni dev lo-$vni master static

    # Advertise MAC + IP (type-2 EVPN route).
    podman exec $h2 ip neigh add dev br-$vni 42.42.2.$vni lladdr 00:02:42:42:00:$vni nud noarp
    podman exec $h2 bridge fdb add 00:02:42:42:00:$vni dev lo-$vni master static
done

# Add a "workload" on host3, simulate OVN adding it.
for vni in 10 20; do
    # Advertise MAC (type-2 EVPN route).
    podman exec $h3 bridge fdb add 00:03:84:84:84:$vni dev lo-$vni master static

    # Advertise MAC + IP (type-2 EVPN route).
    podman exec $h3 ip neigh add dev br-$vni 42.42.3.$vni lladdr 00:03:42:42:00:$vni nud noarp
    podman exec $h3 bridge fdb add 00:03:42:42:00:$vni dev lo-$vni master static
done

echo Sleeping for a bit....
sleep 10

echo Checking routes were learnt...
h=$h1
for vni in 10 20; do
    echo "FDB entries in VRF $vni on $h:"
    podman exec $h bridge fdb show | grep br-$vni | grep 02:84:84 || true
    podman exec $h bridge fdb show | grep br-$vni | grep 03:84:84 || true
    echo
    echo "IP neigh entries in VRF $vni on $h:"
    podman exec $h ip neigh | grep br-$vni | grep 02:42:42 || true
    podman exec $h ip neigh | grep br-$vni | grep 03:42:42 || true
    echo
done

h=$h2
for vni in 10 20; do
    echo "FDB entries in VRF $vni on $h:"
    podman exec $h bridge fdb show | grep br-$vni | grep 01:84:84 || true
    podman exec $h bridge fdb show | grep br-$vni | grep 03:84:84 || true
    echo
    echo "IP neigh entries in VRF $vni on $h:"
    podman exec $h ip neigh | grep br-$vni | grep 01:42:42 || true
    podman exec $h ip neigh | grep br-$vni | grep 03:42:42 || true
    echo
done

h=$h3
for vni in 10 20; do
    echo "FDB entries in VRF $vni on $h:"
    podman exec $h bridge fdb show | grep br-$vni | grep 01:84:84 || true
    podman exec $h bridge fdb show | grep br-$vni | grep 02:84:84 || true
    echo
    echo "IP neigh entries in VRF $vni on $h:"
    podman exec $h ip neigh | grep br-$vni | grep 01:42:42 || true
    podman exec $h ip neigh | grep br-$vni | grep 02:42:42 || true
    echo
done

sleep infinity

# TODO: when L2 EVPN is supported the following ping (to host2/3) should succeed!!
podman exec $h1 ip netns exec workload ping -c1 42.42.2.10
podman exec $h1 ip netns exec workload ping -c1 42.42.3.10
