/*
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

#ifndef EN_DATAPATH_LOGICAL_SWITCH_H
#define EN_DATAPATH_LOGICAL_SWITCH_H

#include "lib/inc-proc-eng.h"
#include "openvswitch/hmap.h"
#include "lib/hmapx.h"

void *en_datapath_logical_switch_init(struct engine_node *,
                                      struct engine_arg *);

enum engine_input_handler_result
datapath_logical_switch_handler(struct engine_node *, void *data);
enum engine_node_state en_datapath_logical_switch_run(struct engine_node *,
                                                      void *data);
void en_datapath_logical_switch_cleanup(void *data);
void en_datapath_logical_switch_clear_tracked_data(void *data);

struct ovn_synced_logical_switch {
    struct hmap_node hmap_node;
    const struct nbrec_logical_switch *nb;
    const struct sbrec_datapath_binding *sb;
};

struct ovn_synced_logical_switch_map {
    struct hmap synced_switches;

    struct hmapx new;
    struct hmapx updated;
    struct hmapx deleted;
};

struct uuid;
struct ovn_synced_logical_switch *
ovn_synced_logical_switch_find(const struct ovn_synced_logical_switch_map *map,
                               const struct uuid *nb_uuid);

void *en_datapath_synced_logical_switch_init(struct engine_node *,
                                             struct engine_arg *);

enum engine_node_state en_datapath_synced_logical_switch_run(
    struct engine_node *, void *data);
void en_datapath_synced_logical_switch_clear_tracked_data(void *data);
enum engine_input_handler_result
en_datapath_synced_logical_switch_datapath_sync_handler(
        struct engine_node *node, void *data);
void en_datapath_synced_logical_switch_cleanup(void *data);

#endif /* EN_DATAPATH_LOGICAL_SWITCH_H */
