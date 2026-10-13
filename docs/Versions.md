# Versions

The version is the one line of `VERSION`, semantic (`0.1.0` was the first), and a tag `v<version>`
on `main` marks each. Every report opens with a note naming it and the build, as git describes the
tree: `edenprobe 0.1.0 (dd94263) starting`, or `-dirty` when uncommitted changes were built, and the
report sits in a folder named after that commit (see [Where a run leaves its
report](Report.md#where-a-run-leaves-its-report)). Two reports are worth diffing only when that line
agrees, or when the difference between the builds is what is being measured. The version moves when
what a case asks or how it judges changes: a new suite or case is a minor step, a changed verdict
for an existing case at least that, and the counts in these pages move with it.

The cover a loader shows is `icon.jpg`, drawn from `static/icon.svg` (the reden mark, the Eden
sunset without the palm, on the site's dark ground) by `make icon`; the JPEG is committed so a build
needs neither drawing tool.
