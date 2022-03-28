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

//TODO: move?
struct nb_lb_switch_ref {
    struct hmap_node hmap_node;

    const struct northd_logical_switch *ls;
    struct hmapx lbs; /* Stores 'struct ovn_northd_lb *'. */
};

static struct nb_lb_switch_ref *
nb_lb_switch_ref_create(struct hmap *ls_lbs,
                        const struct northd_logical_switch *ls)
{
    struct nb_lb_switch_ref *ls_ref = xzalloc(sizeof *ls_ref);

    ls_ref->ls = ls;
    hmapx_init(&ls_ref->lbs);

    size_t hash = hash_pointer(ls, 0);
    hmap_insert(ls_lbs, &ls_ref->hmap_node, hash);
    return ls_ref;
}

static struct nb_lb_switch_ref *
nb_lb_switch_ref_find(struct hmap *ls_lbs,
                      const struct northd_logical_switch *ls)
{
    size_t hash = hash_pointer(ls, 0);
    struct nb_lb_switch_ref *ls_ref;

    HMAP_FOR_EACH_WITH_HASH (ls_ref, hmap_node, hash, ls_lbs) {
        if (ls_ref->ls == ls) {
            return ls_ref;
        }
    }
    return NULL;
}

static void
nb_lb_switch_ref_destroy(struct hmap *ls_lbs,
                         struct nb_lb_switch_ref *ls_ref)
{
    hmapx_destroy(&ls_ref->lbs);
    hmap_remove(ls_lbs, &ls_ref->hmap_node);
    free(ls_ref);
}

static void
nb_lb_switch_ref_add_lb(struct hmap *ls_lbs,
                        const struct northd_logical_switch *ls,
                        const struct ovn_northd_lb *lb)
{
    struct nb_lb_switch_ref *ls_ref =
        nb_lb_switch_ref_find(ls_lbs, ls);

    if (!ls_ref) {
        ls_ref = nb_lb_switch_ref_create(ls_lbs, ls);
    }
    hmapx_add(&ls_ref->lbs, CONST_CAST(void *, lb));
}

static void
nb_lb_switch_ref_del_lb(struct hmap *ls_lbs,
                        const struct northd_logical_switch *ls,
                        const struct ovn_northd_lb *lb)
{
    struct nb_lb_switch_ref *ls_ref =
        nb_lb_switch_ref_find(ls_lbs, ls);

    // ovs_assert(ls_ref);
    hmapx_find_and_delete_assert(&ls_ref->lbs, lb);
}

//TODO: duplicated
struct nb_lb_router_ref {
    struct hmap_node hmap_node;

    const struct northd_logical_router *lr;
    struct hmapx lbs; /* Stores 'struct ovn_northd_lb *'. */
};

static struct nb_lb_router_ref *
nb_lb_router_ref_create(struct hmap *lr_lbs,
                        const struct northd_logical_router *lr)
{
    struct nb_lb_router_ref *lr_ref = xzalloc(sizeof *lr_ref);

    lr_ref->lr = lr;
    hmapx_init(&lr_ref->lbs);

    size_t hash = hash_pointer(lr, 0);
    hmap_insert(lr_lbs, &lr_ref->hmap_node, hash);
    return lr_ref;
}

static struct nb_lb_router_ref *
nb_lb_router_ref_find(struct hmap *lr_lbs,
                      const struct northd_logical_router *lr)
{
    size_t hash = hash_pointer(lr, 0);
    struct nb_lb_router_ref *lr_ref;

    HMAP_FOR_EACH_WITH_HASH (lr_ref, hmap_node, hash, lr_lbs) {
        if (lr_ref->lr == lr) {
            return lr_ref;
        }
    }
    return NULL;
}

static void
nb_lb_router_ref_destroy(struct hmap *lr_lbs,
                         struct nb_lb_router_ref *lr_ref)
{
    hmapx_destroy(&lr_ref->lbs);
    hmap_remove(lr_lbs, &lr_ref->hmap_node);
    free(lr_ref);
}

