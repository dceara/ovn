nsw=$1
nlb=$2

export OVN_NB_DAEMON=$(ovn-nbctl --detach --pidfile --no-leader-only --log-file=ovn-nbctl.log --db unix:nb1.ovsdb,unix:nb2.ovsdb,unix:nb3.ovsdb)

sleep 1

echo "Adding cluster LB group.."
lb_group_uuid=$(ovn-nbctl create load_balancer_group name="cluster-lbs")

ovn-nbctl lr-add cluster_rtr
ovn-nbctl ls-add join
ovn-nbctl lrp-add cluster_rtr crtr_join 00:00:00:00:03:01 43.43.43.1/24
ovn-nbctl lsp-add join join_crtr
ovn-nbctl lsp-set-addresses join_crtr "00:00:00:00:03:01 43.43.43.1"
ovn-nbctl lsp-set-type join_crtr router
ovn-nbctl lsp-set-options join_crtr router-port=crtr_join

echo "Adding switches/routers.."
for ((i = 0; i < $nsw; i++)); do
    echo "Switch/Router $i"
    ip2=42.42.$((i/256)).$((i%256))
    ip3=43.43.$((i/256)).$((i%256))

    ovn-nbctl ls-add ls$i -- add logical_switch ls$i load_balancer_group ${lb_group_uuid}
    ovn-nbctl lrp-add cluster_rtr crtr_sw${i} 00:00:00:00:02:01 ${ip2}/16
    ovn-nbctl lsp-add ls$i sw${i}_crtr
    ovn-nbctl lsp-set-addresses sw${i}_crtr "00:00:00:00:02:01 ${ip2}"
    ovn-nbctl lsp-set-type      sw${i}_crtr router
    ovn-nbctl lsp-set-options   sw${i}_crtr router-port=crtr_sw${i}
    ovn-nbctl lsp-add ls$i sw${i}_mgmt
    ovn-nbctl lsp-add ls$i sw${i}_mgmt2

    ovn-nbctl lr-add lr$i -- add logical_router lr$i load_balancer_group ${lb_group_uuid}
    ovn-nbctl set logical_router lr$i options:dynamic_neigh_routers=true
    ovn-nbctl set logical_router lr$i options:chassis=foo
    ovn-nbctl lrp-add lr$i lr${i}_join 00:00:00:00:04:01 ${ip3}/16
    ovn-nbctl lsp-add join join_lr${i}
    ovn-nbctl lsp-set-addresses join_lr${i} "00:00:00:00:04:01 ${ip3}"
    ovn-nbctl lsp-set-type      join_lr${i} router
    ovn-nbctl lsp-set-options   join_lr${i} router-port=lr${i}_join
done

echo "Adding load balancers.."
for ((j = 0; j < $nlb; j++)); do
    echo "Adding LB $j.."
    ip1="42.$((j/255)).$((j%255)).1"
    ip2="42.$((j/255)).$((j%255)).2"
    ovn-nbctl --id=@id create load_balancer name=lb$j protocol=tcp vips:"\"${ip1}:8080\""="\"${ip2}:8081\"" \
              -- add load_balancer_group ${lb_group_uuid} load_balancer @id
done

ovn-nbctl --wait=sb sync

echo "DONE with the base setup, adding one more load balancer"
echo "Adding LB $j.."
ip1="42.$((j/255)).$((j%255)).1"
ip2="42.$((j/255)).$((j%255)).2"
ovn-nbctl --id=@id create load_balancer name=lb$j protocol=tcp vips:"\"${ip1}:8080\""="\"${ip2}:8081\"" \
            -- add load_balancer_group ${lb_group_uuid} load_balancer @id
