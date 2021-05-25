/* Copyright (c) 2021, Red Hat, Inc.
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

#ifndef OVN_FEATURES_H
#define OVN_FEATURES_H 1

#include <stdbool.h>

/* ovn-controller supported feature names. */
#define OVN_FEATURE_PORT_UP_NOTIF "port-up-notif"

/* OVS datapath supported features.  Based on availability OVN might generate
 * different types of openflows.
 */
struct ovs_feature_support {
    bool ct_all_zero_nat;
};

const struct ovs_feature_support *ovs_feature_support_get(void);
bool ovs_feature_support_update(bool ct_all_zero_snat);

#endif
