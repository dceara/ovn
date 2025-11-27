Plan for FDP-2646, Step 3: Implement Partial Engine Graph Dump

**Objective:** Enhance the engine graph dump functionality to support an optional argument for a starting node, allowing partial graph visualization up to that node and its inputs.

**Details:**
1.  **Modify `engine_dump_graph` signature:**
    *   Change `void engine_dump_graph(void)` to `void engine_dump_graph(const char *node_name)` in `lib/inc-proc-eng.h` and `lib/inc-proc-eng.c`.

2.  **Implement recursive traversal in `engine_dump_graph`:**
    *   In `lib/inc-proc-eng.c`, modify `engine_dump_graph` to handle the `node_name` argument.
    *   If `node_name` is `NULL`, keep the existing behavior (iterate over all nodes and dump).
    *   If `node_name` is provided:
        *   Find the `engine_node` matching the name.
        *   Implement a helper function to perform the dump recursively.
        *   The helper should take the current node and a `struct sset *visited`.
        *   Logic:
            *   If node is in `visited`, return.
            *   Add node to `visited`.
            *   Print node definition (DOT format).
            *   Iterate over inputs:
                *   Print edge from input to current node.
                *   Recursively call helper for the input node.
        *   Initialize `sset` in `engine_dump_graph`, call helper, and destroy `sset`.

3.  **Update CLI Argument Parsing:**
    *   In `northd/ovn-northd.c` and `controller/ovn-controller.c`:
        *   Update `long_options` for `dump-inc-proc-graph` to use `optional_argument` instead of `no_argument`.
        *   Update the option handler to capture `optarg`.
        *   Update the call to `engine_dump_graph` to pass the captured `optarg` (or `NULL` if not provided).

4.  **Add/Update Tests:**
    *   Verify existing tests still pass (full graph dump).
    *   Add new tests invoking the daemons with `--dump-inc-proc-graph=<node_name>` and check that the output contains only the subgraph leading to `<node_name>`.
