# What it reports

One JSON object per line, to two channels at once, because the two machines read different ones:

- the debug string service, which an emulator logs with no setup and hardware ignores unless a
  debugger is attached;
- a file on the memory card, which is what a console run leaves behind.

A case line carries its tier next to the result:

```json
{"type":"case","suite":"clear","name":"clear to opaque red","tier":"exact","result":"pass","detail":""}
```

The tier is on the line because a pass means nothing on its own: two machines agreeing is worth
something only if both were held to the same tolerance, and a reader diffing two reports cannot
otherwise know which one a case used. The values are `exact`, `ulp`, `position` and `level`, and one
that is not a tier at all, `none`: any line, in any suite, that reports no comparison of something
the hardware produced. That covers the two suites that judge this program's own arithmetic, every
case that reports whether a precondition held (from the device coming up to a texture reaching the
graphics unit), and any drawing case stopped before it could compare anything, which still gives the
reason in its detail. Calling any of those `exact` would read, in a diff, as a passing claim about a
machine that was never asked anything.

A case that could not ask its question at all says so with the result `skip`, the none tier, a
`reason` of one word and a sentence in `detail`, and is counted apart from passes and failures,
since a machine that was never asked has agreed with nothing:

```json
{"type":"case","suite":"acc","name":"the id token refresh completes and succeeds","tier":"none","result":"skip","reason":"not configured","detail":"needs an online session: ..."}
{"type":"summary","passed":424,"failed":9,"skipped":27,"crashed":2}
```

| reason | why the case did not ask |
|---|---|
| `crashed` | the program ended inside it on this machine: in this run, which the journal says, or in an earlier one, which the crash list says |
| `may crash` | it is in the may-crash table, and neither the word `fatal` nor the start screen asked for it: today the SM4 case, the CNTVCT_EL0 and DAIF reads, the two geometry shader cases and the three bcn array cases |
| `hangs` | it is in the hang table, and the mode word `hang` did not ask: today the bsd receive that asks for a timeout and does not come back. `retry` does not run it |
| `not configured` | the net file does not name what it needs: a session, a server, a rewrite, an account, a certificate |
| `unavailable` | a service or a resource this program cannot have here |
| `depends` | a case before it did not leave what it needs: it did not run, failed, or gave nothing to ask about |

The summary counts crashes on their own: `skipped` is every skip but the ones whose reason is
`crashed`, which `crashed` counts, so the four numbers add up to the case lines. The runner's
`EDENPROBE DONE` line on the debug channel carries the same four.

A run that dies halfway still leaves everything up to its last finished case, and the summary
carries the counts so a runner does not have to parse the rest. The summary is the last line of
every run but one: a detailed run nobody attended adds one note after it (see [Looking at it on the
console](Console.md)).

## Where a run leaves its report

Every build reports in a folder of its own, so runs of several builds, or of one build under
several emulators, sit side by side instead of each overwriting the last:

```
sdmc:/edenprobe/
  mode              the mode file: a person's, read by every build
  net               what the net family is pointed at
  crashes           the crash list: this machine's, shared by every build
  last-run          the folder the latest launch reported in, e.g. runs/5fd6ec1_phone
  fs/               the fs family's files, written once
  runs/
    5fd6ec1/        a build, by the hash of its commit
      report.jsonl
      images/       the pictures of the failing cases
      journal       where the run is, for the launch after a crash
    5fd6ec1_phone/  the same build with label=phone in the mode file
    9e1c2a4-dirty/  a build of a tree with uncommitted changes
```

The folder is the commit the build came from, as `git describe --always --dirty --exclude='*'`
gives it: seven hex digits, `-dirty` when the tree had uncommitted changes, `unknown` when it was
not built from git. The report's first note names the same build, and the second the folder.
`label=<name>` in the mode file adds `_<name>`, for one build tested under two emulators that share
a card: `label=eden-master` for one and `label=pr4477` for the other. The name runs to the next
space or line end and is 1 to 32 letters, digits, dots, dashes and underscores, the first not a
dot; anything else is refused, quoted in the mode line, and the run goes in the folder with no
label.

The journal is per folder, so the run a build left in the middle stays where it stopped while
another build runs, and goes on when that build is started again. The label is not one of the words
a launch compares before going on with a run: another label is another folder, with its own
journal. The crash list stays the machine's, shared by every build, because a case that ended this
emulator's program will end it again whichever build asks. `last-run` is rewritten by every launch
before the report is opened, which is how `make run`, and a person with a file manager, find the
report without knowing the hash.
