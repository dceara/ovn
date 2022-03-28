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

#ifndef OVN_NB_LB_H
#define OVN_NB_LB_H 1

#include "openvswitch/hmap.h"

struct nbrec_load_balancer;

struct nb_lb_input {
    /* Input data for 'en-nb-lb'. */

    /* en-lswitch and en-lrouter references. */
    const struct hmap *northd_logical_switches;
    const struct hmap *northd_logical_routers;

    /* Northbound table references */
    const struct nbrec_load_balancer_table *nbrec_load_balancer_table;
};

struct nb_lb_data {
    /* Global state for 'en-nb-lb'. */
    struct hmap lbs;    /* Stores 'struct ovn_northd_lb'. */
    struct hmap lr_lbs; /* Stores 'struct nb_lb_router_ref'. */
    struct hmap ls_lbs; /* Stores 'struct nb_lb_switch_ref'. */
};

void nb_lb_init(struct nb_lb_data *data);
void nb_lb_destroy(struct nb_lb_data *data);
void nb_lb_run(struct nb_lb_input *input_data, struct nb_lb_data *data);

void nb_lb_handle_new(struct nb_lb_data *data, const struct nbrec_load_balancer *lb);
void nb_lb_handle_deleted(struct nb_lb_data *data, const struct nbrec_load_balancer *lb);
void nb_lb_handle_updated(struct nb_lb_data *data, const struct nbrec_load_balancer *lb);

void nb_lb_handle_new_lrouter(struct nb_lb_data *data, const struct northd_logical_router *lr);
void nb_lb_handle_deleted_lrouter(struct nb_lb_data *data, const struct northd_logical_router *lr);
void nb_lb_handle_updated_lrouter(struct nb_lb_data *data, const struct northd_logical_router *lr);

void nb_lb_handle_new_lswitch(struct nb_lb_data *data, const struct northd_logical_switch *ls);
void nb_lb_handle_deleted_lswitch(struct nb_lb_data *data, const struct northd_logical_switch *ls);
void nb_lb_handle_updated_lswitch(struct nb_lb_data *data, const struct northd_logical_switch *ls);

#endif /* northd/nb-lb.h */
