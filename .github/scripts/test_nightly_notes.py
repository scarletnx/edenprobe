# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""nightly-notes.py on a history of its own: merged pull requests, direct commits, a branch never merged; the NRO with
its size and SHA-256, and text that would be markdown shown as it is."""

import importlib.util
import os
import re
import tempfile
import unittest

from fake_github import World

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("nightly_notes", os.path.join(HERE, "nightly-notes.py"))
nightly = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(nightly)

WHERE = "https://github.com/owner/repo/releases/download/t"
URL = "https://github.com/owner/repo"
DIGEST = "ab" * 32


class NightlyNotes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        world = cls.world = World(cls.temporary.name, "owner/repo")
        # written on the 1st, landed on main on the 15th: the title dates the landing
        cls.previous = world.commit("the previous nightly")
        world.git("switch", "-q", "-c", "feature")
        world.commit("edenprobe: a commit of the pull request", date="2026-10-01T12:00:00Z")
        world.git("switch", "-q", "main")
        world.git(
            "merge", "-q", "--no-ff", "feature", "-m", "Merge pull request #5 from someone/feature\n\n[ci] its title"
        )
        cls.merge = world.git("rev-parse", "HEAD")
        cls.sha = world.commit("edenprobe: a commit rebased onto main")
        world.git("switch", "-q", "-c", "elsewhere", cls.previous)
        cls.unmerged = world.commit("never merged")
        world.git("switch", "-q", "main")
        cls.checkout = str(world.checkout)
        cls.text = nightly.notes(cls.checkout, cls.sha, cls.previous, "owner/repo", WHERE, 1_797_929, DIGEST, "Kept.")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_changes_newest_first(self):
        self.assertEqual(
            nightly.changes(self.checkout, self.previous, self.sha),
            [("edenprobe: a commit rebased onto main", None, self.sha), ("[ci] its title", 5, self.merge)],
        )

    def test_change_lines_link_the_repository(self):
        self.assertEqual(
            nightly.change_line(nightly.Change("[ci] its title", 5, self.merge), URL),
            f"\\[ci\\] its title ([#5]({URL}/pull/5))",
        )
        self.assertEqual(
            nightly.change_line(nightly.Change("a fix", None, self.sha), URL),
            f"a fix ([`{self.sha[:7]}`]({URL}/commit/{self.sha}))",
        )

    def test_markdown_shown_as_it_is(self):
        self.assertEqual(
            nightly.escape("[x](http://e) <b> a|b `c` *d* _e_ ~f~ \\"),
            "\\[x\\](http://e) \\<b\\> a\\|b \\`c\\` \\*d\\* \\_e\\_ \\~f\\~ \\\\",
        )

    def test_no_changes_without_an_ancestor(self):
        self.assertIsNone(nightly.changes(self.checkout, "", self.sha))
        self.assertIsNone(nightly.changes(self.checkout, self.unmerged, self.sha))

    def test_title_dates_the_landing(self):
        self.assertEqual(nightly.title(self.checkout, self.sha), f"edenprobe nightly 2026-10-15 ({self.sha[:7]})")

    def test_the_built_commit_is_named_first(self):
        # where the next nightly looks for the commit this one was built from
        self.assertEqual(re.search(r"[0-9a-f]{40}", self.text).group(0), self.sha)

    def test_the_nro_its_size_and_checksum(self):
        self.assertIn(f"| [edenprobe.nro]({WHERE}/edenprobe.nro) | 1.8 MB | `{DIGEST}` |", self.text)
        self.assertIn(f"[edenprobe.nro.sha256]({WHERE}/edenprobe.nro.sha256)", self.text)
        self.assertIn("`sha256sum -c edenprobe.nro.sha256`", self.text)
        self.assertIn(f"{URL}/blob/{self.sha}/docs/Releases.md", self.text)

    def test_changes_counted_and_linked(self):
        self.assertIn(f"- \\[ci\\] its title ([#5]({URL}/pull/5))", self.text)
        self.assertIn(
            f"[`{self.previous[:7]}...{self.sha[:7]}`]({URL}/compare/{self.previous}...{self.sha}): 2 changes, "
            "the newest first.",
            self.text,
        )

    def test_source_folded_at_the_end(self):
        self.assertIn(f"{URL}/tree/{self.sha}", self.text)
        self.assertGreater(self.text.index("<details>"), self.text.index("## Changes"))
        self.assertTrue(self.text.rstrip().endswith("</details>"))

    def test_what_becomes_of_it_and_the_lead(self):
        self.assertIn("committed 2026-10-15. Kept.", self.text)
        lead = nightly.notes(
            self.checkout, self.sha, "", "owner/repo", WHERE, 1, DIGEST, "", lead="edenprobe v1, built from"
        )
        self.assertTrue(lead.startswith(f"edenprobe v1, built from [`{self.sha[:7]}`]"))
        self.assertNotIn("## Changes", lead)

    def test_the_same_commit_again(self):
        again = nightly.notes(self.checkout, self.sha, self.sha, "owner/repo", WHERE, 1, DIGEST, "Kept.")
        self.assertIn("None: the same commit", again)

    def test_a_long_list_cut(self):
        world = World(os.path.join(self.temporary.name, "long"), "owner/repo")
        first = world.commit("first")
        for n in range(nightly.MOST_CHANGES + 2):
            last = world.commit(f"commit {n}")
        many = nightly.notes(str(world.checkout), last, first, "owner/repo", WHERE, 1, DIGEST, "")
        listed = many.split("## Changes")[1].split("<details>")[0].count("\n- ")
        self.assertEqual(listed, nightly.MOST_CHANGES + 1)
        self.assertIn("- and [2 more](", many)


if __name__ == "__main__":
    unittest.main()
