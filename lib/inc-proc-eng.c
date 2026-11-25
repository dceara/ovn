/*
 * Copyright (c) 2018 eBay Inc.
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

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "lib/util.h"
#include "openvswitch/dynamic-string.h"
#include "openvswitch/hmap.h"
#include "openvswitch/poll-loop.h"
#include "openvswitch/vlog.h"
#include "ovsdb-idl.h"
#include "inc-proc-eng.h"
#include "timeval.h"
#include "unixctl.h"
#include "sset.h"
#include "lib/stopwatch.h"

VLOG_DEFINE_THIS_MODULE(inc_proc_eng);

#define ENGINE_INITIALIZER(E, NAME)                                  \
    (struct engine) {                                                \
        .name = (NAME),                                              \
        .nodes = VECTOR_EMPTY_INITIALIZER(struct engine_node *),     \
        .stopwatch_keys = SHASH_INITIALIZER(&(E)->stopwatch_keys),   \
        .force_recompute = false,                                    \
        .run_canceled = false,                                       \
        .compute_log_timeout_msec = 500,                             \
    }

/* Keep used names so a new graph cannot inherit the process-wide OVS
 * stopwatch records of a retired graph. */
static struct shash registered_engines =
    SHASH_INITIALIZER(&registered_engines);
static struct sset engine_names = SSET_INITIALIZER(&engine_names);

static const struct engine_context *engine_context;

static const char *engine_node_state_name[EN_STATE_MAX] = {
    [EN_STALE]     = "Stale",
    [EN_UPDATED]   = "Updated",
    [EN_UNCHANGED] = "Unchanged",
    [EN_CANCELED]   = "Canceled",
};

static void
engine_recompute(struct engine *, struct engine_node *, bool allowed,
                 const char *reason_fmt, ...) OVS_PRINTF_FORMAT(4, 5);

void
engine_set_force_recompute(struct engine *e)
{
    e->force_recompute = true;
}

void
engine_set_force_recompute_immediate(struct engine *e)
{
    e->force_recompute = true;
    poll_immediate_wake();
}

void
engine_clear_force_recompute(struct engine *e)
{
    e->force_recompute = false;
}

bool
engine_get_force_recompute(const struct engine *e)
{
    return e->force_recompute;
}

const struct engine_context *
engine_get_context(void)
{
    return engine_context;
}

void
engine_set_context(const struct engine_context *ctx)
{
    engine_context = ctx;
}

/* Builds the topologically sorted 'sorted_nodes' array starting from
 * 'node'.
 */
static void
engine_topo_sort(struct engine_node *node, struct vector *sorted_nodes)
{
    /* It's not so efficient to walk the array of already sorted nodes but
     * we know that sorting is done only once at startup so it's ok for now.
     */
    struct engine_node *sorted_node;
    VECTOR_FOR_EACH (sorted_nodes, sorted_node) {
        if (sorted_node == node) {
            return;
        }
    }

    for (size_t i = 0; i < node->n_inputs; i++) {
        engine_topo_sort(node->inputs[i].node, sorted_nodes);
    }

    vector_push(sorted_nodes, &node);
}

static void
engine_clear_stats(struct engine *e, struct unixctl_conn *conn,
                   int argc OVS_UNUSED, const char *argv[] OVS_UNUSED)
{
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        memset(&node->stats, 0, sizeof node->stats);
    }
    unixctl_command_reply(conn, NULL);
}

