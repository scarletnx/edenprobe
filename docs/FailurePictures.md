# Failure pictures

A drawing case that fails writes what it saw beside the report and names the files on its line (one line, wrapped
here):

```json
{"type":"case","suite":"intmath","name":"bitwise and","tier":"exact","result":"fail",
 "detail":"4095 of 4096 pixels wrong, first at (1,0) is 0x00000000, expected 0x00106000",
 "image":"images/intmath/bitwise-and.pam","expected":"images/intmath/bitwise-and-expected.pam",
 "diff":"images/intmath/bitwise-and-diff.pam","raw":"images/intmath/bitwise-and.bin"}
```

Each field is a path relative to `report.jsonl`'s folder, present only when the file was written. The files go in
`images/<suite>/`, named after the case in lower case with every run of other characters one dash. A name two cases
would share gets `-2` before the extension, shortened if it must; past ninety eight, the case gets no picture and
its line none of the fields, and a folder too long to hold whole is refused rather than cut.

The pictures are never references: nothing reads them, and deleting them changes no verdict
([Design](Design.md#no-screenshots-compared)). The word `images` in [the mode file](ModeFile.md) writes them for
passing cases too.

| file | what it is |
|---|---|
| `<case>.pam` | what the machine drew, four channels, MAXVAL 255 (`drawn` on screen) |
| `<case>-expected.pam` | what the processor worked out for each pixel |
| `<case>-diff.pam` | the judge's verdict per pixel, three channels (`judged` on screen) |
| `<case>.bin` | the target's bytes as read back, for the formats an image cannot hold |

PAM keeps alpha in the picture; `magick a.pam a.png` makes a PNG. A pixel's four bytes go in as the readback gave
them: for a colour target red, green, blue and alpha; for 32-bit integers or floats the low byte first, a picture of
bit patterns, with the bytes also in the `.bin` for `cmp`.

The diff is the judge's verdict, not a comparison of two images: dark grey accepted, red refused, blue not judged
(the band an edge runs through). Magenta is a pixel no comparison looked at: every pixel starts magenta, and any left
is a bug in this program. A pixel within its budget of units in the last place is not a difference here.

The support suite writes a few 8x4 fixtures to `sdmc:/edenprobe/support-check/` while it runs and takes them away;
a run that leaves the folder has failed that case.
