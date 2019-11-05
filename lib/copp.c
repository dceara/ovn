/* Copyright (c) 2019, Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#include <stdlib.h>

#include "openvswitch/shash.h"
#include "smap.h"
#include "lib/ovn-nb-idl.h"
#include "lib/copp.h"

static char *copp_proto_names[COPP_PROTO_MAX] = {
    [COPP_ARP]           = "arp",
    [COPP_ARP_RESOLVE]   = "arp-resolve",
    [COPP_DHCPV4_OPTS]   = "dhcpv4-opts",
    [COPP_DHCPV6_OPTS]   = "dhcpv6-opts",
    [COPP_DNS]           = "dns",
    [COPP_EVENT_ELB]     = "event-elb",
    [COPP_ICMP4_ERR]     = "icmp4-error",
    [COPP_ICMP6_ERR]     = "icmp6-error",
    [COPP_IGMP]          = "igmp",
    [COPP_ND_NA]         = "nd-na",
    [COPP_ND_NS]         = "nd-ns",
    [COPP_ND_NS_RESOLVE] = "nd-ns-resolve",
    [COPP_ND_RA_OPTS]    = "nd-ra-opts",
    [COPP_TCP_RESET]     = "tcp-reset",
};

static bool copp_port_support[COPP_PROTO_MAX] = {
    [COPP_DHCPV4_OPTS] = true,
    [COPP_DHCPV6_OPTS] = true,
    [COPP_ICMP4_ERR]   = true,
    [COPP_ICMP6_ERR]   = true,
    [COPP_ND_RA_OPTS]  = true,
    [COPP_TCP_RESET]   = true,
};

/* Return true if the copp meter can be configured on a logical port. Return
 * false if the meter is only supported on a logical switch/router.
 */
bool copp_port_meter_supported(enum copp_proto proto)
{
    if (proto >= COPP_PROTO_MAX) {
        return false;
    }

    return copp_port_support[proto];
}

const char *
copp_proto_get(enum copp_proto proto)
{
    if (proto >= COPP_PROTO_MAX) {
        return "<Invalid control protocol ID>";
    }
    return copp_proto_names[proto];
}

const char *
copp_meter_get(enum copp_proto proto, const struct nbrec_copp *copp,
               const struct shash *meter_groups)
{
    if (!copp || proto >= COPP_PROTO_MAX) {
        return NULL;
    }

    const char *meter = smap_get(&copp->meters, copp_proto_names[proto]);

    if (meter && shash_find(meter_groups, meter)) {
        return meter;
    }

    return NULL;
}

const char *
copp_port_meter_get(enum copp_proto proto, const struct nbrec_copp *port_copp,
                    const struct nbrec_copp *dp_copp,
                    const struct shash *meter_groups)
{
    const char *meter = copp_meter_get(proto, port_copp, meter_groups);

    if (!meter) {
        return copp_meter_get(proto, dp_copp, meter_groups);
    }
    return meter;
}
