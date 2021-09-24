nsw=$1
nlb=$2

export OVN_NB_DAEMON=$(ovn-nbctl --detach --pidfile --log-file=ovn-nbctl.log)

sleep 1

echo "Adding cluster LS group.."
ls_group_uuid=$(ovn-nbctl create logical_switch_group name="cluster-ls-group")

echo "Adding cluster LR group.."
lr_group_uuid=$(ovn-nbctl create logical_router_group name="cluster-lr-group")

echo "Adding switches/routers.."
for ((i = 0; i < $nsw; i++)); do
    ovn-nbctl --id=@id create Logical_Switch name=ls$i -- add logical_switch_group ${ls_group_uuid} ls @id
    ovn-nbctl --id=@id create Logical_Router name=lr$i -- add logical_router_group ${lr_group_uuid} lr @id
done

echo "Adding load balancers.."
for ((j = 0; j < $nlb; j++)); do
    echo "Adding LB $j.."
    ovn-nbctl --id=@id create load_balancer name=lb$j protocol=tcp vips:'"42.42.42.1:8080"'='"42.42.42.2:8081"' \
              -- add logical_switch_group ${ls_group_uuid} lbs @id \
              -- add logical_router_group ${lr_group_uuid} lbs @id
done

echo "CPU Time NB:"
ps -eo pcpu,time,pid,user,args | grep ovsdb-server | grep Northbound | grep -v IC | grep -v grep
echo "CPU Time ovn-nbctl:"
ps -eo pcpu,time,pid,user,args | grep ovn-nbctl | grep -v grep
