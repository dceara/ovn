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
    local pvid=$3
    local vlans=$4

    podman exec $h ip link add int-$name type veth peer name ovs-$name
    podman exec $h ip link set dev ovs-$name up
    podman exec $h ip link set dev int-$name up

    podman exec $h ip link add int2-$name type veth peer name $name
    podman exec $h ip link set dev int2-$name up

    podman exec $h ovs-vsctl add-br br-vif-$name \
        -- add-port br-vif-$name int-$name \
        -- add-port br-vif-$name int2-$name \
        -- set port int-$name vlan_mode=native-tagged tag=$pvid trunks=$vlans \
        -- set port int2-$name vlan_mode=native-untagged tag=$pvid trunks=$vlans

    podman exec $h ovs-vsctl add-port br-int ovs-$name \
        -- set interface ovs-$name external-ids:iface-id=$name

    podman exec $h ip netns add $name
    podman exec $h ip link set dev $name netns $name
    podman exec $h ip netns exec $name ip link set dev $name up
    podman exec $h ip netns exec $name ip link set lo up
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
    -- lsp-add ls lsp1 -- lsp-set-addresses lsp1 unknown     \
    -- lsp-add ls lsp11 -- lsp-set-addresses lsp11 unknown   \
    -- lsp-add ls lsp2 -- lsp-set-addresses lsp2 unknown     \
    -- lsp-add ls lsp22 -- lsp-set-addresses lsp22 unknown   \
    -- lsp-add ls lsp3 -- lsp-set-addresses lsp3 unknown     \
    -- lsp-add ls lsp33 -- lsp-set-addresses lsp33 unknown   \
    -- lsp-add-localnet-port ls lsp-ln phys                  \
    -- set logical_switch ls other_config:vlan-passthru=true

setup_workload $h1 lsp1  42 42,43
setup_workload $h1 lsp11 42 42,43
setup_workload $h2 lsp2  43 43,44
setup_workload $h2 lsp22 43 43,44
setup_workload $h3 lsp3  44 42,44
setup_workload $h3 lsp33 44 42,44

podman exec $h1 ip netns exec lsp1 ip a a dev lsp1 42.42.42.1/24
podman exec $h1 ip netns exec lsp1 ip link add link lsp1 name lsp1.43 type vlan id 43
podman exec $h1 ip netns exec lsp1 ip link set lsp1.43 up
podman exec $h1 ip netns exec lsp1 ip a a dev lsp1.43 43.43.43.1/24

podman exec $h1 ip netns exec lsp11 ip a a dev lsp11 42.42.42.11/24
podman exec $h1 ip netns exec lsp11 ip link add link lsp11 name lsp11.43 type vlan id 43
podman exec $h1 ip netns exec lsp11 ip link set lsp11.43 up
podman exec $h1 ip netns exec lsp11 ip a a dev lsp11.43 43.43.43.11/24

podman exec $h2 ip netns exec lsp2 ip a a dev lsp2 43.43.43.2/24
podman exec $h2 ip netns exec lsp2 ip link add link lsp2 name lsp2.44 type vlan id 44
podman exec $h2 ip netns exec lsp2 ip link set lsp2.44 up
podman exec $h2 ip netns exec lsp2 ip a a dev lsp2.44 44.44.44.2/24

podman exec $h2 ip netns exec lsp22 ip a a dev lsp22 43.43.43.22/24
podman exec $h2 ip netns exec lsp22 ip link add link lsp22 name lsp22.44 type vlan id 44
podman exec $h2 ip netns exec lsp22 ip link set lsp22.44 up
podman exec $h2 ip netns exec lsp22 ip a a dev lsp22.44 44.44.44.22/24

podman exec $h3 ip netns exec lsp3 ip a a dev lsp3 44.44.44.3/24
podman exec $h3 ip netns exec lsp3 ip link add link lsp3 name lsp3.42 type vlan id 42
podman exec $h3 ip netns exec lsp3 ip link set lsp3.42 up
podman exec $h3 ip netns exec lsp3 ip a a dev lsp3.42 42.42.42.3/24

podman exec $h3 ip netns exec lsp33 ip a a dev lsp33 44.44.44.33/24
podman exec $h3 ip netns exec lsp33 ip link add link lsp33 name lsp33.42 type vlan id 42
podman exec $h3 ip netns exec lsp33 ip link set lsp33.42 up
podman exec $h3 ip netns exec lsp33 ip a a dev lsp33.42 42.42.42.33/24

echo "Test from LSP1"
for dest in 42.42.42.11 42.42.42.3 42.42.42.33; do
    podman exec $h1 ip netns exec lsp1 ping -c1 $dest
done

echo "Test from LSP11"
for dest in 42.42.42.1 42.42.42.3 42.42.42.33; do
    podman exec $h1 ip netns exec lsp11 ping -c1 $dest
done

echo "Test from LSP2"
for dest in 43.43.43.1 43.43.43.11 43.43.43.22; do
    podman exec $h2 ip netns exec lsp2 ping -c1 $dest
done

echo "Test from LSP22"
for dest in 43.43.43.1 43.43.43.11 43.43.43.2; do
    podman exec $h2 ip netns exec lsp22 ping -c1 $dest
done

echo "Test from LSP3"
for dest in 44.44.44.2 44.44.44.22 44.44.44.33; do
    podman exec $h3 ip netns exec lsp3 ping -c1 $dest
done

echo "Test from LSP33"
for dest in 44.44.44.2 44.44.44.22 44.44.44.3; do
    podman exec $h3 ip netns exec lsp3 ping -c1 $dest
done

sleep infinity
