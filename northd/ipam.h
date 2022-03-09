#ifndef NORTHD_IPAM_H
#define NORTHD_IPAM_H 1

#include <stdint.h>
#include <stdbool.h>

#include "openvswitch/types.h"

struct ipam_config {
    uint32_t start_ipv4;
    size_t total_ipv4s;
    bool ipv6_prefix_set;
    struct in6_addr ipv6_prefix;
    bool mac_only;
    char *exclude_ip_list;
    char *id;
};

struct ipam_info {
    const struct ipam_config *cfg;

    unsigned long *allocated_ipv4s; /* A bitmap of allocated IPv4s */
};

struct smap;
void init_ipam_config(struct ipam_config *ipam_config,
                      const struct smap *config,
                      const char *id);
void destroy_ipam_config(struct ipam_config *ipam_config);
void init_ipam_info(struct ipam_info *info,
                    const struct ipam_config *ipam_config);

void destroy_ipam_info(struct ipam_info *info);

bool ipam_insert_ip(struct ipam_info *info, uint32_t ip);

uint32_t ipam_get_unused_ip(struct ipam_info *info);

void ipam_insert_mac(struct eth_addr *ea, bool check);

uint64_t ipam_get_unused_mac(ovs_be32 ip);

void cleanup_macam(void);

struct eth_addr get_mac_prefix(void);

const char *set_mac_prefix(const char *hint);

#endif /* NORTHD_IPAM_H */
