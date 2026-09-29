/*
 * Copyright (c) 2026 Red Hat, Inc.
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
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compiler.h"
#include "jsonrpc.h"
#include "openvswitch/json.h"
#include "openvswitch/shash.h"
#include "openvswitch/vlog.h"
#include "ovsdb-data.h"
#include "ovsdb-error.h"
#include "ovsdb/column.h"
#include "ovsdb/file.h"
#include "ovsdb/ovsdb.h"
#include "ovsdb/row.h"
#include "ovsdb/storage.h"
#include "ovsdb/table.h"
#include "ovsdb/transaction.h"
#include "socket-util.h"
#include "stream.h"
#include "svec.h"
#include "timeval.h"
#include "util.h"

#include "lib/ovn-util.h"

VLOG_DEFINE_THIS_MODULE(ovsdb_replay);

/* One database being replayed. */
struct replay_db {
    char *filename;                /* Source .db file path. */
    char *remote;                  /* JSON-RPC target (e.g., "unix:nb.sock"). */
    char *db_name;                 /* Schema name (e.g., "OVN_Northbound"). */
    struct ovsdb *shadow;          /* In-memory shadow database. */
    struct jsonrpc *rpc;           /* JSON-RPC connection to server. */
    int index;                     /* For stable sort tiebreaking. */
};

/* One transaction queued for replay. */
struct replay_txn {
    long long int date;            /* _date field (ms since epoch). */
    struct json *json;             /* Raw on-disk transaction JSON. */
    struct replay_db *db;          /* Which database this belongs to. */
    size_t seq;                    /* Original order within the DB. */
};

/* Accumulates wire-format ops during txn change traversal. */
struct wire_ops {
    struct json *ops;              /* JSON array: [db_name, op1, op2, ...]. */
    struct svec ifaces_to_add;     /* Interfaces to create before send. */
    struct svec ifaces_to_del;     /* Interfaces to delete after send. */
};

/* Builds a where clause matching a row by UUID:
 * [["_uuid", "==", ["uuid", "UUID_STRING"]]]. */
static struct json *
make_uuid_where(const struct uuid *uuid)
{
    char uuid_s[UUID_LEN + 1];
    snprintf(uuid_s, sizeof uuid_s, UUID_FMT, UUID_ARGS(uuid));

    struct json *uuid_json = json_array_create_2(
        json_string_create("uuid"),
        json_string_create(uuid_s));
    struct json *condition = json_array_create_3(
        json_string_create("_uuid"),
        json_string_create("=="),
        uuid_json);
    return json_array_create_1(condition);
}

/* Checks whether 'row' is in the "Interface" table and has an
 * external_ids entry with key "iface-id".  If so, returns the
 * interface name (from the "name" column).  Otherwise returns NULL.
 * Internal interfaces (type "internal") are skipped because OVS
 * creates them itself. */
static const char *
row_has_iface_id(const struct ovsdb_row *row)
{
    const char *table_name = row->table->schema->name;
    if (strcmp(table_name, "Interface")) {
        return NULL;
    }

    const struct ovsdb_column *ext_ids_col =
        ovsdb_table_schema_get_column(row->table->schema, "external_ids");
    const struct ovsdb_column *name_col =
        ovsdb_table_schema_get_column(row->table->schema, "name");
    const struct ovsdb_column *type_col =
        ovsdb_table_schema_get_column(row->table->schema, "type");
    if (!ext_ids_col || !name_col || !type_col) {
        return NULL;
    }

    /* Internal interfaces are created by OVS itself; do not create
     * dummy Linux interfaces for them. */
    const struct ovsdb_datum *type_datum = &row->fields[type_col->index];
    if (type_datum->n > 0
        && !strcmp(json_string(type_datum->keys[0].s), "internal")) {
        return NULL;
    }

    const struct ovsdb_datum *ext_ids = &row->fields[ext_ids_col->index];
    for (unsigned int i = 0; i < ext_ids->n; i++) {
        if (!strcmp(json_string(ext_ids->keys[i].s), "iface-id")) {
            const struct ovsdb_datum *name_datum =
                &row->fields[name_col->index];
            if (name_datum->n > 0) {
                return json_string(name_datum->keys[0].s);
            }
            return NULL;
        }
    }
    return NULL;
}

