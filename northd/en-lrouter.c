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

void en_lrouter_run(struct engine_node *node, void *data)
{
    struct lrouter_input input_data;
    input_data.nbrec_logical_router =
        EN_OVSDB_GET(engine_get_input("NB_logical_router", node));

    lrouter_destroy(data);
    lrouter_init(data);

    lrouter_run(&input_data, data);
    engine_set_node_state(node, EN_UPDATED);
}

void *en_lrouter_init(struct engine_node *node OVS_UNUSED,
                      struct engine_arg *arg OVS_UNUSED)
{
    struct lrouter_data *data = xmalloc(sizeof *data);

    lrouter_init(data);
    return data;
}

void en_lrouter_cleanup(void *data)
{
    lrouter_destroy(data);
}