static void
engine_dump_stats(struct engine *e, struct unixctl_conn *conn, int argc,
                  const char *argv[])
{
    struct ds dump = DS_EMPTY_INITIALIZER;
    const char *dump_eng_node_name = argc > 0 ? argv[0] : NULL;
    const char *dump_stat_type = argc > 1 ? argv[1] : NULL;

    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (dump_eng_node_name && strcmp(node->name, dump_eng_node_name)) {
            continue;
        }

        if (!dump_stat_type) {
            ds_put_format(&dump,
                         "Node: %s\n"
                         "- recompute: %12"PRIu64"\n"
                         "- compute:   %12"PRIu64"\n"
                         "- cancel:    %12"PRIu64"\n",
                         node->name, node->stats.recompute,
                         node->stats.compute, node->stats.cancel);
        } else {
            if (!strcmp(dump_stat_type, "recompute")) {
                ds_put_format(&dump, "%"PRIu64, node->stats.recompute);
            } else if (!strcmp(dump_stat_type, "compute")) {
                ds_put_format(&dump, "%"PRIu64, node->stats.compute);
            } else if (!strcmp(dump_stat_type, "cancel")) {
                ds_put_format(&dump, "%"PRIu64, node->stats.cancel);
            } else {
                ds_put_format(&dump, "Invalid stat type : %s", dump_stat_type);
            }
        }

        if (dump_eng_node_name) {
            break;
        }
    }

    unixctl_command_reply(conn, ds_cstr(&dump));

    ds_destroy(&dump);
}

static void
engine_trigger_recompute_cmd(struct engine *e, struct unixctl_conn *conn,
                             int argc OVS_UNUSED,
                             const char *argv[] OVS_UNUSED)
{
    engine_trigger_recompute(e);
    unixctl_command_reply(conn, NULL);
}

static void
engine_set_log_timeout_cmd(struct engine *e, struct unixctl_conn *conn,
                           int argc OVS_UNUSED, const char *argv[])
{

    unsigned int timeout;
    if (!str_to_uint(argv[0], 10, &timeout)) {
        unixctl_command_reply_error(conn, "unsigned integer required");
        return;
    }
    e->compute_log_timeout_msec = timeout;
    unixctl_command_reply(conn, NULL);
}

#define ENGINE_STOPWATCH_MAX_NAME 31
#define ENGINE_STOPWATCH_PREFIX "@ip:"

BUILD_ASSERT_DECL(sizeof ENGINE_STOPWATCH_PREFIX "4294967295:4294967295"
                  <= ENGINE_STOPWATCH_MAX_NAME + 1);

static void
engine_list_stopwatches_cmd(struct engine *e, struct unixctl_conn *conn,
                            int argc, const char *argv[])
{
    const char *node_name = argc ? argv[0] : NULL;
    struct ds output = DS_EMPTY_INITIALIZER;

    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (node_name && strcmp(node->name, node_name)) {
            continue;
        }
        ds_put_format(&output, "%s -> %s\n", node->name,
                      node->stopwatch_key);
        for (size_t i = 0; i < node->n_inputs; i++) {
            const struct engine_node_input *input = &node->inputs[i];
            if (input->change_handler) {
                ds_put_format(&output, "%s -> %s\n",
                              input->change_handler_name,
                              input->stopwatch_key);
            }
        }
    }

    unixctl_command_reply(conn, ds_cstr(&output));
    ds_destroy(&output);
}

static void
engine_set_stopwatch_cmd(struct engine *e, struct unixctl_conn *conn,
                         int argc, const char *argv[], bool enabled)
{
    const char *node_name = argc ? argv[0] : NULL;

    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (!node_name || !strcmp(node->name, node_name)) {
            node->stopwatch_enabled = enabled;
            if (node_name) {
                unixctl_command_reply(conn, NULL);
                return;
            }
        }
    }
    if (node_name) {
        unixctl_command_reply_error(conn, "node not found");
    } else {
        unixctl_command_reply(conn, NULL);
    }
}

static void
engine_enable_stopwatches_cmd(struct engine *e, struct unixctl_conn *conn,
                              int argc, const char *argv[])
{
    engine_set_stopwatch_cmd(e, conn, argc, argv, true);
}

static void
engine_disable_stopwatches_cmd(struct engine *e, struct unixctl_conn *conn,
                               int argc, const char *argv[])
{
    engine_set_stopwatch_cmd(e, conn, argc, argv, false);
}

