#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Publish a Build run's NRO as a release, and say so on Discord.

    .github/scripts/publish-builds.py plan --repo OWNER/REPO
    .github/scripts/publish-builds.py publish --repo OWNER/REPO --kind nightly|release --run ID --sha SHA [--tag TAG]
        --artifacts DIR [--checkout DIR] [--keep N] [--dry-run]

plan follows the Build run that ended (GITHUB_EVENT_NAME workflow_run, GITHUB_EVENT_PATH), and writes to GITHUB_OUTPUT
the run whose NRO publish takes (kind, run, sha, short, tag), when there is one to publish:
- a push of a version tag (v...) that built becomes the release of that tag, the latest (a prerelease for a tag with a
  '-', v1.0.0-rc1);
- a push to main that built becomes the prerelease nightly-<date>-<commit>, never the latest, only the newest N
  nightlies staying; the prerelease "nightly" takes its files and notes, its tag moved to that commit, so
  .../releases/download/nightly/edenprobe.nro is always the newest nightly's NRO.
A pull request's run is not published: its NRO is the run's artifact, which build.yml links from the pull request.
Nothing is built: the NRO is the run's artifact edenprobe-<commit>.nro, which the workflow downloads into DIR between
the two, and every release holds it as edenprobe.nro with edenprobe.nro.sha256 beside it, which sha256sum -c reads.

Discord: a version's release gets a message on DISCORD_WEBHOOK_RELEASES's channel, the NRO linked with its size, and
the changes since the version before. A nightly's card (discord-cards.py), posted when it was pushed, is drawn again
once it is published. Without a webhook nothing is posted, and the run says so. gh reads and writes with GH_TOKEN.
--dry-run reads and prints each write instead. .github/workflows/publish.yml runs this; test_publish_builds.py beside
it runs it against a fake gh and a fake Discord.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
from pathlib import Path

spec = importlib.util.spec_from_file_location("discord_cards", Path(__file__).with_name("discord-cards.py"))
cards = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cards)
notes = cards.notes

SERVER = notes.SERVER
VERSION = re.compile(r"v\d[0-9A-Za-z.+-]*")
ROLLING = "nightly"
# Where every NRO says what it is: the header's magic, after the first 16 bytes
MAGIC, AT = b"NRO0", 16


def published(release):
    """a release's order: when it was published. Its created_at is its tag's commit's date, which two nightlies of one
    day may share"""
    return release.get("published_at") or ""


