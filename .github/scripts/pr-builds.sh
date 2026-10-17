#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Links a pull request's NRO from one comment, which each run of .github/workflows/build.yml rewrites, so it can be had
# from the pull request itself. A run of a commit that is no longer the branch's head leaves the comment alone.
#
#   .github/scripts/pr-builds.sh REPO PR RUN SHA [--print]
#
# NEEDS, when set, is the workflow's toJSON(needs): a job that did not succeed gets a line. --print writes the comment
# to stdout and posts nothing. gh needs a token that may write on pull requests. test_pr_builds.py beside it runs it
# against a fake gh.
set -euo pipefail

if [ $# -lt 4 ] || [ $# -gt 5 ] || { [ $# -eq 5 ] && [ "$5" != --print ]; }; then
    echo "usage: $0 REPO PR RUN SHA [--print]" >&2
    exit 2
fi
repo=$1 pr=$2 run=$3 sha=$4 print=${5:-}
mark='<!-- edenprobe-builds -->'

if [ -z "$print" ] && [ "$(gh api "repos/$repo/pulls/$pr" --jq .head.sha)" != "$sha" ]; then
    echo "$sha is no longer the head of #$pr: its comment stays"
    exit 0
fi
# the size to a tenth of a MiB: the NRO is under two, which a whole number would call 1
artifacts=$(gh api "repos/$repo/actions/runs/$run/artifacts?per_page=100")
links=$(jq -r --arg url "https://github.com/$repo/actions/runs/$run/artifacts" '
    .artifacts | map(select(.expired | not)) | sort_by(.name)[] |
    "- [\(.name)](\($url)/\(.id)), \((.size_in_bytes / 1048576 * 10 | floor) / 10) MiB"' <<<"$artifacts")
until=$(jq -r '[.artifacts[] | select(.expired | not) | .expires_at] | min // empty | .[:10]' <<<"$artifacts")
needs=${NEEDS:-}
[ -n "$needs" ] || needs='{}'
failed=$(jq -r 'to_entries[] | select(.value.result != "success") | "\(.key) (\(.value.result))"' <<<"$needs" |
    paste -sd, - | sed 's/,/, /g')

body="$mark
Builds of ${sha::7}${until:+, kept until $until}:
${links:-none}${failed:+

Not built: $failed}"

if [ -n "$print" ]; then
    printf '%s\n' "$body"
    exit 0
fi
id=$(gh api --paginate "repos/$repo/issues/$pr/comments?per_page=100" --jq \
    ".[] | select(.user.login == \"github-actions[bot]\" and (.body | startswith(\"$mark\"))) | .id" | head -n1)
if [ -n "$id" ]; then
    gh api -X PATCH "repos/$repo/issues/comments/$id" -f body="$body" > /dev/null
else
    gh api "repos/$repo/issues/$pr/comments" -f body="$body" > /dev/null
fi
echo "linked the builds of $sha from #$pr"
