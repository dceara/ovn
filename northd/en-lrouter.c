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

#include "en-lrouter.h"
#include "lrouter.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(en_lrouter);

void en_lrouter_run(struct engine_node *node, void *data_)
{
    struct lrouter_data *data = data_;
    struct lrouter_input input_data;
    input_data.nbrec_logical_router =
        EN_OVSDB_GET(engine_get_input("NB_logical_router", node));

    lrouter_destroy(data);
    lrouter_init(data);

    lrouter_run(&input_data, data);
    data->change_tracked = false;
    engine_set_node_state(node, EN_UPDATED);
}

void *en_lrouter_init(struct engine_node *node OVS_UNUSED,
                      struct engine_arg *arg OVS_UNUSED)
{
    struct lrouter_data *data = xmalloc(sizeof *data);

    lrouter_init(data);
    return data;
}

void en_lrouter_clear_tracked_data(void *data_)
{
    struct lrouter_data *data = data_;
    data->change_tracked = false;
    lrouter_clear_tracked(data);
}

void en_lrouter_cleanup(void *data)
{
    en_lrouter_clear_tracked_data(data);
    lrouter_destroy(data);
}

bool
en_lrouter_nb_logical_router_handler(struct engine_node *node,
                                     void *data_)
{
    struct lrouter_data *data = data_;
    const struct nbrec_logical_router_table *lr_table =
        EN_OVSDB_GET(engine_get_input("NB_logical_router", node));

    const struct nbrec_logical_router *lr;
    NBREC_LOGICAL_ROUTER_TABLE_FOR_EACH_TRACKED (lr, lr_table) {
        if (nbrec_logical_router_is_deleted(lr)) {
            lrouter_track_deleted(data, lr);
            continue;
        }

        if (nbrec_logical_router_is_new(lr)) {
            lrouter_track_new(data, lr);
            continue;
        }

        lrouter_track_updated(data, lr);
    }
    if (!hmapx_is_empty(&data->new_routers)
            || !hmapx_is_empty(&data->updated_routers)
            || !hmapx_is_empty(&data->deleted_routers)) {
        engine_set_node_state(node, EN_UPDATED);
    }
    data->change_tracked = true;
    return true;
}