/* Computes the difference between 'old_datum' and 'new_datum' for a
 * non-scalar column 'col' and appends "insert" and/or "delete" mutations
 * to the 'mutations' JSON array.  Both datums must be sorted by key.
 *
 * For sets, insertions are elements in 'new_datum' but not 'old_datum'
 * and deletions are the reverse.  For maps, a key present in both datums
 * but with a different value is handled by first deleting the old key
 * and then inserting the new key+value pair, because the OVSDB "insert"
 * mutator skips existing keys rather than overwriting them. */
static void
build_mutations_for_column(const struct ovsdb_datum *old_datum,
                           const struct ovsdb_datum *new_datum,
                           const struct ovsdb_column *col,
                           struct json *mutations)
{
    bool is_map = ovsdb_type_is_map(&col->type);
    enum ovsdb_atomic_type key_type = col->type.key.type;
    enum ovsdb_atomic_type value_type = col->type.value.type;

    struct json *ins_elems = json_array_create_empty();
    struct json *del_elems = json_array_create_empty();
    bool any_ins = false;
    bool any_del = false;

    /* Merge-walk: both datums are sorted by key. */
    size_t oi = 0, ni = 0;
    while (oi < old_datum->n && ni < new_datum->n) {
        int cmp = ovsdb_atom_compare_3way(&old_datum->keys[oi],
                                           &new_datum->keys[ni],
                                           key_type);
        if (cmp < 0) {
            /* In old but not new: deletion. */
            json_array_add(del_elems,
                           ovsdb_atom_to_json(&old_datum->keys[oi], key_type));
            any_del = true;
            oi++;
        } else if (cmp > 0) {
            /* In new but not old: insertion. */
            if (is_map) {
                json_array_add(ins_elems, json_array_create_2(
                    ovsdb_atom_to_json(&new_datum->keys[ni], key_type),
                    ovsdb_atom_to_json(&new_datum->values[ni], value_type)));
            } else {
                json_array_add(ins_elems,
                               ovsdb_atom_to_json(&new_datum->keys[ni],
                                                   key_type));
            }
            any_ins = true;
            ni++;
        } else {
            /* Same key in both.  For maps, check if value changed. */
            if (is_map
                && ovsdb_atom_compare_3way(&old_datum->values[oi],
                                            &new_datum->values[ni],
                                            value_type)) {
                /* Value changed: delete old key then insert new key+value.
                 * The "insert" mutator skips existing keys, so we must
                 * delete first to allow the re-insertion. */
                json_array_add(del_elems,
                               ovsdb_atom_to_json(&old_datum->keys[oi],
                                                   key_type));
                any_del = true;
                json_array_add(ins_elems, json_array_create_2(
                    ovsdb_atom_to_json(&new_datum->keys[ni], key_type),
                    ovsdb_atom_to_json(&new_datum->values[ni], value_type)));
                any_ins = true;
            }
            oi++;
            ni++;
        }
    }

    /* Remaining old elements are deletions. */
    for (; oi < old_datum->n; oi++) {
        json_array_add(del_elems,
                       ovsdb_atom_to_json(&old_datum->keys[oi], key_type));
        any_del = true;
    }

    /* Remaining new elements are insertions. */
    for (; ni < new_datum->n; ni++) {
        if (is_map) {
            json_array_add(ins_elems, json_array_create_2(
                ovsdb_atom_to_json(&new_datum->keys[ni], key_type),
                ovsdb_atom_to_json(&new_datum->values[ni], value_type)));
        } else {
            json_array_add(ins_elems,
                           ovsdb_atom_to_json(&new_datum->keys[ni], key_type));
        }
        any_ins = true;
    }

