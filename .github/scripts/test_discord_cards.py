# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""discord-cards.py against fake_github.py's fake gh and fake Discord: a push to main's nightly card posted at once and
edited as each job ends, then once published; a pull request's card drawn at each push, comment and review, its NRO
linked as its run's artifact, merged, and a build its newer push cancelled; an issue's card on its own channel, its latest comment and how it was closed;
runs racing for one card; what a card quotes; Discord's limits; and the jobs the cards follow being build.yml's.
Offline."""

import base64
import datetime
import importlib.util
import json
import os
import re
import tempfile
import unittest

from fake_github import Discord, World, python, serve

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "discord-cards.py")
REPO = "owner/repo"
SPEC = importlib.util.spec_from_file_location("discord_cards", SCRIPT)
cards = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cards)
NAMES = [name for _, name, _ in cards.JOBS]
WAIT, BUILD, OK, FAIL = ("queued", None), ("in_progress", None), ("completed", "success"), ("completed", "failure")
server = HOOK = None


def setUpModule():
    global server, HOOK
    server, HOOK = serve()


def tearDownModule():
    server.shutdown()
    server.server_close()


def sent(channel, mark):
    return [r for r in Discord.requests[mark:] if f"token-{channel}" in r["path"]]


def jobs(states, run=1):
    """the jobs the cards follow, the NRO's then the tests': each (status, conclusion)"""
    return [
        {"name": name, "status": status, "conclusion": conclusion, "html_url": f"https://x/runs/{run}/{name[:3]}"}
        for name, (status, conclusion) in zip(NAMES, states, strict=True)
    ]


def user(login, kind="User"):
    return {"login": login, "html_url": f"https://x/{login}", "avatar_url": f"https://x/{login}.png", "type": kind}


def values(post):
    return [f["value"] for f in post["body"]["embeds"][0].get("fields", [])]


class CardsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        world = self.world = World(self.temporary.name, REPO)
        self.previous = world.commit("the previous nightly")
        world.git("switch", "-q", "-c", "feature")
        world.commit("edenprobe: a commit of the pull request")
        world.git("switch", "-q", "main")
        world.git(
            "merge", "-q", "--no-ff", "feature", "-m", "Merge pull request #5 from someone/feature\n\n[ci] its title"
        )
        self.sha = world.git("rev-parse", "HEAD")
        self.newer = world.commit("a newer commit")
        self.base = {
            "next": 1000,
            "clock": 0,
            "pulls": [],
            "runs": [],
            "tags": {},
            "releases": [self.nightly_release(self.previous, "2026-10-01")],
        }
        world.save(self.base)

    def nightly_release(self, commit, published, assets=False):
        tag = f"nightly-2026-10-15-{commit[:7]}"
        return {
            "id": len(published) + int(published[-2:]),
            "tag_name": tag,
            "name": f"edenprobe nightly 2026-10-15 ({commit[:7]})",
            "created_at": published,
            "published_at": published,
            "html_url": f"https://github.com/{REPO}/releases/tag/{tag}",
            "body": f"Built from main at [`{commit[:7]}`](https://github.com/{REPO}/commit/{commit}).",
            "assets": [
                {
                    "id": 90 + n,
                    "name": name,
                    "size": 1_797_929,
                    "browser_download_url": f"https://github.com/{REPO}/releases/download/{tag}/{name}",
                }
                for n, name in enumerate(["edenprobe.nro", "edenprobe.nro.sha256"] if assets else [])
            ],
            "prerelease": True,
        }

    def push_run(self, commit, steps=(), number=1, **more):
        made = {
            "id": number,
            "event": "push",
            "head_branch": "main",
            "head_sha": commit,
            "status": "queued",
            "conclusion": None,
            "created_at": "2026-10-16T10:00:00Z",
            "html_url": f"https://x/runs/{number}",
            "head_repository": {"full_name": REPO},
            "pull_requests": [],
            "jobs": [],
            "steps": list(steps),
        }
        return dict(made, **more)

    def pull(self, number, head, fork=False, **more):
        made = {
            "number": number,
            "state": "open",
            "draft": False,
            "title": "Fix it @everyone",
            "html_url": f"https://x/pull/{number}",
            "user": user("alice"),
            "labels": [],
            "merged": False,
            "comments": 0,
            "review_comments": 0,
            "base": {"sha": "f" * 40},
            "head": {"sha": head, "repo": {"full_name": "alice/fork" if fork else REPO}},
        }
        return dict(made, **more)

    def edit(self, **changes):
        state = self.world.state()
        state.update(changes)
        self.world.save(state)

    def run_cards(self, *args, event=None, name="push", hooks=("NIGHTLY", "PRS", "ISSUES")):
        event_file = self.world.root / "event.json"
        event_file.write_text(json.dumps(event or {}))
        env = {"GITHUB_EVENT_NAME": name, "GITHUB_EVENT_PATH": str(event_file)}
        env.update({f"DISCORD_WEBHOOK_{kind}": f"{HOOK}-{kind.lower()}" for kind in hooks})
        command = python(SCRIPT, *args, "--repo", REPO, "--checkout", str(self.world.checkout), "--every", "0")
        return self.world.run(command, **env)

    def card_id(self, key):
        entry = self.world.state().get("files", {}).get(f"discord-cards:cards/{key}")
        return base64.b64decode(entry["content"]).decode().strip() if entry else None

    # ---- a push to main
    def test_a_nightly_card_posted_at_once_then_followed_then_published(self):
        steps = [
            {"status": "in_progress", "jobs": jobs([WAIT, WAIT])},
            {"status": "in_progress", "jobs": jobs([BUILD, BUILD])},
            {"status": "in_progress", "jobs": jobs([BUILD, BUILD])},  # no change: no edit
            {"status": "in_progress", "jobs": jobs([BUILD, OK])},
            {"status": "completed", "conclusion": "success", "jobs": jobs([OK, OK])},
        ]
        self.edit(runs=[self.push_run(self.sha, steps)])
        mark = len(Discord.requests)
        watch = {"workflow_run": {"id": 1}}
        done = self.run_cards("event", event=watch, name="workflow_run")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("has no card yet", done.stdout)
        self.assertFalse(sent("nightly", mark), "following a build before its card is there posts nothing")

        self.edit(runs=[self.push_run(self.sha, steps)])
        push = {"ref": "refs/heads/main", "after": self.sha, "repository": {"default_branch": "main"}}
        done = self.run_cards("event", event=push, name="push")
        posts = sent("nightly", mark)
        self.assertEqual((done.returncode, len(posts)), (0, 1), done.stdout + done.stderr)
        card = posts[0]["body"]["embeds"][0]
        self.assertEqual(card["title"], f"edenprobe nightly 2026-10-15 ({self.sha[:7]})")
        self.assertTrue(card["description"].startswith(f"Building main at [`{self.sha[:7]}`]"))
        self.assertIn(f"- \\[ci\\] its title ([#5](https://github.com/{REPO}/pull/5))", card["description"])
        self.assertEqual(card["color"], cards.BUILDING)
        self.assertEqual(values(posts[0])[:2], ["Waiting", "Waiting"])
        self.assertEqual([f["name"] for f in card["fields"]], ["NRO", "Host tests", "Source", "Newest"])
        message = posts[0]["reply"]
        self.assertEqual(self.card_id(f"nightly-{self.sha}"), message)
        stored = [c for c in self.world.calls() if c["args"][:3] in (["api", "-X", "PUT"], ["api", "-X", "POST"])]
        self.assertEqual(
            [c["args"][3] for c in stored],
            [f"repos/{REPO}/git/refs"] + [f"repos/{REPO}/contents/cards/nightly-{self.sha}"] * 2,
            "claimed, then kept, on a branch of its own made once",
        )

        done = self.run_cards("event", event=watch, name="workflow_run")
        edits = sent("nightly", mark)[1:]
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(all(e["method"] == "PATCH" and e["path"].endswith(f"/messages/{message}") for e in edits))
        self.assertEqual(
            [[v.split("\n")[0] for v in values(e)[:2]] for e in edits],
            [["Waiting", "Waiting"], ["Building", "Running"], ["Building", "Passed"], ["Built", "Passed"]],
        )
        self.assertEqual(values(edits[1])[0], "Building\n[log](https://x/runs/1/NRO)")
        last = edits[-1]["body"]["embeds"][0]
        self.assertEqual(last["fields"][0]["value"], "Built\nbeing published")
        self.assertIn("being published", last["description"])

        published = self.nightly_release(self.sha, "2026-10-02", assets=True)
        state = self.world.state()
        state["releases"].append(published)
        self.world.save(state)
        done = self.run_cards("refresh", "nightly", self.sha)
        post = sent("nightly", mark)[-1]
        card = post["body"]["embeds"][0]
        self.assertEqual((done.returncode, post["method"]), (0, "PATCH"), done.stdout + done.stderr)
        self.assertEqual(card["url"], published["html_url"])
        self.assertEqual(
            card["fields"][0]["value"],
            f"[NRO](https://github.com/{REPO}/releases/download/{published['tag_name']}/edenprobe.nro)\n1.8 MB",
        )
        self.assertEqual(card["color"], cards.PUBLISHED)
        self.assertIn(f"[`{self.previous[:7]}...{self.sha[:7]}`]", card["description"])

    def test_a_nightly_not_published(self):
        mark = len(Discord.requests)
        older = [self.nightly_release(self.previous, "2026-10-01"), self.nightly_release(self.newer, "2026-10-03")]
        done_run = self.push_run(self.sha, status="completed", conclusion="success", jobs=jobs([OK, OK]))
        self.edit(releases=older, runs=[done_run])
        self.run_cards("refresh", "nightly", self.sha)
        card = sent("nightly", mark)[-1]["body"]["embeds"][0]
        self.assertTrue(card["description"].startswith("Not published: [edenprobe nightly 2026-10-15"), card)
        self.assertEqual((card["color"], card["fields"][0]["value"]), (cards.CLOSED, "Built\nnot published"))

        failed_run = self.push_run(self.sha, status="completed", conclusion="failure", jobs=jobs([OK, FAIL]))
        self.edit(releases=older[:1], runs=[failed_run])
        self.run_cards("refresh", "nightly", self.sha)
        card = sent("nightly", mark)[-1]["body"]["embeds"][0]
        self.assertTrue(card["description"].startswith(f"Not published: the build of [`{self.sha[:7]}`]"), card)
        self.assertEqual(card["fields"][1]["value"], "Failed\n[log](https://x/runs/1/Hos)")
        self.assertEqual(card["color"], cards.AMBER)

        done = self.run_cards(
            "event", event={"ref": "refs/heads/dev", "after": self.sha, "repository": {"default_branch": "main"}}
        )
        self.assertIn("no card", done.stdout)
        self.assertEqual(len(sent("nightly", mark)), 2)

    # ---- a pull request
    def test_a_pull_request_card(self):
        head = "a" * 40
        commits = [
            {"sha": f"{n:x}" * 40, "commit": {"message": f"edenprobe: commit {n}\n\nIts body."}} for n in range(3)
        ]
        self.edit(
            pulls=[self.pull(7, head, labels=[{"name": "kind: bug"}], assignees=[user("bob")])],
            commits={"7": commits},
            merge_base=self.sha,
            links={"7": [{"number": 12, "url": "https://x/issues/12"}]},
            releases=[self.nightly_release(self.sha, "2026-10-02", assets=True)],
        )
        mark = len(Discord.requests)
        done = self.run_cards(
            "event", event={"action": "opened", "pull_request": {"number": 7}}, name="pull_request_target"
        )
        posts = sent("prs", mark)
        self.assertEqual((done.returncode, len(posts)), (0, 1), done.stdout + done.stderr)
        card = posts[0]["body"]["embeds"][0]
        self.assertEqual(card["title"], "edenprobe PR #7: Fix it @everyone")
        self.assertEqual(card["author"], {"name": "alice", "url": "https://x/alice", "icon_url": "https://x/alice.png"})
        self.assertTrue(
            card["description"].startswith(f"**Open** at [`aaaaaaa`](https://github.com/{REPO}/pull/7/commits/{head})")
        )
        self.assertIn(
            f"([its nightly](https://github.com/{REPO}/releases/tag/nightly-2026-10-15-{self.sha[:7]})).\n"
            "`kind: bug` | assigned to [bob](https://x/bob) | closes [#12](https://x/issues/12)\n",
            card["description"],
        )
        self.assertIn("**3 commits**, the newest first\n- edenprobe: commit 2\n", card["description"])
        self.assertEqual(values(posts[0]), ["Waiting", "Waiting", "Not started"])
        self.assertEqual(card["color"], cards.OPEN)
        message = posts[0]["reply"]

        # a comment: the newest a person wrote, quoted; the bot's left out
        comments = [
            {
                "user": user("bob"),
                "created_at": "2026-10-16T10:00:00Z",
                "body": "Looks good <!-- hidden --> ![shot](https://x/a.png)\n\nOne more thing.",
                "html_url": "https://x/c/1",
            },
            {
                "user": user("github-actions[bot]", "Bot"),
                "created_at": "2026-10-16T11:00:00Z",
                "body": "<!-- edenprobe-builds -->\nBuilds of aaaaaaa",
                "html_url": "https://x/c/2",
            },
        ]
        self.edit(pulls=[self.pull(7, head, comments=2)], comments={"7": comments})
        said = {"action": "created", "issue": {"number": 7, "pull_request": {"url": "x"}}, "comment": {}}
        done = self.run_cards("event", event=said, name="issue_comment")
        post = sent("prs", mark)[-1]
        description = post["body"]["embeds"][0]["description"]
        self.assertEqual((done.returncode, post["method"]), (0, "PATCH"), done.stdout + done.stderr)
        self.assertTrue(post["path"].split("?")[0].endswith(f"/messages/{message}"))
        self.assertIn(
            "**Latest comment**, by **bob** ([on GitHub](https://x/c/1))\n> Looks good  [image]\n>\n> One more thing.",
            description,
        )
        self.assertNotIn("Builds of", description)

        # a review after it is the latest
        review = {
            "user": user("carol"),
            "submitted_at": "2026-10-16T12:00:00Z",
            "body": "Approved.",
            "html_url": "https://x/r/1",
        }
        self.edit(reviews={"7": [review]})
        self.run_cards(
            "event", event={"action": "submitted", "pull_request": {"number": 7}}, name="pull_request_review"
        )
        self.assertIn("**Latest comment**, by **carol**", sent("prs", mark)[-1]["body"]["embeds"][0]["description"])

        # a fork's run names no pull request: found by its commit; its NRO linked as the run's artifact
        artifact = {"id": 555, "name": "edenprobe-aaaaaaa.nro", "size_in_bytes": 1_797_929, "expired": False}
        run = {
            "id": 60,
            "event": "pull_request",
            "head_branch": "x",
            "head_sha": head,
            "status": "completed",
            "conclusion": "success",
            "created_at": "2026-10-16T10:00:00Z",
            "html_url": "https://x/runs/60",
            "head_repository": {"full_name": "alice/fork"},
            "pull_requests": [],
            "jobs": jobs([OK, OK], 60),
            "artifacts": [dict(artifact, id=554, name="edenprobe-bbbbbbb.nro"), artifact],
        }
        self.edit(pulls=[self.pull(7, head, fork=True)], runs=[run])
        done = self.run_cards("event", event={"workflow_run": {"id": 60}}, name="workflow_run")
        post = sent("prs", mark)[-1]
        self.assertEqual((done.returncode, post["method"]), (0, "PATCH"), done.stdout + done.stderr)
        self.assertEqual(
            values(post),
            [
                f"[Artifact](https://github.com/{REPO}/actions/runs/60/artifacts/555)\n1.8 MB",
                "Passed",
                "[Passed](https://x/runs/60)",
            ],
        )

        # its artifact expired: built, no link
        self.edit(runs=[dict(run, artifacts=[dict(artifact, expired=True)])])
        self.run_cards("refresh", "pr", "7")
        self.assertEqual(values(sent("prs", mark)[-1])[0], "Built")

        # merged: no jobs, the merge commit named
        self.edit(pulls=[self.pull(7, head, state="closed", merged=True, merge_commit_sha=self.newer)])
        self.run_cards("event", event={"action": "closed", "pull_request": {"number": 7}}, name="pull_request_target")
        card = sent("prs", mark)[-1]["body"]["embeds"][0]
        self.assertTrue(card["description"].startswith(f"**Merged** into main at [`{self.newer[:7]}`]"))
        self.assertEqual(card["color"], cards.MERGED)
        self.assertNotIn("fields", card)

    def test_a_cancelled_build_leaves_its_card_to_the_newer_one(self):
        """build.yml cancels a pull request's build when a newer push comes: its follower, ending, draws the card
        from the newest build of the pull request's head, not its own "Cancelled" over the newer one's state"""
        old, head = "a" * 40, "b" * 40
        commits = [{"sha": head, "commit": {"message": "edenprobe: its one commit"}}]
        self.edit(pulls=[self.pull(7, head)], commits={"7": commits}, merge_base=self.sha, links={"7": []})
        done = self.run_cards(
            "event", event={"action": "opened", "pull_request": {"number": 7}}, name="pull_request_target"
        )
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        base = {
            "event": "pull_request",
            "head_branch": "feature",
            "head_repository": {"full_name": REPO},
            "pull_requests": [{"number": 7}],
            "artifacts": [],
        }
        cancelled = dict(
            base,
            id=61,
            head_sha=old,
            status="completed",
            conclusion="cancelled",
            created_at="2026-10-16T10:00:00Z",
            html_url="https://x/runs/61",
            jobs=jobs([("completed", "cancelled"), ("completed", "cancelled")], 61),
        )
        newer = dict(
            base,
            id=62,
            head_sha=head,
            status="completed",
            conclusion="success",
            created_at="2026-10-16T10:01:00Z",
            html_url="https://x/runs/62",
            jobs=jobs([OK, OK], 62),
        )
        self.edit(runs=[cancelled, newer])
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"workflow_run": {"id": 62}}, name="workflow_run")
        drawn_by_newer = values(sent("prs", mark)[-1])
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"workflow_run": {"id": 61}}, name="workflow_run")
        post = sent("prs", mark)[-1]
        self.assertEqual((done.returncode, post["method"]), (0, "PATCH"), done.stdout + done.stderr)
        self.assertEqual(values(post), drawn_by_newer)
        self.assertFalse(any("Cancelled" in v for v in values(post)), values(post))

        # cancelled with no newer build of the head (by hand): the card says so
        self.edit(runs=[dict(cancelled, head_sha=head)])
        mark = len(Discord.requests)
        self.run_cards("event", event={"workflow_run": {"id": 61}}, name="workflow_run")
        self.assertIn("Cancelled", values(sent("prs", mark)[-1]))

    # ---- an issue
    issue = {
        "number": 12,
        "state": "open",
        "state_reason": None,
        "title": "It crashes",
        "html_url": "https://x/issues/12",
        "user": user("dave"),
        "labels": [{"name": "kind: bug", "color": "d73a4a"}, {"name": "priority: high", "color": "d93f0b"}],
        "type": {"name": "Bug"},
        "assignees": [user("erin")],
        "milestone": {"title": "v0.1", "html_url": "https://x/milestone/1"},
        "comments": 0,
        "body": "Steps:\n\n1. open it\n2. " + "word " * 100,
    }

    def test_an_issue_card(self):
        self.edit(issues={"12": self.issue}, links={"12": [{"number": 267, "url": "https://x/pull/267"}]})
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"action": "opened", "issue": {"number": 12}}, name="issues")
        posts = sent("issues", mark)
        self.assertEqual((done.returncode, len(posts)), (0, 1), done.stdout + done.stderr)
        card = posts[0]["body"]["embeds"][0]
        self.assertEqual(card["title"], "edenprobe #12: It crashes")
        self.assertTrue(
            card["description"].startswith(
                "**Open**\n**Bug** | \U0001f7e5 `kind: bug` \U0001f7e7 `priority: high` | assigned to "
                "[erin](https://x/erin) | milestone [v0.1](https://x/milestone/1) | fixed by [#267](https://x/pull/267)"
                "\n\n> Steps:\n>\n> 1. open it"
            ),
            card["description"],
        )
        self.assertTrue(card["description"].endswith("word..."))
        self.assertEqual((card["color"], card["footer"]["text"][:10]), (cards.OPEN, "0 comments"))

        comment = {
            "user": user("erin"),
            "created_at": "2026-10-16T10:00:00Z",
            "body": "Fixed.",
            "html_url": "https://x/c/9",
        }
        self.edit(
            issues={"12": dict(self.issue, comments=1, state="closed", state_reason="completed")},
            comments={"12": [comment]},
        )
        self.run_cards("event", event={"action": "created", "issue": {"number": 12}}, name="issue_comment")
        post = sent("issues", mark)[-1]
        card = post["body"]["embeds"][0]
        self.assertEqual(post["method"], "PATCH")
        self.assertTrue(card["description"].startswith("**Closed** as done"))
        self.assertIn("**Latest comment**, by **erin**", card["description"])
        self.assertEqual(card["color"], cards.DONE)

        self.edit(issues={"12": dict(self.issue, state="closed", state_reason="not_planned")})
        self.run_cards("event", event={"action": "closed", "issue": {"number": 12}}, name="issues")
        card = sent("issues", mark)[-1]["body"]["embeds"][0]
        self.assertTrue(card["description"].startswith("**Closed** as not planned"))
        self.assertEqual(card["color"], cards.NOT_PLANNED)

        done = self.run_cards(
            "event", event={"action": "edited", "issue": {"number": 12}}, name="issues", hooks=("NIGHTLY", "PRS")
        )
        self.assertIn("::notice::No DISCORD_WEBHOOK_ISSUES", done.stdout)
        self.assertEqual(len(sent("issues", mark)), 3)

    # ---- runs racing for one card
    def test_a_branch_and_a_claim_another_run_made_first(self):
        # one event starts several runs: one makes the cards' branch while another looked, one's write comes first
        self.edit(
            issues={"14": dict(self.issue, number=14)},
            hide_branch_once="discord-cards",
            fail_puts=1,
            put_error='gh: Invalid request.\n\n"sha" wasn\'t supplied. (HTTP 422)',  # GitHub's, the file made first
        )
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"action": "opened", "issue": {"number": 14}}, name="issues")
        writes = [c["args"][3] for c in self.world.calls() if c["args"][:2] == ["api", "-X"]]
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.card_id("issue-14"), sent("issues", mark)[-1]["reply"])
        self.assertEqual(writes, [f"repos/{REPO}/git/refs"] + [f"repos/{REPO}/contents/cards/issue-14"] * 3)

    def test_a_card_that_cannot_be_kept_stops_naming_its_message(self):
        self.edit(
            issues={"15": dict(self.issue, number=15)},
            pass_puts=1,
            fail_puts=1,
            put_error="gh: Resource not accessible by integration (HTTP 403)",
        )
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"action": "opened", "issue": {"number": 15}}, name="issues")
        self.assertNotEqual(done.returncode, 0)
        self.assertIn(
            f"issue-15 is message {sent('issues', mark)[-1]['reply']} on Discord, but it could not be kept", done.stderr
        )
        self.assertIn("HTTP 403", done.stderr)

    def claim_of(self, number, when):
        state = self.world.state()
        state.setdefault("issues", {})[str(number)] = dict(self.issue, number=number)
        content = base64.b64encode(f"posting {when}\n".encode()).decode()
        state.setdefault("heads", {"main": "c0ffee" * 6 + "c0ff"})["discord-cards"] = "c0ffee" * 6 + "c0ff"
        state.setdefault("files", {})[f"discord-cards:cards/issue-{number}"] = {
            "content": content,
            "sha": f"claim-{number}",
        }
        self.world.save(state)

    def test_a_card_another_run_is_posting_is_waited_for(self):
        self.claim_of(16, datetime.datetime.now(datetime.UTC).isoformat(timespec="seconds"))
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"action": "labeled", "issue": {"number": 16}}, name="issues")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("issue-16 is being posted by another run", done.stdout)
        self.assertFalse(sent("issues", mark))
        self.assertEqual(self.world.state()["files"]["discord-cards:cards/issue-16"]["sha"], "claim-16")

    def test_a_claim_a_dead_run_left_is_taken_over(self):
        self.claim_of(17, "2026-01-01T00:00:00+00:00")
        mark = len(Discord.requests)
        done = self.run_cards("event", event={"action": "opened", "issue": {"number": 17}}, name="issues")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(sent("issues", mark)), 1)
        self.assertEqual(self.card_id("issue-17"), sent("issues", mark)[-1]["reply"])

    def test_dry_run_writes_nothing(self):
        self.edit(issues={"13": dict(self.issue, number=13)})
        mark = len(Discord.requests)
        done = self.run_cards("event", "--dry-run", event={"action": "opened", "issue": {"number": 13}}, name="issues")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse([c for c in self.world.calls() if "-X" in c["args"]])
        self.assertIn("would post on Discord (issues)", done.stdout)
        self.assertFalse(sent("issues", mark))


class Drawing(unittest.TestCase):
    def test_labels_as_github_pills_after_their_colour(self):
        self.assertEqual(
            [cards.chip({"name": n, "color": c}) for n, c in (("a", "b60205"), ("b", "d93f0b"), ("c", "fbca04"))],
            ["\U0001f7e5 `a`", "\U0001f7e7 `b`", "\U0001f7e8 `c`"],
        )
        self.assertEqual(
            [cards.square(c) for c in ("0e8a16", "0075ca", "d4c5f9", "ededed", "24292f", "5c3317")],
            ["\U0001f7e9", "\U0001f7e6", "\U0001f7ea", "⬜", "⬛", "\U0001f7eb"],
        )
        self.assertEqual(cards.chip({"name": "x"}), "`x`")

    def test_github_colours(self):
        self.assertEqual(
            (cards.OPEN, cards.DRAFT, cards.MERGED, cards.REFUSED, cards.DONE, cards.NOT_PLANNED),
            (0x1F883D, 0x6E7781, 0x8250DF, 0xCF222E, 0x8250DF, 0x6E7781),
        )

    def test_a_quote(self):
        self.assertEqual(cards.excerpt('a <!-- x --> b <img src="y"> <details>z</details>'), "> a  b [image] [details]")
        self.assertEqual(cards.excerpt("one two three", 9), "> one two...")
        self.assertTrue(cards.excerpt("```\ncode code code", 12).endswith("\n> ```"))

    def test_discords_limits(self):
        hub = cards.Hub(REPO, True)
        lines = "\n".join(f"- change {n} ([#{n}](https://x/pull/{n}))" for n in range(400))
        field = {"name": "NRO", "value": "[NRO](https://x/a)\n1.8 MB", "inline": True}
        cut = hub.embed("Nightly", "https://x", lines, [field] * 4)
        self.assertLessEqual(len(cut["description"]), cards.DESCRIPTION)
        self.assertTrue(lines.startswith(cut["description"]))
        self.assertEqual(lines[len(cut["description"])], "\n")
        self.assertEqual(len(cut["fields"]), 4)
        big = {"name": "x" * 256, "value": "y" * 1024, "inline": True}
        self.assertEqual(hub.embed("Nightly", "https://x", lines, [big] * 6)["description"], "")

    def test_the_jobs_followed_are_builds(self):
        # a renamed job would leave its field waiting forever
        with open(os.path.join(HERE, "..", "workflows", "build.yml"), encoding="utf-8") as file:
            build = file.read()
        for _, name, _ in cards.JOBS:
            self.assertRegex(build, rf"\n    name: {re.escape(name)}\n")
        self.assertIn(cards.ARTIFACT.format(short="${{ steps.commit.outputs.short }}"), build)


if __name__ == "__main__":
    unittest.main()
