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

#ifndef DATAPATH_LEGACY_UPDATE_H
#define DATAPATH_LEGACY_UPDATE_H

#include "lib/hmapx.h"
#include "ovn-sb-idl.h"

void datapath_legacy_update_run(
    struct hmapx *legacy_sb_dps, struct ovsdb_idl *,
    const struct sbrec_datapath_binding_table *);

#endif /* DATAPATH_LEGACY_UPDATE_H */
