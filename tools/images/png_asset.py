"""Portable PNG inspection and decoding helpers using only the standard library."""

from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass
from typing import Iterator

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

WIDTH = 3072
HEIGHT = 1024
DPI = 96
PIXELS_PER_METER = round(DPI / 0.0254)
BIT_DEPTH = 8
COLOR_TYPE_RGBA = 6
INTERLACE_NONE = 0
BYTES_PER_PIXEL = 4


@dataclass(frozen=True)
class PngInfo:
    width: int
    height: int
    bit_depth: int
    color_type: int
    interlace: int
    pixels_per_meter_x: int | None
    pixels_per_meter_y: int | None
    density_unit: int | None


def iter_chunks(data: bytes) -> Iterator[tuple[bytes, bytes]]:
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")
    offset = len(PNG_SIGNATURE)
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("truncated PNG chunk header")
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("truncated PNG chunk")
        payload = data[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack(">I", data[offset + 8 + length : end])[0]
        if zlib.crc32(chunk_type + payload) & 0xFFFFFFFF != expected_crc:
            raise ValueError(f"bad CRC for PNG chunk {chunk_type!r}")
        yield chunk_type, payload
        offset = end
    if offset != len(data):
        raise ValueError("trailing bytes after PNG chunks")


def pack_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(chunk_type + payload) & 0xFFFFFFFF
    return (
        struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)
    )


def read_png_info(data: bytes) -> PngInfo:
    width = height = bit_depth = color_type = interlace = None
    density = None
    last_chunk = None
    chunks = iter_chunks(data)
    for index, (chunk_type, payload) in enumerate(chunks):
        if index == 0:
            if chunk_type != b"IHDR" or len(payload) != 13:
                raise ValueError("PNG must start with a valid IHDR chunk")
            (
                width,
                height,
                bit_depth,
                color_type,
                compression,
                filter_method,
                interlace,
            ) = struct.unpack(">IIBBBBB", payload)
            if compression != 0 or filter_method != 0:
                raise ValueError("unsupported PNG compression or filter method")
        elif chunk_type == b"IHDR":
            raise ValueError("duplicate PNG IHDR chunk")
        elif chunk_type == b"pHYs":
            if len(payload) != 9:
                raise ValueError("invalid PNG pHYs chunk")
            density = struct.unpack(">IIB", payload)
        elif chunk_type == b"IEND" and payload:
            raise ValueError("invalid PNG IEND chunk")
        last_chunk = chunk_type
    if width is None or height is None:
        raise ValueError("PNG has no IHDR chunk")
    if last_chunk != b"IEND":
        raise ValueError("PNG has no IEND chunk")
    return PngInfo(
        width=width,
        height=height,
        bit_depth=bit_depth,
        color_type=color_type,
        interlace=interlace,
        pixels_per_meter_x=density[0] if density else None,
        pixels_per_meter_y=density[1] if density else None,
        density_unit=density[2] if density else None,
    )


def _unfilter_line(
    filter_type: int, line: bytearray, previous: bytearray, stride: int
) -> None:
    if filter_type == 0:
        return
    if filter_type == 1:
        for index in range(BYTES_PER_PIXEL, stride):
            line[index] = (line[index] + line[index - BYTES_PER_PIXEL]) & 0xFF
        return
    if filter_type == 2:
        for index in range(stride):
            line[index] = (line[index] + previous[index]) & 0xFF
        return
    if filter_type == 3:
        for index in range(stride):
            left = line[index - BYTES_PER_PIXEL] if index >= BYTES_PER_PIXEL else 0
            line[index] = (line[index] + ((left + previous[index]) >> 1)) & 0xFF
        return
    if filter_type == 4:
        for index in range(stride):
            left = line[index - BYTES_PER_PIXEL] if index >= BYTES_PER_PIXEL else 0
            above = previous[index]
            upper_left = (
                previous[index - BYTES_PER_PIXEL] if index >= BYTES_PER_PIXEL else 0
            )
            estimate = left + above - upper_left
            distance_left = abs(estimate - left)
            distance_above = abs(estimate - above)
            distance_upper_left = abs(estimate - upper_left)
            if distance_left <= distance_above and distance_left <= distance_upper_left:
                predictor = left
            elif distance_above <= distance_upper_left:
                predictor = above
            else:
                predictor = upper_left
            line[index] = (line[index] + predictor) & 0xFF
        return
    raise ValueError(f"unknown PNG filter type {filter_type}")


def decode_rgba(data: bytes) -> bytes:
    info = read_png_info(data)
    if (
        info.bit_depth != BIT_DEPTH
        or info.color_type != COLOR_TYPE_RGBA
        or info.interlace != INTERLACE_NONE
    ):
        raise ValueError("PNG must be 8-bit, non-interlaced RGBA")
    compressed = b"".join(
        payload for chunk_type, payload in iter_chunks(data) if chunk_type == b"IDAT"
    )
    if not compressed:
        raise ValueError("PNG has no IDAT data")
    try:
        raw = zlib.decompress(compressed)
    except zlib.error as error:
        raise ValueError(f"invalid PNG image data: {error}") from None
    stride = info.width * BYTES_PER_PIXEL
    expected = info.height * (stride + 1)
    if len(raw) != expected:
        raise ValueError(f"unexpected PNG image data length {len(raw)} != {expected}")
    output = bytearray(info.height * stride)
    previous = bytearray(stride)
    position = 0
    for row in range(info.height):
        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position : position + stride])
        position += stride
        _unfilter_line(filter_type, line, previous, stride)
        start = row * stride
        output[start : start + stride] = line
        previous = line
    return bytes(output)


def pixel_sha256(data: bytes) -> str:
    return hashlib.sha256(decode_rgba(data)).hexdigest()
