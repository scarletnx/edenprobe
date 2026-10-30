# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""publish-builds.py against fake_github.py's fake gh and fake Discord: which Build runs are published; a nightly
prerelease with the NRO and its checksum, kept to the newest N, never one older than the newest, its files and notes
copied to the "nightly" prerelease whose tag follows it, and nothing said on Discord; a version tag's release, the
latest, and its Discord message, or a notice without the webhook; an artifact that is missing or is no NRO stopping the
run. Offline."""

import hashlib
import json
import os
import tempfile
import unittest

from fake_github import Discord, World, python, serve

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "publish-builds.py")
REPO = "owner/repo"
NRO = b"\0" * 16 + b"NRO0" + b"\0" * 12 + b"the program"
FILES = ["edenprobe.nro", "edenprobe.nro.sha256"]
server = HOOK = None


def setUpModule():
    global server, HOOK
    server, HOOK = serve()


def tearDownModule():
    server.shutdown()
    server.server_close()


def sent(channel, mark):
    """what reached the channel's webhook since mark (a count of Discord's requests)"""
    return [r for r in Discord.requests[mark:] if f"token-{channel}" in r["path"]]


class Publish(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        world = self.world = World(self.temporary.name, REPO)
        self.oldest = world.commit("the older nightly")
        self.previous = world.commit("the previous nightly")
        world.git("tag", "v0.1.0")
        world.git("switch", "-q", "-c", "feature")
        world.commit("edenprobe: a commit of the pull request")
        world.git("switch", "-q", "main")
        world.git(
            "merge", "-q", "--no-ff", "feature", "-m", "Merge pull request #5 from someone/feature\n\n[ci] its title"
        )
        self.sha = world.git("rev-parse", "HEAD")
        world.git("tag", "v0.2.0")
        self.tag = f"nightly-2026-10-15-{self.sha[:7]}"
        self.rolling = {
            "id": 9,
            "tag_name": "nightly",
            "name": "old",
            "created_at": "2026-09-01",
            "body": "old",
            "prerelease": True,
            "assets": [{"id": 7, "name": "edenprobe.nro", "data": "old"}, {"id": 8, "name": "gone.txt", "data": ""}],
        }
        world.save(
            {
                "next": 100,
                "clock": 0,
                "pulls": [],
                "runs": [],
                "tags": {
                    f"nightly-2026-10-15-{self.oldest[:7]}": "x",
                    f"nightly-2026-10-15-{self.previous[:7]}": "x",
                    "nightly": self.oldest,
                    "v0.1.0": self.previous,
                    "v0.2.0": self.sha,
                },
                "releases": [
                    self.nightly_release(self.oldest, "2026-10-01", 1),
                    self.nightly_release(self.previous, "2026-10-02", 2),
                    self.rolling,
                ],
            }
        )

    @staticmethod
    def nightly_release(commit, published, number):
        return {
            "id": number,
            "tag_name": f"nightly-2026-10-15-{commit[:7]}",
            "name": "n",
            "created_at": "2026-10-01T00:00:00Z",  # GitHub's: the tag's commit's date
            "published_at": published,
            "body": f"Built from [`{commit[:7]}`](https://github.com/{REPO}/commit/{commit}).",
            "assets": [],
            "prerelease": True,
        }

    # ---- helpers
    def find(self, tag):
        return next((r for r in self.world.state()["releases"] if r["tag_name"] == tag), None)

    def artifacts(self, name, commit, data=NRO, as_file=False):
        """the folder the workflow downloads into: the NRO in a folder named after the artifact, or at its path"""
        path = self.world.root / name / f"edenprobe-{commit[:7]}.nro"
        if as_file:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        elif data is not None:
            path.mkdir(parents=True, exist_ok=True)
            (path / f"edenprobe-{commit[:7]}.nro").write_bytes(data)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
        return str(self.world.root / name)

    def run_script(self, step, *extra, event=None, name="workflow_run", hooks=True):
        event_file, output = self.world.root / "event.json", self.world.root / "output"
        event_file.write_text(json.dumps(event or {}))
        output.write_text("")
        env = {"GITHUB_EVENT_NAME": name, "GITHUB_EVENT_PATH": str(event_file), "GITHUB_OUTPUT": str(output)}
        if hooks:
            env.update(DISCORD_WEBHOOK_RELEASES=f"{HOOK}-releases")
        done = self.world.run(python(SCRIPT, step, "--repo", REPO, *extra), **env)
        planned = dict(line.split("=", 1) for line in output.read_text().splitlines())
        return done, planned

    def publish(self, kind, commit, *extra, run="41", data=NRO, as_file=False, hooks=True):
        folder = self.artifacts(f"artifacts-{run}", commit, data, as_file)
        args = [
            "--kind",
            kind,
            "--run",
            run,
            "--sha",
            commit,
            "--artifacts",
            folder,
            "--checkout",
            str(self.world.checkout),
        ]
        return self.run_script("publish", *args, *extra, hooks=hooks)[0]

    def push_run(self, commit, conclusion="success", branch="main", number=41, repo=REPO, event="push"):
        return {
            "workflow_run": {
                "id": number,
                "event": event,
                "head_branch": branch,
                "head_sha": commit,
                "conclusion": conclusion,
                "created_at": "2026-10-16T10:00:00Z",
                "head_repository": {"full_name": repo},
            }
        }

    # ---- what plan publishes
    def test_a_green_push_to_main_is_planned_as_a_nightly(self):
        done, planned = self.run_script("plan", event=self.push_run(self.sha))
        self.assertEqual(planned, {"kind": "nightly", "run": "41", "sha": self.sha, "short": self.sha[:7], "tag": ""})

    def test_a_version_tag_pushed_is_planned_as_a_release(self):
        done, planned = self.run_script("plan", event=self.push_run(self.sha, branch="v0.2.0"))
        self.assertEqual((planned.get("kind"), planned.get("tag")), ("release", "v0.2.0"), done.stdout + done.stderr)

    def test_what_is_not_planned(self):
        for event, why in (
            (self.push_run(self.sha, "failure"), "a failed run"),
            (self.push_run(self.sha, branch="dev"), "another branch"),
            (self.push_run(self.sha, branch="v9.9.9"), "a branch named like a version, no tag"),
            (self.push_run(self.sha, repo="someone/fork"), "a push to a fork"),
            # its branch named as a version tag that exists: only the event tells it from the tag's push
            (self.push_run(self.sha, event="pull_request", branch="v0.2.0"), "a pull request's run"),
        ):
            with self.subTest(why):
                done, planned = self.run_script("plan", event=event)
                self.assertEqual((planned, done.returncode), ({}, 0), done.stdout + done.stderr)
        done, planned = self.run_script("plan", name="push", event={})
        self.assertEqual((planned, done.returncode), ({}, 0), done.stdout + done.stderr)

    # ---- a nightly
    def test_a_nightly_with_the_nro_and_its_checksum(self):
        mark = len(Discord.requests)
        done = self.publish("nightly", self.sha, "--keep", "2")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        made = self.find(self.tag)
        self.assertTrue(made["prerelease"])
        self.assertEqual(made["make_latest"], "false")  # the latest is a version's
        data = {a["name"]: a["data"] for a in made["assets"]}
        self.assertEqual(sorted(data), FILES)
        self.assertEqual(data["edenprobe.nro"].encode("latin-1"), NRO)
        digest = hashlib.sha256(NRO).hexdigest()
        self.assertEqual(data["edenprobe.nro.sha256"], f"{digest}  edenprobe.nro\n")
        where = f"https://github.com/{REPO}/releases/download/{self.tag}"
        body = made["body"]
        self.assertIn(f"| [edenprobe.nro]({where}/edenprobe.nro) | 0.0 MB | `{digest}` |", body)
        self.assertIn(f"- \\[ci\\] its title ([#5](https://github.com/{REPO}/pull/5))", body)
        self.assertIn(f"[`{self.previous[:7]}...{self.sha[:7]}`]", body)
        self.assertIn("The newest 2 nightlies are kept", body)
        self.assertIn(f"https://github.com/{REPO}/tree/{self.sha}", body)
        state = self.world.state()
        nightlies = sorted(r["tag_name"] for r in state["releases"] if r["tag_name"].startswith("nightly-"))
        self.assertEqual(nightlies, sorted([self.tag, f"nightly-2026-10-15-{self.previous[:7]}"]))
        self.assertNotIn(f"nightly-2026-10-15-{self.oldest[:7]}", state["tags"])
        # the rolling prerelease: the same files, its tag on the commit, the dated one linked
        rolling = self.find("nightly")
        self.assertTrue(rolling["prerelease"])
        self.assertEqual({a["name"]: a["data"] for a in rolling["assets"]}, data)
        self.assertEqual(state["tags"]["nightly"], self.sha)
        self.assertIn(f"https://github.com/{REPO}/releases/tag/{self.tag}", rolling["body"])
        self.assertIn(f"https://github.com/{REPO}/releases/download/nightly/edenprobe.nro", rolling["body"])
        self.assertEqual(rolling["name"], f"edenprobe nightly 2026-10-15 ({self.sha[:7]}), the newest")
        # every write is the repository's, with its token
        writes = [c for c in self.world.calls() if "-X" in c["args"] or c["args"][0] == "release"]
        self.assertTrue(writes)
        self.assertTrue(all(c["token"] == "repo-token" and REPO in " ".join(c["args"]) for c in writes), writes)
        # nothing said on Discord: a nightly has no message
        self.assertEqual(Discord.requests[mark:], [])

    def test_a_nightly_again_replaces_it(self):
        self.publish("nightly", self.sha, "--keep", "2")
        done = self.publish("nightly", self.sha, "--keep", "2")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len([r for r in self.world.state()["releases"] if r["tag_name"] == self.tag]), 1)

    def test_an_older_commit_is_not_published(self):
        self.publish("nightly", self.sha)
        done = self.publish("nightly", self.oldest, run="40")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("older than the newest nightly", done.stdout)
        self.assertEqual(self.world.state()["tags"]["nightly"], self.sha)

    def test_the_artifact_at_its_own_path(self):
        done = self.publish("nightly", self.sha, as_file=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(sorted(a["name"] for a in self.find(self.tag)["assets"]), FILES)

    def test_a_missing_artifact_stops(self):
        done = self.publish("nightly", self.sha, data=None)
        self.assertNotEqual(done.returncode, 0)
        self.assertIn(f"run 41 built, yet has no edenprobe-{self.sha[:7]}.nro", done.stderr)
        self.assertIsNone(self.find(self.tag))

    def test_a_file_that_is_no_nro_stops(self):
        done = self.publish("nightly", self.sha, data=b"<html>Not Found</html>")
        self.assertNotEqual(done.returncode, 0)
        self.assertIn("is no NRO: no NRO0 at byte 16", done.stderr)
        self.assertIsNone(self.find(self.tag))

    def test_dry_run_writes_nothing(self):
        done = self.publish("nightly", self.sha, "--dry-run")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse([c for c in self.world.calls() if "-X" in c["args"] or c["args"][0] == "release"])
        self.assertIn("would run: gh api -X POST", done.stdout)

    def test_the_rolling_prerelease_made_when_there_is_none(self):
        state = self.world.state()
        state["releases"] = [r for r in state["releases"] if r["tag_name"] != "nightly"]
        del state["tags"]["nightly"]
        self.world.save(state)
        done = self.publish("nightly", self.sha)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        rolling = self.find("nightly")
        self.assertEqual((rolling["target_commitish"], rolling["prerelease"]), (self.sha, True))
        self.assertEqual(sorted(a["name"] for a in rolling["assets"]), FILES)
        self.assertEqual(self.world.state()["tags"]["nightly"], self.sha)

    def test_without_a_webhook_published_and_said(self):
        mark = len(Discord.requests)
        done = self.publish("release", self.sha, "--tag", "v0.2.0", hooks=False)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("::notice::No DISCORD_WEBHOOK_RELEASES", done.stdout)
        self.assertIsNotNone(self.find("v0.2.0"))
        self.assertEqual(Discord.requests[mark:], [])

    # ---- a version
    def add_first_version(self):
        state = self.world.state()
        state["releases"].append(
            {
                "id": 50,
                "tag_name": "v0.1.0",
                "name": "edenprobe v0.1.0",
                "created_at": "2026-10-01",
                "published_at": "2026-10-01",
                "body": "",
                "prerelease": False,
                "assets": [],
            }
        )
        self.world.save(state)

    def test_a_version_release_and_its_message(self):
        self.add_first_version()
        mark = len(Discord.requests)
        done = self.publish("release", self.sha, "--tag", "v0.2.0")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        version = self.find("v0.2.0")
        self.assertEqual(
            (version["make_latest"], version["prerelease"], version["target_commitish"]), ("true", False, self.sha)
        )
        self.assertEqual(sorted(a["name"] for a in version["assets"]), FILES)
        self.assertTrue(version["body"].startswith("edenprobe v0.2.0, built from [`"), version["body"])
        self.assertIn("## Changes since v0.1.0", version["body"])
        self.assertIn(f"https://github.com/{REPO}/releases/download/v0.2.0/edenprobe.nro", version["body"])
        posts = sent("releases", mark)
        self.assertEqual(len(posts), 1)
        message = posts[0]["body"]["embeds"][0]
        self.assertEqual(message["title"], "edenprobe v0.2.0")
        self.assertEqual([f["name"] for f in message["fields"]], ["NRO", "Source", "Since"])
        self.assertIn("/compare/v0.1.0...v0.2.0", message["fields"][2]["value"])
        self.assertIn(hashlib.sha256(NRO).hexdigest(), message["description"])
        self.assertEqual(posts[0]["body"]["allowed_mentions"], {"parse": []})
        # its build again: the files replaced, nothing said twice
        done = self.publish("release", self.sha, "--tag", "v0.2.0", run="42")
        self.assertEqual((done.returncode, len(sent("releases", mark))), (0, 1), done.stdout + done.stderr)

    def test_a_tag_with_a_dash_is_a_prerelease(self):
        self.world.git("tag", "v0.3.0-rc1")
        done = self.publish("release", self.sha, "--tag", "v0.3.0-rc1")
        candidate = self.find("v0.3.0-rc1")
        self.assertEqual((candidate["prerelease"], candidate["make_latest"]), (True, "false"), done.stderr)

    def test_a_release_takes_a_version_tag(self):
        done = self.publish("release", self.sha, "--tag", "main")
        self.assertEqual(done.returncode, 2, done.stderr)


if __name__ == "__main__":
    unittest.main()
