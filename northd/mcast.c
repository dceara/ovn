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

#include <config.h>

#include "ip-mcast-index.h"
#include "mcast.h"
#include "mcast-group-index.h"
#include "ovn-util.h"

void
init_mcast_switch_config(struct mcast_switch_config *sw_config,
                         const struct nbrec_logical_switch *nbs)
{
    sw_config->enabled =
        smap_get_bool(&nbs->other_config, "mcast_snoop", false);
    sw_config->querier =
        smap_get_bool(&nbs->other_config, "mcast_querier", true);
    sw_config->flood_unregistered =
        smap_get_bool(&nbs->other_config, "mcast_flood_unregistered",
                      false);

    sw_config->table_size =
        smap_get_ullong(&nbs->other_config, "mcast_table_size",
                        OVN_MCAST_DEFAULT_MAX_ENTRIES);

    uint32_t idle_timeout =
        smap_get_ullong(&nbs->other_config, "mcast_idle_timeout",
                        OVN_MCAST_DEFAULT_IDLE_TIMEOUT_S);
    if (idle_timeout < OVN_MCAST_MIN_IDLE_TIMEOUT_S) {
        idle_timeout = OVN_MCAST_MIN_IDLE_TIMEOUT_S;
    } else if (idle_timeout > OVN_MCAST_MAX_IDLE_TIMEOUT_S) {
        idle_timeout = OVN_MCAST_MAX_IDLE_TIMEOUT_S;
    }
    sw_config->idle_timeout = idle_timeout;

    uint32_t query_interval =
        smap_get_ullong(&nbs->other_config, "mcast_query_interval",
                        sw_config->idle_timeout / 2);
    if (query_interval < OVN_MCAST_MIN_QUERY_INTERVAL_S) {
        query_interval = OVN_MCAST_MIN_QUERY_INTERVAL_S;
    } else if (query_interval > OVN_MCAST_MAX_QUERY_INTERVAL_S) {
        query_interval = OVN_MCAST_MAX_QUERY_INTERVAL_S;
    }
    sw_config->query_interval = query_interval;

    sw_config->eth_src =
        nullable_xstrdup(smap_get(&nbs->other_config, "mcast_eth_src"));
    sw_config->ipv4_src =
        nullable_xstrdup(smap_get(&nbs->other_config, "mcast_ip4_src"));
    sw_config->ipv6_src =
        nullable_xstrdup(smap_get(&nbs->other_config, "mcast_ip6_src"));

    sw_config->query_max_response =
        smap_get_ullong(&nbs->other_config, "mcast_query_max_response",
                        OVN_MCAST_DEFAULT_QUERY_MAX_RESPONSE_S);
}

void
destroy_mcast_switch_config(struct mcast_switch_config *sw_config)
{
    free(sw_config->eth_src);
    free(sw_config->ipv4_src);
    free(sw_config->ipv6_src);
}

void
init_mcast_router_config(struct mcast_router_config *rtr_config,
                         const struct nbrec_logical_router *nbr)
{
    rtr_config->relay = smap_get_bool(&nbr->options, "mcast_relay", false);
}

static void
init_mcast_info(struct mcast_info *mcast_info)
{
    hmap_init(&mcast_info->group_tnlids);
    mcast_info->group_tnlid_hint = OVN_MIN_IP_MULTICAST;
    ovs_list_init(&mcast_info->groups);
}

static void
destroy_mcast_info(struct mcast_info *mcast_info)
{
    ovn_destroy_tnlids(&mcast_info->group_tnlids);
}

void
init_mcast_switch_info(struct mcast_info *mcast_info,
                       const struct mcast_switch_config *sw_config)
{
    struct mcast_switch_info *sw_info = &mcast_info->sw_info;

    init_mcast_info(mcast_info);
    sw_info->cfg = sw_config;
    sw_info->flood_relay = false;
    sw_info->active_v4_flows = ATOMIC_VAR_INIT(0);
    sw_info->active_v6_flows = ATOMIC_VAR_INIT(0);
}

void
destroy_mcast_switch_info(struct mcast_info *mcast_info)
{
    destroy_mcast_info(mcast_info);
}

void
init_mcast_router_info(struct mcast_info *mcast_info,
                       const struct mcast_router_config *rtr_config)
{
    struct mcast_router_info *rtr_info = &mcast_info->rtr_info;

    init_mcast_info(mcast_info);
    rtr_info->cfg = rtr_config;
    rtr_info->flood_static = false;
}

void
destroy_mcast_router_info(struct mcast_info *mcast_info)
{
    destroy_mcast_info(mcast_info);
}

void
init_mcast_port_info(struct mcast_port_info *mcast_info,
                     const struct nbrec_logical_switch_port *nbsp,
                     const struct nbrec_logical_router_port *nbrp)
{
    if (nbsp) {
        mcast_info->flood =
            smap_get_bool(&nbsp->options, "mcast_flood", false);
        mcast_info->flood_reports =
            smap_get_bool(&nbsp->options, "mcast_flood_reports",
                          false);
    } else if (nbrp) {
        /* We don't process multicast reports in any special way on logical
         * routers so just treat them as regular multicast traffic.
         */
        mcast_info->flood =
            smap_get_bool(&nbrp->options, "mcast_flood", false);
        mcast_info->flood_reports = mcast_info->flood;
    }
}
