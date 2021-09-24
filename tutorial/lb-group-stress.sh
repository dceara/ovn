nsw=$1
nlb=$2

export OVN_NB_DAEMON=$(ovn-nbctl --detach --pidfile --log-file=ovn-nbctl.log)

sleep 1

echo "Adding cluster LB group.."
lb_group_uuid=$(ovn-nbctl create load_balancer_group name="cluster-lbs")

echo "Adding switches/routers.."
for ((i = 0; i < $nsw; i++)); do
    ovn-nbctl ls-add ls$i -- add logical_switch ls$i load_balancer_group ${lb_group_uuid}
    ovn-nbctl lr-add lr$i -- add logical_router lr$i load_balancer_group ${lb_group_uuid}
done

echo "Adding load balancers.."
for ((j = 0; j < $nlb; j++)); do
    echo "Adding LB $j.."
    ovn-nbctl --id=@id create load_balancer name=lb$j protocol=tcp vips:'"42.42.42.1:8080"'='"42.42.42.2:8081"' \
              -- add load_balancer_group ${lb_group_uuid} lbs @id
done

echo "CPU Time NB:"
ps -eo pcpu,time,pid,user,args | grep ovsdb-server | grep Northbound | grep -v IC | grep -v grep
echo "CPU Time ovn-nbctl:"
ps -eo pcpu,time,pid,user,args | grep ovn-nbctl | grep -v grep
