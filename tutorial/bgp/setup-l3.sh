#!/bin/bash
set -ex

image=ovn-bgp-test:dev
net=evpn-l2-net

podman build -t $image -f Dockerfile ../../../

h1=evpn-host1
h2=evpn-host2

function cleanup() {
    set +e
    for host in $h1 $h2; do
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


# configure BGP IPs
podman exec $h1 ip a a dev eth0 20.0.0.1/8
podman exec $h2 ip a a dev eth0 20.0.0.2/8
# configure VTEP loopback IPs
podman exec $h1 ip a a dev lo 100.64.0.1/32
podman exec $h2 ip a a dev lo 100.64.0.2/32

podman exec $h1 ping -c 1 20.0.0.2

# start frr
for host in $h1 $h2; do
    podman exec $host sed -i 's/bgpd=no/bgpd=yes/g' /etc/frr/daemons
    podman exec $host systemctl start frr
done

# configure BGP on host 1
echo "configure
  vrf vrf10
   vni 10
  exit-vrf
  !
  vrf vrf20
   vni 20
  exit-vrf
  !
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000
    bgp log-neighbor-changes
    neighbor 20.0.0.2 remote-as 65000
    !
    address-family ipv4 unicast
      network 100.64.0.1/32
    exit-address-family
    !
    address-family l2vpn evpn
     neighbor 20.0.0.2 activate
     advertise-all-vni
     advertise-svi-ip
    exit-address-family
  exit
  !
  router bgp 65000 vrf vrf10
   !
   address-family ipv4 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family ipv6 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family l2vpn evpn
    advertise ipv4 unicast
    advertise ipv6 unicast
   exit-address-family
  exit
  !
  router bgp 65000 vrf vrf20
   !
   address-family ipv4 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family ipv6 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family l2vpn evpn
    advertise ipv4 unicast
    advertise ipv6 unicast
   exit-address-family
  exit
  !
  do copy running-config startup-config" | podman exec -i $h1 vtysh

# configure BGP on host 2
echo "configure
  vrf vrf10
   vni 10
  exit-vrf
  !
  vrf vrf20
   vni 20
  exit-vrf
  !
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000
    bgp log-neighbor-changes
    neighbor 20.0.0.1 remote-as 65000
    !
    address-family ipv4 unicast
      network 100.64.0.2/32
    exit-address-family
    !
    address-family l2vpn evpn
     neighbor 20.0.0.1 activate
     advertise-all-vni
     advertise-svi-ip
    exit-address-family
  exit
  !
  router bgp 65000 vrf vrf10
   !
   address-family ipv4 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family ipv6 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family l2vpn evpn
    advertise ipv4 unicast
    advertise ipv6 unicast
   exit-address-family
  exit
  !
  router bgp 65000 vrf vrf20
   !
   address-family ipv4 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family ipv6 unicast
    redistribute kernel
    redistribute connected
   exit-address-family
   !
   address-family l2vpn evpn
    advertise ipv4 unicast
    advertise ipv6 unicast
   exit-address-family
  exit
  !
  do copy running-config startup-config" | podman exec -i $h2 vtysh

# Restart frr
for host in $h1 $h2; do
    podman exec $host systemctl restart frr
done

echo sleeping for a bit..
sleep 10
echo Configuring VTEPS..

# Create VTEPs
# - host1
for vni in 10 20; do
    podman exec $h1 ip link add vrf$vni type vrf table $vni
    podman exec $h1 ip link set vrf$vni up

    podman exec $h1 ip link add br-$vni type bridge
    podman exec $h1 ip link set br-$vni type bridge vlan_filtering 0
    podman exec $h1 ip link set br-$vni master vrf$vni addrgenmode none
    podman exec $h1 ip link set br-$vni address 00:01:42:42:42:$vni

    podman exec $h1 ip link add vxlan-$vni type vxlan id $vni dstport 4789 local 100.64.0.1 nolearning
    podman exec $h1 ip link set vxlan-$vni master br-$vni addrgenmode none
    podman exec $h1 ip link set vxlan-$vni type bridge_slave neigh_suppress on learning off
    podman exec $h1 ip link set vxlan-$vni address 00:01:42:42:50:$vni

    podman exec $h1 ip link set vxlan-$vni up
    podman exec $h1 ip link set br-$vni up
done

# - host2
for vni in 10 20; do
    podman exec $h2 ip link add vrf$vni type vrf table $vni
    podman exec $h2 ip link set vrf$vni up

    podman exec $h2 ip link add br-$vni type bridge
    podman exec $h2 ip link set br-$vni type bridge vlan_filtering 0
    podman exec $h2 ip link set br-$vni master vrf$vni addrgenmode none
    podman exec $h2 ip link set br-$vni address 00:02:42:42:42:$vni

    podman exec $h2 ip link add vxlan-$vni type vxlan id $vni dstport 4789 local 100.64.0.2 nolearning
    podman exec $h2 ip link set vxlan-$vni master br-$vni addrgenmode none
    podman exec $h2 ip link set vxlan-$vni type bridge_slave neigh_suppress on learning off
    podman exec $h2 ip link set vxlan-$vni address 00:02:42:42:50:$vni

    podman exec $h2 ip link set vxlan-$vni up
    podman exec $h2 ip link set br-$vni up
done

echo sleeping for a bit..
sleep 10
echo Configuring workloads..

# Workloads on host1:
for vni in 10 20; do
    # add a workload (add a "blackhole" route as if it was injected by OVN)
    podman exec $h1 ip route add table $vni blackhole 66.66.1.$vni/32
    # also add a loopback in the vrf to check connectivity across vxlan
    podman exec $h1 ip link add name lo$vni type dummy
    podman exec $h1 ip link set lo$vni master vrf$vni
    podman exec $h1 ip a a dev lo$vni 77.77.1.$vni/32
    podman exec $h1 ip link set lo$vni up
done

# Workloads on host2:
for vni in 10 20; do
    # add a workload (add a "blackhole" route as if it was injected by OVN)
    podman exec $h2 ip route add table $vni blackhole 66.66.2.$vni/32
    # also add a loopback in the vrf to check connectivity across vxlan
    podman exec $h2 ip link add name lo$vni type dummy
    podman exec $h2 ip link set lo$vni master vrf$vni
    podman exec $h2 ip a a dev lo$vni 77.77.2.$vni/32
    podman exec $h2 ip link set lo$vni up
done

echo sleeping for a bit..
sleep 10
echo =======================
echo Dumping VRF routes!!
echo =======================
for vni in 10 20; do
    for h in $h1 $h2; do
      echo "Routes in VRF $vni on $h:"
      podman exec $h ip vrf exec vrf$vni ip r l table $vni
      echo
    done
done

echo =======================
echo Checking connectivity!!
echo =======================

# Check connectivity (don't fail the script on ping failure):
for vni in 10 20; do
    podman exec $h1 ip vrf exec vrf$vni ping -c 1 77.77.2.$vni || true
done

sleep infinity
