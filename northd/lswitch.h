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

#ifndef OVN_LSWITCH_H
#define OVN_LSWITCH_H 1

#include "ipam.h"
#include "mcast.h"
#include "openvswitch/hmap.h"

struct ovn_datapath;

struct northd_logical_switch {
    struct hmap_node node;
    const struct nbrec_logical_switch *nbs;
    struct ovn_datapath *od;

    struct ipam_config ipam_config;
    struct mcast_switch_config mcast_config;
};

struct lswitch_input {
    /* Input data for 'en-lswitch'. */

    /* Northbound table references */
    const struct nbrec_logical_switch_table *nbrec_logical_switch;
};

struct lswitch_data {
    /* Global state for 'en-lswitch'. */
    struct hmap switches;
};

void lswitch_init(struct lswitch_data *data);
void lswitch_destroy(struct lswitch_data *data);
void lswitch_run(struct lswitch_input *input_data, struct lswitch_data *data);

#endif /* northd/lswitch.h */
