/* Copyright (c) 2022, Red Hat, Inc.
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

#ifndef OVN_MCAST_H
#define OVN_MCAST_H 1

#include <stdint.h>

#include "lib/ovn-nb-idl.h"
#include "ovs-atomic.h"

/*
 * Multicast snooping and querier per datapath configuration.
 */
struct mcast_switch_config {
    bool enabled;               /* True if snooping enabled. */
    bool querier;               /* True if querier enabled. */
    bool flood_unregistered;    /* True if unregistered multicast should be
                                 * flooded.
                                 */

    int64_t table_size;         /* Max number of IP multicast groups. */
    int64_t idle_timeout;       /* Timeout after which an idle group is
                                 * flushed.
                                 */
    int64_t query_interval;     /* Interval between multicast queries. */
    char *eth_src;              /* ETH src address of the queries. */
    char *ipv4_src;             /* IPv4 src address of the queries. */
    char *ipv6_src;             /* IPv6 src address of the queries. */

    int64_t query_max_response; /* Expected time after which reports should
                                 * be received for queries that were sent out.
                                 */
};

struct mcast_router_config {
    bool relay;        /* True if the router should relay IP multicast. */
};

struct mcast_switch_info {
    const struct mcast_switch_config *cfg;

    bool flood_relay; /* True if the switch is connected to a
                       * multicast router and unregistered multicast
                       * should be flooded to the mrouter. Only
                       * applicable if flood_unregistered == false.
                       */
    bool flood_reports; /* True if the switch has at least one port
                         * configured to flood reports.
                         */
    bool flood_static;  /* True if the switch has at least one port
                         * configured to flood traffic.
                         */

    atomic_uint64_t active_v4_flows; /* Current number of active IPv4 multicast
                                      * flows.
                                      */
    atomic_uint64_t active_v6_flows; /* Current number of active IPv6 multicast
                                      * flows.
                                      */
};

struct mcast_router_info {
    const struct mcast_router_config *cfg;

    bool flood_static; /* True if the router has at least one port configured
                        * to flood traffic.
                        */
};

struct mcast_info {
    union {
        struct mcast_switch_info sw_info;
        struct mcast_router_info rtr_info;
    };
    struct hmap group_tnlids;  /* Group tunnel IDs in use on this DP. */
    uint32_t group_tnlid_hint; /* Hint for allocating next group tunnel ID. */
    struct ovs_list groups;    /* List of groups learnt on this DP. */
};

struct mcast_port_info {
    bool flood;         /* True if the port should flood IP multicast traffic
                         * regardless if it's registered or not. */
    bool flood_reports; /* True if the port should flood IP multicast reports
                         * (e.g., IGMP join/leave). */
};

void init_mcast_switch_config(struct mcast_switch_config *sw_config,
                              const struct nbrec_logical_switch *nbs);
void destroy_mcast_switch_config(struct mcast_switch_config *sw_config);
void init_mcast_router_config(struct mcast_router_config *rtr_config,
                              const struct nbrec_logical_router *nbr);
void init_mcast_switch_info(struct mcast_info *mcast_info,
                            const struct mcast_switch_config *sw_config);
void init_mcast_router_info(struct mcast_info *mcast_info,
                            const struct mcast_router_config *rtr_config);
void destroy_mcast_switch_info(struct mcast_info *mcast_info);
void destroy_mcast_router_info(struct mcast_info *mcast_info);
void init_mcast_port_info(struct mcast_port_info *mcast_info,
                          const struct nbrec_logical_switch_port *nbsp,
                          const struct nbrec_logical_router_port *nbrp);

#endif /* northd/mcast.h */