static void
nb_lb_router_ref_add_lb(struct hmap *lr_lbs,
                        const struct northd_logical_router *lr,
                        const struct ovn_northd_lb *lb)
{
    struct nb_lb_router_ref *lr_ref =
        nb_lb_router_ref_find(lr_lbs, lr);

    if (!lr_ref) {
        lr_ref = nb_lb_router_ref_create(lr_lbs, lr);
    }
    hmapx_add(&lr_ref->lbs, CONST_CAST(void *, lb));
}

static void
nb_lb_router_ref_del_lb(struct hmap *lr_lbs,
                        const struct northd_logical_router *lr,
                        const struct ovn_northd_lb *lb)
{
    struct nb_lb_router_ref *lr_ref =
        nb_lb_router_ref_find(lr_lbs, lr);

    ovs_assert(lr_ref);
    hmapx_find_and_delete_assert(&lr_ref->lbs, lb);
}

//TODO:
static void
nb_lb_new__(struct nb_lb_data *data, const struct nbrec_load_balancer *lb)
{
    struct ovn_northd_lb *lb_nb = ovn_northd_lb_create(lb);
    hmap_insert(&data->lbs, &lb_nb->hmap_node, uuid_hash(&lb->header_.uuid));
}

//TODO:
static void
nb_lb_delete__(struct nb_lb_data *data, struct ovn_northd_lb *lb_nb)
{
    struct hmapx_node *node;

    HMAPX_FOR_EACH (node, &lb_nb->nb_lr) {
        struct northd_logical_router *lr = node->data;
        nb_lb_router_ref_del_lb(&data->lr_lbs, lr, lb_nb);
    }

    HMAPX_FOR_EACH (node, &lb_nb->nb_ls) {
        struct northd_logical_switch *ls = node->data;
        nb_lb_switch_ref_del_lb(&data->ls_lbs, ls, lb_nb);
    }

    ovn_northd_lb_destroy(lb_nb);
}

void
nb_lb_init(struct nb_lb_data *data)
{
    hmap_init(&data->lbs);
    hmap_init(&data->lr_lbs);
    hmap_init(&data->ls_lbs);
}

void
nb_lb_destroy(struct nb_lb_data *data)
{
    struct ovn_northd_lb *lb_nb;
    HMAP_FOR_EACH_POP (lb_nb, hmap_node, &data->lbs) {
        nb_lb_delete__(data, lb_nb);
    }
    hmap_destroy(&data->lbs);

    struct nb_lb_router_ref *lr_ref;
    struct nb_lb_router_ref *lr_ref_next;
    HMAP_FOR_EACH_SAFE (lr_ref, lr_ref_next, hmap_node, &data->lr_lbs) {
        nb_lb_router_ref_destroy(&data->lr_lbs, lr_ref);
    }
    hmap_destroy(&data->lr_lbs);

    struct nb_lb_switch_ref *ls_ref;
    struct nb_lb_switch_ref *ls_ref_next;
    HMAP_FOR_EACH_SAFE (ls_ref, ls_ref_next, hmap_node, &data->ls_lbs) {
        nb_lb_switch_ref_destroy(&data->ls_lbs, ls_ref);
    }
    hmap_destroy(&data->ls_lbs);
}

void
nb_lb_run(struct nb_lb_input *input_data, struct nb_lb_data *data)
{
    const struct nbrec_load_balancer *nbrec_lb;
    NBREC_LOAD_BALANCER_TABLE_FOR_EACH (nbrec_lb,
                               input_data->nbrec_load_balancer_table) {
        nb_lb_new__(data, nbrec_lb);
    }

    const struct northd_logical_switch *ls;
    HMAP_FOR_EACH (ls, node, input_data->northd_logical_switches) {
        nb_lb_handle_new_lswitch(data, ls);
    }

    const struct northd_logical_router *lr;
    HMAP_FOR_EACH (lr, node, input_data->northd_logical_routers) {
        nb_lb_handle_new_lrouter(data, lr);
    }
}

void
nb_lb_handle_new(struct nb_lb_data *data, const struct nbrec_load_balancer *lb)
{
    nb_lb_new__(data, lb);
}

