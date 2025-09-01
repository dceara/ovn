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

#include "datapath-legacy-update.h"
#include "openvswitch/hmap.h"

/* Map with (old-type) SB.Datapath_Binding pointers as keys and (new-type)
 * SB.Datapath_Binding pointers as values. */
struct recreated_datapath {
    struct hmap_node node;
    const struct sbrec_datapath_binding *old;
    const struct sbrec_datapath_binding *new;
};

static const struct recreated_datapath *
recreated_datapath_find(const struct hmap *dps,
                        const struct sbrec_datapath_binding *old)
{
    const struct recreated_datapath *rdp;
    HMAP_FOR_EACH_WITH_HASH (rdp, node, hash_pointer(old, 0), dps) {
        if (rdp->old == old) {
            return rdp;
        }
    }
    return NULL;
}

static void
recreated_datapath_add(struct hmap *dps,
                       const struct sbrec_datapath_binding *old,
                       const struct sbrec_datapath_binding *new)
{
    ovs_assert(!recreated_datapath_find(dps, old));

    struct recreated_datapath *rdp = xmalloc(sizeof *rdp);
    *rdp = (struct recreated_datapath) {
        .old = old,
        .new = new,
    };
    hmap_insert(dps, &rdp->node, hash_pointer(old, 0));
}

static void
recreated_datapaths_init(struct hmap *dps)
{
    hmap_init(dps);
}

static void
recreated_datapaths_destroy(struct hmap *dps)
{
    struct recreated_datapath *rdp;
    HMAP_FOR_EACH_POP (rdp, node, dps) {
        sbrec_datapath_binding_delete(rdp->old);
        free(rdp);
    }
}

static void
recreated_datapaths_build(
    struct hmap *dps, const struct hmapx *recreated_sdps,
    const struct sbrec_datapath_binding_table *sb_dp_table)
{
    struct hmapx_node *node;
    HMAPX_FOR_EACH (node, recreated_sdps) {
        const struct sbrec_datapath_binding *old = node->data;

        struct uuid nb_uuid;
        ovs_assert(datapath_get_nb_uuid(old, &nb_uuid));

        const struct sbrec_datapath_binding *new =
            sbrec_datapath_binding_table_get_for_uuid(sb_dp_table, &nb_uuid);
        ovs_assert(new);

        recreated_datapath_add(dps, old, new);
    }
}

static void
reset_recreated_datapaths_refs(const struct hmap *recreated_dps,
                               struct ovsdb_idl *sb_idl)
{
    if (hmap_is_empty(recreated_dps)) {
        return;
    }

    const struct sbrec_port_binding *pb;
    SBREC_PORT_BINDING_FOR_EACH (pb, sb_idl) {
        const struct recreated_datapath *rdp =
            recreated_datapath_find(recreated_dps, pb->datapath);
        if (rdp) {
            sbrec_port_binding_set_datapath(pb, rdp->new);
        }
    }

    const struct sbrec_mac_binding *mb;
    SBREC_MAC_BINDING_FOR_EACH (mb, sb_idl) {
        const struct recreated_datapath *rdp =
            recreated_datapath_find(recreated_dps, mb->datapath);
        if (rdp) {
            sbrec_mac_binding_set_datapath(mb, rdp->new);
        }
    }

    const struct sbrec_igmp_group *ig;
    SBREC_IGMP_GROUP_FOR_EACH (ig, sb_idl) {
        const struct recreated_datapath *rdp =
            recreated_datapath_find(recreated_dps, ig->datapath);
        if (rdp) {
            sbrec_igmp_group_set_datapath(ig, rdp->new);
        }
    }

    const struct sbrec_learned_route *lr;
    SBREC_LEARNED_ROUTE_FOR_EACH (lr, sb_idl) {
        const struct recreated_datapath *rdp =
            recreated_datapath_find(recreated_dps, lr->datapath);
        if (rdp) {
            sbrec_learned_route_set_datapath(lr, rdp->new);
        }
    }
}

void
datapath_legacy_update_run(
    struct hmapx *legacy_sb_dps, struct ovsdb_idl *sb_idl,
    const struct sbrec_datapath_binding_table *sb_dp_table)
{
    struct hmap recreated_datapaths;
    recreated_datapaths_init(&recreated_datapaths);
    recreated_datapaths_build(&recreated_datapaths, legacy_sb_dps,
                              sb_dp_table);
    reset_recreated_datapaths_refs(&recreated_datapaths, sb_idl);
    recreated_datapaths_destroy(&recreated_datapaths);
}
