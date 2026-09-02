#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_WIDTH = 320
DEFAULT_HEIGHT = 240
DEFAULT_FPS = 12
DEFAULT_AUDIO_RATE = 22050
SUPPORTED_INPUT_EXTENSIONS = {".mp4", ".mkv", ".mov", ".avi", ".webm", ".m4v"}


def sanitize_name(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", name.strip())
    cleaned = cleaned.strip(".-")
    return cleaned or "clip"


def ensure_unique_directory(base_dir: Path, preferred_name: str) -> Path:
    candidate = base_dir / preferred_name
    counter = 2
    while candidate.exists():
        candidate = base_dir / f"{preferred_name}-{counter}"
        counter += 1
    return candidate


def build_frame_command(source: Path, clip_dir: Path, fps: int, width: int, height: int) -> list[str]:
    filter_graph = (
        f"fps={fps},"
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black"
    )
    return [
        "ffmpeg",
        "-y",
        "-i",
        str(source),
        "-vf",
        filter_graph,
        str(clip_dir / "%04d.jpg"),
    ]


def build_audio_command(source: Path, clip_dir: Path, audio_rate: int) -> list[str]:
    return [
        "ffmpeg",
        "-y",
        "-i",
        str(source),
        "-vn",
        "-ac",
        "1",
        "-ar",
        str(audio_rate),
        "-c:a",
        "pcm_s16le",
        str(clip_dir / "audio.wav"),
    ]


def source_has_audio_stream(source: Path) -> bool:
    if shutil.which("ffprobe") is None:
        return False

    result = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "a:0",
            "-show_entries",
            "stream=codec_type",
            "-of",
            "default=noprint_wrappers=1:nokey=1",
            str(source),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0 and "audio" in result.stdout


def write_fps_file(clip_dir: Path, fps: int) -> None:
    (clip_dir / "fps.txt").write_text(f"{fps}\n", encoding="utf-8")


def convert_mp4(source: Path, output_root: Path, fps: int, width: int, height: int, audio_rate: int, dry_run: bool) -> Path:
    media_root = output_root / "media"
    media_root.mkdir(parents=True, exist_ok=True)

    clip_name = sanitize_name(source.stem)
    clip_dir = ensure_unique_directory(media_root, clip_name)
    clip_dir.mkdir(parents=True, exist_ok=False)

    frame_command = build_frame_command(source, clip_dir, fps, width, height)
    audio_command = build_audio_command(source, clip_dir, audio_rate)

    print(f"Preparing {source} -> {clip_dir}")
    print("Frames:", " ".join(frame_command))
    print("Audio :", " ".join(audio_command))

    if not dry_run:
        subprocess.run(frame_command, check=True)
        if source_has_audio_stream(source):
            subprocess.run(audio_command, check=True)
        else:
            print("Audio : skipped (no audio stream detected)")

    write_fps_file(clip_dir, fps)
    return clip_dir


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert MP4 files into CYD SD-card clip folders.")
    parser.add_argument("--input", nargs="+", required=True, help="One or more input MP4 files.")
    parser.add_argument("--output", required=True, help="Output directory that will receive a media/ folder.")
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS, help="Output playback FPS.")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH, help="Output frame width.")
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT, help="Output frame height.")
    parser.add_argument("--audio-rate", type=int, default=DEFAULT_AUDIO_RATE, help="Output WAV sample rate.")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without invoking ffmpeg.")
    return parser.parse_args(argv)


def validate_args(args: argparse.Namespace) -> list[Path]:
    if args.fps < 1 or args.fps > 60:
        raise ValueError("--fps must be between 1 and 60")
    if args.width < 1 or args.height < 1:
        raise ValueError("--width and --height must be positive")
    if args.audio_rate < 8000:
        raise ValueError("--audio-rate must be at least 8000")

    sources: list[Path] = []
    for raw_source in args.input:
        source = Path(raw_source).expanduser().resolve()
        if source.suffix.lower() not in SUPPORTED_INPUT_EXTENSIONS:
            supported = ", ".join(sorted(SUPPORTED_INPUT_EXTENSIONS))
            raise ValueError(f"Unsupported input container ({source.suffix}): {source}. Supported: {supported}")
        if not source.is_file():
            raise FileNotFoundError(f"Input file not found: {source}")
        sources.append(source)

    if not args.dry_run and shutil.which("ffmpeg") is None:
        raise RuntimeError("ffmpeg is required unless --dry-run is used")

    return sources


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])

    try:
        sources = validate_args(args)
        output_root = Path(args.output).expanduser().resolve()

        for source in sources:
            convert_mp4(source, output_root, args.fps, args.width, args.height, args.audio_rate, args.dry_run)
    except Exception as exc:  # pragma: no cover - exercised by CLI usage
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
