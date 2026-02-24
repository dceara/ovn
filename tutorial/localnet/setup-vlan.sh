#!/bin/bash
set -ex

image=ovn-bgp-test:dev
net1=ovn-net
net2=fab-net


podman build -t $image -f Dockerfile ../../../

h1=host1
h2=host2
h3=host3

function cleanup() {
    set +e
    podman exec $h1 systemctl stop ovn-controller
    podman exec $h1 systemctl stop ovn-northd
    podman exec $h1 systemctl stop openvswitch

    for host in $h1 $h2 $h3; do
        podman stop $host
        podman rm -f $host
    done
    podman network rm $net1
    podman network rm $net2
}

trap "cleanup" EXIT

function setup_workload() {
    local h=$1
    local name=$2

    podman exec $h ip link add $name type veth peer name ovs-$name
    podman exec $h ip netns add $name
    podman exec $h ip link set dev $name netns $name
    podman exec $h ip link set dev ovs-$name up
    podman exec $h ip netns exec $name ip link set dev $name up
    podman exec $h ip netns exec $name ip link set lo up
    podman exec $h ovs-vsctl add-port br-int ovs-$name \
        -- set interface ovs-$name external-ids:iface-id=$name
}

modprobe openvswitch

# ovn network
podman network create --internal --ipam-driver=none $net1
# fabric network
podman network create --internal --ipam-driver=none $net2

# hosts
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net1:mac=00:00:00:00:00:01                              \
           --network $net2:mac=00:00:00:00:02:01                              \
           --name $h1 $image
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net1:mac=00:00:00:00:00:02                              \
           --network $net2:mac=00:00:00:00:02:02                              \
           --name $h2 $image
podman run --privileged -d --pids-limit=-1 --security-opt apparmor=unconfined \
           --network $net1:mac=00:00:00:00:00:03                              \
           --network $net2:mac=00:00:00:00:02:03                              \
           --name $h3 $image

h1_eth=$(podman exec $h1 ip link | grep 00:00:00:00:00:01 -B1 | grep ': eth' | cut -f1 -d '@' | cut -f2 -d ' ')
h2_eth=$(podman exec $h2 ip link | grep 00:00:00:00:00:02 -B1 | grep ': eth' | cut -f1 -d '@' | cut -f2 -d ' ')
h3_eth=$(podman exec $h3 ip link | grep 00:00:00:00:00:03 -B1 | grep ': eth' | cut -f1 -d '@' | cut -f2 -d ' ')

# Configure "hv" IPs.
podman exec $h1 ip a a dev $h1_eth 20.0.0.1/8
podman exec $h2 ip a a dev $h2_eth 20.0.0.2/8
podman exec $h3 ip a a dev $h3_eth 20.0.0.3/8

sleep 1

podman exec $h1 ping -c1 20.0.0.2
podman exec $h1 ping -c1 20.0.0.3

# Provision OVN:
echo Provisioning OVN...
podman exec $h1 systemctl start ovn-northd
podman exec $h1 ovn-sbctl set-connection ptcp:6642

for h in $h1 $h2 $h3; do
    podman exec $h systemctl start openvswitch
    podman exec $h ovs-vsctl set open . external_ids:system-id=$h
    podman exec $h systemctl start ovn-controller
done

h1_ovn_eth=$(podman exec $h1 ip link | grep 00:00:00:00:02:01 -B1 | grep ': eth' | cut -f1 -d '@' | cut -f2 -d ' ')
h2_ovn_eth=$(podman exec $h2 ip link | grep 00:00:00:00:02:02 -B1 | grep ': eth' | cut -f1 -d '@' | cut -f2 -d ' ')
h3_ovn_eth=$(podman exec $h3 ip link | grep 00:00:00:00:02:03 -B1 | grep ': eth' | cut -f1 -d '@' | cut -f2 -d ' ')
podman exec $h1 ovs-vsctl add-br br-phys -- add-port br-phys $h1_ovn_eth
podman exec $h2 ovs-vsctl add-br br-phys -- add-port br-phys $h2_ovn_eth
podman exec $h3 ovs-vsctl add-br br-phys -- add-port br-phys $h3_ovn_eth

podman exec $h1 ovs-vsctl set open .            \
  external-ids:ovn-remote=tcp:20.0.0.1:6642     \
  external-ids:ovn-encap-type=geneve            \
  external-ids:ovn-encap-ip=20.0.0.1            \
  external-ids:ovn-bridge-mappings=phys:br-phys
podman exec $h2 ovs-vsctl set open .            \
  external-ids:ovn-remote=tcp:20.0.0.1:6642     \
  external-ids:ovn-encap-type=geneve            \
  external-ids:ovn-encap-ip=20.0.0.2            \
  external-ids:ovn-bridge-mappings=phys:br-phys
podman exec $h3 ovs-vsctl set open .            \
  external-ids:ovn-remote=tcp:20.0.0.1:6642     \
  external-ids:ovn-encap-type=geneve            \
  external-ids:ovn-encap-ip=20.0.0.3            \
  external-ids:ovn-bridge-mappings=phys:br-phys

# Configure OVN:
echo Configuring OVN...

podman exec $h1 ovn-nbctl ls-add ls                          \
    -- lsp-add ls lsp1                                       \
    -- lsp-add ls lsp2                                       \
    -- lsp-add ls lsp3                                       \
    -- lsp-add-localnet-port ls lsp-ln phys                  \
    -- set logical_switch ls other_config:vlan-passthru=true

setup_workload $h1 lsp1
setup_workload $h2 lsp2
setup_workload $h3 lsp3

podman exec $h1 ovs-vsctl set port ovs-lsp1 vlan_mode=native-tagged tag=42 trunks=42,43
podman exec $h2 ovs-vsctl set port ovs-lsp2 vlan_mode=native-tagged tag=43 trunks=43,44
podman exec $h3 ovs-vsctl set port ovs-lsp3 vlan_mode=native-tagged tag=44 trunks=42,44

sleep infinity
