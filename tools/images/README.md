# Banner image assets

`img/banner.png` and `frontend/public/img/banner.png` are the committed banner
assets used by the README and portal. They are byte-identical: the root copy is
consumed by GitHub, and the frontend copy is served by Angular.

`astia_banner_assets` (registered in the root `CMakeLists.txt`) validates the
committed files with `png_asset.py` and `banner-assets.json`, using the Python
standard library only: PNG structure and CRCs, exact 3072x1024 canvas, 96 DPI
`pHYs`, byte-identical copies, decoded pixels pinned by manifest, and raster
references from the README and portal. It does not invoke Inkscape or require
the custom font, so it is safe for normal builds and CI.

## Regeneration (maintainers only)

`render_banner.py` is a maintainer tool, not a build or test dependency. It is
never invoked by CMake, CTest, the frontend prebuild/pretest hooks, or CI. It
requires:

- Inkscape 1.4.4
- ArliradFlash Regular with SHA-256
  `f72b16467d5334a5e62c4a83d066ccd86cbc5ae5163925ed5f1e11f7eb75094c`
- fontconfig (`fc-match`)

Run from the repository root:

```sh
python3 tools/images/render_banner.py
```

The script verifies the renderer and font, renders `img/banner.svg` twice,
drops nondeterministic metadata, writes both PNGs, and fails closed if the
renders differ or the pinned inputs are unavailable.
