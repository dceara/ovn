/*
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

#include "hmapx.h"
#include "lib/lb.h"
#include "lib/ovn-nb-idl.h"
#include "lib/ovn-sb-idl.h"
#include "lswitch.h"
#include "sb-lb.h"
#include "util.h"
#include "northd.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(sb_lb)

void
sb_lb_init(struct sb_lb_data *data OVS_UNUSED)
{
}

void
sb_lb_destroy(struct sb_lb_data *data OVS_UNUSED)
{
}

static bool
sb_lb_needs_update(const struct ovn_northd_lb *lb,
                   const struct sbrec_load_balancer *slb)
{
    if (strcmp(lb->nlb->name, slb->name)) {
        return true;
    }

    if (!smap_equal(&lb->nlb->vips, &slb->vips)) {
        return true;
    }

    if ((lb->nlb->protocol && !slb->protocol)
            || (!lb->nlb->protocol && slb->protocol)) {
        return true;
    }

    if (lb->nlb->protocol && slb->protocol
            && strcmp(lb->nlb->protocol, slb->protocol)) {
        return true;
    }

    if (!smap_get_bool(&slb->options, "hairpin_orig_tuple", false)) {
        return true;
    }

    if (strcmp(smap_get_def(&lb->nlb->options, "hairpin_snat_ip", ""),
               smap_get_def(&slb->options, "hairpin_snat_ip", ""))) {
        return true;
    }

    if (hmapx_count(&lb->nb_ls) != slb->n_datapaths) {
        return true;
    }

    struct hmapx nb_datapaths = HMAPX_INITIALIZER(&nb_datapaths);
    struct hmapx_node *node;
    HMAPX_FOR_EACH (node, &lb->nb_ls) {
        struct northd_logical_switch *ls = node->data;
        hmapx_add(&nb_datapaths,
                  CONST_CAST(void *, northd_get_sb_datapath(ls->od)));
    }

    bool stale = false;
    for (size_t i = 0; i < slb->n_datapaths; i++) {
        if (!hmapx_contains(&nb_datapaths, slb->datapaths[i])) {
            stale = true;
            break;
        }
    }
    hmapx_destroy(&nb_datapaths);
    return stale;
}

void
sb_lb_run(struct sb_lb_input *input_data, struct sb_lb_data *data OVS_UNUSED,
          struct ovsdb_idl_txn *ovnsb_txn)
{
    struct ovn_northd_lb *lb;

    /* Delete any stale SB load balancer rows. */
    struct hmapx existing_lbs = HMAPX_INITIALIZER(&existing_lbs);
    const struct sbrec_load_balancer *sbrec_lb, *next;
    SBREC_LOAD_BALANCER_TABLE_FOR_EACH_SAFE (sbrec_lb, next,
                            input_data->sbrec_load_balancer_table) {
        const char *nb_lb_uuid = smap_get(&sbrec_lb->external_ids, "lb_id");
        struct uuid lb_uuid;
        if (!nb_lb_uuid || !uuid_from_string(&lb_uuid, nb_lb_uuid)) {
            sbrec_load_balancer_delete(sbrec_lb);
            continue;
        }

        /* Delete any SB load balancer entries that refer to NB load balancers
         * that don't exist anymore or are not applied to switches anymore.
         *
         * There is also a special case in which duplicate LBs might be created
         * in the SB, e.g., due to the fact that OVSDB only ensures
         * "at-least-once" consistency for clustered database tables that
         * are not indexed in any way.
         */
        lb = ovn_northd_lb_find(input_data->nb_lbs, &lb_uuid);
        if (!lb || !hmapx_count(&lb->nb_ls) || !hmapx_add(&existing_lbs,
                                              CONST_CAST(void *, lb))) {
            sbrec_load_balancer_delete(sbrec_lb);
        } else {
            lb->slb = sbrec_lb;
        }
    }
    hmapx_destroy(&existing_lbs);

    /* Create SB Load balancer records if not present and sync
     * the SB load balancer columns. */
    HMAP_FOR_EACH (lb, hmap_node, input_data->nb_lbs) {

        if (!hmapx_count(&lb->nb_ls)) {
            continue;
        }

        sbrec_lb = lb->slb;
        if (!sbrec_lb) {
            sbrec_lb = sbrec_load_balancer_insert(ovnsb_txn);
            lb->slb = sbrec_lb;
            char *lb_id = xasprintf(
                UUID_FMT, UUID_ARGS(&lb->nlb->header_.uuid));
            const struct smap external_ids =
                SMAP_CONST1(&external_ids, "lb_id", lb_id);
            sbrec_load_balancer_set_external_ids(sbrec_lb, &external_ids);
            free(lb_id);
        }

        /* Only update SB.Load_Balancer columns if needed. */
        if (!lb->slb || sb_lb_needs_update(lb, sbrec_lb)) {
            lb->slb = sbrec_lb;
            sbrec_load_balancer_set_name(lb->slb, lb->nlb->name);
            sbrec_load_balancer_set_vips(lb->slb, &lb->nlb->vips);
            sbrec_load_balancer_set_protocol(lb->slb, lb->nlb->protocol);

            struct sbrec_datapath_binding **lb_dps =
                xmalloc(hmapx_count(&lb->nb_ls) * sizeof *lb_dps);
            struct hmapx_node *node;
            size_t i = 0;
            HMAPX_FOR_EACH (node, &lb->nb_ls) {
                struct northd_logical_switch *ls = node->data;
                lb_dps[i++] =
                    CONST_CAST(struct sbrec_datapath_binding *,
                               northd_get_sb_datapath(ls->od));
            }
            sbrec_load_balancer_set_datapaths(lb->slb, lb_dps,
                                              hmapx_count(&lb->nb_ls));
            free(lb_dps);

            /* Store the fact that northd provides the original (destination
             * IP + transport port) tuple.
             */
            struct smap options;
            smap_clone(&options, &lb->nlb->options);
            smap_add(&options, "hairpin_orig_tuple", "true");
            const char *hairpin_snat_ip =
                smap_get(&lb->nlb->options, "hairpin_snat_ip");
            if (hairpin_snat_ip) {
                smap_add(&options, "hairpin_snat_ip", hairpin_snat_ip);
            }
            sbrec_load_balancer_set_options(lb->slb, &options);
            smap_destroy(&options);
        }

        /* XXX: Awkward. */
        lb->slb = NULL;
    }
}
