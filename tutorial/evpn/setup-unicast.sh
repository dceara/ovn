#!/bin/bash
set -ex

image=ovn-bgp-test:dev
net=unicast-l2-net

podman build -t $image -f Dockerfile ../../../

h1=unicast-host1
h2=unicast-host2

vrf=10

function cleanup() {
    set +e

    for host in $h2; do
        podman exec $host systemctl stop ovn-controller
        podman exec $host systemctl stop ovn-northd
        podman exec $host systemctl stop openvswitch
    done

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

# host2 IP is owned by OVN
#podman exec $h2 ip a a dev eth0 20.0.0.2/8

# start frr
for host in $h1 $h2; do
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
    address-family ipv4 unicast
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
    address-family ipv4 unicast
      redistribute kernel
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
echo Configuring VRF on HOST2..

# Create VRF on host 2.
podman exec $h2 ip link add vrf$vrf type vrf table $vrf
podman exec $h2 ip link set vrf$vrf up

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
podman exec $h2 ovs-vsctl set open . external-ids:ovn-remote=tcp:127.0.0.1:6642
podman exec $h2 ovs-vsctl set open . external-ids:ovn-encap-type=geneve
podman exec $h2 ovs-vsctl set open . external-ids:ovn-encap-ip=127.0.0.1

# Add a public switch.
podman exec $h2 ovn-nbctl                                  \
    -- ls-add public                                       \
    -- lsp-add public lsp-localnet-port                    \
    -- lsp-set-type lsp-localnet-port localnet             \
    -- lsp-set-addresses lsp-localnet-port unknown         \
    -- lsp-set-options lsp-localnet-port network_name=phys

podman exec $h2 ovs-vsctl add-br br-ex                        \
    -- add-port br-ex eth0                                    \
    -- set open . external-ids:ovn-bridge-mappings=phys:br-ex

# Add internal port (veth) to run FRR on.
podman exec $h2 ovn-nbctl                       \
    -- lsp-add public local-bgp-port            \
    -- lsp-set-addresses local-bgp-port unknown

podman exec $h2 ovs-vsctl add-port br-int local-bgp-port                 \
    -- set interface local-bgp-port type=internal                        \
    -- set interface local-bgp-port external_ids:iface-id=local-bgp-port

# Bring port up, move it to FRR VRF, and configure it's MAC address and IP:
podman exec $h2 ip link set local-bgp-port master vrf$vrf
podman exec $h2 ip link set local-bgp-port address 00:00:00:00:00:03
podman exec $h2 ip addr add dev local-bgp-port 20.0.0.2/8
podman exec $h2 ip link set local-bgp-port up

# Connect an "FRR" (invisible) per-chassis router, to advertise OVN routes and
# learn from upstream routers.  Use the same MAC and IP as on the logical
# port bound to FRR>
podman exec $h2 ovn-nbctl                                             \
    -- lr-add lr-frr                                                  \
    -- set logical_router lr-frr options:chassis=$h2_chassis          \
    -- lrp-add lr-frr lrp-local-bgp-port 00:00:00:00:00:03 20.0.0.2/8 \
    -- set logical_router_port lrp-local-bgp-port                     \
            options:routing-protocol-redirect=local-bgp-port          \
    -- set logical_router_port lrp-local-bgp-port                     \
            options:routing-protocols=BGP                             \
    -- lsp-add public public-lr-frr                                   \
    -- set logical_switch_port public-lr-frr                          \
            type=router options:router-port=lrp-local-bgp-port        \
    -- lsp-set-addresses public-lr-frr router

# Enable dynamic routing and configure FRR router tunnel key to match the
# VRF ID on the "FRR" (invisible) router.
podman exec $h2 ovn-nbctl                                       \
    -- set Logical_Router lr-frr options:dynamic-routing=true   \
                                 options:requested-tnl-key=$vrf \
                                 options:dynamic-routing-maintain-vrf=false

# Redistribute OVN routes learnt from the tenant routers connected to
# the "public" switch:
podman exec $h2 ovn-nbctl                            \
    -- set logical_router_port lrp-local-bgp-port    \
            options:dynamic-routing-redistribute=nat

echo sleeping for a bit..
sleep 2
echo Testing connectivity from HOST1..

podman exec $h1 ping -c 1 20.0.0.2

echo sleeping for a bit..
sleep 2
echo Check BGP established..

podman exec $h1 vtysh -c "show bgp neighbors" | grep "Connections established 1"
podman exec $h2 vtysh -c "show bgp vrf vrf$vrf neighbors" | grep "Connections established 1"

# Connect a "workload" - e.g. a tenant router + tenant switch + VM + FIP.
podman exec $h2 ovn-nbctl                                               \
    -- lr-add lr-tenant                                                 \
    -- lrp-add lr-tenant lr-tenant-public 00:00:00:00:00:42 20.0.0.42/8 \
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


# Add a Floating IP, 20.0.0.142.
FIP=20.0.0.142
podman exec $h2 ovn-nbctl \
    -- lr-nat-add lr-tenant dnat_and_snat $FIP 192.168.1.10

echo sleeping for a bit..
sleep 2
echo Check FIP advertised to host1 via BGP:
podman exec -it unicast-host1 vtysh -c "show ip route" | grep $FIP
podman exec -it unicast-host1 ip r l | grep $FIP

echo Check connectivity to the FIP from host1:
podman exec -it unicast-host1 ping -c1 $FIP

sleep infinity
