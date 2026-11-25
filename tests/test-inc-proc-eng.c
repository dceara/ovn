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

#include <config.h>

#include <string.h>

#include "lib/inc-proc-eng.h"
#include "openvswitch/poll-loop.h"
#include "stopwatch.h"
#include "tests/ovstest.h"
#include "unixctl.h"
#include "util.h"

struct test_graph {
    struct engine *engine;
    struct engine_node source;
    struct engine_node second_source;
    struct engine_node sink;
    bool registered;
};

static enum engine_node_state
test_engine_run(struct engine_node *node OVS_UNUSED, void *data OVS_UNUSED)
{
    return EN_UPDATED;
}

static enum engine_input_handler_result
test_engine_change(struct engine_node *node OVS_UNUSED, void *data OVS_UNUSED)
{
    return EN_HANDLED_UPDATED;
}

static void
test_graph_run(struct test_graph *graph, bool allow)
{
    const struct engine_context ctx = { 0 };

    engine_init_run(graph->engine);
    engine_set_context(&ctx);
    engine_run(graph->engine, allow);
    engine_set_context(NULL);
}

static void
test_graph_cleanup(struct test_graph *graph)
{
    engine_deregister(graph->engine);
    graph->registered = false;
}

static void
test_graph_init(struct test_graph *graph, const char *name, bool long_labels)
{
    ovs_assert(!graph->registered);
    graph->source = (struct engine_node) {
        .name = "source",
        .run = test_engine_run,
    };
    graph->sink = (struct engine_node) {
        .name = "sink",
        .run = test_engine_run,
        .sb_write = true,
    };

    const char *handler_label = "test_engine_change_handler_with_long_name";
    engine_add_input_impl(&graph->sink, &graph->source, test_engine_change,
                          handler_label);
    if (long_labels) {
        graph->source.name = "long_source_label_with_a_common_prefix_alpha";
        graph->second_source = (struct engine_node) {
            .name = "long_source_label_with_a_common_prefix_beta",
            .run = test_engine_run,
        };
        engine_add_input_impl(&graph->sink, &graph->second_source,
                              test_engine_change, handler_label);
    }
    graph->engine = engine_register(name, &graph->sink, NULL);
    ovs_assert(!engine_get_force_recompute(graph->engine));
    ovs_assert(!engine_canceled(graph->engine));
    graph->registered = true;
}

static void
test_engine_step(struct unixctl_conn *conn, int argc OVS_UNUSED,
                 const char *argv[], void *aux)
{
    struct test_graph *graphs = aux;
    struct test_graph *left = &graphs[0];
    struct test_graph *right = &graphs[1];

    if (!strcmp(argv[1], "start-left")) {
        test_graph_init(left, "left", false);
    } else if (!strcmp(argv[1], "start-right")) {
        test_graph_init(right, "right", false);
    } else if (!strcmp(argv[1], "start-long-graphs")) {
        test_graph_init(left,
                        "shared_engine_name_more_than_thirty_one_bytes_"
                        "left", true);
        test_graph_init(right,
                        "shared_engine_name_more_than_thirty_one_bytes_"
                        "right", true);
    } else if (!strcmp(argv[1], "run-long-left")) {
        test_graph_run(left, true);
        stopwatch_sync();
    } else if (!strcmp(argv[1], "run")) {
        ovs_assert(engine_get_force_recompute(left->engine));
        ovs_assert(!engine_get_force_recompute(right->engine));
        test_graph_run(left, false);
        test_graph_run(right, true);
        ovs_assert(engine_canceled(left->engine));
        ovs_assert(!engine_canceled(right->engine));
        test_graph_run(left, true);
        ovs_assert(!engine_canceled(left->engine));
        stopwatch_sync();
    } else if (!strcmp(argv[1], "sync")) {
        stopwatch_sync();
    } else if (!strcmp(argv[1], "retire-left")) {
        test_graph_cleanup(left);
        test_graph_run(right, true);
        stopwatch_sync();
    } else if (!strcmp(argv[1], "retire-right")) {
        test_graph_cleanup(right);
    } else {
        OVS_NOT_REACHED();
    }
    unixctl_command_reply(conn, NULL);
}

static void
test_engine_exit(struct unixctl_conn *conn, int argc OVS_UNUSED,
                 const char *argv[] OVS_UNUSED, void *aux)
{
    bool *exiting = aux;

    *exiting = true;
    unixctl_command_reply(conn, NULL);
}

static void
test_inc_proc_eng_main(int argc, char *argv[])
{
    ovs_assert(argc == 2);
    struct test_graph graphs[2] = { 0 };
    bool exiting = false;
    struct unixctl_server *server;

    ovs_assert(!unixctl_server_create(argv[1], &server));
    engine_infra_init();
    unixctl_command_register("test/step", "STEP", 1, 1, test_engine_step,
                             graphs);
    unixctl_command_register("exit", "", 0, 0, test_engine_exit, &exiting);

    while (!exiting) {
        unixctl_server_run(server);
        if (!exiting) {
            unixctl_server_wait(server);
            poll_block();
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(graphs); i++) {
        if (graphs[i].registered) {
            test_graph_cleanup(&graphs[i]);
        }
    }
    unixctl_server_destroy(server);
}

OVSTEST_REGISTER("test-inc-proc-eng", test_inc_proc_eng_main);
