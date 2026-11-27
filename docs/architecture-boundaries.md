# Public subsystem dependency boundaries

The architecture check covers the small set of public namespaces that already
form a coherent dependency surface:

| Surface | Public headers | Current CMake target |
| --- | --- | --- |
| core | `astia/core/` | `core` |
| I/O contracts | `astia/io.hpp` | no separate target |
| hardware contracts | `astia/hw/` | `hw` |
| device contracts | `astia/dev/` | `dev` |
| filesystem contracts | `astia/fs.hpp` | `fs` |
| networking contracts | `astia/net.hpp` | `net` |

Other public headers currently live in `astia/api/`, `astia/arch/`,
`astia/boot/`, `astia/mm/`, `astia/proc/`, and the top-level ABI, utility, and
loader headers.  They remain outside this guard because their current layering
is broader than the contract above.

The check scans direct public `#include` edges and direct
`target_link_libraries` edges.  It deliberately ignores implementation
includes, foundation namespaces such as `mm`, `arch`, and `api`, and unrelated
targets.  It also ignores links inside one subsystem: those are implementation
details rather than subsystem dependencies.

The enforced contract is:

* `core` is lowest among the selected surfaces.
* reusable `io` contracts do not depend on hardware, devices, filesystems, or
  networking.
* `hw` does not depend on devices, filesystems, or networking.
* `net` does not depend on devices or filesystems.
* selected device and hardware targets do not depend on networking.
* selected public subsystem dependencies are acyclic.

`astia/dev/device_file.hpp` is an intentional exception.  It is the existing
VFS adapter under the device namespace and may include `astia/fs.hpp`.
Filesystem interfaces may depend on I/O and device interfaces as adapter work
is added; no reverse filesystem rule is imposed beyond the explicit rules
above.

The current main branch has layering debt outside this narrow guard:

* the `dev` target currently links `sys`;
* `arch_x86_64` currently links `dev`.
* Together with `sys` linking `arch_x86_64`, those edges form the broader
  implementation target cycle `arch_x86_64 -> dev -> sys -> arch_x86_64`.

Those edges are recorded here for follow-up rather than enforced as new
failures, since they are existing implementation layering and are not part of
the selected public contract.

Architecture header ownership, overlay resolution, and the standalone header
matrix are covered separately in
[architecture-includes.md](architecture-includes.md).