void
nb_lb_handle_deleted(struct nb_lb_data *data, const struct nbrec_load_balancer *lb)
{
    struct ovn_northd_lb *lb_nb = ovn_northd_lb_find(&data->lbs, &lb->header_.uuid);

    ovs_assert(lb_nb);
    hmap_remove(&data->lbs, &lb_nb->hmap_node);
    nb_lb_delete__(data, lb_nb);
}

void
nb_lb_handle_updated(struct nb_lb_data *data, const struct nbrec_load_balancer *lb)
{
    struct ovn_northd_lb *lb_nb = ovn_northd_lb_find(&data->lbs, &lb->header_.uuid);

    ovs_assert(lb_nb);
    hmap_remove(&data->lbs, &lb_nb->hmap_node);
    nb_lb_delete__(data, lb_nb);
    nb_lb_new__(data, lb);
    //TODO: comment that updating a LB triggers updates to all LSs and LRs
    //the LB is applied on (also via LB Groups).
}

void
nb_lb_handle_new_lrouter(struct nb_lb_data *data, const struct northd_logical_router *lr)
{
    for (size_t i = 0; i < lr->nbr->n_load_balancer; i++) {
        const struct uuid *lb_uuid =
            &lr->nbr->load_balancer[i]->header_.uuid;
        struct ovn_northd_lb *lb =
            ovn_northd_lb_find(&data->lbs, lb_uuid);
        ovn_northd_lb_add_lr(lb, lr);
        nb_lb_router_ref_add_lb(&data->lr_lbs, lr, lb);
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
            nb_lb_router_ref_add_lb(&data->lr_lbs, lr, lb);
        }
    }
}

void
nb_lb_handle_deleted_lrouter(struct nb_lb_data *data, const struct northd_logical_router *lr)
{
    struct nb_lb_router_ref *lr_ref = nb_lb_router_ref_find(&data->lr_lbs, lr);
    if (!lr_ref) {
        return;
    }

    struct hmapx_node *node;
    HMAPX_FOR_EACH (node, &lr_ref->lbs) {
        struct ovn_northd_lb *lb = node->data;
        ovn_northd_lb_del_lr(lb, lr);
    }

    nb_lb_router_ref_destroy(&data->lr_lbs, lr_ref);
}

void
nb_lb_handle_updated_lrouter(struct nb_lb_data *data, const struct northd_logical_router *lr)
{
    struct nb_lb_router_ref *lr_ref = nb_lb_router_ref_find(&data->lr_lbs, lr);
    if (!lr_ref) {
        lr_ref = nb_lb_router_ref_create(&data->lr_lbs, lr);
    }

    // TODO: we just can't do this:
    // nb_lb_handle_deleted_lrouter(data, lr);
    // nb_lb_handle_new_lrouter(data, lr);
    // It would be way too costly.
    // Instead we need to compute the set of LBs that used to be applied on lr
    // and not applied anymore.

    struct hmapx old_lbs = HMAPX_INITIALIZER(&old_lbs);
    hmapx_swap(&old_lbs, &lr_ref->lbs);

    for (size_t i = 0; i < lr->nbr->n_load_balancer; i++) {
        const struct uuid *lb_uuid =
            &lr->nbr->load_balancer[i]->header_.uuid;
        struct ovn_northd_lb *lb =
            ovn_northd_lb_find(&data->lbs, lb_uuid);
        ovs_assert(lb);
        hmapx_add(&lr_ref->lbs, lb);
    }

    for (size_t i = 0; i < lr->nbr->n_load_balancer_group; i++) {
        const struct nbrec_load_balancer_group *lbg =
            lr->nbr->load_balancer_group[i];
        for (size_t j = 0; j < lbg->n_load_balancer; j++) {
            const struct uuid *lb_uuid =
                &lbg->load_balancer[j]->header_.uuid;
            struct ovn_northd_lb *lb =
                ovn_northd_lb_find(&data->lbs, lb_uuid);
            ovs_assert(lb);
            hmapx_add(&lr_ref->lbs, lb);
        }
    }

    // Process the ones that were removed.
    struct hmapx_node *node;
    HMAPX_FOR_EACH (node, &old_lbs) {
        if (hmapx_find(&lr_ref->lbs, node->data)) {
            continue;
        }

        struct ovn_northd_lb *lb = node->data;
        ovn_northd_lb_del_lr(lb, lr);
    }

    // Process the ones that were added.
    HMAPX_FOR_EACH (node, &lr_ref->lbs) {
        if (hmapx_find(&old_lbs, node->data)) {
            continue;
        }

        struct ovn_northd_lb *lb = node->data;
        ovn_northd_lb_add_lr(lb, lr);
    }

    hmapx_destroy(&old_lbs);
}

