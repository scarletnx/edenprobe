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

    def test_one_declared_twice_is_named(self):
        self.assertIn("declared twice: _ZN2nn2fs11MountSdCardEPKc",
                      sdk_exports.problems(self.symbols + self.symbols[:1], self.table))

    def test_the_header_declares_each_once(self):
        with open(sdk_exports.SDK_H, encoding="utf-8") as f:
            symbols = sdk_exports.declared(f.read())
        self.assertGreater(len(symbols), 0)
        self.assertEqual(len(symbols), len(set(symbols)))
        self.assertTrue(all(s.startswith("_ZN2nn") for s in symbols))


if __name__ == "__main__":
    unittest.main()
