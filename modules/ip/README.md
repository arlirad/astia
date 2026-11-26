# IP stack module

Scaffold for the loadable IP layer. It exports only the task-shaped lifecycle
entrypoint `core::task<void> module_main()`; success is
`astia::error_t::enone` and teardown runs through the validated `.fini_array`
path. There is no explicit `module_exit()` symbol.

Networking state, transport hooks, and device registration will be added on
top of this entrypoint.
