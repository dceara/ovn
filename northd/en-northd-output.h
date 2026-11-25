#ifndef EN_NORTHD_OUTPUT_H
#define EN_NORTHD_OUTPUT_H 1

#include "lib/inc-proc-eng.h"

void *en_northd_output_init(struct engine_node *node OVS_UNUSED,
                            struct engine_arg *arg OVS_UNUSED);
enum engine_node_state en_northd_output_run(const struct engine_context *,
                                            struct engine_node *, void *data);

void en_northd_output_cleanup(void *data);
enum engine_input_handler_result
northd_output_sync_to_sb_handler(const struct engine_context *eng_ctx,
                                 struct engine_node *node,
                                 void *data OVS_UNUSED);
enum engine_input_handler_result
northd_output_dp_group_handler(const struct engine_context *eng_ctx,
                               struct engine_node *node,
                               void *data OVS_UNUSED);
enum engine_input_handler_result
northd_output_mac_binding_aging_handler(const struct engine_context *eng_ctx,
                                        struct engine_node *node,
                                        void *data OVS_UNUSED);
enum engine_input_handler_result
northd_output_fdb_aging_handler(const struct engine_context *eng_ctx,
                                struct engine_node *node,
                                void *data OVS_UNUSED);
enum engine_input_handler_result
northd_output_ecmp_nexthop_handler(const struct engine_context *eng_ctx,
                                   struct engine_node *node,
                                   void *data OVS_UNUSED);
enum engine_input_handler_result
northd_output_acl_id_handler(const struct engine_context *eng_ctx,
                             struct engine_node *node, void *data OVS_UNUSED);
enum engine_input_handler_result
northd_output_advertised_route_sync_handler(
    const struct engine_context *eng_ctx,
    struct engine_node *node, void *data OVS_UNUSED);

#endif
