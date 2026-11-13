#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/uam.sh DIR: uam as devkitPro's toolchain ships it (1.1.0, the commit below) with tools/uam/*.patch on it,
# built into DIR/uam. The toolchain's uam puts a multisample fetch's sample in the register the console reads the
# handle from (tools/uam/0001-*.patch); the shaders that make one are built by this one (Makefile, UAM_FIXED).
#
# The first run clones the source, which needs the network, and it needs git, meson, ninja, bison, flex, Python 3 with
# mako, and a C and a C++ compiler for this machine. A run that finds DIR/uam built from this commit, these patches and
# this script only marks it current, so make does not ask again.
set -eu
[ $# -eq 1 ] || { echo "usage: tools/uam.sh DIR" >&2; exit 2; }
dir=$1
repo=https://github.com/devkitPro/uam.git
commit=5a5afc2bae8b55409ab36ba45be63fcb73f68993
here=$(cd "$(dirname "$0")" && pwd)
stamp="$commit $(cat "$here/uam.sh" "$here"/uam/*.patch | sha256sum | cut -c1-16)"
if [ -x "$dir/uam" ] && [ "$(cat "$dir/uam.stamp" 2>/dev/null)" = "$stamp" ]; then
    touch "$dir/uam"
    exit 0
fi
mkdir -p "$dir"
src=$dir/src
[ -d "$src/.git" ] || git clone --quiet "$repo" "$src"
git -C "$src" checkout --quiet --force --detach "$commit"
for patch in "$here"/uam/*.patch; do
    git -C "$src" apply "$patch"
done
build=$dir/build-$(echo "$stamp" | cut -d' ' -f2)
[ -d "$build" ] || meson setup "$build" "$src" --buildtype=release
ninja -C "$build"
cp "$build/uam" "$dir/uam"
echo "$stamp" > "$dir/uam.stamp"