void
nb_lb_handle_new_lswitch(struct nb_lb_data *data, const struct northd_logical_switch *ls)
{
    for (size_t i = 0; i < ls->nbs->n_load_balancer; i++) {
        const struct uuid *lb_uuid =
            &ls->nbs->load_balancer[i]->header_.uuid;
        struct ovn_northd_lb *lb =
            ovn_northd_lb_find(&data->lbs, lb_uuid);
        ovn_northd_lb_add_ls(lb, ls);
        nb_lb_switch_ref_add_lb(&data->ls_lbs, ls, lb);
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
            nb_lb_switch_ref_add_lb(&data->ls_lbs, ls, lb);
        }
    }
}

void
nb_lb_handle_deleted_lswitch(struct nb_lb_data *data, const struct northd_logical_switch *ls)
{
    struct nb_lb_switch_ref *ls_ref = nb_lb_switch_ref_find(&data->ls_lbs, ls);

    if (!ls_ref) {
        return;
    }

    struct hmapx_node *node;
    HMAPX_FOR_EACH (node, &ls_ref->lbs) {
        struct ovn_northd_lb *lb = node->data;
        ovn_northd_lb_del_ls(lb, ls);
    }

    nb_lb_switch_ref_destroy(&data->ls_lbs, ls_ref);
}

void
nb_lb_handle_updated_lswitch(struct nb_lb_data *data, const struct northd_logical_switch *ls)
{
    struct nb_lb_switch_ref *ls_ref = nb_lb_switch_ref_find(&data->ls_lbs, ls);
    if (!ls_ref) {
        ls_ref = nb_lb_switch_ref_create(&data->ls_lbs, ls);
    }

    // TODO: we just can't do this:
    // nb_lb_handle_deleted_lswitch(data, ls);
    // nb_lb_handle_new_lswitch(data, ls);
    // It would be way too costly.
    // Instead we need to compute the set of LBs that used to be applied on ls
    // and not applied anymore.

    struct hmapx old_lbs = HMAPX_INITIALIZER(&old_lbs);
    hmapx_swap(&old_lbs, &ls_ref->lbs);

    for (size_t i = 0; i < ls->nbs->n_load_balancer; i++) {
        const struct uuid *lb_uuid =
            &ls->nbs->load_balancer[i]->header_.uuid;
        struct ovn_northd_lb *lb =
            ovn_northd_lb_find(&data->lbs, lb_uuid);
        ovs_assert(lb);
        hmapx_add(&ls_ref->lbs, lb);
    }

    for (size_t i = 0; i < ls->nbs->n_load_balancer_group; i++) {
        const struct nbrec_load_balancer_group *lbg =
            ls->nbs->load_balancer_group[i];
        for (size_t j = 0; j < lbg->n_load_balancer; j++) {
            const struct uuid *lb_uuid =
                &lbg->load_balancer[j]->header_.uuid;
            struct ovn_northd_lb *lb =
                ovn_northd_lb_find(&data->lbs, lb_uuid);
            ovs_assert(lb);
            hmapx_add(&ls_ref->lbs, lb);
        }
    }

    // Process the ones that were removed.
    struct hmapx_node *node;
    HMAPX_FOR_EACH (node, &old_lbs) {
        if (hmapx_find(&ls_ref->lbs, node->data)) {
            continue;
        }

        struct ovn_northd_lb *lb = node->data;
        ovn_northd_lb_del_ls(lb, ls);
    }

    // Process the ones that were added.
    HMAPX_FOR_EACH (node, &ls_ref->lbs) {
        if (hmapx_find(&old_lbs, node->data)) {
            continue;
        }

        struct ovn_northd_lb *lb = node->data;
        ovn_northd_lb_add_ls(lb, ls);
    }

    hmapx_destroy(&old_lbs);
}
