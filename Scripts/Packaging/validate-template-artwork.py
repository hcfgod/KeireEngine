#!/usr/bin/env python3
"""Validate every PNG artwork file declared by the Hub template catalog."""

from __future__ import annotations

import argparse
import binascii
import json
import struct
import sys
import zlib
from pathlib import Path, PurePosixPath


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_ENCODED_BYTES = 8 * 1024 * 1024
MAX_DECODED_BYTES = 64 * 1024 * 1024
MAX_DIMENSION = 4096
MAX_SOURCE_PIXELS = 16 * 1024 * 1024
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
VALID_BIT_DEPTHS = {0: {1, 2, 4, 8, 16}, 2: {8, 16}, 3: {1, 2, 4, 8}, 4: {8, 16}, 6: {8, 16}}
ADAM7_PASSES = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
                (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))


class ValidationError(ValueError):
    pass


def _is_link(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or (callable(is_junction) and is_junction())


def _pass_extent(size: int, start: int, step: int) -> int:
    return 0 if size <= start else (size - start + step - 1) // step


def _expected_scanlines(width: int, height: int, bits_per_pixel: int, interlace: int) -> tuple[int, list[int]]:
    passes = ((0, 0, 1, 1),) if interlace == 0 else ADAM7_PASSES
    expected = 0
    row_lengths: list[int] = []
    for x_start, y_start, x_step, y_step in passes:
        pass_width = _pass_extent(width, x_start, x_step)
        pass_height = _pass_extent(height, y_start, y_step)
        if pass_width == 0 or pass_height == 0:
            continue
        row_bytes = (pass_width * bits_per_pixel + 7) // 8
        expected += (row_bytes + 1) * pass_height
        row_lengths.extend([row_bytes] * pass_height)
    return expected, row_lengths


def _validate_png(path: Path) -> None:
    encoded_size = path.stat().st_size
    if encoded_size <= len(PNG_SIGNATURE) or encoded_size > MAX_ENCODED_BYTES:
        raise ValidationError(f"encoded size must be 9..{MAX_ENCODED_BYTES} bytes")
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValidationError("invalid PNG signature")

    offset = len(PNG_SIGNATURE)
    ihdr: tuple[int, int, int, int, int] | None = None
    compressed = bytearray()
    saw_palette = False
    saw_idat = False
    ended_idat = False
    saw_iend = False
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValidationError("truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_end = offset + 12 + length
        if chunk_end > len(data):
            raise ValidationError("PNG chunk exceeds the file")
        chunk_data = data[offset + 8:offset + 8 + length]
        expected_crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        actual_crc = binascii.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ValidationError(f"invalid {chunk_type.decode('ascii', 'replace')} chunk CRC")
        if offset == len(PNG_SIGNATURE) and chunk_type != b"IHDR":
            raise ValidationError("IHDR must be the first chunk")
        if chunk_type == b"IHDR":
            if ihdr is not None or length != 13:
                raise ValidationError("invalid or duplicate IHDR")
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", chunk_data)
            if width == 0 or height == 0 or width > MAX_DIMENSION or height > MAX_DIMENSION:
                raise ValidationError(f"dimensions must be 1..{MAX_DIMENSION}")
            if width * height > MAX_SOURCE_PIXELS:
                raise ValidationError(f"source image exceeds {MAX_SOURCE_PIXELS} pixels")
            if color_type not in CHANNELS or bit_depth not in VALID_BIT_DEPTHS[color_type]:
                raise ValidationError("unsupported PNG color type or bit depth")
            if compression != 0 or filtering != 0 or interlace not in (0, 1):
                raise ValidationError("unsupported PNG compression, filter, or interlace method")
            ihdr = (width, height, bit_depth, color_type, interlace)
        elif chunk_type == b"PLTE":
            if saw_idat or length == 0 or length % 3 != 0 or length > 768:
                raise ValidationError("invalid PLTE chunk")
            saw_palette = True
        elif chunk_type == b"IDAT":
            if ihdr is None or ended_idat:
                raise ValidationError("IDAT chunks must be consecutive and follow IHDR")
            saw_idat = True
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            if length != 0 or not saw_idat:
                raise ValidationError("invalid IEND chunk")
            saw_iend = True
            offset = chunk_end
            break
        else:
            if chunk_type[0] & 0x20 == 0:
                raise ValidationError(f"unsupported critical PNG chunk {chunk_type!r}")
            if saw_idat:
                ended_idat = True
        offset = chunk_end

    if not saw_iend or offset != len(data):
        raise ValidationError("missing IEND or trailing bytes")
    assert ihdr is not None
    width, height, bit_depth, color_type, interlace = ihdr
    if color_type == 3 and not saw_palette:
        raise ValidationError("indexed PNG is missing PLTE")

    expected_size, row_lengths = _expected_scanlines(
        width, height, bit_depth * CHANNELS[color_type], interlace)
    if expected_size > MAX_DECODED_BYTES:
        raise ValidationError(f"decoded scanlines exceed {MAX_DECODED_BYTES} bytes")
    inflater = zlib.decompressobj()
    try:
        decoded = inflater.decompress(bytes(compressed), expected_size + 1)
    except zlib.error as error:
        raise ValidationError(f"invalid IDAT stream: {error}") from error
    if not inflater.eof or inflater.unused_data or inflater.unconsumed_tail or len(decoded) != expected_size:
        raise ValidationError("IDAT stream does not match the declared image")
    cursor = 0
    for row_bytes in row_lengths:
        if decoded[cursor] > 4:
            raise ValidationError("invalid PNG scanline filter")
        cursor += row_bytes + 1


def _resolve_artwork(root: Path, declared: str) -> Path:
    if not declared or "\\" in declared:
        raise ValidationError("artwork path must use non-empty POSIX syntax")
    relative = PurePosixPath(declared)
    if relative.is_absolute() or any(part in ("", ".", "..") for part in relative.parts):
        raise ValidationError("artwork path must be a confined relative path")
    candidate = root.joinpath(*relative.parts)
    current = root
    for part in relative.parts:
        current = current / part
        if _is_link(current):
            raise ValidationError("artwork path contains a symbolic link")
    if not candidate.is_file():
        raise ValidationError("artwork path is not a regular file")
    resolved = candidate.resolve(strict=True)
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValidationError("artwork path escapes the template root") from error
    return resolved


def validate_catalog(templates_root: Path) -> int:
    if _is_link(templates_root) or not templates_root.is_dir():
        raise ValidationError("template root must be an ordinary directory")
    root = templates_root.resolve(strict=True)
    catalog_path = root / "catalog.json"
    if _is_link(catalog_path) or not catalog_path.is_file():
        raise ValidationError("catalog.json must be an ordinary file")
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValidationError(f"could not read catalog.json: {error}") from error
    templates = catalog.get("templates") if isinstance(catalog, dict) else None
    if not isinstance(templates, list) or not templates:
        raise ValidationError("catalog.json must contain a non-empty templates array")

    validated = 0
    for template in templates:
        if not isinstance(template, dict):
            raise ValidationError("every template must be an object")
        template_id = template.get("id", "<unknown>")
        thumbnail = template.get("thumbnail")
        screenshots = template.get("screenshots", [])
        if not isinstance(template_id, str) or not isinstance(thumbnail, str):
            raise ValidationError("every template requires string id and thumbnail fields")
        if not isinstance(screenshots, list) or any(not isinstance(item, str) for item in screenshots):
            raise ValidationError(f"{template_id}: screenshots must be an array of paths")
        for declared in (thumbnail, *screenshots):
            try:
                artwork = _resolve_artwork(root, declared)
                if artwork.suffix.lower() != ".png":
                    raise ValidationError("declared artwork must be PNG")
                _validate_png(artwork)
            except (OSError, ValidationError) as error:
                raise ValidationError(f"{template_id}: {declared}: {error}") from error
            validated += 1
    return validated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--templates-root", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        count = validate_catalog(arguments.templates_root)
    except ValidationError as error:
        print(f"Hub template artwork validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {count} Hub template artwork file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
