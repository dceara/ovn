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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "lswitch.h"
#include "northd.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(lswitch)

static void
init_northd_logical_switch(struct northd_logical_switch *nls)
{
    char uuid_s[UUID_LEN + 1];
    sprintf(uuid_s, UUID_FMT, UUID_ARGS(&nls->nbs->header_.uuid));
    init_ipam_config(&nls->ipam_config, &nls->nbs->other_config, uuid_s);

    init_mcast_switch_config(&nls->mcast_config, nls->nbs);
}

static void
uninit_northd_logical_switch(struct northd_logical_switch *nls)
{
    destroy_ipam_config(&nls->ipam_config);
    destroy_mcast_switch_config(&nls->mcast_config);
}

static struct northd_logical_switch *
create_northd_logical_switch(struct lswitch_data *data,
                             const struct nbrec_logical_switch *nbs)
{
    struct northd_logical_switch *nls = xzalloc(sizeof *nls);
    nls->nbs = nbs;
    init_northd_logical_switch(nls);

    hmap_insert(&data->switches, &nls->node, uuid_hash(&nbs->header_.uuid));
    return nls;
}

static void
delete_northd_logical_switch(struct lswitch_data *data,
                             struct northd_logical_switch *nls)
{
    hmap_remove(&data->switches, &nls->node);
}

static void
destroy_northd_logical_switch(struct northd_logical_switch *nls)
{
    if (!nls) {
        return;
    }

    uninit_northd_logical_switch(nls);
    northd_detach_logical_switch(nls);
    free(nls);
}

static struct northd_logical_switch *
find_northd_logical_switch(struct lswitch_data *data,
                           const struct nbrec_logical_switch *nbs)
{
    struct northd_logical_switch *ls;

    HMAP_FOR_EACH_WITH_HASH (ls, node, uuid_hash(&nbs->header_.uuid),
                             &data->switches) {
        if (nbs == ls->nbs) {
            return ls;
        }
    }
    return NULL;
}

void lswitch_init(struct lswitch_data *data)
{
    hmap_init(&data->switches);
    hmapx_init(&data->new_switches);
    hmapx_init(&data->deleted_switches);
    hmapx_init(&data->updated_switches);
}

void lswitch_destroy(struct lswitch_data *data)
{
    struct northd_logical_switch *ls;
    struct northd_logical_switch *ls_next;
    HMAP_FOR_EACH_SAFE (ls, ls_next, node, &data->switches) {
        delete_northd_logical_switch(data, ls);
        destroy_northd_logical_switch(ls);
    }
    hmap_destroy(&data->switches);
    hmapx_destroy(&data->new_switches);
    hmapx_destroy(&data->updated_switches);
    hmapx_destroy(&data->deleted_switches);
}

void lswitch_run(struct lswitch_input *input_data, struct lswitch_data *data)
{
    const struct nbrec_logical_switch *nbs;

    NBREC_LOGICAL_SWITCH_TABLE_FOR_EACH (nbs,
                                         input_data->nbrec_logical_switch) {
        create_northd_logical_switch(data, nbs);
    }
}

void
lswitch_clear_tracked(struct lswitch_data *data)
{
    hmapx_clear(&data->new_switches);
    hmapx_clear(&data->updated_switches);

    struct hmapx_node *node;
    struct hmapx_node *next;
    HMAPX_FOR_EACH_SAFE (node, next, &data->deleted_switches) {
        destroy_northd_logical_switch(node->data);
        hmapx_delete(&data->deleted_switches, node);
    }
}

void
lswitch_track_new(struct lswitch_data *data,
                  const struct nbrec_logical_switch *nbs)
{
    struct northd_logical_switch *nls
        = create_northd_logical_switch(data, nbs);
    hmapx_add(&data->new_switches, nls);
}

void
lswitch_track_updated(struct lswitch_data *data,
                      const struct nbrec_logical_switch *nbs)
{
    struct northd_logical_switch *nls
        = find_northd_logical_switch(data, nbs);
    uninit_northd_logical_switch(nls);
    init_northd_logical_switch(nls);
    hmapx_add(&data->updated_switches, nls);
}

void
lswitch_track_deleted(struct lswitch_data *data,
                      const struct nbrec_logical_switch *nbs)
{
    struct northd_logical_switch *nls
        = find_northd_logical_switch(data, nbs);
    hmap_remove(&data->switches, &nls->node);
    hmapx_add(&data->deleted_switches, nls);
}