static void
engine_list_cmd(struct unixctl_conn *conn, int argc OVS_UNUSED,
                const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ds output = DS_EMPTY_INITIALIZER;
    const struct shash_node **engines = shash_sort(&registered_engines);

    for (size_t i = 0; i < shash_count(&registered_engines); i++) {
        ds_put_format(&output, "%s\n", engines[i]->name);
    }
    unixctl_command_reply(conn, ds_cstr(&output));
    free(engines);
    ds_destroy(&output);
}

struct engine_command {
    const char *name;
    const char *usage;
    int min_args;
    int max_args;
    void (*run)(struct engine *, struct unixctl_conn *,
                int argc, const char *argv[]);
};

static void
engine_dispatch_cmd(struct unixctl_conn *conn, int argc, const char *argv[],
                     void *aux)
{
    const struct engine_command *command = aux;
    struct engine *e;

    /* Skip the command name and consume an optional leading selector. */
    argc--;
    argv++;
    const char selector[] = "--engine=";
    const char *name = NULL;
    for (int i = 0; i < argc; i++) {
        bool has_value = !strncmp(argv[i], selector, sizeof selector - 1);
        if (!strcmp(argv[i], "--engine")
            || (has_value && (i || !argv[i][sizeof selector - 1]))) {
            unixctl_command_reply_error(conn, "expected one leading "
                                        "--engine=NAME selector");
            return;
        }
        if (has_value) {
            name = argv[i] + sizeof selector - 1;
        }
    }
    if (name) {
        e = shash_find_data(&registered_engines, name);
        if (!e) {
            unixctl_command_reply_error(conn, "unknown registered engine");
            return;
        }
        argc--;
        argv++;
    } else if (shash_is_empty(&registered_engines)) {
        unixctl_command_reply_error(conn, "no registered engines");
        return;
    } else if (shash_count(&registered_engines) > 1) {
        unixctl_command_reply_error(conn, "multiple registered engines; use"
                                    " inc-engine/list-engines and"
                                    " --engine=NAME");
        return;
    } else {
        e = shash_first(&registered_engines)->data;
    }

    if (argc < command->min_args || argc > command->max_args) {
        unixctl_command_reply_error(conn, "wrong number of arguments");
        return;
    }
    command->run(e, conn, argc, argv);
}

void
engine_infra_init(void)
{
    static struct engine_command commands[] = {
        { "inc-engine/show-stats", "[--engine=NAME] [NODE [COUNTER]]", 0, 2,
          engine_dump_stats },
        { "inc-engine/clear-stats", "[--engine=NAME]", 0, 0,
          engine_clear_stats },
        { "inc-engine/recompute", "[--engine=NAME]", 0, 0,
          engine_trigger_recompute_cmd },
        { "inc-engine/compute-log-timeout", "[--engine=NAME] MSEC", 1, 1,
          engine_set_log_timeout_cmd },
        { "inc-engine/list-stopwatches", "[--engine=NAME] [NODE]", 0, 1,
          engine_list_stopwatches_cmd },
        { "inc-engine/enable-stopwatch", "[--engine=NAME] [NODE]", 0, 1,
          engine_enable_stopwatches_cmd },
        { "inc-engine/disable-stopwatch", "[--engine=NAME] [NODE]", 0, 1,
          engine_disable_stopwatches_cmd },
    };

    unixctl_command_register("inc-engine/list-engines", "", 0, 0,
                             engine_list_cmd, NULL);
    for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
        struct engine_command *command = &commands[i];
        unixctl_command_register(command->name, command->usage,
                                 command->min_args, command->max_args + 1,
                                 engine_dispatch_cmd, command);
    }
}