class Publisher(cards.Hub):
    def __init__(self, args):
        super().__init__(args.repo, args.dry_run)
        self.checkout, self.keep = args.checkout, args.keep
        self.artifacts = Path(args.artifacts) if args.artifacts else None
        self.work = Path(tempfile.mkdtemp(prefix="publish-builds-"))
        self.output = {}

    def cards_now(self):
        """the cards, as GitHub has them now"""
        return cards.Cards(self, self.checkout, self.keep)

    def release(self, tag):
        return next((r for r in self.releases() if r["tag_name"] == tag), None)

    def put_release(self, tag, name, body, prerelease, files, target=None):
        """the release of tag with these notes and exactly these files (each name to its path), made or replaced; it
        and whether it is new"""
        existing = self.release(tag)
        fields = {"name": name, "body": body, "prerelease": prerelease, "make_latest": str(not prerelease).lower()}
        if existing:
            made = self.write("PATCH", f"repos/{self.repo}/releases/{existing['id']}", fields) or existing
        else:
            fields = dict(fields, tag_name=tag, **({"target_commitish": target} if target else {}))
            made = self.write("POST", f"repos/{self.repo}/releases", fields)
        self.gh(["release", "upload", tag, *map(str, files.values()), "--repo", self.repo, "--clobber"], write=True)
        for asset in (existing or {}).get("assets", []):
            if asset["name"] not in files:  # a file this run did not make: the old one would pass for it
                self.write("DELETE", f"repos/{self.repo}/releases/assets/{asset['id']}")
        return made, existing is None

    def drop_release(self, release):
        self.write("DELETE", f"repos/{self.repo}/releases/{release['id']}")
        try:
            self.write("DELETE", f"repos/{self.repo}/git/refs/tags/{release['tag_name']}")
        except RuntimeError as error:
            if "Reference does not exist" not in str(error):
                raise
            print(f"{release['tag_name']} had no tag left")

    # ---- a run's NRO
    def nro(self, run, sha):
        """the run's NRO as a release holds it, and its checksum file: each name to its path, and the NRO's SHA-256.
        actions/download-artifact puts a file it does not unpack at the artifact's path, or in a folder of that name"""
        name = cards.ARTIFACT.format(short=sha[:7])
        found = self.artifacts / name
        if found.is_dir():
            inside = sorted(path for path in found.rglob("*") if path.is_file())
            if len(inside) != 1:
                raise RuntimeError(f"{name} holds {len(inside)} files, not the one NRO")
            found = inside[0]
        if not found.is_file():
            raise RuntimeError(f"run {run} built, yet has no {name}")
        data = found.read_bytes()
        if data[AT : AT + len(MAGIC)] != MAGIC:
            raise RuntimeError(f"{name} of run {run} is no NRO: no {MAGIC.decode()} at byte {AT}")
        path, check = self.work / notes.NRO, self.work / notes.SUM
        path.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        check.write_text(f"{digest}  {notes.NRO}\n")
        return {notes.NRO: path, notes.SUM: check}, digest

    def older(self, sha, previous):
        """sha is an ancestor of previous, a newer commit already published"""
        if not previous or previous == sha:
            return False
        command = ["git", "-C", self.checkout, "merge-base", "--is-ancestor", sha, previous]
        return subprocess.run(command, capture_output=True, check=False).returncode == 0

    # ---- what each run publishes
    def nightly(self, run, sha):
        files, digest = self.nro(run, sha)
        date = notes.git(self.checkout, "log", "-1", "--format=%cs", sha).strip()
        tag = f"nightly-{date}-{sha[:7]}"
        older = sorted(
            (r for r in self.releases() if r["tag_name"].startswith("nightly-") and r["tag_name"] != tag),
            key=published,
            reverse=True,
        )
        previous = next((m.group(0) for r in older[:1] for m in [re.search(r"[0-9a-f]{40}", r["body"] or "")] if m), "")
        if self.older(sha, previous):  # a run that ended after a newer commit's: that one stays the newest
            print(f"{sha[:7]} is older than the newest nightly, {previous[:7]}: not published")
            return
        size = files[notes.NRO].stat().st_size
        newest = f"{SERVER}/{self.repo}/releases/tag/{ROLLING}"
        kept = f"The newest {self.keep} nightlies are kept; [{ROLLING}]({newest}) always holds the newest one's files."
        where = f"{SERVER}/{self.repo}/releases/download/{tag}"
        body = notes.notes(self.checkout, sha, previous, self.repo, where, size, digest, kept)
        title = notes.title(self.checkout, sha)
        made, new = self.put_release(tag, title, body, True, files)
        for old in older[self.keep - 1 :]:
            self.drop_release(old)
            print(f"deleted {old['tag_name']}: past the newest {self.keep}")
        print(f"{'published' if new else 'replaced the files of'} {tag}")
        url = made.get("html_url", f"{SERVER}/{self.repo}/releases/tag/{tag}")
        self.point_rolling(title, url, sha, previous, files, size, digest)
        self.cards_now().nightly(sha)

    def point_rolling(self, title, url, sha, previous, files, size, digest):
        """the prerelease ROLLING: the newest nightly's files and notes, its tag on that commit"""
        if self.release(ROLLING):
            self.write("PATCH", f"repos/{self.repo}/git/refs/tags/{ROLLING}", {"sha": sha, "force": True})
        where = f"{SERVER}/{self.repo}/releases/download/{ROLLING}"
        lead = (
            f"The newest nightly, [{title}]({url}), whose files this release holds under a name that stays; the "
            f"newest {self.keep} are kept as releases of their own. Built from main at"
        )
        body = notes.notes(self.checkout, sha, previous, self.repo, where, size, digest, "", lead=lead)
        self.put_release(ROLLING, f"{title}, the newest", body, True, files, sha)
        print(f'"{ROLLING}" holds {title}')

    def version(self, run, sha, tag):
        files, digest = self.nro(run, sha)
        earlier = sorted(
            (
                r
                for r in self.releases()
                if VERSION.fullmatch(r["tag_name"]) and r["tag_name"] != tag and not r.get("draft")
            ),
            key=published,
            reverse=True,
        )
        since = earlier[0]["tag_name"] if earlier else ""
        previous = notes.git(self.checkout, "rev-list", "-n", "1", since).strip() if since else ""
        where = f"{SERVER}/{self.repo}/releases/download/{tag}"
        size = files[notes.NRO].stat().st_size
        title = f"edenprobe {tag}"
        body = notes.notes(
            self.checkout, sha, previous, self.repo, where, size, digest, "", since, f"{title}, built from"
        )
        made, new = self.put_release(tag, title, body, "-" in tag, files, sha)
        print(f"{'published' if new else 'replaced the files of'} {tag}")
        if not new:
            return
        url = made.get("html_url", f"{SERVER}/{self.repo}/releases/tag/{tag}")
        commit = f"{SERVER}/{self.repo}/commit/{sha}"
        fields = [
            {"name": "NRO", "value": f"[{notes.NRO}]({where}/{notes.NRO})\n{notes.size(size)}", "inline": True},
            {"name": "Source", "value": f"[`{sha[:7]}`]({commit})", "inline": True},
        ]
        compare = url
        if since:
            compare = f"{SERVER}/{self.repo}/compare/{since}...{tag}"
            fields.append({"name": "Since", "value": f"[{since}]({compare})", "inline": True})
        items = notes.changes(self.checkout, previous, sha) or []
        lines = [notes.change_line(item, f"{SERVER}/{self.repo}") for item in items]
        said = "\n".join(
            [
                f"Built from [`{sha[:7]}`]({commit}). SHA-256 `{digest}`.",
                *cards.Cards.listed(lines, compare, cards.MOST_LINES),
            ]
        )
        self.post("releases", self.embed(title, url, said, fields))

    # ---- what a run that ended asks for
    def take(self, kind, run, tag=""):
        """the run publish takes the NRO of, for the workflow to download it"""
        self.output = {
            "kind": kind,
            "run": str(run["id"]),
            "sha": run["head_sha"],
            "short": run["head_sha"][:7],
            "tag": tag,
        }
        print(f"to publish: the {kind} NRO of run {run['id']} ({run['head_sha'][:7]})")

    def tagged(self, name):
        """name is a tag of REPO (a run's head_branch is the tag a tag push pushed)"""
        try:
            return bool(self.read(f"repos/{self.repo}/git/ref/tags/{urllib.parse.quote(name)}"))
        except RuntimeError:
            return False

    def on_run(self, run):
        if run["event"] != "push":
            print(f"a run of {run['event']}: nothing to publish (a pull request's NRO is its run's artifact)")
            return
        name = run["head_branch"]
        if run["head_repository"]["full_name"] != self.repo:
            print(f"a push to {run['head_repository']['full_name']}: nothing to publish")
        elif name != "main" and not (VERSION.fullmatch(name) and self.tagged(name)):
            print(f"a push to {name}: neither main nor a version tag, nothing to publish")
        elif run["conclusion"] != "success":
            print(f"run {run['id']} of {name} ended {run['conclusion']}: nothing published")
        elif name == "main":
            self.take("nightly", run)
        else:
            self.take("release", run, tag=name)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("step", choices=["plan", "publish"])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--kind", choices=["nightly", "release"])
    parser.add_argument("--run", help="the Build run the NRO is of")
    parser.add_argument("--sha", help="the commit it built")
    parser.add_argument("--tag", help="the release's version tag")
    parser.add_argument("--artifacts", help="the folder the workflow downloaded the artifact into")
    parser.add_argument("--checkout", default=".", help="REPO's history and tags, for the notes")
    parser.add_argument("--keep", type=int, default=30, help="the nightlies kept")
    parser.add_argument("--dry-run", action="store_true", help="read, and print each write instead")
    args = parser.parse_args()
    if args.keep < 1:
        parser.error("--keep takes 1 or more")
    if args.step == "publish" and not (args.kind and args.run and args.sha and args.artifacts):
        parser.error("publish takes --kind, --run, --sha and --artifacts")
    if args.kind == "release" and not (args.tag and VERSION.fullmatch(args.tag)):
        parser.error("--kind release takes a version --tag (v...)")
    publisher = Publisher(args)
    try:
        if args.step == "publish":
            if args.kind == "nightly":
                publisher.nightly(args.run, args.sha)
            else:
                publisher.version(args.run, args.sha, args.tag)
            return
        name = os.environ["GITHUB_EVENT_NAME"]
        with open(os.environ["GITHUB_EVENT_PATH"], encoding="utf-8") as file:
            event = json.load(file)
        if name == "workflow_run":
            publisher.on_run(event["workflow_run"])
        else:
            print(f"{name}: nothing to publish")
        if publisher.output and os.environ.get("GITHUB_OUTPUT"):
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as file:
                file.writelines(f"{key}={value}\n" for key, value in publisher.output.items())
    finally:
        shutil.rmtree(publisher.work, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        sys.exit(f"publish-builds: {error}")
