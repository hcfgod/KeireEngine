#!/usr/bin/env python3
"""Exercise text validation without depending on the checkout's current file list."""

from __future__ import annotations

import contextlib
import io
import runpy
import tempfile
import unittest
from pathlib import Path
from unittest import mock


CHECKER = runpy.run_path(str(Path(__file__).with_name("check-text-integrity.py")))


class TextIntegrityTests(unittest.TestCase):
    def check_files(self, files: dict[str, bytes]) -> tuple[int, str]:
        with tempfile.TemporaryDirectory(prefix="keire-text-integrity-") as temporary:
            root = Path(temporary).resolve()
            paths = []
            for name, content in files.items():
                path = root / name
                path.write_bytes(content)
                paths.append(path)
            output = io.StringIO()
            main = CHECKER["main"]
            with mock.patch.dict(main.__globals__, {"versioned_paths": lambda: paths}):
                with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                    result = main()
            return result, output.getvalue()

    def test_video_and_webp_are_binary(self):
        result, output = self.check_files({"clip.mp4": b"\xff\x00\x80", "image.webp": b"\xff\x00\x80"})
        self.assertEqual(result, 0, output)

    def test_unicode_source_is_valid(self):
        result, output = self.check_files({"source.cpp": "// Café\n".encode("utf-8")})
        self.assertEqual(result, 0, output)

    def test_invalid_source_encoding_is_rejected(self):
        result, output = self.check_files({"source.cpp": b"// invalid \xff\n"})
        self.assertEqual(result, 1)
        self.assertIn("invalid UTF-8", output)

    def test_mojibake_source_is_rejected(self):
        result, output = self.check_files({"source.cpp": "// Caf\u00c3\u00a9\n".encode("utf-8")})
        self.assertEqual(result, 1)
        self.assertIn("suspicious mojibake", output)


if __name__ == "__main__":
    unittest.main()
