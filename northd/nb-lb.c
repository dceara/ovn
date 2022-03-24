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

#include "lib/lb.h"
#include "lib/ovn-nb-idl.h"
#include "lrouter.h"
#include "lswitch.h"
#include "nb-lb.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(nb_lb)

void
nb_lb_init(struct nb_lb_data *data)
{
    hmap_init(&data->lbs);
}

void
nb_lb_destroy(struct nb_lb_data *data)
{
    struct ovn_northd_lb *lb;
    HMAP_FOR_EACH_POP (lb, hmap_node, &data->lbs) {
        ovn_northd_lb_destroy(lb);
    }
    hmap_destroy(&data->lbs);
}

void
nb_lb_run(struct nb_lb_input *input_data, struct nb_lb_data *data)
{
    const struct nbrec_load_balancer *nbrec_lb;
    NBREC_LOAD_BALANCER_TABLE_FOR_EACH (nbrec_lb,
                               input_data->nbrec_load_balancer_table) {
        struct ovn_northd_lb *lb_nb = ovn_northd_lb_create(nbrec_lb);
        hmap_insert(&data->lbs, &lb_nb->hmap_node,
                    uuid_hash(&nbrec_lb->header_.uuid));
    }

    const struct northd_logical_switch *ls;
    HMAP_FOR_EACH (ls, node, input_data->northd_logical_switches) {
        for (size_t i = 0; i < ls->nbs->n_load_balancer; i++) {
            const struct uuid *lb_uuid =
                &ls->nbs->load_balancer[i]->header_.uuid;
            struct ovn_northd_lb *lb =
                ovn_northd_lb_find(&data->lbs, lb_uuid);
            ovn_northd_lb_add_ls(lb, ls);
        }

        for (size_t i = 0; i < ls->nbs->n_load_balancer_group; i++) {
            const struct nbrec_load_balancer_group *lbg =
                ls->nbs->load_balancer_group[i];
            for (size_t j = 0; j < lbg->n_load_balancer; j++) {
                const struct uuid *lb_uuid =
                    &lbg->load_balancer[j]->header_.uuid;
                struct ovn_northd_lb *lb =
                    ovn_northd_lb_find(&data->lbs, lb_uuid);
                ovn_northd_lb_add_ls(lb, ls);
            }
        }
    }

    const struct northd_logical_router *lr;
    HMAP_FOR_EACH (lr, node, input_data->northd_logical_routers) {
        for (size_t i = 0; i < lr->nbr->n_load_balancer; i++) {
            const struct uuid *lb_uuid =
                &lr->nbr->load_balancer[i]->header_.uuid;
            struct ovn_northd_lb *lb =
                ovn_northd_lb_find(&data->lbs, lb_uuid);
            ovn_northd_lb_add_lr(lb, lr);
        }

        for (size_t i = 0; i < lr->nbr->n_load_balancer_group; i++) {
            const struct nbrec_load_balancer_group *lbg =
                lr->nbr->load_balancer_group[i];
            for (size_t j = 0; j < lbg->n_load_balancer; j++) {
                const struct uuid *lb_uuid =
                    &lbg->load_balancer[j]->header_.uuid;
                struct ovn_northd_lb *lb =
                    ovn_northd_lb_find(&data->lbs, lb_uuid);
                ovn_northd_lb_add_lr(lb, lr);
            }
        }
    }
}
