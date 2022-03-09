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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "lswitch.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(lswitch)

struct northd_logical_switch *
create_northd_logical_switch(const struct nbrec_logical_switch *nbs,
                             struct ovn_datapath *od)
{
    struct northd_logical_switch *nls = xzalloc(sizeof *nls);
    nls->nbs = nbs;
    nls->od = od;

    char uuid_s[UUID_LEN + 1];
    sprintf(uuid_s, UUID_FMT, UUID_ARGS(&nls->nbs->header_.uuid));
    init_ipam_config(&nls->ipam_config, &nbs->other_config, uuid_s);

    init_mcast_switch_config(&nls->mcast_config, nbs);
    return nls;
}

void
destroy_northd_logical_switch(struct northd_logical_switch *nls)
{
    if (!nls) {
        return;
    }

    destroy_ipam_config(&nls->ipam_config);
    destroy_mcast_switch_config(&nls->mcast_config);
    free(nls);
}
