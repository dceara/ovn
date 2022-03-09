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

#include "lrouter.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(lrouter)

/* Returns true if a 'nat_entry' is valid, i.e.:
 * - parsing was successful.
 * - the string yielded exactly one IPv4 address or exactly one IPv6 address.
 */
bool
nat_entry_is_valid(const struct northd_nat *nat_entry)
{
    const struct lport_addresses *ext_addrs = &nat_entry->ext_addrs;

    return (ext_addrs->n_ipv4_addrs == 1 && ext_addrs->n_ipv6_addrs == 0) ||
        (ext_addrs->n_ipv4_addrs == 0 && ext_addrs->n_ipv6_addrs == 1);
}

bool
nat_entry_is_v6(const struct northd_nat *nat_entry)
{
    return nat_entry->ext_addrs.n_ipv6_addrs > 0;
}

static bool
northd_logical_router_get_force_snat_ip(struct northd_logical_router *nlr,
                                        const char *key_type,
                                        struct lport_addresses *laddrs)
{
    char *key = xasprintf("%s_force_snat_ip", key_type);
    const char *addresses = smap_get(&nlr->nbr->options, key);
    free(key);

    if (!addresses) {
        return false;
    }

    if (!extract_ip_address(addresses, laddrs)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 1);
        VLOG_WARN_RL(&rl, "bad ip %s in options of router "UUID_FMT"",
                     addresses, UUID_ARGS(&nlr->nbr->header_.uuid));
        return false;
    }

    return true;
}

static void
northd_logical_router_snat_ip_add(struct northd_logical_router *nlr,
                                  const char *ip, struct northd_nat *nat_entry)
{
    struct northd_snat_ip *snat_ip = shash_find_data(&nlr->snat_ips, ip);

    if (!snat_ip) {
        snat_ip = xzalloc(sizeof *snat_ip);
        ovs_list_init(&snat_ip->snat_entries);
        shash_add(&nlr->snat_ips, ip, snat_ip);
    }

    if (nat_entry) {
        ovs_list_push_back(&snat_ip->snat_entries,
                           &nat_entry->ext_addr_list_node);
    }
}

static void
northd_logical_router_init_nat(struct northd_logical_router *nlr)
{
    shash_init(&nlr->snat_ips);
    if (northd_logical_router_get_force_snat_ip(
            nlr, "dnat", &nlr->dnat_force_snat_addrs)) {
        if (nlr->dnat_force_snat_addrs.n_ipv4_addrs) {
            northd_logical_router_snat_ip_add(
                nlr, nlr->dnat_force_snat_addrs.ipv4_addrs[0].addr_s,
                NULL);
        }
        if (nlr->dnat_force_snat_addrs.n_ipv6_addrs) {
            northd_logical_router_snat_ip_add(
                nlr, nlr->dnat_force_snat_addrs.ipv6_addrs[0].addr_s,
                NULL);
        }
    }

    /* Check if 'lb_force_snat_ip' is configured with 'router_ip'. */
    const char *lb_force_snat =
        smap_get(&nlr->nbr->options, "lb_force_snat_ip");
    if (lb_force_snat && !strcmp(lb_force_snat, "router_ip")
            && smap_get(&nlr->nbr->options, "chassis")) {
        /* Set it to true only if its gateway router and
         * options:lb_force_snat_ip=router_ip. */
        nlr->lb_force_snat_router_ip = true;
    } else {
        nlr->lb_force_snat_router_ip = false;

        /* Check if 'lb_force_snat_ip' is configured with a set of
         * IP address(es). */
        if (northd_logical_router_get_force_snat_ip(
                nlr, "lb", &nlr->lb_force_snat_addrs)) {
            if (nlr->lb_force_snat_addrs.n_ipv4_addrs) {
                northd_logical_router_snat_ip_add(
                    nlr, nlr->lb_force_snat_addrs.ipv4_addrs[0].addr_s,
                    NULL);
            }
            if (nlr->lb_force_snat_addrs.n_ipv6_addrs) {
                northd_logical_router_snat_ip_add(
                    nlr, nlr->lb_force_snat_addrs.ipv6_addrs[0].addr_s,
                    NULL);
            }
        }
    }

    if (!nlr->nbr->n_nat) {
        return;
    }

    nlr->nat_entries = xmalloc(nlr->nbr->n_nat * sizeof *nlr->nat_entries);

    for (size_t i = 0; i < nlr->nbr->n_nat; i++) {
        const struct nbrec_nat *nat = nlr->nbr->nat[i];
        struct northd_nat *nat_entry = &nlr->nat_entries[i];

        nat_entry->nb = nat;
        if (!extract_ip_addresses(nat->external_ip,
                                  &nat_entry->ext_addrs) ||
                !nat_entry_is_valid(nat_entry)) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 1);

            VLOG_WARN_RL(&rl,
                         "Bad ip address %s in nat configuration "
                         "for router %s", nat->external_ip, nlr->nbr->name);
            continue;
        }

        /* If this is a SNAT rule add the IP to the set of unique SNAT IPs. */
        if (!strcmp(nat->type, "snat")) {
            if (!nat_entry_is_v6(nat_entry)) {
                northd_logical_router_snat_ip_add(
                    nlr, nat_entry->ext_addrs.ipv4_addrs[0].addr_s,
                    nat_entry);
            } else {
                northd_logical_router_snat_ip_add(
                    nlr, nat_entry->ext_addrs.ipv6_addrs[0].addr_s,
                    nat_entry);
            }
        }

        if (!strcmp(nat->type, "dnat_and_snat")
            && nat->logical_port && nat->external_mac) {
            nlr->has_distributed_nat = true;
        }
    }
    nlr->n_nat_entries = nlr->nbr->n_nat;
}

static void
northd_logical_router_destroy_nat(struct northd_logical_router *nlr)
{
    shash_destroy_free_data(&nlr->snat_ips);
    destroy_lport_addresses(&nlr->dnat_force_snat_addrs);
    destroy_lport_addresses(&nlr->lb_force_snat_addrs);

    for (size_t i = 0; i < nlr->n_nat_entries; i++) {
        destroy_lport_addresses(&nlr->nat_entries[i].ext_addrs);
    }
    free(nlr->nat_entries);
}

static void
northd_logical_router_init_external_ips(struct northd_logical_router *nlr)
{
    sset_init(&nlr->external_ips);
    for (size_t i = 0; i < nlr->nbr->n_nat; i++) {
        sset_add(&nlr->external_ips, nlr->nbr->nat[i]->external_ip);
    }
}

static void
northd_logical_router_destroy_external_ips(struct northd_logical_router *nlr)
{
    sset_destroy(&nlr->external_ips);
}

struct northd_logical_router *
create_northd_logical_router(const struct nbrec_logical_router *nbr,
                             struct ovn_datapath *od)
{
    struct northd_logical_router *nlr = xzalloc(sizeof *nlr);
    nlr->nbr = nbr;
    nlr->od = od;

    init_mcast_router_config(&nlr->mcast_config, nbr);
    northd_logical_router_init_nat(nlr);
    northd_logical_router_init_external_ips(nlr);

    if (smap_get(&nbr->options, "chassis")) {
        nlr->is_gw_router = true;
    }
    return nlr;
}

void
destroy_northd_logical_router(struct northd_logical_router *nlr)
{
    if (!nlr) {
        return;
    }

    northd_logical_router_destroy_nat(nlr);
    northd_logical_router_destroy_external_ips(nlr);
    free(nlr);
}
