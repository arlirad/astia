import hashlib
import json
import unittest
from functools import cache
from pathlib import Path

try:
    from .png_asset import (
        BIT_DEPTH,
        COLOR_TYPE_RGBA,
        DPI,
        HEIGHT,
        INTERLACE_NONE,
        PIXELS_PER_METER,
        WIDTH,
        decode_rgba,
        iter_chunks,
        pixel_sha256,
        read_png_info,
    )
except ImportError:
    from png_asset import (
        BIT_DEPTH,
        COLOR_TYPE_RGBA,
        DPI,
        HEIGHT,
        INTERLACE_NONE,
        PIXELS_PER_METER,
        WIDTH,
        decode_rgba,
        iter_chunks,
        pixel_sha256,
        read_png_info,
    )

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "tools" / "images" / "banner-assets.json"
BANNER_PATHS = ("img/banner.png", "frontend/public/img/banner.png")
SOURCE_SVG = REPO_ROOT / "img" / "banner.svg"
PUBLIC_SVG = REPO_ROOT / "frontend" / "public" / "img" / "banner.svg"
README = REPO_ROOT / "README.md"
HOME = REPO_ROOT / "frontend" / "src" / "app" / "home" / "home.html"


@cache
def asset_bytes(relative_path: str) -> bytes:
    return (REPO_ROOT / relative_path).read_bytes()


@cache
def asset_pixels(relative_path: str) -> bytes:
    return decode_rgba(asset_bytes(relative_path))


@cache
def manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def manifest_assets() -> dict[str, dict]:
    return {entry["path"]: entry for entry in manifest()["assets"]}


class BannerAssetTest(unittest.TestCase):
    def test_png_structure_and_chunk_crcs_are_valid(self):
        for relative_path in BANNER_PATHS:
            chunks = list(iter_chunks(asset_bytes(relative_path)))

            self.assertEqual(chunks[0][0], b"IHDR")
            self.assertEqual(chunks[-1], (b"IEND", b""))

    def test_canvas_dimensions_and_density_are_exact(self):
        for relative_path in BANNER_PATHS:
            info = read_png_info(asset_bytes(relative_path))

            self.assertEqual((info.width, info.height), (WIDTH, HEIGHT))
            self.assertEqual(
                (info.pixels_per_meter_x, info.pixels_per_meter_y),
                (PIXELS_PER_METER, PIXELS_PER_METER),
            )
            self.assertEqual(info.density_unit, 1)
            self.assertEqual(round(info.pixels_per_meter_x * 0.0254), DPI)

    def test_color_type_and_alpha_channel_are_intact(self):
        for relative_path in BANNER_PATHS:
            info = read_png_info(asset_bytes(relative_path))
            pixels = asset_pixels(relative_path)

            self.assertEqual(
                (info.bit_depth, info.color_type, info.interlace),
                (BIT_DEPTH, COLOR_TYPE_RGBA, INTERLACE_NONE),
            )
            self.assertEqual(len(pixels), info.width * info.height * 4)
            alpha = pixels[3::4]
            self.assertEqual(min(alpha), 0)
            self.assertEqual(max(alpha), 255)

    def test_committed_bytes_and_pixels_match_manifest(self):
        assets = manifest_assets()

        self.assertEqual(set(assets), set(BANNER_PATHS))
        for relative_path in BANNER_PATHS:
            data = asset_bytes(relative_path)
            entry = assets[relative_path]

            self.assertEqual(
                hashlib.sha256(data).hexdigest(),
                entry["sha256"],
                f"{relative_path} bytes differ from the canonical manifest",
            )
            self.assertEqual(
                pixel_sha256(data),
                entry["pixels_sha256"],
                f"{relative_path} decoded pixels differ from the canonical manifest",
            )

    def test_manifest_records_the_expected_png_shape(self):
        self.assertEqual(
            manifest()["canvas"],
            {
                "width": WIDTH,
                "height": HEIGHT,
                "dpi": DPI,
                "pixels_per_meter": PIXELS_PER_METER,
            },
        )
        self.assertEqual(
            manifest()["png"],
            {
                "bit_depth": BIT_DEPTH,
                "color_type": COLOR_TYPE_RGBA,
                "interlace": INTERLACE_NONE,
            },
        )

    def test_root_and_portal_assets_are_byte_identical(self):
        root_bytes, public_bytes = (
            asset_bytes(relative_path) for relative_path in BANNER_PATHS
        )

        self.assertEqual(root_bytes, public_bytes)
        self.assertEqual(
            hashlib.sha256(root_bytes).hexdigest(),
            hashlib.sha256(public_bytes).hexdigest(),
        )

    def test_presentation_references_use_the_raster_asset(self):
        readme = README.read_text(encoding="utf-8")
        home = HOME.read_text(encoding="utf-8")

        self.assertIn('src="img/banner.png" alt="Astia banner" width="320px"', readme)
        self.assertIn('src="img/banner.png"', home)
        self.assertIn('alt="Astia project banner artwork"', home)
        self.assertIn('width="3072"', home)
        self.assertIn('height="1024"', home)

    def test_no_live_svg_banner_references_remain(self):
        self.assertNotIn("banner.svg", README.read_text(encoding="utf-8"))
        self.assertNotIn("banner.svg", HOME.read_text(encoding="utf-8"))
        self.assertFalse(PUBLIC_SVG.exists())

    def test_authoritative_svg_source_is_retained(self):
        self.assertTrue(SOURCE_SVG.is_file())


if __name__ == "__main__":
    unittest.main()
