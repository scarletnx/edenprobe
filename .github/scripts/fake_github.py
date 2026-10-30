# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""A fake gh (a small GitHub kept in a JSON file: releases and tags, pull requests, issues and their comments, Build
runs with their jobs and artifacts) and a fake Discord (a local HTTP server keeping each webhook's
messages), for the tests of the scripts beside it. Offline."""

import http.server
import json
import os
import subprocess
import sys
import threading
from pathlib import Path

FAKE_GH = r"""#!/usr/bin/env python3
import json, os, re, subprocess, sys
path = os.environ['FAKE_GH_STATE']
with open(path) as f:
    s = json.load(f)
args = sys.argv[1:]
with open(os.environ['FAKE_GH_LOG'], 'a') as log:
    log.write(json.dumps({'args': args, 'token': os.environ.get('GH_TOKEN')}) + '\n')
def save():
    with open(path, 'w') as f:
        json.dump(s, f)
jq = None
def out(value):
    if jq is None:
        print(json.dumps(value))
    else:  # gh's --jq: a string printed as it is
        done = subprocess.run(['jq', '-r', jq], input=json.dumps(value), capture_output=True, text=True, check=True)
        sys.stdout.write(done.stdout)
def stop(text):
    sys.stderr.write(text + '\n')
    sys.exit(1)
if args[:2] == ['release', 'upload']:
    end = args.index('--repo')
    repo = args[end + 1]
    release = next((r for r in s['releases'] if r['tag_name'] == args[2]), None) or stop('release not found')
    for file in args[3:end]:
        name = os.path.basename(file)
        release['assets'] = [a for a in release['assets'] if a['name'] != name]
        s['next'] += 1
        with open(file, 'rb') as f:
            data = f.read()
        url = f'https://github.com/{repo}/releases/download/{args[2]}/{name}'
        release['assets'].append({'id': s['next'], 'name': name, 'data': data.decode('latin-1'), 'size': len(data),
                                  'browser_download_url': url})
    save()
    sys.exit(0)
assert args[0] == 'api', args
method, target, fields, rest_args = None, None, {}, iter(args[1:])
for a in rest_args:
    if a == '-X':
        method = next(rest_args)
    elif a == '--jq':
        jq = next(rest_args)
    elif a in ('-f', '-F'):
        key, _, value = next(rest_args).partition('=')
        fields[key] = value
    elif a == '--input':
        next(rest_args)
    elif a.startswith('-'):
        pass  # --paginate: one page here
    elif target is None:
        target = a
data = json.load(sys.stdin) if '--input' in args else (fields or None)
method = method or ('POST' if fields and target != 'graphql' else 'GET')
if target == 'graphql':  # the links of a pull request or an issue, as GitHub's GraphQL gives them
    asked = fields['query']
    number = re.search(r'number:(\d+)', asked).group(1)
    field, refs = (('pullRequest', 'closingIssuesReferences') if 'pullRequest(' in asked
                   else ('issue', 'closedByPullRequestsReferences'))
    out({'data': {'repository': {field: {refs: {'nodes': s.get('links', {}).get(number, [])}}}}})
    sys.exit(0)
route, _, query = target.partition('?')
query = dict(p.split('=', 1) for p in query.split('&') if p)
page, per = int(query.get('page', 1)), int(query.get('per_page', 30))
def listing(items):
    out(items[(page - 1) * per:page * per])
m = re.fullmatch(r'repos/([^/]+/[^/]+)(?:/(.*))?', route)
repo, rest = m.group(1), m.group(2) or ''
if repo != s['repo']:
    stop(f'gh: Not Found (HTTP 404): {repo} is not this test\'s repository')
releases, tags = s['releases'], s['tags']
if rest == 'releases' and method == 'GET':
    listing(sorted(releases, key=lambda r: r['created_at'], reverse=True))