    /* Emit delete mutation: ["col", "delete", ["set", [keys...]]].
     * Map deletions also use "set" format (delete by key only). */
    if (any_del) {
        struct json *mutation = json_array_create_3(
            json_string_create(col->name),
            json_string_create("delete"),
            json_array_create_2(json_string_create("set"), del_elems));
        json_array_add(mutations, mutation);
    } else {
        json_destroy(del_elems);
    }

    /* Emit insert mutation.  Sets use ["set", [elems...]]; maps use
     * ["map", [[k,v], ...]]. */
    if (any_ins) {
        const char *wrapper = is_map ? "map" : "set";
        struct json *mutation = json_array_create_3(
            json_string_create(col->name),
            json_string_create("insert"),
            json_array_create_2(json_string_create(wrapper), ins_elems));
        json_array_add(mutations, mutation);
    } else {
        json_destroy(ins_elems);
    }
}

/* Callback for ovsdb_txn_for_each_change().  Converts each row change
 * (insert, update, or delete) into a wire-format transact operation
 * and appends it to the wire_ops array. */
static bool
build_wire_ops_cb(const struct ovsdb_row *old,
                  const struct ovsdb_row *new,
                  const unsigned long int *changed OVS_UNUSED,
                  void *aux)
{
    struct wire_ops *w = aux;
    const struct ovsdb_row *row = new ? new : old;
    const char *table_name = row->table->schema->name;
    const struct uuid *row_uuid = ovsdb_row_get_uuid(row);

    if (!old && new) {
        /* INSERT: serialize all non-default persistent columns. */
        struct json *op = json_object_create();
        json_object_put_string(op, "op", "insert");
        json_object_put_string(op, "table", table_name);

        /* Use "uuid" field to preserve original UUID. */
        char uuid_s[UUID_LEN + 1];
        snprintf(uuid_s, sizeof uuid_s, UUID_FMT, UUID_ARGS(row_uuid));
        json_object_put_string(op, "uuid", uuid_s);

        struct json *row_json = json_object_create();
        struct shash_node *node;
        SHASH_FOR_EACH (node, &row->table->schema->columns) {
            const struct ovsdb_column *col = node->data;
            if (col->index >= OVSDB_N_STD_COLUMNS && col->persistent) {
                const struct ovsdb_datum *datum = &new->fields[col->index];
                if (!ovsdb_datum_is_default(datum, &col->type)) {
                    json_object_put(row_json, col->name,
                                    ovsdb_datum_to_json(datum, &col->type));
                }
            }
        }
        json_object_put(op, "row", row_json);
        json_array_add(w->ops, op);

        const char *iface_name = row_has_iface_id(new);
        if (iface_name) {
            svec_add(&w->ifaces_to_add, iface_name);
        }

    } else if (old && !new) {
        /* DELETE: use UUID-based where clause. */
        struct json *op = json_object_create();
        json_object_put_string(op, "op", "delete");
        json_object_put_string(op, "table", table_name);
        json_object_put(op, "where", make_uuid_where(row_uuid));
        json_array_add(w->ops, op);

        const char *iface_name = row_has_iface_id(old);
        if (iface_name) {
            svec_add(&w->ifaces_to_del, iface_name);
        }

    } else if (old && new) {
        /* UPDATE: compare columns manually since changed[] is all-zeros
         * before precommit.  Scalar columns go into an "update" op;
         * set/map columns go into a "mutate" op with insert/delete
         * mutations so that each transaction independently adds or
         * removes its own entries. */
        struct json *row_json = json_object_create();
        struct json *mutations = json_array_create_empty();
        bool any_scalar = false;

        struct shash_node *node;
        SHASH_FOR_EACH (node, &row->table->schema->columns) {
            const struct ovsdb_column *col = node->data;
            if (col->index >= OVSDB_N_STD_COLUMNS && col->persistent
                && !ovsdb_datum_equals(&old->fields[col->index],
                                       &new->fields[col->index],
                                       &col->type)) {
                if (ovsdb_type_is_scalar(&col->type)) {
                    json_object_put(
                        row_json, col->name,
                        ovsdb_datum_to_json(&new->fields[col->index],
                                            &col->type));
                    any_scalar = true;
                } else {
                    build_mutations_for_column(&old->fields[col->index],
                                               &new->fields[col->index],
                                               col, mutations);
                }
            }
        }

        if (any_scalar) {
            struct json *op = json_object_create();
            json_object_put_string(op, "op", "update");
            json_object_put_string(op, "table", table_name);
            json_object_put(op, "where", make_uuid_where(row_uuid));
            json_object_put(op, "row", row_json);
            json_array_add(w->ops, op);
        } else {
            json_destroy(row_json);
        }

        if (json_array_size(mutations) > 0) {
            struct json *op = json_object_create();
            json_object_put_string(op, "op", "mutate");
            json_object_put_string(op, "table", table_name);
            json_object_put(op, "where", make_uuid_where(row_uuid));
            json_object_put(op, "mutations", mutations);
            json_array_add(w->ops, op);
        } else {
            json_destroy(mutations);
        }
    }
    return true;
}

