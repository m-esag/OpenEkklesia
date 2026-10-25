#!/usr/bin/env python3
"""Generate assets/openekklesia.ico from assets/openekklesia.png.

Windows needs a real .ico for the installer (SetupIconFile), for the shortcut
icon and for the shell. Rather than adding a third-party imaging dependency,
this helper decodes the source PNG with Python's standard library only
(zlib, struct, array) and re-encodes square, downsampled frames as
uncompressed 32-bit BGRA DIBs inside an ICO container.

Why DIB frames instead of PNG-compressed frames: PNG entries in an ICO are
only understood by Windows Vista and later, and some older tools (including
older Inno Setup releases) reject them. Uncompressed DIB entries are accepted
everywhere, which matters more here than the few hundred KB of file size for
a committed icon.

Usage (from anywhere; paths are resolved from this file's location):
    python installer/make-icon.py            # (re)write assets/openekklesia.ico
    python installer/make-icon.py --verify   # validate the existing .ico

Exit codes: 0 success / valid, 1 failure.
"""

from __future__ import annotations

import math
import struct
import sys
import zlib
from array import array
from pathlib import Path

# Sizes Windows itself asks for (Explorer: 16/32/48/256, shell lists and
# high-DPI: 24/64/128). Keep 256 so the icon stays crisp in large views.
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
SOURCE_PNG = REPO_ROOT / "assets" / "openekklesia.png"
TARGET_ICO = REPO_ROOT / "assets" / "openekklesia.ico"


