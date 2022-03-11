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

#include "en-lswitch.h"
#include "lswitch.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(en_lswitch);

void en_lswitch_run(struct engine_node *node, void *data_)
{
    struct lswitch_data *data = data_;
    struct lswitch_input input_data;
    input_data.nbrec_logical_switch =
        EN_OVSDB_GET(engine_get_input("NB_logical_switch", node));

    lswitch_destroy(data);
    lswitch_init(data);

    lswitch_run(&input_data, data);
    data->change_tracked = false;
    engine_set_node_state(node, EN_UPDATED);
}

void *en_lswitch_init(struct engine_node *node OVS_UNUSED,
                      struct engine_arg *arg OVS_UNUSED)
{
    struct lswitch_data *data = xmalloc(sizeof *data);

    lswitch_init(data);
    return data;
}

void en_lswitch_clear_tracked_data(void *data_)
{
    struct lswitch_data *data = data_;
    data->change_tracked = false;
    lswitch_clear_tracked(data);
}

void en_lswitch_cleanup(void *data)
{
    en_lswitch_clear_tracked_data(data);
    lswitch_destroy(data);
}

bool
en_lswitch_nb_logical_switch_handler(struct engine_node *node,
                                     void *data_)
{
    struct lswitch_data *data = data_;
    const struct nbrec_logical_switch_table *ls_table =
        EN_OVSDB_GET(engine_get_input("NB_logical_switch", node));

    const struct nbrec_logical_switch *ls;
    NBREC_LOGICAL_SWITCH_TABLE_FOR_EACH_TRACKED (ls, ls_table) {
        if (nbrec_logical_switch_is_deleted(ls)) {
            lswitch_track_deleted(data, ls);
            continue;
        }

        if (nbrec_logical_switch_is_new(ls)) {
            lswitch_track_new(data, ls);
            continue;
        }

        lswitch_track_updated(data, ls);
    }
    if (!hmapx_is_empty(&data->new_switches)
            || !hmapx_is_empty(&data->updated_switches)
            || !hmapx_is_empty(&data->deleted_switches)) {
        engine_set_node_state(node, EN_UPDATED);
    }
    data->change_tracked = true;
    return true;
}
