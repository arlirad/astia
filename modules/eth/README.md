# Ethernet layer module

Scaffold for the loadable Ethernet layer. It exports only the task-shaped
lifecycle entrypoint `core::task<void> module_main()`; success is
`astia::error_t::enone` and teardown runs through the validated `.fini_array`
path. There is no explicit `module_exit()` symbol.

Frame receive/transmit paths, device binding, and the link to the IP layer
will be added on top of this entrypoint.