static const char *
engine_register_stopwatch(struct engine *e, uint32_t id, const char *label)
{
    char *key = shash_find_data(&e->stopwatch_keys, label);
    if (!key) {
        size_t n_keys = shash_count(&e->stopwatch_keys);
        ovs_assert(n_keys <= UINT32_MAX);
        key = xasprintf(ENGINE_STOPWATCH_PREFIX"%"PRIu32":%"PRIuSIZE,
                        id, n_keys);
        stopwatch_create(key, SW_MS);
        shash_add(&e->stopwatch_keys, label, key);
    }
    return key;
}

static void
engine_register(struct engine *e)
{
    if (!e->name || !e->name[0]) {
        ovs_fatal(0, "Missing incremental processing engine name");
    }
    if (sset_contains(&engine_names, e->name)) {
        ovs_fatal(0, "Could not register incremental processing engine '%s'",
                  e->name);
    }

    size_t id = sset_count(&engine_names);
    ovs_assert(id <= UINT32_MAX);
    sset_add(&engine_names, e->name);

    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        node->stopwatch_key =
            engine_register_stopwatch(e, id, node->name);
        for (size_t i = 0; i < node->n_inputs; i++) {
            struct engine_node_input *input = &node->inputs[i];
            if (input->change_handler) {
                input->stopwatch_key =
                    engine_register_stopwatch(e, id,
                                              input->change_handler_name);
            }
        }
    }
    shash_add(&registered_engines, e->name, e);
}

static void
engine_get_compute_failure_info(struct engine_node *node)
{
    VLOG_DBG("Node \"%s\" is missing compute failure debug info.", node->name);
}

void
engine_init(struct engine *e, const char *name, struct engine_node *node,
            struct engine_arg *arg)
{
    *e = ENGINE_INITIALIZER(e, name);
    engine_topo_sort(node, &e->nodes);

    struct engine_node *sorted_node;
    VECTOR_FOR_EACH (&e->nodes, sorted_node) {
        if (sorted_node->init) {
            sorted_node->data = sorted_node->init(sorted_node, arg);
        } else {
            sorted_node->data = NULL;
        }
        if (!sorted_node->get_compute_failure_info) {
            /* Provide default get_compute_failure_info implementation. */
            sorted_node->get_compute_failure_info =
                engine_get_compute_failure_info;
        }
    }

    engine_register(e);
}

void
engine_cleanup(struct engine *e)
{
    ovs_assert(e->name);
    struct shash_node *registered = shash_find(&registered_engines, e->name);
    ovs_assert(registered && registered->data == e);
    shash_delete(&registered_engines, registered);

    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (node->clear_tracked_data) {
            node->clear_tracked_data(node->data);
        }

        if (node->cleanup) {
            node->cleanup(node->data);
        }
        free(node->data);
        node->stopwatch_enabled = false;
        node->stopwatch_key = NULL;
        for (size_t i = 0; i < node->n_inputs; i++) {
            node->inputs[i].stopwatch_key = NULL;
        }
    }

    shash_destroy_free_data(&e->stopwatch_keys);
    vector_destroy(&e->nodes);
}

struct engine_node *
engine_get_input(const char *input_name, struct engine_node *node)
{
    size_t i;
    for (i = 0; i < node->n_inputs; i++) {
        if (!strcmp(node->inputs[i].node->name, input_name)) {
            return node->inputs[i].node;
        }
    }
    OVS_NOT_REACHED();
    return NULL;
}

void *
engine_get_input_data(const char *input_name, struct engine_node *node)
{
    struct engine_node *input_node = engine_get_input(input_name, node);
    return engine_get_data(input_node);
}

void
engine_add_input_impl(struct engine_node *node, struct engine_node *input,
                      enum engine_input_handler_result (*change_handler)
                          (struct engine_node *, void *),
                      const char *change_handler_name)
{
    ovs_assert(node->n_inputs < ENGINE_MAX_INPUT);
    node->inputs[node->n_inputs].node = input;
    node->inputs[node->n_inputs].change_handler = change_handler;
    node->inputs[node->n_inputs].change_handler_name = change_handler_name;
    node->n_inputs ++;
}

