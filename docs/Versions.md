# Versions

`VERSION` holds the version (semantic), and a tag `v<version>` on `main` marks each. Every report opens with a note
naming it and the build as git describes the tree, `edenprobe 0.7.0 (f99d5d2) starting` (`-dirty` when uncommitted
changes were built), and goes in a folder named after that commit
([Where a run leaves its report](Report.md#where-a-run-leaves-its-report)). Compare two reports only when that line
agrees, or when the difference between the builds is what is measured. A new suite or case is a minor version; a
changed verdict of an existing case at least that.

The cover a loader shows is `icon.jpg`, made from `static/icon.svg` by `make icon` and committed, so a build needs
neither drawing tool: Wimsy as edenprobe, in Eden's sunset colours with a test probe, over the banded sun, with the
program's name. wimsy-dist's `tools/make_edenprobe.py` makes `static/icon.svg`.
