# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every document over 100 lines carries an index of its sections that tools/doc-index.py would write as it is."""

import glob
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SPEC = importlib.util.spec_from_file_location("doc_index", os.path.join(HERE, "doc-index.py"))
doc_index = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(doc_index)

LONG = 100  # lines; a shorter document is read whole


def documents():
    names = ["README.md"] + sorted(glob.glob("docs/**/*.md", root_dir=ROOT, recursive=True))
    return [os.path.join(ROOT, n) for n in names]


class Index(unittest.TestCase):
    def test_long_documents_have_a_current_index(self):
        for path in documents():
            text = open(path, encoding="utf-8").read()
            if text.count("\n") <= LONG:
                continue
            with self.subTest(path=os.path.relpath(path, ROOT)):
                self.assertEqual(doc_index.indexed(text, 3), text, "run tools/doc-index.py on it")

    def test_index_lists_sections_with_github_anchors(self):
        text = "# T\n\nintro\n\n## A b\n\n### C `d`\n\n```\n## not a heading\n```\n\n## A b\n"
        want = "# T\n\n## Index\n\n* [A b](#a-b)\n  * [C `d`](#c-d)\n* [A b](#a-b-1)\n\nintro\n"
        self.assertTrue(doc_index.indexed(text, 3).startswith(want))

    def test_second_run_changes_nothing(self):
        text = "# T\n\nThe intro.\n\n## One\n\n## Two\n\n## Three\n"
        once = doc_index.indexed(text, 3)
        self.assertEqual(doc_index.indexed(once, 3), once)

    def test_a_section_named_index_stays(self):
        text = "# T\n\n## clear\n\nA.\n\n## index\n\nEight cases.\n\n## fill\n\nB.\n"
        once = doc_index.indexed(text, 3)
        self.assertIn("* [index](#index-1)", once)
        self.assertIn("## index\n\nEight cases.", once)
        self.assertEqual(doc_index.indexed(once, 3), once)

    def test_too_few_sections_lose_the_index(self):
        text = "# T\n\n## Index\n\n* [One](#one)\n\n## One\n"
        self.assertEqual(doc_index.indexed(text, 3), "# T\n\n## One\n")


if __name__ == "__main__":
    unittest.main()
