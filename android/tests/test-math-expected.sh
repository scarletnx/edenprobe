#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tests/test-math-expected.sh: src/main/cpp/math_expected.h is what tools/math-expected.py prints, byte for byte.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
if python3 -I "$here/tools/math-expected.py" | cmp -s - "$here/src/main/cpp/math_expected.h"; then
    echo "test-math-expected: the header is the script's"
else
    echo "test-math-expected: FAIL: src/main/cpp/math_expected.h differs from tools/math-expected.py's output"
    exit 1
fi
