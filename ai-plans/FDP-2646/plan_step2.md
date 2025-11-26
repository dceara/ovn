# Plan Step 2: Refactor Graph Dump to stdout (DOT only)

## Goal
Refactor the `--dump-inc-proc-graph` command-line option in `ovn-northd` and `ovn-controller`. The new implementation will output the incremental processing graph in DOT format directly to `stdout` and remove support for the Mermaid format.

## Requirements
1.  **Remove Mermaid Support**: Delete all logic related to generating Mermaid graphs (.mmd/.mermaid).
2.  **Stdout Output**: The graph description must be printed to standard output.
3.  **Command Line Interface**:
    *   Change `--dump-inc-proc-graph` from an option that requires an argument (filename) to a flag (no argument).
    *   Usage: `ovn-northd --dump-inc-proc-graph` or `ovn-controller --dump-inc-proc-graph`.

## Implementation Steps

### 1. Modify `lib/inc-proc-eng.[ch]`
*   **`lib/inc-proc-eng.h`**:
    *   Update the declaration of `engine_dump_graph`.
    *   New signature: `void engine_dump_graph(void);`.
*   **`lib/inc-proc-eng.c`**:
    *   Remove the file opening logic.
    *   Remove the Mermaid format detection and generation logic.
    *   Update the function to write to stdout.
    *   Ensure the output is strictly DOT format.

### 2. Modify `northd/ovn-northd.c`
*   **`parse_options`**:
    *   Change `long_options` for "dump-inc-proc-graph" from `required_argument` to `no_argument`.
    *   Change the static variable `dump_inc_proc_graph_file` (char*) to a boolean flag (e.g., `dump_inc_proc_graph`).
    *   Remove the check for `argc != 0` inside the option handler (or adapt it if necessary, though usually flags don't strictly check argc unless we want to forbid other args strictly). *Self-correction*: We previously removed the exclusivity check. We should probably ensure it's still clean.
*   **`main`**:
    *   If the `dump_inc_proc_graph` flag is true:
        *   Initialize the engine/IDLs (as before).
        *   Call `engine_dump_graph(stdout)`.
        *   Exit.

### 3. Modify `controller/ovn-controller.c`
*   **`parse_options`**:
    *   Change `long_options` for "dump-inc-proc-graph" from `required_argument` to `no_argument`.
    *   Change the static variable `dump_inc_proc_graph_file` (char*) to a boolean flag `dump_inc_proc_graph`.
*   **`main`**:
    *   If the `dump_inc_proc_graph` flag is true:
        *   Initialize the engine/IDLs.
        *   Call `engine_dump_graph()`.
        *   Exit.

### 4. Modify Tests
*   **`tests/ovn-northd.at`**:
    *   Remove test cases related to Mermaid format.
    *   Update existing DOT test cases to use the new syntax.
    *   Instead of passing a filename, redirect stdout to a file for verification.
    *   Example change:
        *   Old: `check ovn-northd --dump-inc-proc-graph=graph.dot`
        *   New: `check ovn-northd --dump-inc-proc-graph > graph.dot`
*   **`tests/ovn-controller.at`**:
    *   If any tests were added in step 1 (I verified there were none, but good to double-check), update them similarly.
    *   Add a simple test case for `ovn-controller` if one doesn't exist, ensuring it outputs to stdout.

## Verification
*   Compile the code.
*   Run `make check TESTSUITEFLAGS="-k inc-proc-graph"` to ensure the updated tests pass.
*   Manually verify by running `./northd/ovn-northd --dump-inc-proc-graph` and checking the output.
