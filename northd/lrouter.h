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

#ifndef OVN_LROUTER_H
#define OVN_LROUTER_H 1

#include "hmapx.h"
#include "lib/ovn-util.h"
#include "mcast.h"
#include "openvswitch/hmap.h"

struct ovn_datapath;

/* Contains a NAT entry with the external addresses pre-parsed. */
struct northd_nat {
    const struct nbrec_nat *nb;
    struct lport_addresses ext_addrs;
    struct ovs_list ext_addr_list_node; /* Linkage in the per-external IP
                                         * list of nat entries. Currently
                                         * only used for SNAT.
                                         */
};

bool nat_entry_is_valid(const struct northd_nat *nat_entry);
bool nat_entry_is_v6(const struct northd_nat *nat_entry);

/* Stores the list of SNAT entries referencing a unique SNAT IP address.
 * The 'snat_entries' list will be empty if the SNAT IP is used only for
 * dnat_force_snat_ip or lb_force_snat_ip.
 */
struct northd_snat_ip {
    struct ovs_list snat_entries;
};

struct northd_logical_router {
    struct hmap_node node;
    const struct nbrec_logical_router *nbr;
    struct ovn_datapath *od;

    struct mcast_router_config mcast_config;

    /* True if logical router is a gateway router. i.e options:chassis is set.
     * If this is true, then 'l3dgw_port' will be ignored. */
    bool is_gw_router;

    /* NAT entries configured on the router. */
    struct northd_nat *nat_entries;
    size_t n_nat_entries;

    bool has_distributed_nat;

    /* Set of nat external ips on the router. */
    struct sset external_ips;

    /* SNAT IPs owned by the router (shash of 'struct northd_snat_ip'). */
    struct shash snat_ips;

    struct lport_addresses dnat_force_snat_addrs;
    struct lport_addresses lb_force_snat_addrs;
    bool lb_force_snat_router_ip;
};

struct lrouter_input {
    /* Input data for 'en-lrouter'. */

    /* Northbound table references */
    const struct nbrec_logical_router_table *nbrec_logical_router;
};

struct lrouter_data {
    /* Global state for 'en-lrouter'. */
    struct hmap routers;

    /* Incremental processing information. */
    bool change_tracked;
    struct hmapx new_routers;
    struct hmapx updated_routers;
    struct hmapx deleted_routers;
};

void lrouter_init(struct lrouter_data *data);
void lrouter_destroy(struct lrouter_data *data);
void lrouter_run(struct lrouter_input *input_data, struct lrouter_data *data);
void lrouter_clear_tracked(struct lrouter_data *data);
void lrouter_track_new(struct lrouter_data *data,
                       const struct nbrec_logical_router *nbr);
void lrouter_track_updated(struct lrouter_data *data,
                           const struct nbrec_logical_router *nbr);
void lrouter_track_deleted(struct lrouter_data *data,
                           const struct nbrec_logical_router *nbr);

#endif /* northd/lrouter.h */
