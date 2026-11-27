#pragma once

// Minimal kernel VFS foundation.  This is deliberately fixed-storage:
// Permission metadata is enforced against owner bits.  Astia does not yet
// have per-process uid/gid credentials, so chmod authorization and
// group/other selection are intentionally deferred rather than faked.
// It runs before the general heap and gives device drivers a stable
// path -> file -> operation seam.  The async ring owns submission,
// parking, and completion; the VFS owns names and shared polymorphic
// file instances.  Descriptor allocation remains an ABI/ring concern.
//
// The pieces live in astia/fs/ and this header is the stable include for
// callers that want the whole VFS surface.

#include <astia/fs/contracts.hpp>
#include <astia/fs/file.hpp>
#include <astia/fs/namespace.hpp>
#include <astia/fs/open_file.hpp>
#include <astia/fs/search.hpp>
#include <astia/fs/types.hpp>
