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
podman exec $h1 ping -c 1 20.0.0.3

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

# - host1 - SVD (Single VXLAN Device)
# Based on https://docs.frrouting.org/en/latest/evpn.html
# In OVN terms we would need to configure two logical switches:
# LS-10:
# - other_config:dynamic-routing-bridge-ifname=vlan-10
# - other_config:dynamic-routing-vxlan-ifname=vxlan-evpn
# - other_config:dynamic-routing-advertise-ifname=lo-10
#
# AND
#
# LS-20:
# - other_config:dynamic-routing-bridge-ifname=vlan-20
# - other_config:dynamic-routing-vxlan-ifname=vxlan-evpn
# - other_config:dynamic-routing-advertise-ifname=lo-20
#
br_h1=br-evpn
vxlan_h1=vxlan-evpn
podman exec $h1 ip link add $br_h1 type bridge vlan_filtering 1 vlan_default_pvid 0
# the key setting for SVD configuration is "external"
# "vnifilter" isn't strictly necessary but is correct
podman exec $h1 ip link add $vxlan_h1 type vxlan dstport 4789 local 20.0.0.1 nolearning external vnifilter
podman exec $h1 ip link set $br_h1 addrgenmode none
podman exec $h1 ip link set $vxlan_h1 addrgenmode none master $br_h1

podman exec $h1 ip link set $br_h1 address 00:22:33:44:55:66
podman exec $h1 ip link set $vxlan_h1 address 00:22:33:44:55:66
podman exec $h1 ip link set $br_h1 up
podman exec $h1 ip link set $vxlan_h1 up

# and the last key setting for SVD here is "vlan_tunnel"
podman exec $h1 bridge link set dev $vxlan_h1 vlan_tunnel on neigh_suppress on learning off

for vni in 10 20; do
    podman exec $h1 ip link add vrf-$vni type vrf table $vni
    podman exec $h1 ip link set vrf-$vni up


    mac=aa:bb:cc:00:01:$vni
    podman exec $h1 bridge vlan add dev $br_h1 vid $vni self
    podman exec $h1 bridge vlan add dev $vxlan_h1 vid $vni
    podman exec $h1 bridge vni add dev $vxlan_h1 vni $vni
    podman exec $h1 bridge vlan add dev $vxlan_h1 vid $vni tunnel_info id $vni
    podman exec $h1 ip link add vlan-$vni link $br_h1 type vlan id $vni
    podman exec $h1 ip link set vlan-$vni master vrf-$vni
    podman exec $h1 ip link set vlan-$vni addr $mac
    podman exec $h1 ip link set vlan-$vni up

    # Add a dummy loopback to the VNI bridge to be used for advertising local
    # MACs and ARPs.
    podman exec $h1 ip link add name lo-$vni type dummy
    podman exec $h1 ip link set lo-$vni master $br_h1
    podman exec $h1 bridge vlan add dev lo-$vni vid $vni pvid untagged
    podman exec $h1 ip link set lo-$vni up
done

# - host2 - MVD (Multiple VXLAN Device) - traditional deployment
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

# - host3 - MVD (Multiple VXLAN Device) - traditional deployment
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

podman exec $h1 vtysh -c 'show bgp neighbors'

echo Sleeping for a bit....
sleep 10

echo Creating workloads...

# Add a "workload" on host1, simulate OVN adding it.
for vni in 10 20; do
    # Advertise MAC (type-2 EVPN route).
    podman exec $h1 bridge fdb add 00:01:84:84:84:$vni dev lo-$vni master static

    # Advertise MAC + IP (type-2 EVPN route).
    # IMPORTANT, use the vlan-$vni interface for learning/advertising ARPs,
    # similar to br-$vni for the MVD case.
    podman exec $h1 ip neigh add dev vlan-$vni 42.42.1.$vni lladdr 00:01:42:42:00:$vni nud noarp
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
    # IMPORTANT: for SVD monitor the vlan-$vni interface.
    echo "FDB entries in VRF $vni on $h:"
    podman exec $h bridge fdb show | grep vlan-$vni | grep 02:84:84 || true
    podman exec $h bridge fdb show | grep vlan-$vni | grep 03:84:84 || true
    echo
    echo "IP neigh entries in VRF $vni on $h:"
    podman exec $h ip neigh | grep vlan-$vni | grep 02:42:42 || true
    podman exec $h ip neigh | grep vlan-$vni | grep 03:42:42 || true
    echo
done

h=$h2
for vni in 10 20; do
    # IMPORTANT: for MVD monitor the br-$vni interface.
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
    # IMPORTANT: for MVD monitor the br-$vni interface.
    echo "FDB entries in VRF $vni on $h:"
    podman exec $h bridge fdb show | grep br-$vni | grep 01:84:84 || true
    podman exec $h bridge fdb show | grep br-$vni | grep 02:84:84 || true
    echo
    echo "IP neigh entries in VRF $vni on $h:"
    podman exec $h ip neigh | grep br-$vni | grep 01:42:42 || true
    podman exec $h ip neigh | grep br-$vni | grep 02:42:42 || true
    echo
done

