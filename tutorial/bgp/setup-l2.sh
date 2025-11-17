#!/bin/bash
set -ex

image=ovn-bgp-test:dev
net=evpn-l2-net

podman build -t $image -f Dockerfile ../../../

h1=evpn-host1
h2=evpn-host2
h3=evpn-host3

function cleanup() {
    set +e
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

# configure BGP IPs
podman exec $h1 ip a a dev eth0 20.0.0.1/8
podman exec $h2 ip a a dev eth0 20.0.0.2/8
podman exec $h3 ip a a dev eth0 20.0.0.3/8

podman exec $h1 ping -c 1 20.0.0.2

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

# Create VTEPs

echo Creating VTEPs...

# - host1
for vni in 10 20; do
    # Add VNI bridge.
    podman exec $h1 ip link add br-$vni type bridge
    podman exec $h1 ip link set dev br-$vni up

    # Add VXLAN VTEP for the VNI.
    podman exec $h1 ip link add vxlan-$vni type vxlan id $vni dstport 4789 local 20.0.0.1 nolearning
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