class PngError(RuntimeError):
    pass


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgba(path: Path) -> tuple[int, int, bytearray]:
    """Decode a non-interlaced 8-bit RGBA PNG into a flat RGBA bytearray."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise PngError(f"{path} is not a PNG file")

    pos = 8
    width = height = 0
    idat = bytearray()
    saw_ihdr = saw_iend = False
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            (width, height, depth, color_type, _compression,
             _filter, interlace) = struct.unpack(">IIBBBBB", payload)
            saw_ihdr = True
            if depth != 8 or color_type != 6 or interlace != 0:
                raise PngError(
                    "expected a non-interlaced 8-bit RGBA PNG "
                    f"(found depth={depth} color_type={color_type} "
                    f"interlace={interlace}); re-export the artwork as RGBA8"
                )
        elif kind == b"IDAT":
            idat += payload
        elif kind == b"IEND":
            saw_iend = True
            break
    if not saw_ihdr or not saw_iend or not idat:
        raise PngError(f"{path} is truncated or has no image data")

    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as exc:  # pragma: no cover - corrupt file only
        raise PngError(f"cannot inflate PNG image data: {exc}") from exc

    bpp = 4
    stride = width * bpp
    if len(raw) != height * (stride + 1):
        raise PngError("unexpected PNG scanline layout")

    out = bytearray(height * stride)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        filter_type = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if filter_type == 1:      # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filter_type == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:    # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:    # Paeth
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], up_left)) & 0xFF
        elif filter_type != 0:
            raise PngError(f"unknown PNG filter type {filter_type}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, out


def _box_weights(source: int, target: int) -> list[tuple[list[tuple[int, float]], float]]:
    """Area-coverage weights: for each target pixel, its source pixels."""
    mapping: list[tuple[list[tuple[int, float]], float]] = []
    scale = source / target
    for i in range(target):
        start = i * scale
        end = (i + 1) * scale
        first = int(start)
        last = min(source - 1, int(math.ceil(end)) - 1)
        spans: list[tuple[int, float]] = []
        total = 0.0
        for s in range(first, last + 1):
            overlap = min(end, s + 1) - max(start, s)
            if overlap > 0.0:
                spans.append((s, overlap))
                total += overlap
        mapping.append((spans, total))
    return mapping


def resize_rgba(pixels: bytearray, sw: int, sh: int, dw: int, dh: int) -> bytearray:
    """Premultiplied-alpha box filter downscale (no halos on transparency)."""
    xmap = _box_weights(sw, dw)
    ymap = _box_weights(sh, dh)

    # Horizontal pass: keep premultiplied colour and alpha sums per source row.
    rows = array("f", bytes(4 * 4 * sh * dw))
    for sy in range(sh):
        src_base = sy * sw * 4
        dst_base = sy * dw * 4
        for tx, (spans, _total) in enumerate(xmap):
            ra = ga = ba = aa = 0.0
            for sx, weight in spans:
                i = src_base + sx * 4
                alpha = pixels[i + 3]
                if alpha:
                    ra += pixels[i] * alpha * weight
                    ga += pixels[i + 1] * alpha * weight
                    ba += pixels[i + 2] * alpha * weight
                aa += alpha * weight
            o = dst_base + tx * 4
            rows[o] = ra
            rows[o + 1] = ga
            rows[o + 2] = ba
            rows[o + 3] = aa

    # Vertical pass: normalise once to produce straight RGBA.
    out = bytearray(dw * dh * 4)
    for ty, (spans, total_y) in enumerate(ymap):
        for tx in range(dw):
            total_x = xmap[tx][1]
            ra = ga = ba = aa = 0.0
            for sy, weight in spans:
                i = (sy * dw + tx) * 4
                ra += rows[i] * weight
                ga += rows[i + 1] * weight
                ba += rows[i + 2] * weight
                aa += rows[i + 3] * weight
            j = (ty * dw + tx) * 4
            if aa > 0.0:
                out[j] = min(255, int(ra / aa + 0.5))
                out[j + 1] = min(255, int(ga / aa + 0.5))
                out[j + 2] = min(255, int(ba / aa + 0.5))
                out[j + 3] = min(255, int(aa / (total_x * total_y) + 0.5))
    return out


def _dib_frame(size: int, rgba: bytearray) -> bytes:
    """One ICO frame: BITMAPINFOHEADER + bottom-up BGRA pixels + AND mask."""
    header = struct.pack(
        "<IiiHHIIiiII",
        40,              # biSize
        size,            # biWidth
        size * 2,        # biHeight: XOR image + AND mask
        1,               # biPlanes
        32,              # biBitCount
        0,               # biCompression: BI_RGB
        size * size * 4, # biSizeImage
        0, 0, 0, 0,      # pixels per meter, palette sizes
    )

    # RGBA -> BGRA, then flip vertically (DIBs are bottom-up).
    bgra = bytearray(size * size * 4)
    bgra[0::4] = rgba[2::4]
    bgra[1::4] = rgba[1::4]
    bgra[2::4] = rgba[0::4]
    bgra[3::4] = rgba[3::4]
    row_bytes = size * 4
    flipped = bytearray()
    for y in range(size - 1, -1, -1):
        flipped += bgra[y * row_bytes:(y + 1) * row_bytes]

    # AND mask: 1 bit per pixel, 1 = transparent, rows padded to 4 bytes.
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)
    for y in range(size):
        target_row = size - 1 - y  # bottom-up like the XOR image
        base = target_row * mask_stride
        for x in range(size):
            if rgba[(y * size + x) * 4 + 3] < 128:
                mask[base + (x >> 3)] |= 0x80 >> (x & 7)
    return header + bytes(flipped) + bytes(mask)


def build_ico(frames: list[tuple[int, bytearray]]) -> bytes:
    count = len(frames)
    payloads = [(size, _dib_frame(size, rgba)) for size, rgba in frames]
    out = bytearray(struct.pack("<HHH", 0, 1, count))  # reserved, type=icon, count
    offset = 6 + 16 * count
    for size, payload in payloads:
        out += struct.pack(
            "<BBBBHHII",
            0 if size >= 256 else size,   # 0 means 256
            0 if size >= 256 else size,
            0, 0,                          # palette count, reserved
            1, 32,                         # planes, bit depth
            len(payload), offset,
        )
        offset += len(payload)
    for _size, payload in payloads:
        out += payload
    return bytes(out)


def verify_ico(path: Path) -> bool:
    """Structural validation: header, directory, and every DIB frame."""
    data = path.read_bytes()
    ok = True

    def fail(message: str) -> None:
        nonlocal ok
        ok = False
        print(f"  FAIL: {message}")

    if len(data) < 6:
        fail("file is smaller than an ICO header")
        return ok
    reserved, image_type, count = struct.unpack("<HHH", data[:6])
    if reserved != 0:
        fail(f"reserved field is {reserved}, expected 0")
    if image_type != 1:
        fail(f"type is {image_type}, expected 1 (icon)")
    if count != len(ICON_SIZES):
        fail(f"{count} images, expected {len(ICON_SIZES)}")
    if len(data) < 6 + 16 * count:
        fail("truncated icon directory")
        return ok

    expected_offset = 6 + 16 * count
    seen: list[int] = []
    for index in range(count):
        base = 6 + 16 * index
        width, height, _colors, _res, planes, bits, length, offset = struct.unpack(
            "<BBBBHHII", data[base:base + 16])
        size = width or 256
        if (width, height) != (0, 0) and width != height:
            fail(f"frame {index}: non-square directory entry {width}x{height}")
        if planes != 1 or bits != 32:
            fail(f"frame {index}: planes/bit depth {planes}/{bits}, expected 1/32")
        if offset != expected_offset:
            fail(f"frame {index}: payload offset {offset}, expected {expected_offset}")
        end = offset + length
        if end > len(data):
            fail(f"frame {index}: payload runs past end of file")
            break
        if length < 40:
            fail(f"frame {index}: DIB header truncated")
            expected_offset = end
            continue
        (bi_size, bi_width, bi_height, bi_planes, bi_bits, bi_compression,
         bi_size_image) = struct.unpack("<IiiHHII", data[offset:offset + 24])
        if bi_size != 40:
            fail(f"frame {index}: BITMAPINFOHEADER size {bi_size}, expected 40")
        if bi_width != size or bi_height != size * 2:
            fail(f"frame {index}: DIB is {bi_width}x{bi_height}, "
                 f"expected {size}x{size * 2}")
        if bi_planes != 1 or bi_bits != 32 or bi_compression != 0:
            fail(f"frame {index}: unsupported DIB format "
                 f"(planes={bi_planes} bits={bi_bits} compression={bi_compression})")
        mask_stride = ((size + 31) // 32) * 4
        want = 40 + size * size * 4 + mask_stride * size
        if bi_size_image != size * size * 4:
            fail(f"frame {index}: biSizeImage {bi_size_image}, expected {size * size * 4}")
        if length != want:
            fail(f"frame {index}: payload {length} bytes, expected {want}")
        seen.append(size)
        expected_offset = end

    if sorted(seen) != sorted(ICON_SIZES):
        fail(f"sizes {sorted(seen)} do not match {sorted(ICON_SIZES)}")
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        fail("file starts like a PNG, not an ICO")
    print(f"  sizes: {', '.join(f'{s}x{s}' for s in sorted(seen))}")
    print(f"  bytes: {len(data)} ({len(data) / 1024.0:.1f} KiB)")
    return ok


def main(argv: list[str]) -> int:
    if "--verify" in argv[1:]:
        if not TARGET_ICO.exists():
            print(f"ERROR: {TARGET_ICO} does not exist", file=sys.stderr)
            return 1
        print(f"Verifying {TARGET_ICO.relative_to(REPO_ROOT)}")
        return 0 if verify_ico(TARGET_ICO) else 1

    if not SOURCE_PNG.exists():
        print(f"ERROR: source artwork {SOURCE_PNG} is missing", file=sys.stderr)
        return 1

    print(f"Reading {SOURCE_PNG.relative_to(REPO_ROOT)}")
    src_w, src_h, pixels = read_png_rgba(SOURCE_PNG)
    print(f"  source: {src_w}x{src_h} RGBA")

    # Centre-crop to a square first so the icon never looks stretched.
    if src_w != src_h:
        side = min(src_w, src_h)
        left = (src_w - side) // 2
        top = (src_h - side) // 2
        cropped = bytearray(side * side * 4)
        for y in range(side):
            src_off = ((top + y) * src_w + left) * 4
            cropped[y * side * 4:(y + 1) * side * 4] = pixels[src_off:src_off + side * 4]
        pixels, src_w, src_h = cropped, side, side
        print(f"  centre-cropped to {side}x{side}")

    frames: list[tuple[int, bytearray]] = []
    # Start from the source and shrink stepwise (fast and artefact-free).
    current, cur_w, cur_h = pixels, src_w, src_h
    for size in sorted(ICON_SIZES, reverse=True):
        if cur_w > size:
            current = resize_rgba(current, cur_w, cur_h, size, size)
            cur_w = cur_h = size
        frames.append((size, current))
    frames.reverse()

    ico = build_ico(frames)
    TARGET_ICO.write_bytes(ico)
    relative = TARGET_ICO.relative_to(REPO_ROOT)
    print(f"Wrote {relative} ({len(ico)} bytes, "
          f"{len(frames)} frames: {', '.join(f'{s}' for s in ICON_SIZES)})")

    print("Verifying written file")
    return 0 if verify_ico(TARGET_ICO) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