void
engine_add_input_with_compute_debug_impl(
        struct engine_node *node, struct engine_node *input,
        enum engine_input_handler_result (*change_handler)
            (struct engine_node *, void *),
        void (*get_compute_failure_info)(struct engine_node *),
        const char *change_handler_name)
{
    engine_add_input_impl(node, input, change_handler, change_handler_name);
    node->get_compute_failure_info = get_compute_failure_info;
}

struct ovsdb_idl_index *
engine_ovsdb_node_get_index(struct engine_node *node, const char *name)
{
    struct ed_type_ovsdb_table *ed = node->data;
    for (size_t i = 0; i < ed->n_indexes; i++) {
        if (!strcmp(ed->indexes[i].name, name)) {
            return ed->indexes[i].index;
        }
    }
    OVS_NOT_REACHED();
    return NULL;
}

void
engine_ovsdb_node_add_index(struct engine_node *node, const char *name,
                            struct ovsdb_idl_index *index)
{
    struct ed_type_ovsdb_table *ed = node->data;
    ovs_assert(ed->n_indexes < ENGINE_MAX_OVSDB_INDEX);

    ed->indexes[ed->n_indexes].name = name;
    ed->indexes[ed->n_indexes].index = index;
    ed->n_indexes ++;
}

static void
engine_set_node_state(struct engine_node *node,
                      enum engine_node_state state,
                      const char *reason_fmt, ...)
{
    ovs_assert(state < EN_STATE_MAX);

    if (node->state == state) {
        return;
    }

    if (VLOG_IS_DBG_ENABLED()) {
        va_list args;
        va_start(args, reason_fmt);
        char *reason = xvasprintf(reason_fmt, args);
        VLOG_DBG("node: %s, old_state %s, new_state %s, reason: %s.",
                 node->name, engine_node_state_name[node->state],
                 engine_node_state_name[state], reason);
        va_end(args);
        free(reason);
    }

    node->state = state;
}

static bool
engine_node_valid(struct engine_node *node)
{
    if (node->state == EN_UPDATED || node->state == EN_UNCHANGED) {
        return true;
    }

    if (node->is_valid) {
        return node->is_valid(node);
    }
    return false;
}

bool
engine_node_changed(struct engine_node *node)
{
    return node->state == EN_UPDATED;
}

bool
engine_has_run(const struct engine *e)
{
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (node->state != EN_STALE) {
            return true;
        }
    }
    return false;
}

bool
engine_has_updated(const struct engine *e)
{
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (node->state == EN_UPDATED) {
            return true;
        }
    }
    return false;
}

bool
engine_canceled(const struct engine *e)
{
    return e->run_canceled;
}

void *
engine_get_data(struct engine_node *node)
{
    if (engine_node_valid(node)) {
        return node->data;
    }
    return NULL;
}

void *
engine_get_internal_data(struct engine_node *node)
{
    return node->data;
}

void
engine_init_run(struct engine *e)
{
    VLOG_DBG("Initializing new run for engine %s", e->name);
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        engine_set_node_state(node, EN_STALE, "engine_init_run");

        if (node->clear_tracked_data) {
            node->clear_tracked_data(node->data);
        }
    }
}

static enum engine_node_state
run_recompute_callback(struct engine_node *node)
{
    enum engine_node_state ret;
    if (node->stopwatch_enabled) {
        stopwatch_start(node->stopwatch_key, time_msec());
    }
    ret = node->run(node, node->data);
    if (node->stopwatch_enabled) {
        stopwatch_stop(node->stopwatch_key, time_msec());
    }
    return ret;
}

static enum engine_input_handler_result
run_change_handler(struct engine_node *node, struct engine_node_input *input)
{
    enum engine_input_handler_result ret;
    if (node->stopwatch_enabled) {
        stopwatch_start(input->stopwatch_key, time_msec());
    }
    ret = input->change_handler(node, node->data);
    if (node->stopwatch_enabled) {
        stopwatch_stop(input->stopwatch_key, time_msec());
    }
    return ret;
}