/* Replays a single transaction against the shadow DB and sends the
 * resulting wire-format operations to the remote ovsdb-server.
 * Returns true on success, false on error. */
static bool
replay_one_txn(struct replay_db *rdb, struct json *txn_json,
               size_t txn_num, bool dry_run, bool verbose)
{
    /* Parse into ovsdb_txn on the shadow DB. */
    struct ovsdb_txn *txn;
    struct ovsdb_error *error;
    error = ovsdb_file_txn_from_json(rdb->shadow, txn_json, false, &txn);
    if (error) {
        char *msg = ovsdb_error_to_string_free(error);
        VLOG_WARN("txn %"PRIuSIZE": parse error: %s", txn_num, msg);
        free(msg);
        return false;
    }

    /* Walk changes to build wire-format ops (before commit). */
    struct wire_ops w;
    w.ops = json_array_create_empty();
    svec_init(&w.ifaces_to_add);
    svec_init(&w.ifaces_to_del);
    json_array_add(w.ops, json_string_create(rdb->db_name));
    ovsdb_txn_for_each_change(txn, build_wire_ops_cb, &w);

    /* Commit locally to update shadow state for the next transaction. */
    error = ovsdb_txn_replay_commit(txn);
    if (error) {
        char *msg = ovsdb_error_to_string_free(error);
        VLOG_ERR("txn %"PRIuSIZE": shadow commit failed: %s", txn_num, msg);
        free(msg);
        json_destroy(w.ops);
        svec_destroy(&w.ifaces_to_add);
        svec_destroy(&w.ifaces_to_del);
        return false;
    }

    /* Send wire ops to remote server (if any ops besides db_name). */
    size_t n_ops = json_array_size(w.ops);
    if (n_ops <= 1) {
        /* No actual operations (empty txn). */
        json_destroy(w.ops);
        svec_destroy(&w.ifaces_to_add);
        svec_destroy(&w.ifaces_to_del);
        return true;
    }

    if (dry_run) {
        for (size_t i = 0; i < w.ifaces_to_add.n; i++) {
            VLOG_INFO("txn %"PRIuSIZE": would create dummy interface %s",
                      txn_num, w.ifaces_to_add.names[i]);
        }
        char *s = json_to_string(w.ops, JSSF_PRETTY);
        fputs(s, stdout);
        fputc('\n', stdout);
        free(s);
        json_destroy(w.ops);
        svec_destroy(&w.ifaces_to_add);
        svec_destroy(&w.ifaces_to_del);
        return true;
    }

    if (verbose) {
        VLOG_INFO("txn %"PRIuSIZE": sending %"PRIuSIZE" ops to %s",
                  txn_num, n_ops - 1, rdb->remote);
    }

    /* Create dummy interfaces before sending the transaction so that
     * ovs-vswitchd finds the backing network devices. */
    for (size_t i = 0; i < w.ifaces_to_add.n; i++) {
        const char *name = w.ifaces_to_add.names[i];
        char *cmd = xasprintf(
            "ip link add %s type dummy && ip link set %s up", name, name);
        int ret = system(cmd);
        if (ret) {
            VLOG_WARN("txn %"PRIuSIZE": failed to create dummy "
                      "interface %s (exit %d)", txn_num, name, ret);
        } else if (verbose) {
            VLOG_INFO("txn %"PRIuSIZE": created dummy interface %s",
                      txn_num, name);
        }
        free(cmd);
    }

    /* jsonrpc_create_request() takes ownership of w.ops. */
    struct jsonrpc_msg *request =
        jsonrpc_create_request("transact", w.ops, NULL);
    struct jsonrpc_msg *reply;
    int rpc_error = jsonrpc_transact_block(rdb->rpc, request, &reply);
    if (rpc_error) {
        VLOG_WARN("txn %"PRIuSIZE": JSON-RPC error: %s",
                  txn_num, ovs_retval_to_string(rpc_error));
        svec_destroy(&w.ifaces_to_add);
        svec_destroy(&w.ifaces_to_del);
        return false;
    }

    /* Check for errors in the reply. */
    bool success = true;
    if (reply->result && reply->result->type == JSON_ARRAY) {
        for (size_t i = 0; i < json_array_size(reply->result); i++) {
            const struct json *r = json_array_at(reply->result, i);
            if (r && r->type == JSON_OBJECT) {
                const struct json *err =
                    shash_find_data(json_object(r), "error");
                if (err && err->type == JSON_STRING) {
                    VLOG_WARN("txn %"PRIuSIZE" op %"PRIuSIZE": %s",
                              txn_num, i, json_string(err));
                    success = false;
                }
            }
        }
    }
    jsonrpc_msg_destroy(reply);

    /* Delete dummy interfaces after the transaction has been processed. */
    for (size_t i = 0; i < w.ifaces_to_del.n; i++) {
        const char *name = w.ifaces_to_del.names[i];
        char *cmd = xasprintf("ip link del %s", name);
        system(cmd);
        free(cmd);
    }

    svec_destroy(&w.ifaces_to_add);
    svec_destroy(&w.ifaces_to_del);
    return success;
}

