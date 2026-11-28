#pragma once

// Loadable-filesystem contract.
//
// A filesystem module defines one fs::filesystem factory whose mount()
// returns a fs::mount for a source string.  It registers the factory with
// fs::register_type() and must call fs::unregister_type() before unload.
// Unregister and mount-factory teardown are refused while mounts or in-flight
// operations still reference the type.

#include <astia/fs/filesystem.hpp>
