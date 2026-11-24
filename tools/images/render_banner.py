#!/usr/bin/env python3
"""Render the authoritative img/banner.svg into deterministic PNG assets.

Maintainer-only: this script is never invoked by CMake, CTest, the frontend
prebuild/pretest hooks, or CI.  Those validate only the committed PNGs with the
Python standard library.  See tools/images/README.md for provenance.

Pinned inputs:
  renderer: Inkscape 1.4.4
  font: ArliradFlash Regular, sha256 f72b16467d5334a5e62c4a83d066ccd86cbc5ae5163925ed5f1e11f7eb75094c
  canvas: 3072 x 1024 pixels, 96 DPI, transparent background

Inkscape emits ancillary chunks (pHYs and tEXt) whose values can vary between
runs.  This script drops text/time metadata chunks, writes an exact 96 DPI pHYs
chunk, renders twice, and refuses to write output unless both renders are
byte-identical.
"""

from __future__ import annotations

import hashlib
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from .png_asset import (
        DPI,
        HEIGHT,
        PIXELS_PER_METER,
        PNG_SIGNATURE,
        WIDTH,
        iter_chunks,
        pack_chunk,
        read_png_info,
    )
except ImportError:
    from png_asset import (
        DPI,
        HEIGHT,
        PIXELS_PER_METER,
        PNG_SIGNATURE,
        WIDTH,
        iter_chunks,
        pack_chunk,
        read_png_info,
    )

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "img" / "banner.svg"
OUTPUTS = (
    REPO_ROOT / "img" / "banner.png",
    REPO_ROOT / "frontend" / "public" / "img" / "banner.png",
)

INKSCAPE_VERSION = "1.4.4"
FONT_FAMILY = "ArliradFlash"
FONT_STYLE = "Regular"
FONT_SHA256 = "f72b16467d5334a5e62c4a83d066ccd86cbc5ae5163925ed5f1e11f7eb75094c"

METADATA_CHUNKS = {b"tEXt", b"zTXt", b"iTXt", b"tIME"}


def _run(command: list[str]) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True)
    except FileNotFoundError:
        raise SystemExit(
            f"missing required maintainer tool: {command[0]}; "
            "install the pinned renderer/font before regenerating assets"
        ) from None
    except subprocess.CalledProcessError as error:
        details = (error.stderr or "").strip()
        message = f"maintainer tool failed ({error.returncode}): {' '.join(command)}"
        raise SystemExit(f"{message}\n{details}" if details else message) from None


def normalize_png(data: bytes) -> bytes:
    info = read_png_info(data)
    if (info.width, info.height) != (WIDTH, HEIGHT):
        raise ValueError(f"unexpected render size {info.width}x{info.height}")

    output = [PNG_SIGNATURE]
    for chunk_type, payload in iter_chunks(data):
        if chunk_type in METADATA_CHUNKS or chunk_type == b"pHYs":
            continue
        output.append(pack_chunk(chunk_type, payload))
        if chunk_type == b"IHDR":
            output.append(
                pack_chunk(
                    b"pHYs",
                    struct.pack(">IIB", PIXELS_PER_METER, PIXELS_PER_METER, 1),
                )
            )
    return b"".join(output)


def verify_renderer() -> None:
    version = _run(["inkscape", "--version"]).stdout
    match = re.search(r"Inkscape\s+(\S+)", version)
    if not match or match.group(1) != INKSCAPE_VERSION:
        raise SystemExit(
            f"renderer mismatch: need Inkscape {INKSCAPE_VERSION}, found {version.strip()}"
        )


def verify_font() -> Path:
    result = _run(
        [
            "fc-match",
            "--format=%{family}|%{style}|%{file}",
            FONT_FAMILY,
        ]
    ).stdout
    family, style, font_file = result.rsplit("|", 2)
    if family != FONT_FAMILY or style != FONT_STYLE:
        raise SystemExit(
            f"font mismatch: need {FONT_FAMILY} {FONT_STYLE}, found {family} {style}"
        )
    path = Path(font_file)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != FONT_SHA256:
        raise SystemExit(f"font hash mismatch: {path} has sha256 {digest}")
    return path


def render_once(destination: Path) -> bytes:
    _run(
        [
            "inkscape",
            str(SOURCE),
            "--export-type=png",
            f"--export-filename={destination}",
            f"--export-width={WIDTH}",
            f"--export-height={HEIGHT}",
            f"--export-dpi={DPI}",
            "--export-area-page",
            "--export-background-opacity=0",
        ]
    )
    return normalize_png(destination.read_bytes())


def main() -> int:
    if not SOURCE.is_file():
        raise SystemExit(f"missing source SVG: {SOURCE}")
    verify_renderer()
    font_path = verify_font()

    with tempfile.TemporaryDirectory(prefix="astia-banner-") as temporary:
        temporary_path = Path(temporary)
        first = render_once(temporary_path / "banner-1.png")
        second = render_once(temporary_path / "banner-2.png")

    if first != second:
        raise SystemExit("renders are not byte-identical; refusing to write assets")

    for output in OUTPUTS:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(first)

    digest = hashlib.sha256(first).hexdigest()
    print(f"renderer: Inkscape {INKSCAPE_VERSION}")
    print(f"font: {FONT_FAMILY} {FONT_STYLE} ({font_path})")
    print(f"font sha256: {FONT_SHA256}")
    print(f"canvas: {WIDTH}x{HEIGHT}, {DPI} DPI, pHYs {PIXELS_PER_METER} px/m")
    print(f"sha256: {digest}")
    for output in OUTPUTS:
        print(f"wrote: {output.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
