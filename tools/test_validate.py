# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: MIT
"""tools/validate.py reads a layer's messages and gives each to the case after it."""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("validate", os.path.join(HERE, "validate.py"))
validate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validate)

LOG = [
    "Validation Error: [ VUID-VkImageCreateInfo-pNext-06722 ] | MessageID = 0x1",
    "Validation Error: [ VUID-VkImageCreateInfo-pNext-06722 ] | MessageID = 0x1",
    ' [ 1.0] Debug.Emulated <Info> OutputDebugString: {"type":"case","suite":"depth",'
    '"name":"a depth case","tier":"exact","result":"pass","detail":""}',
    "Validation Error: [ SYNC-HAZARD-READ-AFTER-WRITE ] | MessageID = 0x2",
    "Validation Error: [ VUID-VkImageCreateInfo-pNext-06722 ] | MessageID = 0x1",
    ' [ 2.0] OutputDebugString: {"type":"case","suite":"order","name":"a reader",'
    '"tier":"exact","result":"fail","detail":""}',
    "Validation Error: [ VUID-vkCmdEndQuery-None-07007 ] | MessageID = 0x3",
]


class Parse(unittest.TestCase):
    def test_counts_and_first_case(self):
        table = validate.parse(LOG)
        self.assertEqual(table["VUID-VkImageCreateInfo-pNext-06722"], [3, "depth: a depth case"])
        self.assertEqual(table["SYNC-HAZARD-READ-AFTER-WRITE"], [1, "order: a reader"])
        self.assertEqual(table["VUID-vkCmdEndQuery-None-07007"], [1, None])

    def test_diff_names_only_what_moved(self):
        a = validate.parse(LOG)
        b = validate.parse(LOG[:3])
        self.assertEqual(validate.diff(a, b).splitlines(), [
            "VUID-VkImageCreateInfo-pNext-06722 3 -> 2",
            "SYNC-HAZARD-READ-AFTER-WRITE 1 -> 0",
            "VUID-vkCmdEndQuery-None-07007 1 -> 0",
        ])

    def test_nothing_says_so(self):
        self.assertEqual(validate.render({}), "no validation messages\n")


if __name__ == "__main__":
    unittest.main()
