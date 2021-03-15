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

#ifndef INDEXING_H
#define INDEXING_H

struct ovsdb_idl;
struct ovsdb_idl_index;

extern struct ovsdb_idl_index *sbrec_chassis_by_name;
extern struct ovsdb_idl_index *sbrec_chassis_private_by_name;
extern struct ovsdb_idl_index *sbrec_multicast_group_by_name_dp;
extern struct ovsdb_idl_index *sbrec_logical_flow_by_dp;
extern struct ovsdb_idl_index *sbrec_logical_flow_by_dp_group;
extern struct ovsdb_idl_index *sbrec_port_binding_by_name;
extern struct ovsdb_idl_index *sbrec_port_binding_by_key;
extern struct ovsdb_idl_index *sbrec_port_binding_by_dp;
extern struct ovsdb_idl_index *sbrec_datapath_binding_by_key;
extern struct ovsdb_idl_index *sbrec_mac_binding_by_lport_ip;
extern struct ovsdb_idl_index *sbrec_ip_multicast_by_dp;
extern struct ovsdb_idl_index *sbrec_igmp_group_by_addr;
extern struct ovsdb_idl_index *sbrec_fdb_by_dp_mac;

void indexing_init(struct ovsdb_idl *idl);

#endif /* controller/indexing.h */