/* Opens a JSON-RPC connection to 'remote', blocking until connected. */
static struct jsonrpc *
open_jsonrpc(const char *remote)
{
    struct stream *stream;
    int error;
    error = stream_open_block(jsonrpc_stream_open(remote, &stream,
                                                   DSCP_DEFAULT),
                              -1, &stream);
    if (error) {
        ovs_fatal(error, "could not connect to \"%s\"", remote);
    }
    return jsonrpc_open(stream);
}

/* Reads all transaction records from a .db file into the txn array.
 * Also creates the shadow DB and determines the database name from
 * the schema. */
static void
read_db_txns(struct replay_db *rdb, struct replay_txn **txns,
             size_t *n_txns, size_t *allocated_txns)
{
    struct ovsdb_storage *storage =
        ovsdb_storage_open_standalone(rdb->filename, false);
    struct ovsdb_schema *schema = ovsdb_storage_read_schema(storage);

    /* Auto-detect database name from schema if not given. */
    if (!rdb->db_name) {
        rdb->db_name = xstrdup(schema->name);
    }

    /* Create shadow DB. */
    rdb->shadow = ovsdb_create(ovsdb_schema_clone(schema),
                               ovsdb_storage_create_unbacked(NULL));

    /* Read all txn records. */
    size_t seq = 0;
    for (;;) {
        struct json *txn_json;
        struct ovsdb_schema *schema2;
        struct ovsdb_error *error =
            ovsdb_storage_read(storage, &schema2, &txn_json, NULL);
        if (error) {
            char *msg = ovsdb_error_to_string_free(error);
            VLOG_WARN("%s: error reading txn: %s", rdb->filename, msg);
            free(msg);
            break;
        }
        ovs_assert(!schema2);
        if (!txn_json) {
            break;  /* End of file. */
        }

        /* Extract _date. */
        long long int date = 0;
        struct json *date_json = shash_find_data(json_object(txn_json),
                                                  "_date");
        if (date_json && date_json->type == JSON_INTEGER) {
            date = json_integer(date_json);
        }

        /* Append to txn array. */
        if (*n_txns >= *allocated_txns) {
            *allocated_txns = *allocated_txns ? *allocated_txns * 2 : 64;
            *txns = xrealloc(*txns, *allocated_txns * sizeof **txns);
        }
        struct replay_txn *rt = &(*txns)[(*n_txns)++];
        rt->date = date;
        rt->json = txn_json;
        rt->db = rdb;
        rt->seq = seq++;
    }

    ovsdb_schema_destroy(schema);
    ovsdb_storage_close(storage);
}