/* Do a full recompute (or at least try). If we're not allowed then
 * mark the node as "canceled".
 */
static void
engine_recompute(struct engine *e, struct engine_node *node, bool allowed,
                 const char *reason_fmt, ...)
{
    char *reason = NULL;
    va_list reason_args;

    va_start(reason_args, reason_fmt);
    reason = xvasprintf(reason_fmt, reason_args);
    va_end(reason_args);

    if (node->sb_write && !allowed) {
        VLOG_DBG("engine %s, node: %s, recompute (%s) canceled",
                 e->name, node->name, reason);
        engine_set_node_state(node, EN_CANCELED, "recompute not allowed");
        goto done;
    }

    /* Clear tracked data before calling run() so that partially tracked data
     * from some of the change handler executions are cleared. */
    if (node->clear_tracked_data) {
        node->clear_tracked_data(node->data);
    }

    /* Run the node handler which might change state. */
    long long int now = time_msec();
    engine_set_node_state(node, run_recompute_callback(node),
                          "recompute run() result");
    node->stats.recompute++;
    long long int delta_time = time_msec() - now;
    if (delta_time > e->compute_log_timeout_msec) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(20, 10);
        VLOG_INFO_RL(&rl, "engine %s, node: %s, recompute (%s) took %lldms",
                     e->name, node->name, reason, delta_time);
    } else {
        VLOG_DBG("engine %s, node: %s, recompute (%s) took %lldms",
                 e->name, node->name, reason, delta_time);
    }
done:
    free(reason);
}

/* Return true if the node could be computed, false otherwise. */
static bool
engine_compute(struct engine *e, struct engine_node *node,
               bool recompute_allowed)
{
    for (size_t i = 0; i < node->n_inputs; i++) {
        struct engine_node *input_node = node->inputs[i].node;

        /* If the input node data changed call its change handler. */
        if (input_node->state == EN_UPDATED) {
            /* If the input change can't be handled incrementally, run
             * the node handler.
             */
            long long int now = time_msec();
            enum engine_input_handler_result handled;
            handled = run_change_handler(node, &node->inputs[i]);
            long long int delta_time = time_msec() - now;
            if (delta_time > e->compute_log_timeout_msec) {
                static struct vlog_rate_limit rl =
                    VLOG_RATE_LIMIT_INIT(20, 10);
                VLOG_INFO_RL(&rl, "engine %s, node: %s, handler for input %s"
                             " took %lldms", e->name, node->name,
                             input_node->name, delta_time);
            } else {
                VLOG_DBG("engine %s, node: %s, handler for input %s took"
                         " %lldms", e->name, node->name, input_node->name,
                         delta_time);
            }
            if (handled == EN_UNHANDLED) {
                input_node->get_compute_failure_info(input_node);
                engine_recompute(e, node, recompute_allowed,
                                 "failed handler for input %s",
                                 input_node->name);
                return (node->state != EN_CANCELED);
            } else if (!engine_node_changed(node)) {
                /* We only want to update the state if the node is unchanged.
                 * Otherwise, handlers might change the state from EN_UPDATED
                 * back to EN_UNCHANGED.
                 */
                engine_set_node_state(node, (enum engine_node_state) handled,
                                      "input %s updated", input_node->name);
            }
        }
    }
    node->stats.compute++;

    return true;
}

