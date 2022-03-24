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
