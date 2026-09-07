#!/bin/bash

# Arguments
N_UDN=${1:-1}
N_PODS_PER_UDN=${2:-2}
CHURN_INTERVAL=${3:-20}
CHURN_SIZE=${4:-5}

ovs-appctl vlog/set vconn:info
export OVN_NB_DAEMON=$(ovn-nbctl --detach)

cleanup() {
    echo -e "\nCleaning up..."
    ovn-appctl -t $OVN_NB_DAEMON exit
    
    # Remove all veths created for the pods
    for pod in $(ip -o link show | awk -F': ' '{print $2}' | cut -d@ -f1 | grep '^pod-'); do
        ip link delete "$pod" 2>/dev/null || true
    done
}
trap cleanup EXIT

sleep 2

# Create the external bridge
ovs-vsctl --may-exist add-br br-ex
bridge_mappings=""

# Track the number of active pods so our SB DB wait loop doesn't hang on down ports
EXPECTED_PODS=0

echo "Generating topology for $N_UDN UDNs with $N_PODS_PER_UDN pods each (Churn interval: $CHURN_INTERVAL, batch size: $CHURN_SIZE)..."

for i in $(seq 1 "$N_UDN"); do
    ls_name="ls-$i"
    lr_name="lr-$i"
    lb_name="lb-$i"
    
    # Safe octet math to handle N=1000+ (prevents > 255 IP/MAC errors)
    oct2=$(( i / 250 ))
    oct3=$(( (i % 250) + 1 )) 
    
    mac_oct2=$(printf "%02x" "$oct2")
    mac_oct3=$(printf "%02x" "$oct3")
    
    router_mac="00:00:00:${mac_oct2}:${mac_oct3}:01"
    router_ip="10.${oct2}.${oct3}.1"
    vip="10.100.${oct2}.${oct3}:80"

    echo "=== Configuring UDN $i ($ls_name) ==="

    ovn-nbctl ls-add "$ls_name"
    
    # Create router and configure it as a gateway router
    ovn-nbctl lr-add "$lr_name"
    ovn-nbctl set logical_router "$lr_name" options:chassis=chassis-1

    # Connect Router to UDN Switch
    lrp_name="lrp-$i"
    lsp_lr_name="lsp-lr-$i"
    ovn-nbctl lrp-add "$lr_name" "$lrp_name" "$router_mac" "${router_ip}/24"
    ovn-nbctl lsp-add "$ls_name" "$lsp_lr_name"
    ovn-nbctl lsp-set-type "$lsp_lr_name" router
    ovn-nbctl lsp-set-addresses "$lsp_lr_name" router
    ovn-nbctl lsp-set-options "$lsp_lr_name" router-port="$lrp_name"

    # ==========================================
    # Localnet Switch Configuration
    # ==========================================
    ls_ext_name="ls-ext-$i"
    lrp_ext_name="lrp-ext-$i"
    lsp_ext_name="lsp-ext-$i"
    ln_port="ln-$i"
    physnet="physnet-$i"
    
    ext_mac="00:00:01:${mac_oct2}:${mac_oct3}:01"
    # 4 octets total. Scales 172.16.x.1 to 172.20.x.1 for up to N=1000
    ext_ip="172.$((16 + oct2)).${oct3}.1"

    # Create Localnet Switch and attach to Router
    ovn-nbctl ls-add "$ls_ext_name"
    ovn-nbctl lrp-add "$lr_name" "$lrp_ext_name" "$ext_mac" "${ext_ip}/24"
    ovn-nbctl lsp-add "$ls_ext_name" "$lsp_ext_name"
    ovn-nbctl lsp-set-type "$lsp_ext_name" router
    ovn-nbctl lsp-set-addresses "$lsp_ext_name" router
    ovn-nbctl lsp-set-options "$lsp_ext_name" router-port="$lrp_ext_name"

    # Add localnet port to external switch
    ovn-nbctl lsp-add "$ls_ext_name" "$ln_port"
    ovn-nbctl lsp-set-type "$ln_port" localnet
    ovn-nbctl lsp-set-addresses "$ln_port" unknown
    ovn-nbctl lsp-set-options "$ln_port" network_name="$physnet"

    # Append to ovn-bridge-mappings and apply immediately for this UDN
    if [ -z "$bridge_mappings" ]; then
        bridge_mappings="$physnet:br-ex"
    else
        bridge_mappings="$bridge_mappings,$physnet:br-ex"
    fi
    ovs-vsctl set open . external-ids:ovn-bridge-mappings="$bridge_mappings"

    # ==========================================
    # Pod Provisioning & ACLs
    # ==========================================
    backends=""

    for j in $(seq 1 "$N_PODS_PER_UDN"); do
        pod_name="pod-$i-$j"
        peer_name="${pod_name}-p"
        pod_ip="10.${oct2}.${oct3}.$((j+1))"
        
        mac_oct4=$(printf "%02x" "$((j+1))")
        pod_mac="00:00:00:${mac_oct2}:${mac_oct3}:${mac_oct4}"

        # Create veth pair for the kernel datapath and bring them up
        ip link add "$pod_name" type veth peer name "$peer_name"
        ip link set "$pod_name" up
        ip link set "$peer_name" up

        # Add to OVS (no 'type=internal' so it uses the real veth interface)
        ovs-vsctl add-port br-int "$pod_name" -- set interface "$pod_name" external_ids:iface-id="$pod_name"
        
        ovn-nbctl lsp-add "$ls_name" "$pod_name"
        ovn-nbctl lsp-set-addresses "$pod_name" "$pod_mac $pod_ip"

        # Apply Pod ACLs on the Logical Switch
        ovn-nbctl acl-add "$ls_name" from-lport 100 "ip4.src == $pod_ip" allow-related
        ovn-nbctl acl-add "$ls_name" to-lport 100 "ip4.dst == $pod_ip" allow-related

        if [ -z "$backends" ]; then
            backends="$pod_ip:80"
        else
            backends="$backends,$pod_ip:80"
        fi
        
        EXPECTED_PODS=$(( EXPECTED_PODS + 1 ))
    done

    # Create Load Balancer and set Backends
    ovn-nbctl lb-add "$lb_name" "$vip" "$backends"

    # Attach Load Balancer to Logical Switch
    ovn-nbctl ls-lb-add "$ls_name" "$lb_name"

    # ==========================================
    # Churn Injection
    # ==========================================
    if (( i % CHURN_INTERVAL == 0 )); then
        churn_start=$(( i - CHURN_INTERVAL + 1 ))
        churn_end=$(( churn_start + CHURN_SIZE - 1 ))
        
        # Guard in case CHURN_INTERVAL < CHURN_SIZE
        if (( churn_end > i )); then
            churn_end=$i
        fi
        
        echo "=== [Churn] Reached iteration $i ==="
        echo "=== Removing LSPs and veth interfaces for the first $CHURN_SIZE UDNs of this batch (UDNs $churn_start to $churn_end) ==="
        
        for k in $(seq "$churn_start" "$churn_end"); do
            for j in $(seq 1 "$N_PODS_PER_UDN"); do
                churn_pod="pod-$k-$j"
                
                # Remove the NB LSP, the OVS port, and the Linux veth interface
                ovn-nbctl --if-exists lsp-del "$churn_pod"
                ovs-vsctl --if-exists del-port br-int "$churn_pod"
                ip link delete "$churn_pod" 2>/dev/null || true
                
                # Decrement expected pods since they will no longer exist in SB DB
                EXPECTED_PODS=$(( EXPECTED_PODS - 1 ))
            done
        done
    fi
