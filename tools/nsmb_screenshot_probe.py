#!/usr/bin/env python3
"""Small PNG probe for NSMB MvL screenshots.

The current PoC can have matching game-state CSVs while the top screen is
visually invalid, especially when experimenting with Game::localPlayerID=1.
This probe catches the obvious failure mode where the top screen is sky-only
and no ground/block terrain is visible in the gameplay band.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


def load_png_rgba(path: Path) -> tuple[int, int, list[list[tuple[int, int, int, int]]]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not a PNG: {path}")

    pos = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    palette: list[tuple[int, int, int, int]] | None = None
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if typ == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filter, interlace = struct.unpack(">IIBBBBB", chunk)
            if bit_depth != 8 or interlace != 0:
                raise ValueError(f"unsupported PNG format: bit_depth={bit_depth} interlace={interlace}")
        elif typ == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) + (255,) for i in range(0, len(chunk), 3)]
        elif typ == b"IDAT":
            idat.extend(chunk)
        elif typ == b"IEND":
            break

    if width is None or height is None or color_type is None:
        raise ValueError(f"missing IHDR: {path}")
    if color_type not in (2, 3, 6):
        raise ValueError(f"unsupported PNG color type {color_type}: {path}")
    if color_type == 3 and palette is None:
        raise ValueError(f"indexed PNG has no palette: {path}")

    channels = {2: 3, 3: 1, 6: 4}[color_type]
    bpp = channels
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows: list[list[int]] = []
    i = 0
    prev = [0] * stride
    for _y in range(height):
        filter_type = raw[i]
        i += 1
        src = list(raw[i:i + stride])
        i += stride
        row = [0] * stride
        for x, value in enumerate(src):
            left = row[x - bpp] if x >= bpp else 0
            up = prev[x]
            upper_left = prev[x - bpp] if x >= bpp else 0
            if filter_type == 0:
                recon = value
            elif filter_type == 1:
                recon = value + left
            elif filter_type == 2:
                recon = value + up
            elif filter_type == 3:
                recon = value + ((left + up) // 2)
            elif filter_type == 4:
                p = left + up - upper_left
                pa = abs(p - left)
                pb = abs(p - up)
                pc = abs(p - upper_left)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else upper_left)
                recon = value + pred
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}: {path}")
            row[x] = recon & 0xFF
        rows.append(row)
        prev = row

    pixels: list[list[tuple[int, int, int, int]]] = []
    for row in rows:
        out_row: list[tuple[int, int, int, int]] = []
        for x in range(width):
            if color_type == 6:
                r, g, b, a = row[x * 4:x * 4 + 4]
                out_row.append((r, g, b, a))
            elif color_type == 2:
                r, g, b = row[x * 3:x * 3 + 3]
                out_row.append((r, g, b, 255))
            else:
                out_row.append(palette[row[x]])  # type: ignore[index]
        pixels.append(out_row)
    return width, height, pixels


def probe(path: Path, band_start: int, band_end: int) -> dict[str, float | int | str]:
    width, height, pixels = load_png_rgba(path)
    y0 = max(0, band_start)
    y1 = min(height, band_end)
    total = 0
    terrain = 0
    sky = 0
    green_backdrop = 0
    dominant: dict[tuple[int, int, int], int] = {}
    for y in range(y0, y1):
        for x in range(width):
            r, g, b, a = pixels[y][x]
            if a == 0:
                continue
            total += 1
            bucket = (r >> 3, g >> 3, b >> 3)
            dominant[bucket] = dominant.get(bucket, 0) + 1
            is_sky = b > 120 and g > 80 and r < 140 and b > r + 30
            is_green_backdrop = g > 180 and r < 80 and b < 100
            is_green_ground = g > 90 and r < 170 and b < 150
            is_brown_block = r > 120 and g > 80 and b < 100
            if is_sky:
                sky += 1
            if is_green_backdrop:
                green_backdrop += 1
            if is_green_ground or is_brown_block:
                terrain += 1
    terrain_ratio = terrain / total if total else 0.0
    sky_ratio = sky / total if total else 0.0
    green_backdrop_ratio = green_backdrop / total if total else 0.0
    dominant_ratio = max(dominant.values()) / total if total and dominant else 0.0
    unique_buckets = len(dominant)
    return {
        "path": str(path),
        "width": width,
        "height": height,
        "bandStart": y0,
        "bandEnd": y1,
        "pixels": total,
        "terrainPixels": terrain,
        "skyPixels": sky,
        "greenBackdropPixels": green_backdrop,
        "uniqueBuckets": unique_buckets,
        "terrainRatio": terrain_ratio,
        "skyRatio": sky_ratio,
        "greenBackdropRatio": green_backdrop_ratio,
        "dominantRatio": dominant_ratio,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--band-start", type=int, default=150)
    ap.add_argument("--band-end", type=int, default=192)
    ap.add_argument("--min-terrain-ratio", type=float, default=None)
    ap.add_argument("--max-sky-ratio", type=float, default=None)
    ap.add_argument("--max-green-backdrop-ratio", type=float, default=None)
    ap.add_argument("--max-dominant-ratio", type=float, default=None)
    args = ap.parse_args()

    failed = False
    print("path,width,height,bandStart,bandEnd,pixels,terrainPixels,skyPixels,greenBackdropPixels,uniqueBuckets,terrainRatio,skyRatio,greenBackdropRatio,dominantRatio,status")
    for item in args.paths:
        for path in sorted(Path().glob(item) if any(ch in item for ch in "*?[]") else [Path(item)]):
            result = probe(path, args.band_start, args.band_end)
            status = "ok"
            if args.min_terrain_ratio is not None and result["terrainRatio"] < args.min_terrain_ratio:
                status = "fail"
            if args.max_sky_ratio is not None and result["skyRatio"] > args.max_sky_ratio:
                status = "fail"
            if args.max_green_backdrop_ratio is not None and result["greenBackdropRatio"] > args.max_green_backdrop_ratio:
                status = "fail"
            if args.max_dominant_ratio is not None and result["dominantRatio"] > args.max_dominant_ratio:
                status = "fail"
            if status == "fail":
                failed = True
            print(
                f"{result['path']},{result['width']},{result['height']},"
                f"{result['bandStart']},{result['bandEnd']},{result['pixels']},"
                f"{result['terrainPixels']},{result['skyPixels']},{result['greenBackdropPixels']},"
                f"{result['uniqueBuckets']},{result['terrainRatio']:.6f},{result['skyRatio']:.6f},"
                f"{result['greenBackdropRatio']:.6f},{result['dominantRatio']:.6f},{status}"
            )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
