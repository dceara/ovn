#!/bin/bash

ovs-appctl vlog/set vconn:info

export OVN_NB_DAEMON=$(ovn-nbctl --detach)

cleanup() {
    ovn-appctl -t $OVN_NB_DAEMON exit
}

trap cleanup EXIT

N=$1

sleep 2

ovn-nbctl ls-add ls

for i in $(seq $N); do
    iface=if-$i

    echo Adding iface $iface

    ovs-vsctl add-port br-int $iface -- set interface $iface external_ids:iface-id=$iface

    ovn-nbctl lsp-add ls $iface

    echo Done adding iface $iface
    echo
done