done

echo "Done provisioning $N_UDN UDNs."

# ==========================================
# Wait for Ports in Southbound DB
# ==========================================
echo "Waiting for $EXPECTED_PODS active pods to be 'up' in the Southbound DB..."

while true; do
    # Fetch all ports that are up=true in CSV format, then count lines starting with "pod-"
    UP_COUNT=$(ovn-sbctl -f csv --no-headings --data=bare --columns=logical_port find port_binding up=true | grep -c "^pod-")
    
    if [ "$UP_COUNT" -ge "$EXPECTED_PODS" ]; then
        echo "All $EXPECTED_PODS active pods are up in the SB DB!"
        break
    fi
    sleep 1
done

# ==========================================
# Calculate ovn-controller latency from logs
# ==========================================
echo "Calculating port installation latencies from sandbox/ovn-controller.log..."

python3 - << 'EOF'
import re
from datetime import datetime

log_file = "sandbox/ovn-controller.log"
claim_times = {}
install_times = {}

try:
    with open(log_file, 'r') as f:
        for line in f:
            # Match standard OVS ISO8601 timestamp at the start of the line
            ts_match = re.match(r'^(?P<ts>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+Z)', line)
            if not ts_match:
                continue
                
            ts_str = ts_match.group('ts')
            ts = datetime.strptime(ts_str, "%Y-%m-%dT%H:%M:%S.%fZ")

            # Match our specific pod port names
            port_match = re.search(r'(pod-\d+-\d+)', line)
            if not port_match:
                continue
            port = port_match.group(1)

            # Capture ONLY the first "Claiming lport" and first "ovn-installed" event
            # so latency calculations aren't corrupted if a port is churned/re-claimed
            if "Claiming lport" in line:
                if port not in claim_times:
                    claim_times[port] = ts
            elif "ovn-installed" in line:
                if port not in install_times:
                    install_times[port] = ts

    print(f"\n{'Port Name':<15} {'Claimed':<25} {'Installed':<25} {'Latency (s)':<10}")
    print("-" * 77)
    
    for port, claim_ts in claim_times.items():
        if port in install_times:
            install_ts = install_times[port]
            delta = (install_ts - claim_ts).total_seconds()
            print(f"{port:<15} {claim_ts.strftime('%H:%M:%S.%f')[:-3]:<25} {install_ts.strftime('%H:%M:%S.%f')[:-3]:<25} {delta:<10.3f}")
        else:
            print(f"{port:<15} {claim_ts.strftime('%H:%M:%S.%f')[:-3]:<25} {'NOT INSTALLED':<25} {'N/A':<10}")

except FileNotFoundError:
    print(f"Log file {log_file} not found. Cannot calculate latencies.")
except Exception as e:
    print(f"Error parsing logs: {e}")
EOF

# ==========================================
# Pause before exit
# ==========================================
echo
read -p "Press any key to exit and cleanup..." -n 1 -s -r
echo
