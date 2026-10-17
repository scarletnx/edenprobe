# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/eden-controls.py takes a person's [Controls] and touches nothing else."""

import importlib.util
import io
import os
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("eden_controls",
                                              os.path.join(HERE, "eden-controls.py"))
eden_controls = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(eden_controls)

PERSON = """[DisabledAddOns]
player_0_button_a=not this one
[Controls]
vibration_enabled\\default=true
vibration_enabled=true
player_0_button_a\\default=false
player_0_button_a="engine:sdl,guid:0500,port:0,button:1"
player_0_connected=true
only_the_desktop_has_this=1
[Renderer]
backend=1
"""

RUN = """[Controls]\r
vibration_enabled\\default=true\r
vibration_enabled=true\r
player_0_button_a\\default=true\r
player_0_button_a="engine:keyboard,code:4,toggle:0"\r
player_0_connected=false\r
player_1_connected=false\r
[Renderer]\r
backend=0\r
"""


class Merge(unittest.TestCase):
    def test_controls_are_taken_and_nothing_else(self):
        merged, changed = eden_controls.merge(PERSON, RUN)
        self.assertEqual(changed, 3)
        self.assertIn('player_0_button_a="engine:sdl,guid:0500,port:0,button:1"\r\n', merged)
        self.assertIn("player_0_button_a\\default=false\r\n", merged)
        self.assertIn("player_0_connected=true\r\n", merged)
        # Untouched: a key the person lacks, a key the run lacks, another section.
        self.assertIn("player_1_connected=false\r\n", merged)
        self.assertNotIn("only_the_desktop_has_this", merged)
        self.assertIn("backend=0\r\n", merged)
        self.assertNotIn("not this one", merged)
        self.assertEqual(merged.count("\n"), RUN.count("\n"))

    def test_merging_twice_changes_nothing_more(self):
        merged, _ = eden_controls.merge(PERSON, RUN)
        again, changed = eden_controls.merge(PERSON, merged)
        self.assertEqual(changed, 0)
        self.assertEqual(again, merged)

    def test_files_without_the_section_are_refused(self):
        with tempfile.TemporaryDirectory() as d:
            person = os.path.join(d, "qt-config.ini")
            run = os.path.join(d, "sdl2-config.ini")
            with open(person, "w") as f:
                f.write(PERSON)
            with open(run, "w") as f:
                f.write("[Renderer]\nbackend=0\n")
            with redirect_stderr(io.StringIO()):
                self.assertEqual(eden_controls.main(["x", person, run]), 1)
            with open(run, "w", newline="") as f:
                f.write(RUN)
            with redirect_stdout(io.StringIO()) as said:
                self.assertEqual(eden_controls.main(["x", person, run]), 0)
            self.assertIn("3 keys", said.getvalue())
            with open(run, newline="") as f:
                self.assertIn("player_0_connected=true\r\n", f.read())


if __name__ == "__main__":
    unittest.main()
