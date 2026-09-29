#!/bin/bash

# Reports the time from "Claiming lport" to ovn-installed-ts for each
# OVN logical switch port.  Results are sorted descending by install time.
#
# Run this from inside the OVN sandbox shell where OVS_RUNDIR is already
# set.

usage() {
    echo "Usage: $0 [--log <path>]"
    echo ""
    echo "Options:"
    echo "  --log <path>  Path to the ovn-controller log file."
    echo "                Default: tutorial/sandbox/ovn-controller.log"
    echo ""
    echo "  --help, --usage  Show this help message."
    exit 0
}

LOG="tutorial/sandbox/ovn-controller.log"

while [ $# -gt 0 ]; do
    case "$1" in
        --log)
            shift
            LOG="$1"
            ;;
        --help|--usage)
            usage
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            ;;
    esac
    shift
done

if [ ! -f "$LOG" ]; then
    echo "Error: log file not found: $LOG" >&2
    exit 1
fi

# Collect ovs-vsctl output for all interfaces with iface-id set.
vsctl_output=$(ovs-vsctl --columns=name,external_ids \
    find interface 'external_ids:iface-id!=""' 2>/dev/null)

if [ -z "$vsctl_output" ]; then
    echo "No ports with iface-id found."
    exit 0
fi

# Parse multi-line records from ovs-vsctl output.  Records are separated
# by blank lines.  Each record has a "name" line and an "external_ids" line.
results=""

current_name=""
current_extids=""

process_record() {
    local name="$1"
    local extids="$2"

    # Strip quotes from name.
    name=$(echo "$name" | sed 's/^"//;s/"$//')

    # Extract iface-id (handle both quoted and unquoted values).
    local iface_id
    iface_id=$(echo "$extids" | grep -o 'iface-id=[^ ,}]*' \
        | sed 's/^iface-id=//;s/^"//;s/"$//')
    if [ -z "$iface_id" ]; then
        return
    fi

    # Extract ovn-installed-ts (handle both quoted and unquoted values).
    local installed_ts
    installed_ts=$(echo "$extids" | grep -o 'ovn-installed-ts=[^ ,}]*' \
        | sed 's/^ovn-installed-ts=//;s/^"//;s/"$//')
    if [ -z "$installed_ts" ]; then
        return
    fi

    # Convert installed_ts (epoch ms) to human-readable ISO 8601.
    local epoch_s=$((installed_ts / 1000))
    local epoch_ms=$((installed_ts % 1000))
    local installed_hr
    installed_hr=$(date -u -d "@${epoch_s}" "+%Y-%m-%dT%H:%M:%S" 2>/dev/null)
    installed_hr=$(printf "%s.%03dZ" "$installed_hr" "$epoch_ms")

    # Find the first "Claiming lport" log entry for this iface_id.
    local claim_line
    claim_line=$(grep -m1 "Claiming lport ${iface_id} for this chassis\\." \
        "$LOG" 2>/dev/null)
    if [ -z "$claim_line" ]; then
        echo "Warning: no 'Claiming lport' entry found for ${iface_id}" >&2
        return
    fi

    # Extract the timestamp from the log line (first '|'-delimited field).
    local claim_ts
    claim_ts=$(echo "$claim_line" | cut -d'|' -f1)

    # Parse the claim timestamp into epoch milliseconds.
    # Format: 2026-09-29T10:37:19.781Z
    local claim_date claim_frac claim_epoch_s claim_epoch_ms claim_ts_ms
    claim_date=$(echo "$claim_ts" | sed 's/\.[0-9]*Z$//')
    claim_frac=$(echo "$claim_ts" | grep -o '\.[0-9]*Z$' \
        | sed 's/^\.//;s/Z$//')
    claim_epoch_s=$(date -u -d "${claim_date}" "+%s" 2>/dev/null)
    claim_epoch_ms=${claim_frac:-0}
    # Remove leading zeros for arithmetic.
    claim_epoch_ms=$((10#${claim_epoch_ms}))
    claim_ts_ms=$(( claim_epoch_s * 1000 + claim_epoch_ms ))

    # Format the claim timestamp in the same ISO 8601 format.
    local claim_hr
    claim_hr=$(printf "%s.%03dZ" \
        "$(date -u -d "@${claim_epoch_s}" "+%Y-%m-%dT%H:%M:%S")" \
        "$claim_epoch_ms")

    # Compute install time.
    local install_time_ms=$((installed_ts - claim_ts_ms))

    results="${results}${install_time_ms} ${iface_id} ${claim_hr} ${installed_hr}
"
}

while IFS= read -r line; do
    # Skip empty lines (record separators).
    if [ -z "$line" ]; then
        if [ -n "$current_name" ] && [ -n "$current_extids" ]; then
            process_record "$current_name" "$current_extids"
        fi
        current_name=""
        current_extids=""
        continue
    fi

    case "$line" in
        name*)
            current_name=$(echo "$line" | sed 's/^name[[:space:]]*:[[:space:]]*//')
            ;;
        external_ids*)
            current_extids=$(echo "$line" \
                | sed 's/^external_ids[[:space:]]*:[[:space:]]*//')
            ;;
    esac
done <<< "$vsctl_output"

# Process the last record (no trailing blank line).
if [ -n "$current_name" ] && [ -n "$current_extids" ]; then
    process_record "$current_name" "$current_extids"
fi

if [ -z "$results" ]; then
    echo "No ports with ovn-installed-ts found."
    exit 0
fi

# Sort descending by install time and print a formatted table.
{
    echo "INSTALL_TIME_MS IFACE_ID CLAIMED_AT INSTALLED_AT"
    echo "$results" | sort -t' ' -k1 -nr
} | column -t
