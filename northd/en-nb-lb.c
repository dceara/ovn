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

#include "en-nb-lb.h"
#include "lrouter.h"
#include "lswitch.h"
#include "nb-lb.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(en_nb_lb);

void
en_nb_lb_run(struct engine_node *node, void *data_)
{
    struct nb_lb_data *data = data_;
    struct nb_lb_input input_data;
    struct lswitch_data *lswitch_data = engine_get_input_data("lswitch", node);
    struct lrouter_data *lrouter_data = engine_get_input_data("lrouter", node);

    input_data.northd_logical_switches = &lswitch_data->switches;
    input_data.northd_logical_routers = &lrouter_data->routers;
    input_data.nbrec_load_balancer_table =
        EN_OVSDB_GET(engine_get_input("NB_load_balancer", node));

    nb_lb_destroy(data);
    nb_lb_init(data);

    nb_lb_run(&input_data, data);
    engine_set_node_state(node, EN_UPDATED);
}

void *
en_nb_lb_init(struct engine_node *node OVS_UNUSED,
           struct engine_arg *arg OVS_UNUSED)
{
    struct nb_lb_data *data = xmalloc(sizeof *data);

    nb_lb_init(data);
    return data;
}

void en_nb_lb_cleanup(void *data)
{
    nb_lb_destroy(data);
}

bool
en_nb_lb_nb_load_balancer_handler(struct engine_node *node, void *data_)
{
    struct nb_lb_data *data = data_;
    const struct nbrec_load_balancer_table *lb_table =
        EN_OVSDB_GET(engine_get_input("NB_load_balancer", node));

    const struct nbrec_load_balancer *lb;
    NBREC_LOAD_BALANCER_TABLE_FOR_EACH_TRACKED (lb, lb_table) {
        if (nbrec_load_balancer_is_deleted(lb)) {
            nb_lb_handle_deleted(data, lb);
            continue;
        }

        if (nbrec_load_balancer_is_new(lb)) {
            nb_lb_handle_new(data, lb);
            continue;
        }

        nb_lb_handle_updated(data, lb);
    }
    return true;
}

bool
en_nb_lb_lrouter_handler(struct engine_node *node, void *data_)
{
    struct nb_lb_data *data = data_;

    struct lrouter_data *lrouter_data =
        engine_get_input_data("lrouter", node);

    /* If logical switches were not handled incrementally always fall back
     * to full recompute.
     */
    if (!lrouter_data->change_tracked) {
        return false;
    }

    struct hmapx_node *lr_node;
    HMAPX_FOR_EACH (lr_node, &lrouter_data->new_routers) {
        nb_lb_handle_new_lrouter(data, lr_node->data);
    }
    HMAPX_FOR_EACH (lr_node, &lrouter_data->updated_routers) {
        nb_lb_handle_updated_lrouter(data, lr_node->data);
    }
    HMAPX_FOR_EACH (lr_node, &lrouter_data->deleted_routers) {
        nb_lb_handle_deleted_lrouter(data, lr_node->data);
    }
    return true;
}

bool
en_nb_lb_lswitch_handler(struct engine_node *node, void *data_)
{
    struct nb_lb_data *data = data_;

    struct lswitch_data *lswitch_data =
        engine_get_input_data("lswitch", node);

    /* If logical switches were not handled incrementally always fall back
     * to full recompute.
     */
    if (!lswitch_data->change_tracked) {
        return false;
    }

    struct hmapx_node *ls_node;
    HMAPX_FOR_EACH (ls_node, &lswitch_data->new_switches) {
        nb_lb_handle_new_lswitch(data, ls_node->data);
    }
    HMAPX_FOR_EACH (ls_node, &lswitch_data->updated_switches) {
        nb_lb_handle_updated_lswitch(data, ls_node->data);
    }
    HMAPX_FOR_EACH (ls_node, &lswitch_data->deleted_switches) {
        nb_lb_handle_deleted_lswitch(data, ls_node->data);
    }
    return true;
}
