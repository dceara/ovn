/* Copyright (c) 2015, 2016, 2017 Nicira, Inc.
 * Copyright (c) 2025, Red Hat, Inc.
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

#ifndef NEIGHBOR_OF_H
#define NEIGHBOR_OF_H 1

#include "ovn-sb-idl.h"
#include "openvswitch/uuid.h"
#include "ofctrl.h"

void
consider_neighbor_flow(const struct sbrec_port_binding *,
                       const struct uuid *neighbor_uuid,
                       const struct in6_addr *, struct eth_addr,
                       struct ovn_desired_flow_table *,
                       uint16_t priority, bool needs_usage_tracking);

#endif  /* NEIGHBOR_OF_H */
