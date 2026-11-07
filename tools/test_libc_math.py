# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""source/libcmath.h is what tools/libc-math.py prints, byte for byte, and two runs print the same."""

import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "libc-math.py"
HEADER = HERE.parent / "source" / "libcmath.h"


def printed():
    return subprocess.run([sys.executable, "-I", str(SCRIPT)], check=True, capture_output=True).stdout


class LibcMath(unittest.TestCase):
    def test_header_is_the_scripts(self):
        self.assertEqual(printed(), HEADER.read_bytes(),
                         "source/libcmath.h differs from tools/libc-math.py's output: run it again")

    def test_two_runs_agree(self):
        self.assertEqual(printed(), printed())

    def test_values_are_the_known_ones(self):
        # three values worked out apart from the script, so a script that agrees with itself and
        # is wrong is caught: e and sqrt(2) as Python's float has them, and sin(1) from its bits
        text = HEADER.read_text()
        self.assertIn("X(exp, exp(VD(1.0)), 0x4005bf0a8b145769ULL)", text)
        self.assertIn("X(pow, pow(VD(2.0), VD(0.5)), 0x3ff6a09e667f3bcdULL)", text)
        self.assertIn("X(sin, sin(VD(1.0)), 0x3feaed548f090ceeULL)", text)


if __name__ == "__main__":
    unittest.main()
