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

#ifndef OVN_EN_LROUTER_H
#define OVN_EN_LROUTER_H 1

#include "lib/inc-proc-eng.h"

void en_lrouter_run(struct engine_node *node, void *data);
void *en_lrouter_init(struct engine_node *node, struct engine_arg *arg);
void en_lrouter_clear_tracked_data(void *data);
void en_lrouter_cleanup(void *data);

/* I-P change handlers. */
bool en_lrouter_nb_logical_router_handler(struct engine_node *node,
                                          void *data);

#endif /* northd/en-lrouter.h */
