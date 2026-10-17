# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""pr-builds.sh against fake_github.py's fake gh: the comment that links a pull request's NRO posted once, then
rewritten at each run, its size to a tenth of a MiB, a job that did not pass named, nothing posted for a commit that is
no longer the head, and --print. Offline; it needs bash and jq, as the script does."""

import json
import os
import shutil
import tempfile
import unittest

from fake_github import World

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "pr-builds.sh")
REPO = "owner/repo"
HEAD = "a" * 40
MARK = "<!-- edenprobe-builds -->"


@unittest.skipUnless(shutil.which("bash") and shutil.which("jq"), "pr-builds.sh needs bash and jq")
class PrBuilds(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.world = World(self.temporary.name, REPO)
        artifacts = [
            {
                "id": 556,
                "name": "edenprobe-aaaaaaa.nro",
                "size_in_bytes": 1_797_929,
                "expired": False,
                "expires_at": "2027-01-15T10:00:00Z",
            },
            {
                "id": 555,
                "name": "an-old-one",
                "size_in_bytes": 1,
                "expired": True,
                "expires_at": "2026-10-01T10:00:00Z",
            },
        ]
        self.world.save(
            {
                "next": 100,
                "pulls": [{"number": 7, "state": "open", "head": {"sha": HEAD}}],
                "runs": [{"id": 60, "head_sha": HEAD, "artifacts": artifacts}],
                "comments": {
                    "7": [{"id": 9, "user": {"login": "bob", "type": "User"}, "body": f"{MARK} quoted by a person"}]
                },
                "releases": [],
                "tags": {},
            }
        )

    def run_script(self, *args, needs=None):
        env = {"NEEDS": json.dumps(needs)} if needs is not None else {}
        return self.world.run(["bash", SCRIPT, *args], **env)

    def comments(self):
        return self.world.state()["comments"]["7"]

    def test_the_comment_posted_then_rewritten(self):
        done = self.run_script(REPO, "7", "60", HEAD, needs={"nro": {"result": "success"}})
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        ours = [c for c in self.comments() if c["user"]["login"] == "github-actions[bot]"]
        self.assertEqual(len(ours), 1)
        self.assertEqual(
            ours[0]["body"],
            f"{MARK}\nBuilds of aaaaaaa, kept until 2027-01-15:\n"
            f"- [edenprobe-aaaaaaa.nro](https://github.com/{REPO}/actions/runs/60/artifacts/556), 1.7 MiB",
        )
        # the next run: the same comment, a job that failed named; the person's comment left alone
        done = self.run_script(REPO, "7", "60", HEAD, needs={"nro": {"result": "failure"}})
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        ours = [c for c in self.comments() if c["user"]["login"] == "github-actions[bot]"]
        self.assertEqual(len(ours), 1)
        self.assertTrue(ours[0]["body"].endswith("\n\nNot built: nro (failure)"), ours[0]["body"])
        self.assertEqual(self.comments()[0]["body"], f"{MARK} quoted by a person")
        patched = [c for c in self.world.calls() if c["args"][:3] == ["api", "-X", "PATCH"]]
        self.assertEqual(patched[0]["args"][3], f"repos/{REPO}/issues/comments/{ours[0]['id']}")

    def test_a_commit_no_longer_the_head(self):
        done = self.run_script(REPO, "7", "60", "b" * 40)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("is no longer the head of #7", done.stdout)
        self.assertEqual(len(self.comments()), 1)

    def test_print_posts_nothing(self):
        done = self.run_script(REPO, "7", "60", "b" * 40, "--print")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(done.stdout.startswith(f"{MARK}\nBuilds of bbbbbbb"), done.stdout)
        self.assertIn("edenprobe-aaaaaaa.nro", done.stdout)
        self.assertNotIn("an-old-one", done.stdout)
        self.assertEqual(len(self.comments()), 1)

    def test_no_artifact(self):
        state = self.world.state()
        state["runs"][0]["artifacts"] = []
        self.world.save(state)
        done = self.run_script(REPO, "7", "60", HEAD, "--print")
        self.assertEqual(done.stdout, f"{MARK}\nBuilds of aaaaaaa:\nnone\n", done.stderr)

    def test_usage(self):
        self.assertEqual(self.run_script(REPO, "7").returncode, 2)
        self.assertEqual(self.run_script(REPO, "7", "60", HEAD, "--post").returncode, 2)


if __name__ == "__main__":
    unittest.main()
