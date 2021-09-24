nsw=$1
nlb=$2

export OVN_NB_DAEMON=$(ovn-nbctl --detach --pidfile --log-file=ovn-nbctl.log)

sleep 1

echo "Adding switches/routers.."
for ((i = 0; i < $nsw; i++)); do
    ovn-nbctl ls-add ls$i
    ovn-nbctl lr-add lr$i
done

echo "Adding load balancers.."
for ((j = 0; j < $nlb; j++)); do
    echo "Adding LB $j.."
    cmd="-- lb-add lb$j 42.42.42.1:8080 42.42.42.2:8081 tcp"
    for ((i = 0; i < $nsw; i++)); do
        cmd="$cmd -- ls-lb-add ls$i lb$j -- lr-lb-add lr$i lb$j"
    done
    ovn-nbctl $cmd
done

echo "CPU Time NB:"
ps -eo pcpu,time,pid,user,args | grep ovsdb-server | grep Northbound | grep -v IC | grep -v grep
echo "CPU Time ovn-nbctl:"
ps -eo pcpu,time,pid,user,args | grep ovn-nbctl | grep -v grep
