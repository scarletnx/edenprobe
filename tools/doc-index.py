#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Puts an "## Index" of a Markdown document's sections right under its title, as ScarletNX's docs have one.

    tools/doc-index.py [--check] [--min N] FILE...

The index lists the ## and ### headings (### nested under their ##), outside fenced code, each a link to the anchor
GitHub gives it: lowercase, punctuation but hyphens dropped, each space a hyphen, a repeated one numbered. An index
already there is replaced, so a second run changes nothing. A document with fewer than N such sections (default 3)
gets none, and loses one it had.

--check writes nothing: it names each file whose index is missing or out of date and exits 1.
"""

import argparse
import re
import sys

HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")
LINK = re.compile(r"^\s*[*-] \[.*\]\(#[^)]*\)\s*$")


def headings(lines):
    """(line number, level, text) of each heading outside fenced code."""
    out, fenced = [], False
    for i, line in enumerate(lines):
        if FENCE.match(line):
            fenced = not fenced
            continue
        m = None if fenced else HEADING.match(line)
        if m:
            out.append((i, len(m.group(1)), m.group(2)))
    return out


def anchor(text, seen):
    """GitHub's: a link's words, markup dropped, lowercase, word characters and hyphens kept, spaces as hyphens."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    slug = re.sub(r"[^\w\- ]", "", re.sub(r"[`*_]", "", text).lower()).replace(" ", "-")
    n = seen.get(slug, 0)
    seen[slug] = n + 1
    return slug if n == 0 else f"{slug}-{n}"


def indexed(text, minimum):
    lines = text.split("\n")
    heads = headings(lines)
    # an index already there goes: its heading, its links and the blank lines around them. A section named index
    # whose text opens with anything but a link is a section, not the index.
    for i, level, name in heads:
        if level == 2 and name.strip().lower() == "index":
            first = next((line for line in lines[i + 1 :] if line.strip()), "")
            if not LINK.match(first):
                continue
            end = i + 1
            while end < len(lines) and (not lines[end].strip() or LINK.match(lines[end])):
                end += 1
            del lines[i:end]
            return indexed("\n".join(lines), minimum)
    title = next((i for i, level, _ in heads if level == 1), None)
    listed = [(i, level, name) for i, level, name in heads if level in (2, 3) and (title is None or i > title)]
    if title is None or len(listed) < minimum:
        return "\n".join(lines)
    # anchors in document order, the index's own heading right after the title's
    seen, links = {}, []
    for i, level, name in heads:
        a = anchor(name, seen)
        if i == title:
            anchor("Index", seen)
        elif (i, level, name) in listed:
            links.append(("  " if level == 3 else "") + f"* [{name}](#{a})")
    rest = lines[title + 1 :]
    while rest and not rest[0].strip():
        rest = rest[1:]
    return "\n".join(lines[: title + 1] + ["", "## Index", ""] + links + [""] + rest)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--check", action="store_true", help="write nothing; exit 1 when an index is missing or stale")
    p.add_argument("--min", type=int, default=3, help="sections a document needs for an index (default 3)")
    p.add_argument("files", nargs="+")
    args = p.parse_args()
    stale = []
    for path in args.files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        new = indexed(text, args.min)
        if new != text:
            stale.append(path)
            if not args.check:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(new)
    for path in stale:
        print(f"{path}: index {'out of date' if args.check else 'written'}")
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    sys.exit(main())