elif rest == 'releases' and method == 'POST':
    s['next'] += 1
    s['clock'] += 1
    # created_at is the tag's commit's date, as GitHub has it: nightlies of one day share it
    release = dict(data, id=s['next'], created_at='2026-10-01T00:00:00Z', published_at=f'2026-11-01T{s["clock"]:08d}',
                   assets=[], html_url=f'https://github.com/{repo}/releases/tag/{data["tag_name"]}')
    releases.append(release)
    tags.setdefault(data['tag_name'], data.get('target_commitish', 'default-branch'))
    save()
    out(release)
elif rest.startswith('releases/assets/') and method == 'DELETE':
    number = int(rest.split('/')[-1])
    for release in releases:
        release['assets'] = [a for a in release['assets'] if a['id'] != number]
    save()
elif rest.startswith('releases/') and method in ('PATCH', 'DELETE'):
    number = int(rest.split('/')[-1])
    release = next((r for r in releases if r['id'] == number), None) or stop('no such release')
    if method == 'DELETE':
        releases.remove(release)
    else:
        release.update(data)
        out(release)
    save()
elif rest.startswith('git/refs/tags/') and method in ('DELETE', 'PATCH'):
    tag = rest[len('git/refs/tags/'):]
    if tag not in tags:
        stop('gh: Reference does not exist (HTTP 422)')
    if method == 'DELETE':
        del tags[tag]
    else:
        tags[tag] = data['sha']
    save()
elif rest.startswith('git/ref/tags/'):
    tag = rest[len('git/ref/tags/'):]
    out({'ref': f'refs/tags/{tag}', 'object': {'sha': tags[tag]}}) if tag in tags else stop('gh: Not Found (HTTP 404)')
elif rest == 'pulls' and method == 'GET':
    listing([p for p in s['pulls'] if p['state'] == 'open'])
elif re.fullmatch(r'pulls/\d+/commits', rest):
    listing(s.get('commits', {}).get(rest.split('/')[1], []))
elif re.fullmatch(r'compare/[0-9a-f]+\.\.\.[0-9a-f]+', rest):
    out({'merge_base_commit': {'sha': s['merge_base']}})
elif re.fullmatch(r'pulls/\d+', rest):
    out(next(p for p in s['pulls'] if p['number'] == int(rest.split('/')[1])))
elif rest == 'actions/workflows/build.yml/runs':
    out({'workflow_runs': [r for r in s['runs'] if r['head_sha'] == query.get('head_sha')]})
elif rest == '' and method == 'GET':
    out({'full_name': repo, 'default_branch': 'main'})
elif re.fullmatch(r'issues/\d+', rest):
    out(s['issues'][rest.split('/')[1]])
elif re.fullmatch(r'issues/\d+/comments', rest) and method == 'POST':
    s['next'] += 1
    comment = {'id': s['next'], 'user': {'login': 'github-actions[bot]', 'type': 'Bot'}, 'body': data['body']}
    s.setdefault('comments', {}).setdefault(rest.split('/')[1], []).append(comment)
    save()
    out(comment)
elif re.fullmatch(r'issues/\d+/comments', rest):
    listing(s.get('comments', {}).get(rest.split('/')[1], []))
elif re.fullmatch(r'issues/comments/\d+', rest) and method == 'PATCH':
    number = int(rest.split('/')[-1])
    comment = next(c for cs in s.get('comments', {}).values() for c in cs if c.get('id') == number)
    comment['body'] = data['body']
    save()
    out(comment)
elif re.fullmatch(r'pulls/\d+/reviews', rest):
    listing(s.get('reviews', {}).get(rest.split('/')[1], []))
elif re.fullmatch(r'pulls/\d+/comments', rest):
    listing(s.get('review_comments', {}).get(rest.split('/')[1], []))
elif re.fullmatch(r'actions/runs/\d+', rest):
    run = next(r for r in s['runs'] if r['id'] == int(rest.split('/')[-1]))
    if run.get('steps'):  # a build going on: each look at it finds it a step further
        step = run['steps'].pop(0)
        run.update(status=step['status'], conclusion=step.get('conclusion'), jobs=step['jobs'])
        save()
    out(run)
