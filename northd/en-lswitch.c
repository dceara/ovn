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

void en_lswitch_run(struct engine_node *node, void *data)
{
    struct lswitch_input input_data;
    input_data.nbrec_logical_switch =
        EN_OVSDB_GET(engine_get_input("NB_logical_switch", node));

    lswitch_destroy(data);
    lswitch_init(data);

    lswitch_run(&input_data, data);
    engine_set_node_state(node, EN_UPDATED);
}

void *en_lswitch_init(struct engine_node *node OVS_UNUSED,
                      struct engine_arg *arg OVS_UNUSED)
{
    struct lswitch_data *data = xmalloc(sizeof *data);

    lswitch_init(data);
    return data;
}

void en_lswitch_cleanup(void *data)
{
    lswitch_destroy(data);
}