/* Comparator for qsort().  Sorts transactions by (date, db_index, seq)
 * for a stable merge-sort across multiple databases. */
static int
compare_replay_txns(const void *a_, const void *b_)
{
    const struct replay_txn *a = a_;
    const struct replay_txn *b = b_;

    if (a->date != b->date) {
        return a->date < b->date ? -1 : 1;
    }
    if (a->db->index != b->db->index) {
        return a->db->index < b->db->index ? -1 : 1;
    }
    if (a->seq != b->seq) {
        return a->seq < b->seq ? -1 : 1;
    }
    return 0;
}

/* Parses a --db FILE,REMOTE[,DB_NAME] argument.  Returns a new replay_db. */
static struct replay_db *
parse_db_spec(const char *spec, int index)
{
    struct replay_db *rdb = xzalloc(sizeof *rdb);
    rdb->index = index;

    /* Split by commas.  Expect 2 or 3 parts. */
    char *copy = xstrdup(spec);
    char *save = NULL;
    char *file = strtok_r(copy, ",", &save);
    char *remote = strtok_r(NULL, ",", &save);
    char *db_name = strtok_r(NULL, ",", &save);

    if (!file || !remote) {
        ovs_fatal(0, "invalid --db spec \"%s\": "
                  "expected FILE,REMOTE[,DB_NAME]", spec);
    }

    rdb->filename = xstrdup(file);
    rdb->remote = xstrdup(remote);
    if (db_name) {
        rdb->db_name = xstrdup(db_name);
    }
    /* db_name will be auto-detected from schema if NULL. */

    free(copy);
    return rdb;
}

/* Prints usage information. */
static void
usage(void)
{
    printf("Usage: ovsdb-replay [OPTIONS]\n"
           "\n"
           "Replays OVSDB transaction logs against running servers.\n"
           "\n"
           "Options:\n"
           "  --db FILE,REMOTE[,DB_NAME]  database spec (required, "
           "may repeat)\n"
           "  --speed FACTOR              replay speed "
           "(0=fastest, 1.0=realtime; default 0)\n"
           "  --start-txn N               start from transaction N "
           "(0-based, default 0)\n"
           "  --stop-txn N                stop after transaction N\n"
           "  --dry-run                   print wire-format JSON "
           "to stdout, don't send\n"
           "  --verbose                   log each transaction "
           "as it is replayed\n"
           "  -h, --help                  show this help\n");
    exit(EXIT_SUCCESS);
}

