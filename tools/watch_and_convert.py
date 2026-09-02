#!/usr/bin/env python3
"""Watch a folder for new video files and auto-convert them for the SD card.

Drop .mp4/.mkv/.mov/etc. files into --watch-folder and this script converts
each one (once it has finished copying) into the SD card's media/ layout,
then moves the source file into a "converted" subfolder so it isn't reprocessed.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from prepare_mp4_for_sd import (
    DEFAULT_AUDIO_RATE,
    DEFAULT_FPS,
    DEFAULT_HEIGHT,
    DEFAULT_WIDTH,
    SUPPORTED_INPUT_EXTENSIONS,
    convert_mp4,
)

DEFAULT_POLL_INTERVAL = 5.0
STABLE_CHECKS_REQUIRED = 2  # consecutive stable size samples before treating a file as fully copied


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Watch a folder and auto-convert new videos for the SD card.")
    parser.add_argument("--watch-folder", required=True, help="Folder to watch for new video files.")
    parser.add_argument("--output", required=True, help="Output directory that will receive a media/ folder.")
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS, help="Output playback FPS.")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH, help="Output frame width.")
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT, help="Output frame height.")
    parser.add_argument("--audio-rate", type=int, default=DEFAULT_AUDIO_RATE, help="Output WAV sample rate.")
    parser.add_argument("--poll-interval", type=float, default=DEFAULT_POLL_INTERVAL, help="Seconds between folder scans.")
    return parser.parse_args(argv)


def is_file_stable(path: Path) -> bool:
    """Poll a file's size until it stops changing, so we don't convert a half-copied file."""
    last_size = -1
    stable_count = 0
    while stable_count < STABLE_CHECKS_REQUIRED:
        try:
            size = path.stat().st_size
        except FileNotFoundError:
            return False
        if size == last_size:
            stable_count += 1
        else:
            stable_count = 0
        last_size = size
        time.sleep(1)
    return True


def process_file(source: Path, output_root: Path, converted_dir: Path, args: argparse.Namespace) -> None:
    if not is_file_stable(source):
        print(f"Skipping {source.name}: file disappeared before it finished copying")
        return

    try:
        convert_mp4(source, output_root, args.fps, args.width, args.height, args.audio_rate, dry_run=False)
    except Exception as exc:  # noqa: BLE001 - report and keep watching
        print(f"Error converting {source.name}: {exc}", file=sys.stderr)
        return

    converted_dir.mkdir(parents=True, exist_ok=True)
    destination = converted_dir / source.name
    counter = 2
    while destination.exists():
        destination = converted_dir / f"{source.stem}-{counter}{source.suffix}"
        counter += 1
    source.rename(destination)
    print(f"Done: {source.name} -> {destination}")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])

    watch_folder = Path(args.watch_folder).expanduser().resolve()
    output_root = Path(args.output).expanduser().resolve()
    converted_dir = watch_folder / "converted"

    if not watch_folder.is_dir():
        print(f"Error: watch folder does not exist: {watch_folder}", file=sys.stderr)
        return 1

    print(f"Watching {watch_folder} (checking every {args.poll_interval}s). Press Ctrl+C to stop.")
    try:
        while True:
            for entry in sorted(watch_folder.iterdir()):
                if entry.is_file() and entry.suffix.lower() in SUPPORTED_INPUT_EXTENSIONS:
                    process_file(entry, output_root, converted_dir, args)
            time.sleep(args.poll_interval)
    except KeyboardInterrupt:
        print("\nStopped watching.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
