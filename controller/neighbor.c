/* Copyright (c) 2025, Red Hat, Inc.
 *
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

#include "lib/hash.h"
#include "lib/packets.h"
#include "lib/sset.h"
#include "openvswitch/vlog.h"
#include "unixctl.h"

#include "neighbor.h"

VLOG_DEFINE_THIS_MODULE(neighbor);

/* XXX: just for testing: */
static char *test_ifname;

static void
debug_set_test_ifname(struct unixctl_conn *conn, int argc OVS_UNUSED,
                       const char *argv[], void *arg OVS_UNUSED)
{
    free(test_ifname);
    test_ifname = xstrdup(argv[1]);
    VLOG_INFO("DEBUG setting test_ifname to %s", test_ifname);
    unixctl_command_reply(conn, NULL);
}

OVS_CONSTRUCTOR(neighbor_constructor) {
    unixctl_command_register("debug/set_test_ifname", "", 1, 1,
                             debug_set_test_ifname, NULL);
}

static void neighbor_interface_monitor_destroy(
    struct neighbor_interface_monitor *);

uint32_t
advertise_neigh_hash(const struct eth_addr *eth, const struct in6_addr *ip)
{
    return hash_bytes(ip, sizeof *ip, hash_bytes(eth, sizeof *eth, 0));
}

void
neighbor_run(struct neighbor_ctx_in *n_ctx_in OVS_UNUSED,
             struct neighbor_ctx_out *n_ctx_out OVS_UNUSED)
{
    /* XXX: Not implemented yet. */

    /* XXX: TODO GLUE: get information from (n_ctx_in) SB (runtime-data) about:
     * - local datapath vni (and listen on br-$vni, lo-$vni and vxlan-$vni)
     *   for which we want to enable neighbor monitoring
     *   https://issues.redhat.com/browse/FDP-1385
     * - what FDB/neighbor entries to advertise
     *   https://issues.redhat.com/browse/FDP-1389
     *
     * And populate that in n_ctx_out.
     */

    // TODO: just for testing; this should actually build the map
    // of neighbor entries to advertise and interface names to monitor
    // from SB contents.
    if (test_ifname) {
        /* Inject an IPv4 neighbor on test_ifname. */
        struct neighbor_interface_monitor *nim_v4 = xmalloc(sizeof *nim_v4);
        *nim_v4 = (struct neighbor_interface_monitor) {
            .family = NEIGH_AF_INET,
            .announced_neighbors =
                HMAP_INITIALIZER(&nim_v4->announced_neighbors),
        };
        ovs_strzcpy(nim_v4->if_name, test_ifname, IFNAMSIZ + 1);

        struct advertise_neighbor_entry *n1 = xzalloc(sizeof *n1);
        n1->lladdr = (struct eth_addr) ETH_ADDR_C(00, 00, 42, 43, 44, 45);
        in6_addr_set_mapped_ipv4(&n1->addr, htonl(0x2a2a2a2a));
        hmap_insert(&nim_v4->announced_neighbors, &n1->node,
                    advertise_neigh_hash(&n1->lladdr, &n1->addr));

        vector_push(n_ctx_out->monitored_interfaces, &nim_v4);

        /* Inject an IPv6 neighbor on test_ifname. */
        struct neighbor_interface_monitor *nim_v6 = xmalloc(sizeof *nim_v6);
        *nim_v6 = (struct neighbor_interface_monitor) {
            .family = NEIGH_AF_INET6,
            .announced_neighbors =
                HMAP_INITIALIZER(&nim_v6->announced_neighbors),
        };
        ovs_strzcpy(nim_v6->if_name, test_ifname, IFNAMSIZ + 1);

        n1 = xzalloc(sizeof *n1);
        n1->lladdr = (struct eth_addr) ETH_ADDR_C(00, 00, 42, 43, 44, 55);
        ipv6_parse("4242::4242", &n1->addr);
        hmap_insert(&nim_v6->announced_neighbors, &n1->node,
                    advertise_neigh_hash(&n1->lladdr, &n1->addr));

        vector_push(n_ctx_out->monitored_interfaces, &nim_v6);

        /* Inject a bridge FDB entry on test_ifname. */
        struct neighbor_interface_monitor *nim_bridge =
            xmalloc(sizeof *nim_bridge);
        *nim_bridge = (struct neighbor_interface_monitor) {
            .family = NEIGH_AF_BRIDGE,
            .announced_neighbors =
                HMAP_INITIALIZER(&nim_bridge->announced_neighbors),
        };
        ovs_strzcpy(nim_bridge->if_name, test_ifname, IFNAMSIZ + 1);

        n1 = xzalloc(sizeof *n1);
        n1->lladdr = (struct eth_addr) ETH_ADDR_C(00, 00, 42, 43, 44, 66);
        hmap_insert(&nim_bridge->announced_neighbors, &n1->node,
                    advertise_neigh_hash(&n1->lladdr, &n1->addr));

        vector_push(n_ctx_out->monitored_interfaces, &nim_bridge);
    }
}

void
neighbor_cleanup(struct vector *monitored_interfaces)
{
    struct neighbor_interface_monitor *nim;
    VECTOR_FOR_EACH (monitored_interfaces, nim) {
        neighbor_interface_monitor_destroy(nim);
    }
    vector_clear(monitored_interfaces);
}

static void
neighbor_interface_monitor_destroy(struct neighbor_interface_monitor *nim)
{
    struct advertise_neighbor_entry *an;

    HMAP_FOR_EACH_POP (an, node, &nim->announced_neighbors) {
        free(an);
    }
    free(nim);
}
