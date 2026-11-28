import struct
import unittest
import zlib

try:
    from .png_asset import (
        MAX_DIMENSION,
        PNG_SIGNATURE,
        decode_rgba,
        pack_chunk,
        pixel_sha256,
        read_png_info,
    )
except ImportError:
    from png_asset import (
        MAX_DIMENSION,
        PNG_SIGNATURE,
        decode_rgba,
        pack_chunk,
        pixel_sha256,
        read_png_info,
    )


def png_bytes(width, height, idat=b"", extra_chunks=(), interlace=0):
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, interlace)
    chunks = [pack_chunk(b"IHDR", ihdr)]
    for chunk_type, payload in extra_chunks:
        chunks.append(pack_chunk(chunk_type, payload))
    chunks.append(pack_chunk(b"IDAT", idat))
    chunks.append(pack_chunk(b"IEND", b""))
    return PNG_SIGNATURE + b"".join(chunks)


class PngDecodeLimitTest(unittest.TestCase):
    def test_small_valid_png_decodes(self):
        rows = (bytes(range(12)), bytes(range(12, 24)))
        raw = b"".join(b"\x00" + row for row in rows)

        pixels = decode_rgba(png_bytes(3, 2, zlib.compress(raw)))

        self.assertEqual(pixels, b"".join(rows))

    def test_zip_bomb_idat_is_rejected_before_expansion(self):
        data = png_bytes(1, 1, zlib.compress(b"\x00" * (4 << 20)))

        self.assertLess(len(data), 16 << 10)
        with self.assertRaisesRegex(ValueError, "exceeds the expected length"):
            decode_rgba(data)
        with self.assertRaisesRegex(ValueError, "exceeds the expected length"):
            pixel_sha256(data)

    def test_absurd_ihdr_dimensions_are_rejected(self):
        for width, height in (
            (0, 1),
            (1, 0),
            (MAX_DIMENSION + 1, 1),
            (1, MAX_DIMENSION + 1),
            (MAX_DIMENSION, MAX_DIMENSION),
        ):
            with self.subTest(width=width, height=height):
                data = png_bytes(width, height, zlib.compress(b""))

                with self.assertRaisesRegex(ValueError, "dimensions"):
                    read_png_info(data)
                with self.assertRaisesRegex(ValueError, "dimensions"):
                    decode_rgba(data)

    def test_malformed_icc_profile_chunk_is_rejected(self):
        for payload in (
            b"no-terminator",
            b"\x00profile",
            b"profile\x00\x01data",
            b"profile\x00\x00",
        ):
            with self.subTest(payload=payload):
                data = png_bytes(1, 1, zlib.compress(b"\x00" * 5), [(b"iCCP", payload)])

                with self.assertRaisesRegex(ValueError, "iCCP"):
                    read_png_info(data)

    def test_malformed_text_chunks_are_rejected(self):
        cases = (
            (b"tEXt", b"\x00text"),
            (b"tEXt", b"k" * 80 + b"\x00text"),
            (b"zTXt", b"key\x00\x01" + zlib.compress(b"text")),
            (b"iTXt", b"key\x00\x02\x00\x00\x00text"),
            (b"iTXt", b"key\x00\x00\x00en\x00translated"),
        )
        for chunk_type, payload in cases:
            with self.subTest(chunk_type=chunk_type, payload=payload):
                data = png_bytes(
                    1, 1, zlib.compress(b"\x00" * 5), [(chunk_type, payload)]
                )

                with self.assertRaises(ValueError):
                    read_png_info(data)

    def test_well_formed_auxiliary_chunks_are_accepted(self):
        idat = zlib.compress(b"\x00" * 5)
        data = png_bytes(
            1,
            1,
            idat,
            [
                (b"tEXt", b"Comment\x00hello"),
                (b"zTXt", b"Comment\x00\x00" + zlib.compress(b"hello")),
                (b"iTXt", b"Comment\x00\x00\x00en\x00hello\x00hello"),
                (b"iCCP", b"profile\x00\x00" + zlib.compress(b"icc")),
            ],
        )

        self.assertEqual(decode_rgba(data), b"\x00\x00\x00\x00")

    def test_trailing_bytes_after_zlib_stream_are_rejected(self):
        data = png_bytes(1, 1, zlib.compress(b"\x00" * 5) + b"junk")

        with self.assertRaisesRegex(ValueError, "trailing"):
            decode_rgba(data)

    def test_truncated_image_data_is_rejected(self):
        data = png_bytes(1, 1, zlib.compress(b"\x00" * 5)[:-4])

        with self.assertRaises(ValueError):
            decode_rgba(data)

    def test_interlaced_images_are_still_rejected(self):
        data = png_bytes(1, 1, zlib.compress(b"\x00" * 5), interlace=1)

        with self.assertRaisesRegex(ValueError, "non-interlaced"):
            decode_rgba(data)


if __name__ == "__main__":
    unittest.main()
