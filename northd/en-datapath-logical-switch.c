/*
 * Copyright (c) 2025, Red Hat, Inc.
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

#include "openvswitch/hmap.h"
#include "openvswitch/util.h"
#include "openvswitch/vlog.h"

#include "inc-proc-eng.h"
#include "ovn-nb-idl.h"
#include "ovn-sb-idl.h"
#include "datapath_sync.h"
#include "en-datapath-logical-switch.h"
#include "en-global-config.h"
#include "ovn-util.h"

#define LS_SB_KEY_NAME "logical-switch"

VLOG_DEFINE_THIS_MODULE(en_datapath_logical_switch);

void *
en_datapath_logical_switch_init(struct engine_node *node OVS_UNUSED,
                                struct engine_arg *args OVS_UNUSED)
{
    struct ovn_unsynced_datapath_map *map = xzalloc(sizeof *map);
    ovn_unsynced_datapath_map_init(map, LS_SB_KEY_NAME);
    return map;
}

void
en_datapath_logical_switch_run(struct engine_node *node , void *data)
{
    const struct nbrec_logical_switch_table *nb_ls_table =
        EN_OVSDB_GET(engine_get_input("NB_logical_switch", node));
    const struct ed_type_global_config *global_config =
        engine_get_input_data("global_config", node);

    struct ovn_unsynced_datapath_map *map = data;

    ovn_unsynced_datapath_map_destroy(map);
    ovn_unsynced_datapath_map_init(map, LS_SB_KEY_NAME);

    const struct nbrec_logical_switch *nbs;
    NBREC_LOGICAL_SWITCH_TABLE_FOR_EACH (nbs, nb_ls_table) {
        uint32_t requested_tunnel_key = smap_get_int(&nbs->other_config,
                                                     "requested-tnl-key", 0);
        const char *ts = smap_get(&nbs->other_config, "interconn-ts");

        if (!ts && global_config->vxlan_mode &&
            requested_tunnel_key >= 1 << 12) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 1);
            VLOG_WARN_RL(&rl, "Tunnel key %"PRIu32" for datapath %s is "
                         "incompatible with VXLAN", requested_tunnel_key,
                         nbs->name);
            requested_tunnel_key = 0;
        }

        struct ovn_unsynced_datapath *dp;
        dp = ovn_unsynced_datapath_alloc(nbs->name, requested_tunnel_key,
                                         &nbs->header_);

        smap_init(&dp->external_ids);
        smap_add_format(&dp->external_ids, LS_SB_KEY_NAME, UUID_FMT,
                        UUID_ARGS(&nbs->header_.uuid));
        smap_add(&dp->external_ids, "name", dp->name);
        const char *neutron_network = smap_get(&nbs->other_config,
                                               "neutron:network_name");
        if (neutron_network && neutron_network[0]) {
            smap_add(&dp->external_ids, "name2", neutron_network);
        }
        int64_t ct_zone_limit = ovn_smap_get_llong(&nbs->other_config,
                                                   "ct-zone-limit", -1);
        if (ct_zone_limit > 0) {
            smap_add_format(&dp->external_ids, "ct-zone-limit", "%"PRId64,
                            ct_zone_limit);
        }

        if (ts) {
            smap_add(&dp->external_ids, "interconn-ts", ts);
        }

        uint32_t age_threshold = smap_get_uint(&nbs->other_config,
                                               "fdb_age_threshold", 0);
        if (age_threshold) {
            smap_add_format(&dp->external_ids, "fdb_age_threshold",
                            "%u", age_threshold);
        }

        hmap_insert(&map->dps, &dp->hmap_node, uuid_hash(&nbs->header_.uuid));
    }

    engine_set_node_state(node, EN_UPDATED);
}


void
en_datapath_logical_switch_cleanup(void *data)
{
    struct ovn_unsynced_datapath_map *map = data;
    ovn_unsynced_datapath_map_destroy(map);
}

static void
synced_logical_switch_map_init(
    struct ovn_synced_logical_switch_map *switch_map)
{
    hmap_init(&switch_map->synced_switches);
}

static void
synced_logical_switch_map_destroy(
    struct ovn_synced_logical_switch_map *switch_map)
{
    hmap_destroy(&switch_map->synced_switches);
}

void *
en_datapath_synced_logical_switch_init(struct engine_node *node OVS_UNUSED,
                                      struct engine_arg *args OVS_UNUSED)
{
    struct ovn_synced_logical_switch_map *switch_map;
    switch_map = xzalloc(sizeof *switch_map);
    synced_logical_switch_map_init(switch_map);

    return switch_map;
}

void
en_datapath_synced_logical_switch_run(struct engine_node *node , void *data)
{
    const struct ovn_synced_datapaths *dps =
        engine_get_input_data("datapath_sync", node);
    struct ovn_synced_logical_switch_map *switch_map = data;

    synced_logical_switch_map_destroy(switch_map);
    synced_logical_switch_map_init(switch_map);

    struct ovn_synced_datapath *sdp;
    LIST_FOR_EACH (sdp, list_node, &dps->synced_dps) {
        struct uuid key;
        if (!smap_get_uuid(&sdp->sb_dp->external_ids, LS_SB_KEY_NAME, &key)) {
            continue;
        }
        struct ovn_synced_logical_switch *lsw = xzalloc(sizeof *lsw);
        lsw->nb = CONTAINER_OF(sdp->nb_row, struct nbrec_logical_switch,
                               header_);
        lsw->sb = sdp->sb_dp;
        hmap_insert(&switch_map->synced_switches, &lsw->hmap_node,
                    uuid_hash(&key));
    }

    engine_set_node_state(node, EN_UPDATED);
}

void en_datapath_synced_logical_switch_cleanup(void *data)
{
    struct ovn_synced_logical_switch_map *switch_map = data;
    synced_logical_switch_map_destroy(switch_map);
}