int
main(int argc, char *argv[])
{
    ovn_set_program_name(argv[0]);
    vlog_init();

    /* Parse options. */
    double speed = 0;
    bool dry_run = false;
    bool verbose = false;
    size_t start_txn = 0;
    size_t stop_txn = SIZE_MAX;

    struct replay_db **dbs = NULL;
    size_t n_dbs = 0;
    size_t allocated_dbs = 0;

    enum {
        OPT_DB = UCHAR_MAX + 1,
        OPT_SPEED,
        OPT_DRY_RUN,
        OPT_VERBOSE,
        OPT_START_TXN,
        OPT_STOP_TXN,
    };

    static const struct option long_options[] = {
        {"db", required_argument, NULL, OPT_DB},
        {"speed", required_argument, NULL, OPT_SPEED},
        {"dry-run", no_argument, NULL, OPT_DRY_RUN},
        {"verbose", no_argument, NULL, OPT_VERBOSE},
        {"start-txn", required_argument, NULL, OPT_START_TXN},
        {"stop-txn", required_argument, NULL, OPT_STOP_TXN},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "h", long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case OPT_DB:
            if (n_dbs >= allocated_dbs) {
                allocated_dbs = allocated_dbs ? allocated_dbs * 2 : 4;
                dbs = xrealloc(dbs, allocated_dbs * sizeof *dbs);
            }
            dbs[n_dbs] = parse_db_spec(optarg, n_dbs);
            n_dbs++;
            break;

        case OPT_SPEED:
            speed = atof(optarg);
            break;

        case OPT_DRY_RUN:
            dry_run = true;
            break;

        case OPT_VERBOSE:
            verbose = true;
            break;

        case OPT_START_TXN:
            start_txn = strtoull(optarg, NULL, 10);
            break;

        case OPT_STOP_TXN:
            stop_txn = strtoull(optarg, NULL, 10);
            break;

        case 'h':
            usage();
            break;

        case '?':
            exit(EXIT_FAILURE);

        default:
            abort();
        }
    }

    if (!n_dbs) {
        ovs_fatal(0, "at least one --db argument is required");
    }

    /* Read all transactions from all databases. */
    struct replay_txn *txns = NULL;
    size_t n_txns = 0;
    size_t allocated_txns = 0;

    for (size_t i = 0; i < n_dbs; i++) {
        read_db_txns(dbs[i], &txns, &n_txns, &allocated_txns);
    }

    VLOG_INFO("read %"PRIuSIZE" transactions from %"PRIuSIZE" databases",
              n_txns, n_dbs);

    /* Sort by (date, db_index, seq). */
    qsort(txns, n_txns, sizeof *txns, compare_replay_txns);

    /* Connect to remote servers. */
    if (!dry_run) {
        for (size_t i = 0; i < n_dbs; i++) {
            dbs[i]->rpc = open_jsonrpc(dbs[i]->remote);
        }
    }

    /* Replay. */
    long long int prev_date = 0;
    size_t n_ok = 0, n_err = 0, n_skip = 0;
    long long int start_time = time_wall_msec();

    for (size_t i = 0; i < n_txns; i++) {
        if (i < start_txn) {
            /* Still need to apply to shadow for state consistency. */
            struct ovsdb_txn *txn;
            struct ovsdb_error *error =
                ovsdb_file_txn_from_json(txns[i].db->shadow,
                                         txns[i].json, false, &txn);
            if (!error) {
                error = ovsdb_txn_replay_commit(txn);
                if (error) {
                    ovsdb_error_destroy(error);
                }
            } else {
                ovsdb_error_destroy(error);
            }
            n_skip++;
            continue;
        }
        if (i >= stop_txn) {
            break;
        }

        /* Timing delay. */
        if (speed > 0 && prev_date > 0 && txns[i].date > prev_date) {
            long long int gap_ms = txns[i].date - prev_date;
            long long int sleep_us = (long long int)(gap_ms * 1000.0 / speed);
            if (sleep_us > 0) {
                usleep(sleep_us);
            }
        }
        prev_date = txns[i].date;

        if (replay_one_txn(txns[i].db, txns[i].json, i,
                           dry_run, verbose)) {
            n_ok++;
        } else {
            n_err++;
        }
    }

    long long int elapsed = time_wall_msec() - start_time;
    printf("Replay complete: %"PRIuSIZE" ok, %"PRIuSIZE" errors, "
           "%"PRIuSIZE" skipped, %lld ms elapsed\n",
           n_ok, n_err, n_skip, elapsed);

    /* Cleanup. */
    for (size_t i = 0; i < n_txns; i++) {
        json_destroy(txns[i].json);
    }
    free(txns);

    for (size_t i = 0; i < n_dbs; i++) {
        if (dbs[i]->rpc) {
            jsonrpc_close(dbs[i]->rpc);
        }
        ovsdb_destroy(dbs[i]->shadow);
        free(dbs[i]->filename);
        free(dbs[i]->remote);
        free(dbs[i]->db_name);
        free(dbs[i]);
    }
    free(dbs);

    return n_err ? EXIT_FAILURE : EXIT_SUCCESS;
}
