# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/sdk-exports.py reads sdk.h's symbols and a dynamic symbol table, and names what is missing, what is not a
defined global function and what is exported under nn:: without a declaration."""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("sdk_exports", os.path.join(HERE, "sdk-exports.py"))
sdk_exports = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sdk_exports)

HEADER = '''
uint32_t NnFsMountSdCard(const char *name) SDK_EXPORT("_ZN2nn2fs11MountSdCardEPKc");
uint32_t NnTimeUserGetContext(TimeSystemClockContext *out)
    SDK_EXPORT("_ZN2nn4time23StandardUserSystemClock21GetSystemClockContextEPNS0_"
               "18SystemClockContextE");
'''

TABLE = '''
Symbol table '.dynsym' contains 4 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000400     8 FUNC    GLOBAL DEFAULT    1 _ZN2nn2fs11MountSdCardEPKc
     2: 0000000000000410     8 FUNC    GLOBAL DEFAULT    1 _ZN2nn4time23StandardUserSystemClock21GetSystemClockContextEPNS0_18SystemClockContextE
     3: 0000000000000420     4 FUNC    GLOBAL DEFAULT    1 nvnBootstrapLoader
'''


class Exports(unittest.TestCase):
    def setUp(self):
        self.symbols = sdk_exports.declared(HEADER)
        self.table = sdk_exports.exported(TABLE)

    def test_a_symbol_split_over_two_literals_is_joined(self):
        self.assertEqual(self.symbols, [
            "_ZN2nn2fs11MountSdCardEPKc",
            "_ZN2nn4time23StandardUserSystemClock21GetSystemClockContextEPNS0_18SystemClockContextE",
        ])

    def test_every_declared_one_exported_passes(self):
        self.assertEqual(sdk_exports.problems(self.symbols, self.table), [])

    def test_one_missing_is_named(self):
        del self.table["_ZN2nn2fs11MountSdCardEPKc"]
        self.assertEqual(sdk_exports.problems(self.symbols, self.table),
                         ["not exported: _ZN2nn2fs11MountSdCardEPKc"])

    def test_an_undefined_one_is_not_an_export(self):
        self.table["_ZN2nn2fs11MountSdCardEPKc"] = ("FUNC", "GLOBAL", "DEFAULT", "UND")
        self.assertEqual(len(sdk_exports.problems(self.symbols, self.table)), 1)

    def test_one_under_nn_not_declared_is_named(self):
        self.table["_ZN2nn2fs7UnmountEPKc"] = ("FUNC", "GLOBAL", "DEFAULT", "1")
        self.assertEqual(sdk_exports.problems(self.symbols, self.table),
                         ["exported and not declared in sdk.h: _ZN2nn2fs7UnmountEPKc"])

    def test_a_const_method_under_nn_not_declared_is_named(self):
        self.table["_ZNK2nn5audio15BehaviorManager11GetRevisionEv"] = ("FUNC", "GLOBAL", "DEFAULT", "1")
        self.assertEqual(sdk_exports.problems(self.symbols, self.table),
                         ["exported and not declared in sdk.h: _ZNK2nn5audio15BehaviorManager11GetRevisionEv"])

    def test_one_declared_twice_is_named(self):
        self.assertIn("declared twice: _ZN2nn2fs11MountSdCardEPKc",
                      sdk_exports.problems(self.symbols + self.symbols[:1], self.table))

    def test_a_tail_branch_from_another_function_is_named(self):
        code = """
0000000000050500 <VolumeFileSetSize>:
   506ec:\t17ff9389 \tb\t35510 <_ZN2nn2fs11MountSdCardEPKc>
0000000000035510 <_ZN2nn2fs11MountSdCardEPKc>:
   35520:\t17fffffc \tb\t35510 <_ZN2nn2fs11MountSdCardEPKc>
   35524:\t17fffffb \tb\t35518 <_ZN2nn2fs11MountSdCardEPKc+0x8>
0000000000035600 <SdkTailIsInitialized>:
   35600:\t17fffffc \tb\t35510 <_ZN2nn2fs11MountSdCardEPKc>
0000000000035700 <Caller>:
   35700:\t94000000 \tbl\t35510 <_ZN2nn2fs11MountSdCardEPKc>
"""
        self.assertEqual(sdk_exports.tail_branches(code, self.symbols),
                         ["a tail branch to _ZN2nn2fs11MountSdCardEPKc in VolumeFileSetSize at 506ec"])

    def test_the_header_declares_each_once(self):
        with open(sdk_exports.SDK_H, encoding="utf-8") as f:
            symbols = sdk_exports.declared(f.read())
        self.assertGreater(len(symbols), 0)
        self.assertEqual(len(symbols), len(set(symbols)))
        self.assertTrue(all(s.startswith(sdk_exports.NN_PREFIXES) for s in symbols))



class Label(unittest.TestCase):
    LABEL = "SDK MW+Nintendo+NintendoSdk_nnSdk-20_5_0"

    def test_the_header_s_label_is_read(self):
        self.assertEqual(sdk_exports.label('#define SDK_LABEL "%s"\n' % self.LABEL), self.LABEL)
        self.assertIsNone(sdk_exports.label("// no label\n"))

    def test_a_program_that_holds_it_passes(self):
        self.assertEqual(sdk_exports.label_problems(self.LABEL, b"\0" + self.LABEL.encode() + b"\0rest"), [])

    def test_a_program_without_it_is_named(self):
        self.assertEqual(sdk_exports.label_problems(self.LABEL, b"SDK MW+Nintendo+NintendoSdk_nnSdk-20_5\0"),
                         ["the program does not hold SDK_LABEL (%s)" % self.LABEL])

    def test_a_label_no_machine_would_read_is_named(self):
        self.assertEqual(len(sdk_exports.label_problems("nnSdk 20.5.0", b"nnSdk 20.5.0\0")), 1)
        self.assertEqual(sdk_exports.label_problems(None, b""), ["sdk.h defines no SDK_LABEL"])


if __name__ == "__main__":
    unittest.main()