for host in $h1 $h2 $h3; do
    echo Checking FRR EVPN macs on $host...
    podman exec $host vtysh -c 'show evpn mac vni all' > /tmp/evpn-macs-$host.txt

    # This should display something along the lines of (local vs remote depending on host we run on):
    #
    # VNI 10 #MACs (local and remote) 6
    #
    # Flags: N=sync-neighs, I=local-inactive, P=peer-active, X=peer-proxy
    # MAC               Type   Flags Intf/Remote ES/VTEP            VLAN  Seq #'s
    # 00:02:42:42:00:10 remote       20.0.0.2                             0/0
    # 00:02:84:84:84:10 remote       20.0.0.2                             0/0
    # 00:01:84:84:84:10 local        lo-10                          10    0/0
    # 00:01:42:42:00:10 local        lo-10                          10    0/0
    # 00:03:42:42:00:10 remote       20.0.0.3                             0/0
    # 00:03:84:84:84:10 remote       20.0.0.3                             0/0
    #
    # VNI 20 #MACs (local and remote) 6
    #
    # Flags: N=sync-neighs, I=local-inactive, P=peer-active, X=peer-proxy
    # MAC               Type   Flags Intf/Remote ES/VTEP            VLAN  Seq #'s
    # 00:02:42:42:00:20 remote       20.0.0.2                             0/0
    # 00:01:84:84:84:20 local        lo-20                          20    0/0
    # 00:02:84:84:84:20 remote       20.0.0.2                             0/0
    # 00:03:84:84:84:20 remote       20.0.0.3                             0/0
    # 00:03:42:42:00:20 remote       20.0.0.3                             0/0
    # 00:01:42:42:00:20 local        lo-20                          20    0/0

    echo Checking FRR EVPN routes on $host...
    podman exec $host vtysh -c 'show bgp l2vpn evpn route' > /tmp/evpn-routes-$host.txt
    # This should display something along the lines of (local vs remote depending on host we run on):
    #
    # BGP table version is 5, local router ID is 20.0.0.1
    # Status codes: s suppressed, d damped, h history, * valid, > best, i - internal
    # Origin codes: i - IGP, e - EGP, ? - incomplete
    # EVPN type-1 prefix: [1]:[EthTag]:[ESI]:[IPlen]:[VTEP-IP]:[Frag-id]
    # EVPN type-2 prefix: [2]:[EthTag]:[MAClen]:[MAC]:[IPlen]:[IP]
    # EVPN type-3 prefix: [3]:[EthTag]:[IPlen]:[OrigIP]
    # EVPN type-4 prefix: [4]:[ESI]:[IPlen]:[OrigIP]
    # EVPN type-5 prefix: [5]:[EthTag]:[IPlen]:[IP]
    #
    #    Network          Next Hop            Metric LocPrf Weight Path
    #                     Extended Community
    # Route Distinguisher: 20.0.0.1:2
    #  *>  [2]:[0]:[48]:[00:01:42:42:00:10]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:10
    #  *>  [2]:[0]:[48]:[00:01:42:42:00:10]:[32]:[42.42.1.10]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:10
    #  *>  [2]:[0]:[48]:[00:01:84:84:84:10]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:10
    #  *>  [3]:[0]:[32]:[20.0.0.1]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:10
    # Route Distinguisher: 20.0.0.1:3
    #  *>  [2]:[0]:[48]:[00:01:42:42:00:20]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:20
    #  *>  [2]:[0]:[48]:[00:01:42:42:00:20]:[32]:[42.42.1.20]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:20
    #  *>  [2]:[0]:[48]:[00:01:84:84:84:20]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:20
    #  *>  [3]:[0]:[32]:[20.0.0.1]
    #                     20.0.0.1                           32768 i
    #                     ET:8 RT:65000:20
    # Route Distinguisher: 20.0.0.2:2
    #  *>i [2]:[0]:[48]:[00:02:42:42:00:10]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:10 ET:8
    #  *>i [2]:[0]:[48]:[00:02:42:42:00:10]:[32]:[42.42.2.10]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:10 ET:8
    #  *>i [2]:[0]:[48]:[00:02:84:84:84:10]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:10 ET:8
    #  *>i [3]:[0]:[32]:[20.0.0.2]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:10 ET:8
    # Route Distinguisher: 20.0.0.2:3
    #  *>i [2]:[0]:[48]:[00:02:42:42:00:20]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:20 ET:8
    #  *>i [2]:[0]:[48]:[00:02:42:42:00:20]:[32]:[42.42.2.20]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:20 ET:8
    #  *>i [2]:[0]:[48]:[00:02:84:84:84:20]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:20 ET:8
    #  *>i [3]:[0]:[32]:[20.0.0.2]
    #                     20.0.0.2                      100      0 i
    #                     RT:65000:20 ET:8
    # Route Distinguisher: 20.0.0.3:2
    #  *>i [2]:[0]:[48]:[00:03:42:42:00:10]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:10 ET:8
    #  *>i [2]:[0]:[48]:[00:03:42:42:00:10]:[32]:[42.42.3.10]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:10 ET:8
    #  *>i [2]:[0]:[48]:[00:03:84:84:84:10]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:10 ET:8
    #  *>i [3]:[0]:[32]:[20.0.0.3]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:10 ET:8
    # Route Distinguisher: 20.0.0.3:3
    #  *>i [2]:[0]:[48]:[00:03:42:42:00:20]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:20 ET:8
    #  *>i [2]:[0]:[48]:[00:03:42:42:00:20]:[32]:[42.42.3.20]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:20 ET:8
    #  *>i [2]:[0]:[48]:[00:03:84:84:84:20]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:20 ET:8
    #  *>i [3]:[0]:[32]:[20.0.0.3]
    #                     20.0.0.3                      100      0 i
    #                     RT:65000:20 ET:8
    #
    # Displayed 24 prefixes (24 paths)
done

sleep infinity
