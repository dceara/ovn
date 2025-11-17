#!/bin/bash

# Builds a similar topology to:
# https://drive.google.com/file/d/17cdQ45gVcdxgzuprxWVNMXuPBSPsTAIJ/view
# except that dynamic routing is enabled directly on the tenant router.

set -ex

image=ovn-bgp-test:dev
net=unicast-l2-net

podman build -t $image -f Dockerfile ../../../

h1=unicast-host1
h2=unicast-host2
h3=unicast-host3

vrf=10

function cleanup() {
    set +e

    for host in $h2 $h3; do
        podman exec $host systemctl stop ovn-controller
        podman exec $host systemctl stop ovn-northd
        podman exec $host systemctl stop openvswitch
    done

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
# OVN IPs will be owned by bgp-phys.
# podman exec $h2 ip a a dev eth0 20.0.0.2/8
# podman exec $h3 ip a a dev eth0 20.0.0.3/8

# start frr
for host in $h1 $h2 $h3; do
    podman exec $host sed -i 's/bgpd=no/bgpd=yes/g' /etc/frr/daemons
    podman exec $host systemctl start frr
done

# configure BGP on host 1
echo "configure
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000
    bgp log-neighbor-changes
    neighbor 20.0.0.2 remote-as 65000
    !
    neighbor 20.0.0.3 remote-as 65000
    !
    address-family ipv4 unicast
      redistribute connected
    exit-address-family
  exit
  do copy running-config startup-config" | podman exec -i $h1 vtysh

# configure BGP on host 2
echo "configure
  vrf vrf10
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000 vrf vrf$vrf
    bgp log-neighbor-changes
    neighbor 20.0.0.1 remote-as 65000
    !
    neighbor 20.0.0.3 remote-as 65000
    !
    address-family ipv4 unicast
      redistribute kernel
    exit-address-family
  exit
  !
  do copy running-config startup-config" | podman exec -i $h2 vtysh

# configure BGP on host 3
echo "configure
  vrf vrf10
  log file /var/log/frr/frr.log
  log syslog debugging
  router bgp 65000 vrf vrf$vrf
    bgp log-neighbor-changes
    neighbor 20.0.0.1 remote-as 65000
    !
    neighbor 20.0.0.2 remote-as 65000
    !
    address-family ipv4 unicast
      redistribute kernel
    exit-address-family
  exit
  !
  do copy running-config startup-config" | podman exec -i $h3 vtysh

# Restart frr
for host in $h1 $h2 $h3; do
    podman exec $host systemctl restart frr
done

echo sleeping for a bit..
sleep 10
echo Configuring VRF on HOST2..

# Create VRF on host 2.
podman exec $h2 ip link add vrf$vrf type vrf table $vrf
podman exec $h2 ip link set vrf$vrf up

# Create VRF on host 3.
podman exec $h3 ip link add vrf$vrf type vrf table $vrf
podman exec $h3 ip link set vrf$vrf up

echo sleeping for a bit..
sleep 10
echo Starting OVN on HOST2..

# Start OVS on host2 and set system-id.
h2_chassis=h2
podman exec $h2 systemctl start openvswitch
podman exec $h2 ovs-vsctl set open . external_ids:system-id=$h2_chassis

# Start OVN on host2.
podman exec $h2 systemctl start ovn-northd
podman exec $h2 systemctl start ovn-controller
podman exec $h2 ovn-sbctl set-connection ptcp:6642
podman exec $h2 ovs-vsctl set open . external-ids:ovn-remote=tcp:20.0.0.2:6642
podman exec $h2 ovs-vsctl set open . external-ids:ovn-encap-type=geneve
podman exec $h2 ovs-vsctl set open . external-ids:ovn-encap-ip=20.0.0.2

# Start OVS on host3 and set system-id.
h3_chassis=h3
podman exec $h3 systemctl start openvswitch
podman exec $h3 ovs-vsctl set open . external_ids:system-id=$h3_chassis

# Start OVN on host2.
podman exec $h3 systemctl start ovn-controller
podman exec $h3 ovs-vsctl set open . external-ids:ovn-remote=tcp:20.0.0.2:6642
podman exec $h3 ovs-vsctl set open . external-ids:ovn-encap-type=geneve
podman exec $h3 ovs-vsctl set open . external-ids:ovn-encap-ip=20.0.0.3

# Add a public switch.
podman exec $h2 ovn-nbctl                                            \
    -- ls-add bgp-public                                             \
    -- lsp-add-localnet-port bgp-public bgp-public-localnet bgp-phys

# Add bgp-phys on all ovn chassis.
podman exec $h2 ovs-vsctl add-br bgp-phys                                          \
    -- add-port bgp-phys eth0                                                      \
    -- set open . external-ids:ovn-bridge-mappings=bgp-phys:bgp-phys,br-bgp:br-bgp
podman exec $h3 ovs-vsctl add-br bgp-phys                                          \
    -- add-port bgp-phys eth0                                                      \
    -- set open . external-ids:ovn-bridge-mappings=bgp-phys:bgp-phys,br-bgp:br-bgp

podman exec $h2 ip a a dev bgp-phys 20.0.0.2/8
podman exec $h2 ip l set dev bgp-phys up
podman exec $h3 ip a a dev bgp-phys 20.0.0.3/8
podman exec $h3 ip l set dev bgp-phys up

# Add a BGP router (for control plane traffic).
# Add per chassis ports, use the MACs and IPs we have on the host.
podman exec $h2 ovn-nbctl                                            \
    -- lr-add lr-bgp-control                                         \
    -- lrp-add lr-bgp-control lr-bgp-h2 00:00:00:00:00:02 20.0.0.2/8 \
        -- set logical_router_port lr-bgp-h2                         \
            options:routing-protocol-redirect=local-bgp-h2           \
            options:routing-protocols=BGP                            \
        -- lrp-set-gateway-chassis lr-bgp-h2 $h2_chassis             \
    -- lrp-add lr-bgp-control lr-bgp-h3 00:00:00:00:00:03 20.0.0.3/8 \
        -- set logical_router_port lr-bgp-h3                         \
            options:routing-protocol-redirect=local-bgp-h3           \
            options:routing-protocols=BGP                            \
        -- lrp-set-gateway-chassis lr-bgp-h3 $h3_chassis             \
    -- lrp-add lr-bgp-control lrp-public 00:00:00:00:42:42           \
    -- lrp-add lr-bgp-control lrp-ovn 00:00:00:00:42:42 30.0.0.1/8   \
    -- lsp-add-router-port bgp-public bgp-pub-h2 lr-bgp-h2           \
    -- lsp-add-router-port bgp-public bgp-pub-h3 lr-bgp-h3           \
    -- lsp-add bgp-public local-bgp-h2                               \
        -- lsp-set-addresses local-bgp-h2 unknown                    \
    -- lsp-add bgp-public local-bgp-h3                               \
        -- lsp-set-addresses local-bgp-h3 unknown                    \
    -- ls-add ls-ovn                                                 \
        -- lsp-add-router-port ls-ovn ls-ovn-bgp-control lrp-ovn     \
        -- lsp-add-localnet-port ls-ovn ls-ovn-localnet br-bgp

# Add internal port (veth) to run FRR on.
podman exec $h2 ovs-vsctl add-port br-int local-bgp-h2               \
    -- set interface local-bgp-h2 type=internal                      \
    -- set interface local-bgp-h2 external_ids:iface-id=local-bgp-h2

# Bring port up, move it to FRR VRF, and configure its MAC address and IP:
podman exec $h2 ip link set local-bgp-h2 master vrf$vrf
podman exec $h2 ip link set local-bgp-h2 address 00:00:00:00:00:02
podman exec $h2 ip addr add dev local-bgp-h2 20.0.0.2/8
podman exec $h2 ip link set local-bgp-h2 up

# Split geneve, SB, ARP reply traffic towards the kernel.
podman exec $h2 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,tcp,tcp_dst=6642,action=LOCAL
podman exec $h2 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,tcp,tcp_src=6642,action=LOCAL
podman exec $h2 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,udp,udp_dst=6081,action=LOCAL
podman exec $h2 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,arp,arp_op=2,action=FLOOD
podman exec $h2 ovs-ofctl add-flow bgp-phys priority=1000,in_port=LOCAL,action=eth0

# Split geneve, SB, ARP reply traffic towards the kernel.
podman exec $h3 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,tcp,tcp_dst=6642,action=LOCAL
podman exec $h3 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,tcp,tcp_src=6642,action=LOCAL
podman exec $h3 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,udp,udp_dst=6081,action=LOCAL
podman exec $h3 ovs-ofctl add-flow bgp-phys priority=1000,in_port=eth0,arp,arp_op=2,action=FLOOD
podman exec $h3 ovs-ofctl add-flow bgp-phys priority=1000,in_port=LOCAL,action=eth0

# Add internal port (veth) to run FRR on.
podman exec $h3 ovs-vsctl add-port br-int local-bgp-h3               \
    -- set interface local-bgp-h3 type=internal                      \
    -- set interface local-bgp-h3 external_ids:iface-id=local-bgp-h3

# Bring port up, move it to FRR VRF, and configure its MAC address and IP:
podman exec $h3 ip link set local-bgp-h3 master vrf$vrf
podman exec $h3 ip link set local-bgp-h3 address 00:00:00:00:00:03
podman exec $h3 ip addr add dev local-bgp-h3 20.0.0.3/8
podman exec $h3 ip link set local-bgp-h3 up

echo sleeping for a bit..
sleep 2
echo Testing connectivity from HOST1..

podman exec $h1 ping -c 1 20.0.0.2
podman exec $h1 ping -c 1 20.0.0.3

echo sleeping for a bit..
sleep 2
echo Check BGP established..

podman exec $h1 vtysh -c "show bgp neighbors" | grep "Connections established 1"
podman exec $h2 vtysh -c "show bgp vrf vrf$vrf neighbors" | grep "Connections established 1"
podman exec $h3 vtysh -c "show bgp vrf vrf$vrf neighbors" | grep "Connections established 1"

####### ACTUAL OVN NETWORK, NON-BGP WORLD. #############

# Add br-bgp (between bgp-phys and br-int)
podman exec $h2 ovs-vsctl add-br br-bgp
podman exec $h3 ovs-vsctl add-br br-bgp

# Add a public switch.
podman exec $h2 ovn-nbctl                                     \
    -- ls-add public                                          \
    -- lsp-add public lsp-localnet-port                       \
    -- lsp-set-type lsp-localnet-port localnet                \
    -- lsp-set-addresses lsp-localnet-port unknown            \
    -- lsp-set-options lsp-localnet-port network_name=br-bgp

# Connect a "workload" - e.g. a tenant router + tenant switch + VM + FIP.
podman exec $h2 ovn-nbctl                                               \
    -- lr-add lr-tenant                                                 \
    -- lrp-add lr-tenant lr-tenant-public 00:00:00:00:00:42 30.0.0.2/8  \
    -- lsp-add public public-lr-tenant                                  \
    -- set logical_switch_port public-lr-tenant                         \
            type=router options:router-port=lr-tenant-public            \
    -- lsp-set-addresses public-lr-tenant router

# Make the tenant router port to "public" a DGP.
podman exec $h2 ovn-nbctl \
    -- lrp-set-gateway-chassis lr-tenant-public $h2_chassis

# Add a tenant switch:
podman exec $h2 ovn-nbctl                                              \
    -- ls-add ls-tenant                                                \
    -- lrp-add lr-tenant lr-tenant-ls 00:00:00:00:01:01 192.168.1.1/24 \
    -- lsp-add ls-tenant ls-tenant-lr                                  \
    -- set logical_switch_port ls-tenant-lr                            \
            type=router options:router-port=lr-tenant-ls               \
    -- lsp-set-addresses ls-tenant-lr router

# Add a "VM".
podman exec $h2 ovn-nbctl                                     \
    -- lsp-add ls-tenant vm1                                  \
    -- lsp-set-addresses vm1 "00:00:00:00:01:10 192.168.1.10"

# Bind the "VM" port to an internal OVS port (veth).
podman exec $h2 ovs-vsctl                                     \
    -- add-port br-int vm1 -- set interface vm1 type=internal \
    -- set interface vm1 external_ids:iface-id=vm1

# Set te port UP in a separate netns and configure its MAC and IP and gw.
podman exec $h2 ip netns add vm1
podman exec $h2 ip link set vm1 netns vm1
podman exec $h2 ip netns exec vm1 ip link set vm1 address 00:00:00:00:01:10
podman exec $h2 ip netns exec vm1 ip addr add dev vm1 192.168.1.10/24
podman exec $h2 ip netns exec vm1 ip link set vm1 up
podman exec $h2 ip netns exec vm1 ip route add default via 192.168.1.1

# Add a Floating IP, 30.0.0.142.
FIP=30.0.0.142
podman exec $h2 ovn-nbctl \
    -- lr-nat-add lr-tenant dnat_and_snat $FIP 192.168.1.10

# Enable BGP dynamic routing (learning) and advertising of connected networks
# and NATs on the tenant router.
podman exec $h2 ovn-nbctl                                               \
    -- set Logical_Router lr-tenant                                     \
                   options:dynamic-routing=true                         \
                   options:dynamic-routing-vrf-id=$vrf                  \
    -- set Logical_Router_Port lr-tenant-public                         \
                   options:dynamic-routing-maintain-vrf=false           \
                   options:dynamic-routing-redistribute-local-only=true \
                   options:dynamic-routing-redistribute=connected,nat   \
    -- set logical_router_port lr-tenant-ls                             \
                   options:dynamic-routing-redistribute="connected"

echo sleeping for a bit..
sleep 2
echo Check FIP advertised to host1 via BGP nexthop $h2:
podman exec -it $h1 vtysh -c "show ip route" | grep $FIP
podman exec -it $h1 ip r l | grep $FIP | grep 20.0.0.2

# Simulate an external entity on the network of $h1.
podman exec $h1 ip a a dev eth0 40.0.0.1/24
echo sleeping for a bit..
sleep 2

# Check that OVN learned it.
podman exec $h2 ovn-sbctl find learned_route ip_prefix=40.0.0.0/24 nexthop=20.0.0.1 | grep -q uuid

#############################################
###### TODO: hacks to get traffic out of OVN:
#############################################
podman exec $h2 ovn-nbctl lr-route-add lr-bgp-control 0.0.0.0/0 20.0.0.1
podman exec $h2 ovn-nbctl lr-route-add lr-bgp-control 192.168.1.0/24 30.0.0.2
podman exec $h2 ovn-nbctl lr-route-add lr-tenant 0.0.0.0/0 30.0.0.1
echo sleeping for a bit..
sleep 2

echo Check connectivity to the FIP from host1:
podman exec -it $h1 ping -I 40.0.0.1 -c1 $FIP

# Failover the DGP:
podman exec $h2 ovn-nbctl \
    -- lrp-set-gateway-chassis lr-tenant-public $h3_chassis
echo sleeping for a bit..
sleep 2

echo Check FIP advertised to host1 via BGP nexthop $h2:
podman exec -it $h1 vtysh -c "show ip route" | grep $FIP
podman exec -it $h1 ip r l | grep $FIP | grep 20.0.0.3

echo Check connectivity to the FIP from host1:
podman exec -it $h1 ping -I 40.0.0.1 -c1 $FIP

sleep infinity
