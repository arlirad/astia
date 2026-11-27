# Ethernet layer module

Scaffold for the loadable Ethernet layer. It currently exports only the
lifecycle entrypoints: `module_main()` logs readiness and returns zero,
`module_exit()` is the explicit teardown entrypoint kept for the
module-lifecycle work.

The loader's current ABI runs teardown through `.fini_array`; `module_exit()`
is not invoked by the loader yet. Frame receive/transmit paths, device
binding, and the link to the IP layer will be added on top of these
entrypoints.
