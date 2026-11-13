# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/dksh-handles.py finds the register a multisample fetch's handle was loaded into."""

import importlib.util
import os
import struct
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("dksh_handles", os.path.join(HERE, "dksh-handles.py"))
dksh = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dksh)

SCHED = 0x001F8000FC0007E0
EXIT = 0xE30000000007000F
F2I_R4 = 0x5CB0118000171A04  # F2I.S32 R4, R1: the sample, as the bindless suite's shader makes it
LDC_R5 = 0xEF94002000070005  # LDC R5, c[2][R0]: the handle
FETCH = 0xDD3C0007A0470000  # TLD.B.MS R0, R0, R4: Rb is R4
PLAIN = FETCH & ~(1 << 50)  # the same fetch of a texture of one sample


def module(*instructions, fragment=True):
    """a DKSH module of one program, its code the instructions with a scheduling word before each three"""
    words = []
    for i, w in enumerate(instructions):
        if i % 3 == 0:
            words.append(SCHED)
        words.append(w)
    code = (b"\0" * 0x50 if fragment else b"") + b"".join(struct.pack("<Q", w) for w in words)
    code += b"\0" * (-len(code) % 256)
    program = struct.pack("<2I", 1 if fragment else 5, 0) + b"\0" * 56
    control = struct.pack("<6I", 0x48534B44, 24, 256, len(code), 24, 1) + program
    return control + b"\0" * (256 - len(control)) + code


class Check(unittest.TestCase):
    def problems(self, data):
        return [problem for _, _, _, problem in dksh.check(data)]

    def test_uam_order_is_found(self):
        found = self.problems(module(F2I_R4, LDC_R5, FETCH, EXIT))
        self.assertEqual(len(found), 1)
        self.assertIn("R5", found[0])

    def test_handle_first_passes(self):
        self.assertEqual(self.problems(module(F2I_R4 + 1, LDC_R5 - 1, FETCH, EXIT)), [None])

    def test_a_later_load_counts(self):
        self.assertEqual(self.problems(module(LDC_R5, LDC_R5 - 1, F2I_R4 + 1, FETCH, EXIT)), [None])

    def test_no_load_is_a_problem(self):
        self.assertIn("no constant buffer load", self.problems(module(F2I_R4, FETCH, EXIT))[0])

    def test_one_sample_and_past_exit_are_left(self):
        self.assertEqual(self.problems(module(F2I_R4, LDC_R5, PLAIN, EXIT, FETCH)), [])

    def test_compute_program_and_modules_inside_a_file(self):
        data = b"before" + module(F2I_R4, LDC_R5, FETCH, EXIT, fragment=False) + b"between"
        data += module(F2I_R4 + 1, LDC_R5 - 1, FETCH, EXIT)
        found = dksh.check(data)
        self.assertEqual([problem is None for _, _, _, problem in found], [False, True])
        self.assertEqual(found[0][0], len(b"before"))


if __name__ == "__main__":
    unittest.main()
