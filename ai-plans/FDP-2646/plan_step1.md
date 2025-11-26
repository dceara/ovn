# Plan: Visual Representation of Incremental Processing Engine

## Goal
Enhance `ovn-northd` and `ovn-controller` to support a command-line argument that dumps the incremental processing engine's dependency graph to a file in DOT or Mermaid format.

## Requirements
1.  **New Argument**: `--dump-inc-proc-graph=<output-file>`
2.  **Formats**:
    -   **DOT**: If extension is `.dot` (or default).
    -   **Mermaid**: If extension is `.mmd` or `.mermaid`.
3.  **Behavior**:
    -   If this argument is present, it must be the *only* argument (besides the program name).
    -   The program should initialize the engine (construct the graph).
    -   Dump the graph to the specified file.
    -   The graph edges must be labeled with the name of the change handler function (if any).
    -   Exit immediately after dumping.

## Implementation Steps

### 1. Modify `lib/inc-proc-eng.[ch]`
-   **`lib/inc-proc-eng.h`**:
    -   Add `const char *change_handler_name` to `struct engine_node_input`.
    -   Rename `engine_add_input` to `engine_add_input_impl`.
    -   Define macro `engine_add_input(node, input, handler)` to capture `#handler`.
    -   Rename `engine_add_input_with_compute_debug` to `_impl` and define macro.
    -   Declare `void engine_dump_graph(const char *filename);`.
-   **`lib/inc-proc-eng.c`**:
    -   Update `engine_add_input_impl` to store `change_handler_name`.
    -   Implement `engine_dump_graph`:
        -   Determine format from filename extension.
        -   Iterate over `engine_nodes` vector.
        -   Write nodes and edges to the file.
        -   Use `change_handler_name` for edge labels (checking for "NULL" string).

### 2. Modify `controller/ovn-controller.c`
-   In `parse_options`:
    -   Check for `--dump-inc-proc-graph`.
    -   Validate that no other arguments are provided.
    -   Set a static variable or return a state indicating dump is requested.
-   In `main`:
    -   If dump requested:
        -   Initialize necessary IDLs (dummy connection strings if needed) to satisfy `engine_init` requirements.
        -   Call `engine_init` / `inc_proc_eng_init`.
        -   Call `engine_dump_graph`.
        -   Exit.

### 3. Modify `northd/ovn-northd.c`
-   In `parse_options`:
    -   Check for `--dump-inc-proc-graph`.
    -   Validate exclusivity.
-   In `main`:
    -   If dump requested:
        -   Create dummy IDLs (using dummy remotes).
        -   Call `inc_proc_northd_init`.
        -   Call `engine_dump_graph`.
        -   Exit.

## Testing
1.  **Manual Verification**:
    -   `./ovn-controller --dump-inc-proc-graph=controller.dot`
    -   `./ovn-controller --dump-inc-proc-graph=controller.mmd`
    -   `./ovn-northd --dump-inc-proc-graph=northd.dot`
    -   Verify content of files.
    -   Verify edge labels (handler names) are present.
    -   Verify error on extra args: `./ovn-controller --dump-inc-proc-graph=x.dot --other`
2.  **Automated Tests**:
    -   Add test cases in `tests/ovn-controller.at` and `tests/ovn-northd.at`.
    -   Check that the command succeeds and creates a non-empty file.