elif re.fullmatch(r'actions/runs/\d+/jobs', rest):
    run = next(r for r in s['runs'] if r['id'] == int(rest.split('/')[2]))
    out({'jobs': run.get('jobs', [])})
elif re.fullmatch(r'actions/runs/\d+/artifacts', rest):
    run = next((r for r in s['runs'] if r['id'] == int(rest.split('/')[2])), {})
    out({'total_count': len(run.get('artifacts', [])), 'artifacts': run.get('artifacts', [])})
else:
    stop(f'fake gh: {method} {target}')
"""


class Discord(http.server.BaseHTTPRequestHandler):
    requests, messages, next_id = [], {}, [1000]

    def log_message(self, *args):
        pass

    def reply(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def handle_one(self, method):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        Discord.requests.append(
            {"method": method, "path": self.path, "body": body, "agent": self.headers["User-Agent"]}
        )
        path = self.path.split("?")[0]
        if method == "POST":
            Discord.next_id[0] += 1
            Discord.messages[str(Discord.next_id[0])] = body
            Discord.requests[-1]["reply"] = str(Discord.next_id[0])
            self.reply(200, {"id": str(Discord.next_id[0])})
        elif path.split("/")[-1] in Discord.messages:
            Discord.messages[path.split("/")[-1]] = body
            self.reply(200, {"id": path.split("/")[-1]})
        else:
            self.reply(404, {"message": "Unknown Message"})

    def do_POST(self):
        self.handle_one("POST")

    def do_PATCH(self):
        self.handle_one("PATCH")


def posted(channel):
    return [r for r in Discord.requests if f"token-{channel}" in r["path"]]


def serve():
    """the fake Discord started: its server, and the webhook URL every channel's hook starts with"""
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Discord)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/api/webhooks/1/token"


class World:
    """a test's own GitHub: a folder with the fake gh first on PATH, its state and its log, and a git history the
    scripts read as their checkout"""

    def __init__(self, root, repo):
        self.root, self.repo = Path(root), repo
        self.bin, self.checkout = self.root / "bin", self.root / "checkout"
        self.state_file, self.log_file = self.root / "state.json", self.root / "gh.log"
        self.bin.mkdir(parents=True)
        (self.bin / "gh").write_text(FAKE_GH)
        (self.bin / "gh").chmod(0o755)
        self.checkout.mkdir()
        self.git("init", "-q", "-b", "main")

    def git(self, *args, date="2026-10-15T12:00:00Z"):
        env = dict(os.environ, GIT_COMMITTER_DATE=date, GIT_AUTHOR_DATE=date)
        for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE"):  # a hook's, when the tests run from one
            env.pop(name, None)
        command = ["git", "-C", str(self.checkout), "-c", "user.name=t", "-c", "user.email=t@t"]
        command += ["-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "core.hooksPath=" + os.devnull]
        return subprocess.run([*command, *args], check=True, capture_output=True, text=True, env=env).stdout.strip()

    def commit(self, message, date="2026-10-15T12:00:00Z"):
        self.git("commit", "-q", "--allow-empty", "-m", message, date=date)
        return self.git("rev-parse", "HEAD")

    def save(self, state):
        self.state_file.write_text(json.dumps(dict(state, repo=self.repo)))

    def state(self):
        return json.loads(self.state_file.read_text())

    def calls(self):
        return [json.loads(line) for line in self.log_file.read_text().splitlines()] if self.log_file.exists() else []

    def env(self, **more):
        env = dict(
            os.environ,
            PATH=f"{self.bin}{os.pathsep}{os.environ['PATH']}",
            FAKE_GH_STATE=str(self.state_file),
            FAKE_GH_LOG=str(self.log_file),
            GH_TOKEN="repo-token",
        )
        for name in [n for n in env if n.startswith("DISCORD_WEBHOOK_") or n.startswith("GITHUB_")]:
            env.pop(name)
        env.update(more)
        return env

    def run(self, command, **env):
        return subprocess.run(command, capture_output=True, text=True, env=self.env(**env), check=False)


def python(script, *args):
    return [sys.executable, str(script), *args]
