# What a failing case leaves behind

A verdict says how many pixels were wrong and what the first one held, not that the wrong ones were
the right shape in the wrong place, or one block in sixteen, or every other column. So a drawing
case that fails writes what it saw next to the report and names the files on its own line (one line,
wrapped here to fit):

```json
{"type":"case","suite":"intmath","name":"bitwise and","tier":"exact","result":"fail",
 "detail":"4095 of 4096 pixels wrong, first at (1,0) is 0x00000000, expected 0x00106000",
 "image":"images/intmath/bitwise-and.pam","expected":"images/intmath/bitwise-and-expected.pam",
 "diff":"images/intmath/bitwise-and-diff.pam","raw":"images/intmath/bitwise-and.bin"}
```

Each field is a path relative to the directory of `report.jsonl`, present only when that file was
written. The files land under `images/<suite>/`, named after the case with letters lower cased and
every run of other characters turned into one dash: "the fragment coordinate's x is the pixel
centre, bit for bit" becomes `the-fragment-coordinate-s-x-is-the-pixel-centre-bit-for-bit.pam`. Two
case names that spell one file name get a file each: the second gets `-2` before the extension,
giving up characters off the end of the name if it must. A name is held together with the two its
pictures are spelled from, so a case called "bitwise and expected" cannot land on the expectation of
"bitwise and". After ninety eight numbers the case gets no picture and its line none of the four
fields, because one failure quietly overwriting another's evidence is worse than a missing picture;
a directory too long to hold whole is likewise refused rather than cut short.

These are not stored reference images and must never become one (see [Why not compare
screenshots](Design.md#why-not-compare-screenshots)). Nothing compares them to anything and no case
reads one: the readback is judged, then the very bytes the judge looked at are written. Deleting
every image changes no verdict.

**The formats.** All three pictures are binary PAM, the netpbm format that carries four channels, so
a colour target's alpha is in the picture, not in a second file that can go missing. `magick`,
`identify` and the netpbm tools read them; a PNG is one `magick a.pam a.png` away.

| file | what it is |
|---|---|
| `<case>.pam` | what the machine drew, four channels, MAXVAL 255, labelled `drawn` on screen |
| `<case>-expected.pam` | what the processor worked out for that same pixel |
| `<case>-diff.pam` | three channels: the judge's verdict per pixel, labelled `judged` on screen |
| `<case>.bin` | the target's bytes as they were read back, for the formats an image cannot hold |

A pixel's four bytes go into the image in the order the readback handed them over. For a colour
target that is red, green, blue and alpha, and the picture is the picture. For 32 bit integers or
floats it is the little end first, a picture of bit patterns rather than of magnitudes: equal values
look equal and one changed bit changes a colour, while a float range has no honest mapping onto a
byte. Those formats also get their bytes verbatim in a `.bin`, for `cmp` between two machines.

**The diff is the judge's verdict, not a comparison of the two images**, and that is what makes it
safe. Dark grey means the judge accepted the pixel, red that it refused it, blue that it declined to
judge it (the band an edge runs through). Magenta means no compare loop ever looked at the pixel:
every pixel starts magenta, so a loop that skipped one cannot pass for one that meant to leave it
alone, and a run that shows magenta has a bug in this program, not in the machine. A pixel within a
budget of units in the last place differs in its bytes and is not a difference here; an image
comparison would paint the whole float suite red on a machine that did nothing wrong.

By default only a failing case writes anything; the word `images` in [the mode file](ModeFile.md)
asks for every drawing case. The `support` suite writes a few eight by four fixtures into
`sdmc:/edenprobe/support-check/` while it runs, parses them back and takes them away again, and a
run that leaves that directory behind has failed that case and says so.
