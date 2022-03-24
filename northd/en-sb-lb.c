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

#include "en-sb-lb.h"
#include "northd.h"
#include "nb-lb.h"
#include "sb-lb.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(en_sb_lb);

void
en_sb_lb_run(struct engine_node *node, void *data_)
{
    struct sb_lb_data *data = data_;
    struct sb_lb_input input_data;
    struct nb_lb_data *nb_lb_data = engine_get_input_data("nb-lb", node);
    const struct engine_context *eng_ctx = engine_get_context();

    input_data.nb_lbs = &nb_lb_data->lbs;
    input_data.sbrec_load_balancer_table =
        EN_OVSDB_GET(engine_get_input("SB_load_balancer", node));

    sb_lb_destroy(data);
    sb_lb_init(data);

    sb_lb_run(&input_data, data, eng_ctx->ovnsb_idl_txn);
    engine_set_node_state(node, EN_UPDATED);
}

void *
en_sb_lb_init(struct engine_node *node OVS_UNUSED,
           struct engine_arg *arg OVS_UNUSED)
{
    struct sb_lb_data *data = xmalloc(sizeof *data);

    sb_lb_init(data);
    return data;
}

void en_sb_lb_cleanup(void *data)
{
    sb_lb_destroy(data);
}
