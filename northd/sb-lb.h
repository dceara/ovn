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

#ifndef OVN_SB_LB_H
#define OVN_SB_LB_H 1

#include "openvswitch/hmap.h"

struct sb_lb_input {
    /* Input data for 'en-nb-lb'. */

    /* en-lb references */
    /* XXX: this should be const but we keep it as is for now to avoid
     * complex changes when syncing LBs to the Southbound.
     */
    struct hmap *nb_lbs;

    /* Datapath references from en-northd. */
    const struct hmap *datapaths;

    /* Southbound table references */
    const struct sbrec_load_balancer_table *sbrec_load_balancer_table;
};

struct sb_lb_data {
    /* Global state for 'en-sb-lb'. */
};

void sb_lb_init(struct sb_lb_data *data);
void sb_lb_destroy(struct sb_lb_data *data);
void sb_lb_run(struct sb_lb_input *input_data, struct sb_lb_data *data,
               struct ovsdb_idl_txn *ovnsb_txn);

#endif /* northd/sb-lb.h */
