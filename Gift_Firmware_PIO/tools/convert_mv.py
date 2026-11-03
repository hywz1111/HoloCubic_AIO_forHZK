#!/usr/bin/env python3
"""Prepare and validate silent baseline-JPEG movies for the Hazuki gift."""
import argparse
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

MAX_FRAME = 48 * 1024
MAX_FILE = 128 * 1024 * 1024


def jpeg_size(frame):
    """Read baseline SOF dimensions; reject progressive/non-baseline JPEGs."""
    p = 2
    while p + 4 <= len(frame):
        if frame[p] != 0xFF:
            raise ValueError("Invalid JPEG header")
        while p < len(frame) and frame[p] == 0xFF:
            p += 1
        if p >= len(frame):
            break
        marker = frame[p]
        p += 1
        if marker in (0xD9, 0xDA):
            break
        length = struct.unpack_from(">H", frame, p)[0]
        if length < 2 or p + length > len(frame):
            raise ValueError("Truncated JPEG segment")
        if marker == 0xC0:
            if length < 8 or frame[p + 2] != 8:
                raise ValueError("Expected 8-bit baseline JPEG")
            height, width = struct.unpack_from(">HH", frame, p + 3)
            return width, height
        if 0xC1 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            raise ValueError("Progressive or extended JPEG is not supported")
        p += length
    raise ValueError("No baseline JPEG dimensions")


def validate(path):
    if path.stat().st_size > MAX_FILE:
        raise ValueError("Movie exceeds 128 MiB; use --start and --duration to split it")
    pending = bytearray()
    count = maximum = 0
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(65536)
            if chunk:
                pending.extend(chunk)
            while len(pending) >= 2:
                if pending[:2] != b"\xff\xd8":
                    raise ValueError("Expected concatenated JPEG frames without AVI/MIME headers")
                end = pending.find(b"\xff\xd9", 2)
                if end < 0:
                    if len(pending) > MAX_FRAME:
                        raise ValueError("Frame exceeds 48 KiB; increase --quality number")
                    break
                size = end + 2
                if size > MAX_FRAME:
                    raise ValueError("Frame exceeds 48 KiB; increase --quality number")
                frame = bytes(pending[:size])
                if jpeg_size(frame) != (240, 240):
                    raise ValueError("Every frame must be 240 x 240")
                count += 1
                maximum = max(maximum, size)
                del pending[:size]
            if not chunk:
                break
    if pending or not count:
        raise ValueError("Empty or incomplete video")
    return count, maximum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--check", action="store_true", help="Validate a prepared movie without converting")
    parser.add_argument("--fps", type=int, choices=(8, 10, 12, 15, 20), default=12)
    parser.add_argument("--fit", choices=("contain", "crop"), default="contain")
    parser.add_argument("--quality", type=int, choices=range(3, 16), default=5, metavar="3..15")
    parser.add_argument("--start", type=float, default=0)
    parser.add_argument("--duration", type=float)
    args = parser.parse_args()
    if args.check:
        try:
            count, maximum = validate(args.source)
        except (ValueError, OSError) as error:
            parser.error(str(error))
        print(f"Verified {count} frames, largest {maximum} bytes")
        return
    if not args.output or not re.fullmatch(r"[A-Za-z0-9_-]{1,42}\.mjpeg", args.output.name):
        parser.error("Output must be an ASCII name such as 01_stage.mjpeg")
    if not math.isfinite(args.start) or args.start < 0 or (args.duration is not None and
            (not math.isfinite(args.duration) or args.duration <= 0)):
        parser.error("Start must be nonnegative and duration must be positive")
    if not shutil.which("ffmpeg"):
        parser.error("Install FFmpeg and add it to PATH first")
    target = args.output
    temporary = target.with_suffix(".mjpeg.part")
    if target.exists() or temporary.exists():
        parser.error("Output or temporary file already exists; choose a new name")
    if args.fit == "contain":
        sizing = "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:color=black"
    else:
        sizing = "scale=240:240:force_original_aspect_ratio=increase,crop=240:240"
    command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-ss", str(args.start), "-i", str(args.source)]
    if args.duration is not None:
        command += ["-t", str(args.duration)]
    command += ["-map", "0:v:0", "-an", "-sn", "-dn", "-vf", f"{sizing},setsar=1,fps={args.fps}",
                "-c:v", "mjpeg", "-q:v", str(args.quality), "-pix_fmt", "yuvj420p", "-f", "mjpeg", str(temporary)]
    try:
        subprocess.run(command, check=True)
        count, maximum = validate(temporary)
        temporary.rename(target)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        temporary.unlink(missing_ok=True)
        parser.exit(1, f"Conversion failed: {error}\n")
    print(f"Created {target}: {count} frames, max {maximum} bytes; set device FPS to {args.fps}")


if __name__ == "__main__":
    main()
