import tempfile
import unittest
from pathlib import Path

from prepare_mp4_for_sd import (
    build_audio_command,
    build_frame_command,
    convert_mp4,
    ensure_unique_directory,
    sanitize_name,
)


class PrepareMp4ForSdTests(unittest.TestCase):
    def test_sanitize_name_replaces_unsafe_characters(self) -> None:
        self.assertEqual(sanitize_name(" my clip (demo)! "), "my-clip-demo")

    def test_unique_directory_adds_suffix_when_needed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            base_dir = Path(temp_dir)
            (base_dir / "clip").mkdir()
            unique = ensure_unique_directory(base_dir, "clip")
            self.assertEqual(unique.name, "clip-2")

    def test_build_frame_command_targets_numbered_jpegs(self) -> None:
        command = build_frame_command(Path("/tmp/source.mp4"), Path("/tmp/out"), 12, 320, 240)
        self.assertEqual(command[0], "ffmpeg")
        self.assertEqual(command[-1], "/tmp/out/%04d.jpg")
        self.assertIn("fps=12", command[5])

    def test_build_audio_command_targets_wav(self) -> None:
        command = build_audio_command(Path("/tmp/source.mp4"), Path("/tmp/out"), 22050)
        self.assertEqual(command[-1], "/tmp/out/audio.wav")
        self.assertIn("pcm_s16le", command)

    def test_convert_mp4_dry_run_creates_clip_folder_and_fps(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            source = temp_path / "sample.mp4"
            source.write_bytes(b"fake")

            clip_dir = convert_mp4(source, temp_path / "sd", 15, 320, 240, 22050, True)

            self.assertTrue(clip_dir.is_dir())
            self.assertEqual((clip_dir / "fps.txt").read_text(encoding="utf-8"), "15\n")
            self.assertEqual(clip_dir.parent.name, "media")


if __name__ == "__main__":
    unittest.main()
