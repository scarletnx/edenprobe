#!/bin/sh
# Runs hooks/commit-msg against a matrix of messages in a throwaway repository, and with
# --history <range> against the commits already in this one. Exit status is the verdict.
#
#   hooks/test-commit-msg.sh                 the matrix
#   hooks/test-commit-msg.sh --history A..B  every commit in the range, with its own author

set -u
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
hook=$here/commit-msg
failures=0

# Nothing from the caller's environment reaches the check; --history keeps GIT_DIR and the
# global config, so it can be pointed at a repository and a hook.forbidWord kept there counts.
unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL \
    GIT_COMMITTER_DATE GIT_EDITOR GIT_CONFIG_COUNT GIT_CONFIG_PARAMETERS GIT_CONFIG \
    GIT_TEMPLATE_DIR LANGUAGE CDPATH

if [ "${1:-}" = "--history" ]; then
    [ -n "${2:-}" ] || { echo "usage: $0 --history <range>   (a single rev means all of its history)" >&2; exit 2; }
    revs=$(git rev-list --reverse "$2") || exit 2
    export GIT_EDITOR=: HOOK_HISTORY=1    # a landed message is content as it is
    tmp=$(mktemp) || exit 1
    trap 'rm -f "$tmp" "$tmp.err"' EXIT
    for commit in $revs; do
        reason=
        if [ "$(git rev-list --parents -n 1 "$commit" | wc -w)" -gt 2 ]; then
            reason="merge commit"
        fi
        case "$(git log -1 --format=%s "$commit")" in
            'fixup! '*|'squash! '*|'amend! '*) reason="a fixup!, squash! or amend! that was never folded" ;;
        esac
        if [ -n "$reason" ]; then
            printf '%s %s\n    %s\n' "$(git log -1 --format=%h "$commit")" \
                "$(git log -1 --format=%s "$commit")" "$reason"
            failures=$((failures + 1))
            continue
        fi
        printf '%s\n' "$(git log -1 --format=%B "$commit")" > "$tmp"
        if ! GIT_AUTHOR_NAME=$(git log -1 --format=%an "$commit") \
             GIT_AUTHOR_EMAIL=$(git log -1 --format=%ae "$commit") \
             GIT_COMMITTER_NAME=$(git log -1 --format=%cn "$commit") \
             GIT_COMMITTER_EMAIL=$(git log -1 --format=%ce "$commit") \
             "$hook" "$tmp" 2>"$tmp.err"; then
            printf '%s %s\n    %s\n' "$(git log -1 --format=%h "$commit")" \
                "$(git log -1 --format=%s "$commit")" "$(cat "$tmp.err")"
            failures=$((failures + 1))
        fi
    done
    echo "history: $failures commit(s) the hook would refuse"
    [ "$failures" -eq 0 ]
    exit
fi

# The matrix makes its own repository: every variable that could point git elsewhere goes.
# shellcheck disable=SC2046  # the list is git's own, one name per word
unset $(git rev-parse --local-env-vars)
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C
repo=$(mktemp -d) || exit 1
trap 'rm -rf "$repo" "$repo.remote"' EXIT
cd "$repo" || exit 1
git init -q
git config core.hooksPath "$here"
git config commit.gpgsign false
git config user.name AF51
git config user.email AF51@dravee.dev
export GIT_EDITOR=true

SIGNED='Signed-off-by: AF51 <AF51@dravee.dev>'
SUBJECT=$(sed -n "s/^SUBJECT_EXAMPLE='\(.*\)'\$/\1/p" "$hook")
[ -n "$SUBJECT" ] || { echo "no SUBJECT_EXAMPLE in $hook" >&2; exit 2; }

# try <expect: ok|refused> <label> <message> [git commit options...]
try() {
    expect=$1; label=$2; message=$3; shift 3
    printf '%s\n' "$message" > msg
    if git commit -q --allow-empty -F msg "$@" >out 2>&1; then got=ok; else got=refused; fi
    if [ "$got" = "$expect" ]; then
        echo "ok      $label"
    else
        echo "FAILED  $label: expected $expect, got $got"
        sed 's/^/        /' out
        failures=$((failures + 1))
    fi
}

