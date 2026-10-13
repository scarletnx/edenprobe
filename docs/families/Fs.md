# The fs family

The fs family reads the memory card through FsFile, as a game reads its files: one IFile::Read
with an offset each time, which is the command an emulator serves and where it does its seek.

- **fs**, fifteen cases. A 64 MiB file and 256 small ones are written on the first pass; then
  sequential reads of 512 bytes, 4 KiB, 64 KiB, 1 MiB and 16 MiB, 4096 random 4 KiB reads at
  aligned and at odd offsets, 64 KiB reads backwards, 4096 reads of the same 4 KiB, and the small
  files opened, read and closed. Every byte is checked against the pattern, so a fast wrong read
  fails; the times are the point. Seek costs show as the difference between the patterns. Three
  cases pin the edges: a read past the end returns only what is there, one at the end returns no
  bytes, one of no bytes returns none. The host's page cache stays warm after the first pass:
  nothing here empties it, since that takes root. On the fork on RADV (25 September) a read costs
  about 8.5 us whatever its place, so the service path, not the host's seek, is where time goes.
