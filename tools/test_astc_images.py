# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""source/astcimages.c is laid out by tools/astc-images.py's writer, for the picture it makes today.

astcenc is not needed: the blocks and hashes are read back from the C file and written out again
with the tool's own writer, which must give the file byte for byte. The blocks' bytes themselves
are held by the reference suite, which decodes them to the hashes astcenc's decode had."""

import importlib.util
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("astc_images", os.path.join(HERE, "astc-images.py"))
astc_images = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(astc_images)

PATH = os.path.join(HERE, "..", "source", "astcimages.c")


def parse():
    text = open(PATH).read()
    source_hash = int(re.search(r"ASTC_IMAGE_SOURCE_HASH = 0x([0-9a-f]{8})u;", text).group(1), 16)
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m.group(2)))
              for m in re.finditer(r"static const uint8_t BLOCKS_(\w+)\[\d+\] = \{([^}]*)\};", text)}
    rows = re.findall(r'\{ "(\w+)", (\d+), (\d+), (true|false), BLOCKS_(\w+), (\d+), 0x([0-9a-f]{8})u, '
                      r'0x([0-9a-f]{8})u \}', text)
    return text, source_hash, arrays, rows


class Images(unittest.TestCase):
    def setUp(self):
        self.text, self.source_hash, self.arrays, self.rows = parse()

    def test_the_picture_is_the_one_encoded(self):
        self.assertEqual(astc_images.fnv1a(astc_images.picture()), self.source_hash)

    def test_the_table_is_the_tools_list(self):
        self.assertEqual([(r[0], int(r[1]), int(r[2]), r[3] == "true") for r in self.rows],
                         list(astc_images.IMAGES))

    def test_every_image_has_its_blocks(self):
        for name, bw, bh, _, array, count, _, _ in self.rows:
            want = -(-astc_images.SIZE // int(bw)) * -(-astc_images.SIZE // int(bh))
            self.assertEqual(int(count), want, name)
            self.assertEqual(len(self.arrays[array]), 16 * want, name)

    def test_an_srgb_image_has_one_hash(self):
        for name, _, _, srgb, _, _, h, h8 in self.rows:
            if srgb == "true":
                self.assertEqual(h, h8, name)

    def test_the_file_is_the_writers_output(self):
        encoded = [(self.arrays[r[4]], int(r[6], 16), int(r[7], 16)) for r in self.rows]
        self.assertEqual(astc_images.c_file(self.source_hash, encoded), self.text)


if __name__ == "__main__":
    unittest.main()