body1="One line that says why."
body3="One line that says why.
A second line for the how.
A third for what it does not do."

try ok      "subject, one line of intent, sign-off" "$SUBJECT

$body1

$SIGNED"
if [ "$(git log -1 --format=%B | grep -c '^Change-Id: I[0-9a-f]\{40\}$')" -eq 1 ] \
    && [ "$(printf '%s\n' "$(git log -1 --format=%B)" | tail -n 1)" = "$SIGNED" ]; then
    echo "ok      the hook wrote a Change-Id above the sign-off"
else
    echo "FAILED  the hook wrote a Change-Id above the sign-off"; git log -1 --format=%B | sed 's/^/        /'; failures=$((failures + 1))
fi
CHANGE_ID="Change-Id: I$(printf '%040d' 7 | tr 0 a)"
try ok      "a Change-Id given by hand is kept" "$SUBJECT

$body1

$CHANGE_ID
$SIGNED"
if [ "$(git log -1 --format=%B | grep -c "^$CHANGE_ID\$")" -eq 1 ] && [ "$(git log -1 --format=%B | grep -c '^Change-Id: ')" -eq 1 ]; then
    echo "ok      the given Change-Id landed once"
else
    echo "FAILED  the given Change-Id landed once"; git log -1 --format=%B | sed 's/^/        /'; failures=$((failures + 1))
fi
try refused "two Change-Ids" "$SUBJECT

$body1

$CHANGE_ID
Change-Id: I$(printf '%040d' 8 | tr 0 b)
$SIGNED"
try refused "a malformed Change-Id" "$SUBJECT

$body1

Change-Id: I1234
$SIGNED"
try refused "a Change-Id after the sign-off" "$SUBJECT

$body1

$SIGNED
$CHANGE_ID"
try ok      "Squashed lines between the Change-Id and the sign-off" "$SUBJECT

$body1

$CHANGE_ID
Squashed: I$(printf '%040d' 1 | tr 0 c)
Squashed: I$(printf '%040d' 2 | tr 0 d)
$SIGNED"
try refused "a Squashed line before the Change-Id" "$SUBJECT

$body1

Squashed: I$(printf '%040d' 1 | tr 0 c)
$CHANGE_ID
$SIGNED"
try ok      "Squashed lines with no Change-Id get one written above them" "$SUBJECT

$body1

Squashed: I$(printf '%040d' 1 | tr 0 c)
$SIGNED"
if [ "$(printf '%s\n' "$(git log -1 --format=%B)" | grep -n '^Change-Id: ' | cut -d: -f1)" -lt "$(printf '%s\n' "$(git log -1 --format=%B)" | grep -n '^Squashed: ' | cut -d: -f1)" ]; then
    echo "ok      the written Change-Id sits above the Squashed line"
else
    echo "FAILED  the written Change-Id sits above the Squashed line"; git log -1 --format=%B | sed 's/^/        /'; failures=$((failures + 1))
fi
try refused "the same Squashed value twice" "$SUBJECT

$body1

$CHANGE_ID
Squashed: I$(printf '%040d' 1 | tr 0 c)
Squashed: I$(printf '%040d' 1 | tr 0 c)
$SIGNED"
try refused "a Squashed value equal to the Change-Id" "$SUBJECT

$body1

