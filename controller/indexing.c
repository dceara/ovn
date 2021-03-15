/* Copyright (c) 2015, 2016, 2017 Nicira, Inc.
 * Copyright (c) 2021, Red Hat, Inc.
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

#include "indexing.h"
#include "ip-mcast.h"
#include "lib/chassis-index.h"
#include "lib/ip-mcast-index.h"
#include "lib/mcast-group-index.h"
#include "lib/ovn-sb-idl.h"

struct ovsdb_idl_index *sbrec_chassis_by_name;
struct ovsdb_idl_index *sbrec_chassis_private_by_name;
struct ovsdb_idl_index *sbrec_multicast_group_by_name_dp;
struct ovsdb_idl_index *sbrec_logical_flow_by_dp;
struct ovsdb_idl_index *sbrec_logical_flow_by_dp_group;
struct ovsdb_idl_index *sbrec_port_binding_by_name;
struct ovsdb_idl_index *sbrec_port_binding_by_key;
struct ovsdb_idl_index *sbrec_port_binding_by_dp;
struct ovsdb_idl_index *sbrec_datapath_binding_by_key;
struct ovsdb_idl_index *sbrec_mac_binding_by_lport_ip;
struct ovsdb_idl_index *sbrec_ip_multicast_by_dp;
struct ovsdb_idl_index *sbrec_igmp_group_by_addr;
struct ovsdb_idl_index *sbrec_fdb_by_dp_mac;

void indexing_init(struct ovsdb_idl *idl)
{
    sbrec_chassis_by_name = chassis_index_create(idl);
    sbrec_chassis_private_by_name = chassis_private_index_create(idl);
    sbrec_multicast_group_by_name_dp = mcast_group_index_create(idl);
    sbrec_logical_flow_by_dp =
        ovsdb_idl_index_create1(idl, &sbrec_logical_flow_col_logical_datapath);
    sbrec_logical_flow_by_dp_group =
        ovsdb_idl_index_create1(idl, &sbrec_logical_flow_col_logical_dp_group);
    sbrec_port_binding_by_name =
        ovsdb_idl_index_create1(idl, &sbrec_port_binding_col_logical_port);
    sbrec_port_binding_by_key =
        ovsdb_idl_index_create2(idl, &sbrec_port_binding_col_tunnel_key,
                                &sbrec_port_binding_col_datapath);
    sbrec_port_binding_by_dp =
        ovsdb_idl_index_create1(idl, &sbrec_port_binding_col_datapath);
    sbrec_datapath_binding_by_key =
        ovsdb_idl_index_create1(idl, &sbrec_datapath_binding_col_tunnel_key);
    sbrec_mac_binding_by_lport_ip =
        ovsdb_idl_index_create2(idl, &sbrec_mac_binding_col_logical_port,
                                &sbrec_mac_binding_col_ip);
    sbrec_ip_multicast_by_dp = ip_mcast_index_create(idl);
    sbrec_igmp_group_by_addr = igmp_group_index_create(idl);
    sbrec_fdb_by_dp_mac =
        ovsdb_idl_index_create2(idl, &sbrec_fdb_col_mac, &sbrec_fdb_col_dp_key);
}