static void
engine_run_node(struct engine *e, struct engine_node *node,
                bool recompute_allowed)
{
    if (!node->n_inputs) {
        /* Run the node handler which might change state. */
        engine_set_node_state(node, run_recompute_callback(node),
                              "run() result due to having no inputs");
        node->stats.recompute++;
        return;
    }

    if (e->force_recompute) {
        engine_recompute(e, node, recompute_allowed, "forced");
        return;
    }

    /* If any of the inputs updated data but there is no change_handler, then
     * recompute the current node too.
     */
    bool need_compute = false;
    for (size_t i = 0; i < node->n_inputs; i++) {
        struct engine_node *input_node = node->inputs[i].node;
        if (input_node->state == EN_UPDATED) {
            need_compute = true;

            /* Trigger a recompute if we don't have a change handler. */
            if (!node->inputs[i].change_handler) {
                engine_recompute(e, node, recompute_allowed,
                                 "missing handler for input %s",
                                 input_node->name);
                input_node->get_compute_failure_info(input_node);
                return;
            }
        }
    }

    if (need_compute) {
        /* If we couldn't compute the node we either canceled or triggered
         * a full recompute. In any case, stop processing.
         */
        if (!engine_compute(e, node, recompute_allowed)) {
            return;
        }
    }

    /* If we reached this point, either the node was updated or its state is
     * still valid.
     */
    if (!engine_node_changed(node)) {
        engine_set_node_state(node, EN_UNCHANGED, "no change detected");
    }
}

void
engine_run(struct engine *e, bool recompute_allowed)
{
    /* If the last run was canceled skip the incremental run because a
     * recompute is needed first.
     */
    if (!recompute_allowed && e->run_canceled) {
        return;
    }

    struct ovsdb_idl_txn *sb_txn = engine_get_context()->ovnsb_idl_txn;

    e->run_canceled = false;
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        ovsdb_idl_txn_assert_read_only(sb_txn, !node->sb_write);
        engine_run_node(e, node, recompute_allowed);
        ovsdb_idl_txn_assert_read_only(sb_txn, false);

        if (node->state == EN_CANCELED) {
            node->stats.cancel++;
            e->run_canceled = true;
            return;
        }
    }
}

bool
engine_need_run(struct engine *e)
{
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        /* Check only leaf nodes for updates. */
        if (node->n_inputs) {
            continue;
        }

        engine_set_node_state(node, run_recompute_callback(node),
                              "checking if engine needs to be run");
        node->stats.recompute++;
        VLOG_DBG("input node: %s, state: %s", node->name,
                 engine_node_state_name[node->state]);
        if (node->state == EN_UPDATED) {
            return true;
        }
    }
    return false;
}

void
engine_trigger_recompute(struct engine *e)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(20, 10);
    VLOG_INFO_RL(&rl, "User triggered force recompute for engine %s.",
                 e->name);
    engine_set_force_recompute_immediate(e);
}

static void
engine_dump_node(struct engine_node *node, struct sset *visited_nodes)
{
    if (sset_contains(visited_nodes, node->name)) {
        return;
    }
    sset_add(visited_nodes, node->name);

    printf("\t%s [style=filled, shape=box, fillcolor=white, "
           "label=\"%s\"];\n",
           node->name, node->name);
    for (size_t i = 0; i < node->n_inputs; i++) {
        const char *label = node->inputs[i].change_handler
                            ? node->inputs[i].change_handler_name
                            : NULL;

        printf("\t%s -> %s [label=\"%s\"];\n",
               node->inputs[i].node->name, node->name,
               label ? label : "");

        engine_dump_node(node->inputs[i].node, visited_nodes);
    }
}

void
engine_dump_graph(const struct engine *e, const char *node_name)
{
    printf("digraph \"Incremental-Processing-Engine\" {\n");
    printf("\trankdir=LR;\n");

    struct sset visited_nodes = SSET_INITIALIZER(&visited_nodes);
    struct engine_node *node;
    VECTOR_FOR_EACH (&e->nodes, node) {
        if (node_name && strcmp(node->name, node_name)) {
            continue;
        }

        engine_dump_node(node, &visited_nodes);

        if (node_name) {
            break;
        }
    }

    sset_destroy(&visited_nodes);
    printf("}\n");
}