$CHANGE_ID
Squashed: ${CHANGE_ID#Change-Id: }
$SIGNED"
try refused "a short Squashed value" "$SUBJECT

$body1

$CHANGE_ID
Squashed: I1234
$SIGNED"
git commit -q --allow-empty --amend -m "$SUBJECT amended

$body1

$SIGNED" >out 2>&1 || { echo "FAILED  --amend -m: refused"; sed 's/^/        /' out; failures=$((failures + 1)); }
if [ "$(git log -1 --format='%(trailers:key=Change-Id,valueonly)' | tr -d ' ')" = "$(git log -1 --format='%(trailers:key=Change-Id,valueonly)' 'HEAD@{1}' | tr -d ' ')" ]; then
    echo "ok      --amend with a new message keeps the key of the commit it replaces"
else
    echo "FAILED  --amend with a new message keeps the key of the commit it replaces"; failures=$((failures + 1))
fi
# shellcheck disable=SC2016  # the editor script reads its own $1
printf '#!/bin/sh\nsed -i "s/^Signed-off-by: AF51 <AF51@dravee.dev>\$/Signed-off-by: AF51 <AF51@dravee.dev>   /" "$1"\n' > trailing.sh
chmod +x trailing.sh
export GIT_EDITOR=./trailing.sh
try ok      "a sign-off left with trailing spaces by the editor" "$SUBJECT

$body1

$SIGNED" -e
export GIT_EDITOR=true
if [ "$(git log -1 --format=%B | grep -c '^Change-Id: I[0-9a-f]\{40\}$')" -eq 1 ]; then
    echo "ok      the Change-Id was written despite the trailing spaces"
else
    echo "FAILED  the Change-Id was written despite the trailing spaces"; git log -1 --format=%B | sed 's/^/        /'; failures=$((failures + 1))
fi
try refused "a Squashed line with a bad value" "$SUBJECT

$body1

$CHANGE_ID
Squashed: abc
$SIGNED"
try refused "another trailer in the last paragraph" "$SUBJECT

$body1

$CHANGE_ID
Reviewed-by: Someone <someone@example.com>
$SIGNED"
try ok      "three lines of intent" "$SUBJECT

$body3

$SIGNED"
try refused "four lines of intent" "$SUBJECT

$body3
And a fourth that is the changelog.

$SIGNED"
try refused "no line of intent" "$SUBJECT

$SIGNED"
try refused "no blank line after the subject" "$SUBJECT
$body1

$SIGNED"
SUBJECT_HISTORY=$(sed -n "s/^SUBJECT_HISTORY_EXAMPLE='\(.*\)'\$/\1/p" "$hook")
[ -n "$SUBJECT_HISTORY" ] || { echo "no SUBJECT_HISTORY_EXAMPLE in $hook" >&2; exit 2; }
try ok      "the prefix from before the rename, for history" "$SUBJECT_HISTORY

$body1

$SIGNED"
try refused "subject without the prefix" "keep the wakeup a reset used to lose

$body1

$SIGNED"
if grep -q "subject should read: ${SUBJECT%%:*}: " out; then
    echo "ok      the refusal names the prefix new subjects take"
else
    echo "FAILED  the refusal does not name ${SUBJECT%%:*}:"; sed 's/^/        /' out
    failures=$((failures + 1))
fi
try refused "subject over the limit" "$SUBJECT and then it keeps going on and on past seventy two characters

$body1

$SIGNED"
try refused "merge commit" "Merge branch 'feature' into master

$body1

$SIGNED"
try refused "no sign-off" "$SUBJECT

$body1"
try refused "two sign-offs with someone else last" "$SUBJECT

$body1

$SIGNED
Signed-off-by: AO55 <AO55@dravee.dev>"
try ok      "two sign-offs with the author last, as a squash of two authors" "$SUBJECT

$body1

Signed-off-by: AO55 <AO55@dravee.dev>
$SIGNED"
try refused "the same sign-off twice" "$SUBJECT

$body1

$SIGNED
$SIGNED"
try refused "a sign-off without the angle brackets" "$SUBJECT

$body1

Signed-off-by: AO55
$SIGNED"
try ok      "a Ref code between the keys and the sign-offs" "$SUBJECT

$body1

Change-Id: I$(printf '%040d' 9 | tr 0 e)
Ref: ECFE-2026-0042
$SIGNED"
try refused "a malformed Ref" "$SUBJECT

$body1

Ref: ECFE-42
$SIGNED"
try refused "a Ref after the sign-off" "$SUBJECT

$body1

$SIGNED
Ref: ECFE-2026-0042"
try refused "a Ref before a Squashed line" "$SUBJECT

$body1

Change-Id: I$(printf '%040d' 9 | tr 0 e)
Ref: ECFE-2026-0042
Squashed: I$(printf '%040d' 1 | tr 0 c)
$SIGNED"
try refused "vendor trailer as a fourth paragraph line" "$SUBJECT

$body1

$SIGNED
Co-Authored-By: Some Vendor <noreply@example.com>"
try refused "sign-off is not the author" "$SUBJECT

$body1

Signed-off-by: AO55 <AO55@dravee.dev>"
try refused "a body line over ninety characters" "$SUBJECT

This line of intent is padded until it is longer than the ninety characters the hook allows here.

$SIGNED"
try refused "non-ascii" "$SUBJECT

One line $(printf '\342\200\224') with a dash that is not ascii.

$SIGNED"
try ok      "fixup with no body passes untouched" "fixup! $SUBJECT"
try refused "fixup with a body" "fixup! $SUBJECT

A body that autosquash would drop, or that squash would land."
try refused "squash" "squash! $SUBJECT

A body that autosquash lands unchecked."
try ok      "amend with the message the target will get" "amend! $SUBJECT

$SUBJECT

$body1

$SIGNED"
try refused "an amend! header of two lines" "amend! $SUBJECT
and a second line

$SUBJECT

$body1

$SIGNED"
try refused "amend with a bad subject inside" "amend! $SUBJECT

keep the wakeup a reset used to lose

$body1

$SIGNED"

# Shape: exactly three paragraphs, and nothing can hide above or below the sign-off.
try refused "vendor trailer as a paragraph of its own" "$SUBJECT

$body1

Co-Authored-By: Some Vendor <noreply@example.com>

$SIGNED"
try refused "second sign-off as a paragraph of its own" "$SUBJECT

$body1

Signed-off-by: AO55 <AO55@dravee.dev>

$SIGNED"
try refused "session trailer as the intent" "$SUBJECT

Claude-Session: https://example.com/session

$SIGNED"
try refused "a --- paragraph below the sign-off" "$SUBJECT

$body1

$SIGNED

---
Co-Authored-By: Some Vendor <noreply@example.com>"
try refused "sign-off above the intent" "$SUBJECT

$SIGNED

$body1"
try refused "sign-off folded over two lines" "$SUBJECT

$body1

Signed-off-by: AF51
 <AF51@dravee.dev>"
try refused "sign-off without the space" "$SUBJECT

$body1

Signed-off-by:AF51 <AF51@dravee.dev>"
try refused "sign-off in lower case" "$SUBJECT

$body1

signed-off-by: AF51 <AF51@dravee.dev>"
try ok      "a line of intent that starts with Word:" "$SUBJECT

Note: the wakeup is kept now.

$SIGNED"
try ok      "a url as a line of intent" "$SUBJECT

https://example.com/issue/1 has the trace.

$SIGNED"
try refused "a Reviewed-by line as intent" "$SUBJECT

Reviewed-by: Someone <someone@example.com>

$SIGNED"

# Comment lines: git keeps them with -m and -F, so they are not stripped, they are refused.
try refused "a comment line in the body" "$SUBJECT

$body1
# Co-Authored-By: Some Vendor <noreply@example.com>

$SIGNED"
try refused "a comment line as the subject" "# not a subject
$SUBJECT

$body1

$SIGNED"
try refused "a comment line after the sign-off, as -F keeps it" "$SUBJECT

$body1

$SIGNED
# Co-Authored-By: Some Vendor <noreply@example.com>"
try refused "a scissors line inside a -F message hides nothing" "$SUBJECT

$body1

$SIGNED
# ------------------------ >8 ------------------------
Co-Authored-By: Some Vendor <noreply@example.com>"
try refused "an empty message" "" --allow-empty-message
try refused "a message that is only a comment" "# only a comment" --allow-empty-message
try refused "git's template pasted back through -F is content" "$SUBJECT

$body1

$SIGNED
# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit.
#
# On branch master
"
try ok      "commit -v puts the diff below the scissors" "$SUBJECT

$body1

$SIGNED" -v -e
try ok      "git's own template below the message, with an editor" "$SUBJECT

$body1

$SIGNED
# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit.
#
# On branch master
" -e
git config commit.cleanup scissors
try refused "the scissors cleanup mode keeps comment lines above the cut" "$SUBJECT

$body1

$SIGNED
# Co-Authored-By: Some Vendor <noreply@example.com>" -e
git config --unset commit.cleanup
editor_saved=$GIT_EDITOR
unset GIT_EDITOR
git config core.editor true
try ok      "an editor from core.editor, GIT_EDITOR not in the environment" "$SUBJECT

$body1

$SIGNED
# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit.
" -e
try ok      "--amend through core.editor" "$SUBJECT

$body1

$SIGNED" --amend
git config --unset core.editor
export GIT_EDITOR=$editor_saved
if [ -f /usr/share/locale/de/LC_MESSAGES/git.mo ]; then
    german=1
    export LANGUAGE=de LC_ALL=C.UTF-8
else
    german=
    echo "skip    the translated-git cases: no German git catalog on this machine"
fi
[ -z "$german" ] || try ok      "a translated template, with an editor" "$SUBJECT

$body1

$SIGNED
# Bitte geben Sie eine Commit-Beschreibung ein. Zeilen, die mit
# '#' beginnen, werden ignoriert.
" -e
unset LANGUAGE; export LC_ALL=C
echo staged > staged.txt
git add staged.txt
try ok      "commit -v with a real diff below the scissors" "$SUBJECT

$body1

$SIGNED" -v -e
echo again >> staged.txt
git add staged.txt
[ -z "$german" ] || export LANGUAGE=de LC_ALL=C.UTF-8
[ -z "$german" ] || try ok      "commit -v with a diff, translated git" "$SUBJECT

$body1

$SIGNED" -v -e
unset LANGUAGE; export LC_ALL=C
git rm -q --cached staged.txt
echo bundle >> staged.txt
git add staged.txt
try ok      "-va, the v first in the bundle" "$SUBJECT

$body1

$SIGNED" -va -e
echo more >> staged.txt
git add staged.txt
try ok      "--verb, an unambiguous prefix" "$SUBJECT

$body1

$SIGNED" --verb -e
echo more >> staged.txt
git add staged.txt
try refused "-v then --no-verbose, the last word winning" "$SUBJECT

$body1

$SIGNED
# ------------------------ >8 ------------------------
# Do not modify or remove the line above.
diff --git a/f b/f
index df967b9..176d106 100644
--- a/f
+++ b/f
@@ -1 +1,2 @@
 base" -v --no-verbose -e
git rm -q --cached staged.txt
git config commit.cleanup verbatim
if git commit -q --allow-empty --fixup=HEAD >out 2>&1; then
    echo "ok      --fixup=HEAD under commit.cleanup=verbatim"
else
    echo "FAILED  --fixup=HEAD under commit.cleanup=verbatim: expected ok, got refused"; sed 's/^/        /' out; failures=$((failures + 1))
fi
git config --unset commit.cleanup
if git commit -q --allow-empty --fixup=HEAD -s >out 2>&1; then
    echo "ok      --fixup=HEAD with -s, the sign-off autosquash drops"
else
    echo "FAILED  --fixup=HEAD with -s: expected ok, got refused"; sed 's/^/        /' out; failures=$((failures + 1))
fi
try refused "a fixup! with a body besides the sign-off" "fixup! $SUBJECT

$body1

$SIGNED"
try refused "a bare amend! line" "amend! $SUBJECT"
echo lines >> staged.txt
git add staged.txt
# shellcheck disable=SC2016  # the editor script reads its own $1
printf '#!/bin/sh\nprintf "%%s\\n\\n%%s\\n\\n%%s\\n" "$SUBJECT" "One line that says why." "$SIGNED" > "$1"\n' > rewrite.sh
sed -i "s|\$SUBJECT|$SUBJECT|; s|\$SIGNED|$SIGNED|" rewrite.sh
chmod +x rewrite.sh
if GIT_EDITOR=./rewrite.sh git commit -q -e -m "$(printf 'draft\n--\nnotes')" -v >out 2>&1; then
    echo "ok      a multi-line -m with a -- line does not hide -v"
else
    echo "FAILED  a multi-line -m with a -- line: expected ok, got refused"; sed 's/^/        /' out; failures=$((failures + 1))
fi
git config commit.verbose 2
echo v2 >> staged.txt
git add staged.txt
try ok      "commit.verbose=2 cuts the diff like -v" "$SUBJECT

$body1

$SIGNED" -e
git config --unset commit.verbose
git config core.commentChar auto
try refused "core.commentChar=auto with an editor" "$SUBJECT

$body1

$SIGNED" -e
git config --unset core.commentChar
try refused "a saved commit -v message retried without -v keeps its diff" "$SUBJECT

$body1

$SIGNED
# ------------------------ >8 ------------------------
# Do not modify or remove the line above.
# Everything below it will be ignored.
diff --git a/f b/f
index df967b9..176d106 100644
--- a/f
+++ b/f
@@ -1 +1,2 @@
 base" -e
git config commit.verbose true
try ok      "commit.verbose with a submodule line where the diff starts" "$SUBJECT

$body1

$SIGNED
# ------------------------ >8 ------------------------
# Do not modify or remove the line above.
Submodule ext 01bffa3..5c6de0f:
  > three" -e
git config --unset commit.verbose
try refused "a subject and nothing else" "$SUBJECT"
try refused "a glued sign-off after three paragraphs" "$SUBJECT

$body1

more words
$SIGNED"
git config commit.cleanup whitespace
try refused "commit.cleanup=whitespace with an editor" "$SUBJECT

$body1

$SIGNED
# a comment an editor session left" -e
git config commit.cleanup verbatim
try refused "a leading blank line under commit.cleanup=verbatim without an editor" "
$SUBJECT

$body1

$SIGNED"
git config --unset commit.cleanup
try ok      "a message that mentions --cleanup=verbatim in prose" "$SUBJECT

The flag --cleanup=verbatim is never used here.

$SIGNED" -e
git config commit.cleanup verbatim
try refused "doubled blank lines under commit.cleanup=verbatim without an editor" "$SUBJECT


$body1

$SIGNED"
try refused "a trailing blank line under commit.cleanup=verbatim without an editor" "$SUBJECT

$body1

$SIGNED
"
git config --unset commit.cleanup
try refused "a fixup! header of two lines" "fixup! $SUBJECT
and a second line"
try ok      "prose keys with digits are not trailers" "$SUBJECT

ARM64: the JIT now keeps the flags. IPv6: still off.

$SIGNED"
try refused "a Fix: line as intent" "$SUBJECT

Fix: #42

$SIGNED"
git config core.commentChar '.'
try ok      "a comment character that is special in a pattern" "$SUBJECT

$body1

$SIGNED
. Please enter the commit message for your changes.
" -e
git config --unset core.commentChar
printf '%s\n' "$SUBJECT" '' 'BODY' '' "$SIGNED" > template.txt
# shellcheck disable=SC2016  # the editor script reads its own $1
printf '#!/bin/sh\nsed -i "s/^BODY$/One line that says why./" "$1"\n' > edit.sh
chmod +x edit.sh
if GIT_EDITOR=./edit.sh git commit -q --allow-empty -e -t template.txt >out 2>&1; then
    echo "ok      commit.template with an editor"
else
    echo "FAILED  commit.template with an editor: expected ok, got refused"; sed 's/^/        /' out; failures=$((failures + 1))
fi
try refused "-s glues the sign-off to a line that looks like a trailer" "$SUBJECT

Note: the wakeup is kept now." -s
if git commit -q --allow-empty --fixup=amend:HEAD >out 2>&1; then
    echo "ok      --fixup=amend:HEAD, checked as the message it becomes"
else
    echo "FAILED  --fixup=amend:HEAD: expected ok, got refused"; sed 's/^/        /' out; failures=$((failures + 1))
fi
try refused "a forge key as intent" "$SUBJECT

Closes: 42

$SIGNED"
git config commit.cleanup verbatim
try refused "a cleanup mode that keeps comment lines" "$SUBJECT

$body1

$SIGNED" -e
git config --unset commit.cleanup
try refused "--cleanup=whitespace with an editor keeps the template" "$SUBJECT

$body1

$SIGNED" -e --cleanup=whitespace
# shellcheck disable=SC2016  # the editor script reads its own $1
printf '#!/bin/sh\nprintf "Co-authored-by: Some Vendor <noreply@example.com>\\n" >> "$1"\n' > append.sh
chmod +x append.sh
export GIT_EDITOR=./append.sh
try refused "an editor that appends below the template" "$SUBJECT

$body1

$SIGNED" -e
export GIT_EDITOR=true
try refused "a fake template through -F" "$SUBJECT

$body1

$SIGNED
# Please enter the commit message for your changes.
Co-authored-by: Some Vendor <noreply@example.com>"
try ok      "a leading blank line, which git drops before the hook runs" "
$SUBJECT

$body1

$SIGNED"
try refused "a hyphenated key as intent" "$SUBJECT

X-Agent: something

$SIGNED"
try refused "a trailer key with a digit" "$SUBJECT

Co-authored-by2: V <v@example.com>

$SIGNED"
try refused "a trailer hidden behind a leading space" "$SUBJECT

 Co-authored-by: V <v@example.com>

$SIGNED"
try refused "filler punctuation as a line of intent" "$SUBJECT

.
$body1
.

$SIGNED"
SUBJECT_TWICE=$(sed -n "s/^SUBJECT_TWICE_EXAMPLE='\(.*\)'\$/\1/p" "$hook")
[ -n "$SUBJECT_TWICE" ] || { echo "no SUBJECT_TWICE_EXAMPLE in $hook" >&2; exit 2; }
try refused "two prefixes in the subject" "$SUBJECT_TWICE

$body1

$SIGNED"
git config core.commentString '#'
git config core.commentChar ';'
try refused "the last comment setting wins: a ; line is a comment" "$SUBJECT

; hidden intent

$SIGNED" -e
git config --unset core.commentChar
git config --unset core.commentString
git config core.commentChar ';'
try ok      "another comment character in the template" "$SUBJECT

$body1

$SIGNED" -e
git config --unset core.commentChar
git config core.commentString '//'
try ok      "a comment string in the template, with the diff" "$SUBJECT

$body1

$SIGNED" -v -e
git config --unset core.commentString
try refused "a trailer without the space after the colon" "$SUBJECT

Co-authored-by:V <v@example.com>

$SIGNED"
try refused "a Fixes trailer as intent" "$SUBJECT

Fixes: 1234abcd (the earlier change)

$SIGNED"
try ok      "prose whose first word ends in by" "$SUBJECT

Ruby: the binding now keeps its handle.

$SIGNED"
try refused "fixup! naming no recent commit" "fixup! [core] a subject nobody committed"
try refused "amend! naming no recent commit" "amend! [core] a subject nobody committed

$SUBJECT

$body1

$SIGNED"

# Whitespace and bytes, as they would land with --cleanup=verbatim.
try refused "CRLF line endings" "$(printf '%s\r\n\r\n%s\r\n\r\n%s\r' "$SUBJECT" "$body1" "$SIGNED")" --cleanup=verbatim
try refused "trailing whitespace" "$SUBJECT 

$body1

$SIGNED" --cleanup=verbatim
try refused "a tab" "$SUBJECT

$(printf 'One\tline that says why.')

$SIGNED"

# Boundaries.
sub72=$(printf '%s' "$SUBJECT" | head -c 72); while [ ${#sub72} -lt 72 ]; do sub72="${sub72}x"; done
try ok      "a subject of exactly 72" "$sub72

$body1

$SIGNED"
try refused "a subject of 73" "${sub72}x

$body1

$SIGNED"
line90=$(printf 'x%.0s' $(seq 1 90))
try ok      "a line of intent of exactly 90" "$SUBJECT

$line90

$SIGNED"
try refused "a line of intent of 91" "$SUBJECT

${line90}x

$SIGNED"

# Identities.
export GIT_COMMITTER_NAME=AO55 GIT_COMMITTER_EMAIL=AO55@dravee.dev
try ok      "--author with matching sign-off and committer" "$SUBJECT

$body1

Signed-off-by: AO55 <AO55@dravee.dev>" --author='AO55 <AO55@dravee.dev>'
unset GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL
try refused "--author with a sign-off that names the committer" "$SUBJECT

$body1

$SIGNED" --author='AO55 <AO55@dravee.dev>'
export GIT_COMMITTER_NAME=DraVee GIT_COMMITTER_EMAIL=dravee@example.com
try refused "committer differs from the author" "$SUBJECT

$body1

$SIGNED"
unset GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

# Merges: refused by MERGE_HEAD, whatever the message says.
# Two empty commits with the same message and second would be one object, so they differ.
git branch -q side
git commit -q --allow-empty -m "$SUBJECT

One line on the first branch.

$SIGNED"
git switch -q side
git commit -q --allow-empty -m "$SUBJECT

One line on the second branch.

$SIGNED"
git switch -q -
printf '%s\n\n%s\n\n%s\n' "$SUBJECT" "$body1" "$SIGNED" > msg
if git merge -q --no-ff -F msg side >out 2>&1; then
    echo "FAILED  a merge with a conforming message: expected refused, got ok"
    sed 's/^/        /' out
    failures=$((failures + 1))
else
    echo "ok      a merge with a conforming message"
    git merge --abort 2>/dev/null
fi

git switch -q side
git commit -q --allow-empty -m "$SUBJECT

One more line on the second branch.

$SIGNED"
git switch -q -
git merge -q --no-commit --no-ff side >/dev/null 2>&1
try refused "commit after merge --no-commit" "$SUBJECT

$body1

$SIGNED"
git merge --abort 2>/dev/null

# fixup! and amend! only name commits that were never pushed.
git init -q --bare "$repo.remote"
git remote add origin "$repo.remote"
git push -q origin HEAD:master
try refused "fixup! of a commit already pushed" "fixup! $SUBJECT"
try refused "amend! of a commit already pushed" "amend! $SUBJECT

$SUBJECT

$body1

$SIGNED"
git commit -q --allow-empty -m "$SUBJECT for acme

$body1

$SIGNED"
try ok      "fixup! of a commit not yet pushed" "fixup! $SUBJECT for acme"

# Forbidden words, from the config.
git config --add hook.forbidWord ''
git config --add hook.forbidWord Acme
try refused "a word from hook.forbidWord in the body" "$SUBJECT

One line that names ACME Corporation.

$SIGNED"
try refused "a word from hook.forbidWord in the subject" "$SUBJECT for Acme

$body1

$SIGNED"
try refused "a word from hook.forbidWord inside a fixup of a real target" "fixup! $SUBJECT for acme"
git config --unset-all hook.forbidWord
try ok      "the word again once the config is gone" "$SUBJECT

One line that names ACME Corporation.

$SIGNED"

# --history: commits that slipped past the hook are named. The bad ones are made with the
# hook pointed at nothing, which is what a clone without core.hooksPath looks like.
base=$(git rev-parse HEAD)
git -c core.hooksPath=/nonexistent commit -q --allow-empty -m "fixup! $SUBJECT"
git -c core.hooksPath=/nonexistent commit -q --allow-empty -m "$SUBJECT

$body1"
git -c core.hooksPath=/nonexistent commit -q --allow-empty -m "$SUBJECT

$body1

$SIGNED"
git -c core.hooksPath=/nonexistent merge -q --no-ff -m "merge branch side" side 2>/dev/null
report=$(sh "$here/test-commit-msg.sh" --history "$base..HEAD")
case "$report" in
    *"never folded"*"no Change-Id"*"history: 4 commit(s)"*) ;;
    *) echo "FAILED  --history over a fixup, a missing sign-off and a merge"; printf '%s\n' "$report" | sed 's/^/        /'; failures=$((failures + 1)) ;;
esac
case "$report" in
    *"merge commit"*) echo "ok      --history names the merge, the fixup and the missing sign-off" ;;
    *) echo "FAILED  --history did not name the merge"; failures=$((failures + 1)) ;;
esac
sh "$here/test-commit-msg.sh" --history "nonexistent..HEAD" >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 2 ]; then
    echo "ok      --history on a bad range exits 2"
else
    echo "FAILED  --history on a bad range: expected exit 2, got $rc"; failures=$((failures + 1))
fi

echo "matrix: $failures failure(s)"
[ "$failures" -eq 0 ]
