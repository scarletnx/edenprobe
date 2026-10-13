# The net family

The net family asks the services a program reaches the network through, the way a game asks them,
and judges each answer against its protocol or against what `sdmc:/edenprobe/net` says the backend
publishes (see [The net file](#the-net-file) below). Every line says what was expected and what came
back. A case that needs a backend the file does not name, or a service this program cannot open, is
`skip`, with the reason. The tier is exact wherever a value is compared and none where a line only
says a service opened. Nothing waits without a budget: every connect is non-blocking and polled for
three seconds, every read is polled first, and every handshake, write and read over ssl runs
non-blocking against five or three seconds, so a server that drops the attempt or accepts and never
speaks costs a failed case, never the run.

- **nifm**, seven cases. The service opens; a request nobody submitted says so; and, when the file
  names a backend, the connection is reported connected, the machine has an address of its own, a
  submitted request is accepted without being asked for its result and again when it is, and while
  one stands the service says an internet request is accepted.
- **dns**, eleven cases, sfdnsres and nsd. A dotted address resolves to itself through both of
  sfdnsres's serialisations, read by this program's own parsers; a name under `.invalid` does not
  resolve (RFC 6761); nsd names its environment, replaces the `%` placeholder with it, and leaves a
  plain name alone. With a session, once it answers: nsd hands back the destination the backend
  publishes for a name, sfdnsres resolves that name to the destination's address as a hostent, as
  addrinfo and through the C library, and a name with the placeholder, and the name nsd expanded,
  both go to loopback (the host rewriting rules in reden's `docs/OnlineService.md`). The fork signs
  in in the background when the program loads, so the suite asks nsd until it gives the session's
  answer, for at most the fork's fifteen second sign-in budget, and a note says how long it took.
- **bsd**, eight cases. Two datagram sockets on loopback send a datagram one way and back, whole
  both ways and each time from the socket that sent it; an empty socket has nothing to read; a
  stream to a listener the suite opens carries bytes both ways; a port nothing listens on refuses; a
  receive timeout set with `SO_RCVTIMEO` gives up with `EAGAIN` in its time (bounded by a datagram
  another thread sends two seconds in, so a machine that ignores the option fails the case rather
  than hanging). With a server: a stream connects to it, and a connect that does not block
  completes, polls writable and reports no pending error, as a game does.
- **ssl**, sixteen cases. A context; a new connection's default verification (the peer and the name,
  as libnx documents); the name and an option set on a connection reading back (SetOption carries
  the flag before the option, as the console lays raw input out by alignment); a connection taking a
  socket and blocking mode, and saying so; a descriptor handed back by default and none with
  `DoNotCloseSocket` (libnx's `ssl.h` on `sslConnectionSetSocketDescriptor`). With a server whose
  certificate the file names: a verifying handshake refuses a self-signed certificate, succeeds once
  it is imported, and hands back exactly its bytes; a handshake that does not verify succeeds,
  carries a request out whole and reads back a 200 with a status document that says nothing is
  wrong; and the connection keeps its socket after the program closes its own descriptor, since the
  service works on a duplicate. With a session: a verifying handshake under the name the backend
  rewrites, and under the address the service sends a game to, succeeds with nothing imported,
  because the backend publishes the certificate that server presents (`game_server` in reden's
  `docs/OnlineService.md`; the fixture publishes its own).
- **acc**, six cases. acc:u0 and a user, the network account manager, and with a session: the
  account id is the one the backend publishes, the id token refresh completes, succeeds and leaves a
  token in the cache, the token is a bearer token (RFC 6750's alphabet) terminated inside its
  buffer, and the cache hands back the same token again.
- **friend**, five cases. The service opens, and with a session: the blocked user list is the
  backend's, a profile the backend knows comes back valid with its name and picture, one it does not
  comes back invalid in its own slot with the id that was asked, and a picture url comes back with
  every `{size}` replaced by the size asked for.

When a session is named and never answers the sign-in wait, every session case in dns, acc and
friend says so at the end of its detail, so a failure there reads as a sign-in that did not happen.

Some expectations rest on documentation and reasoning, not yet on a console run, which would settle
them: that an unknown profile's slot carries the id that was asked (the contract says only that it
comes back invalid; the id is the fork's `profile_query.cpp`); that the ssl connection keeps its
socket when the program closes its own descriptor (POSIX `dup` semantics); that a submitted nifm
request is accepted by itself within three seconds (the three seconds are this program's); the
descriptor `sslConnectionSetSocketDescriptor` hands back (libnx's reading of it, which switchbrew
shares: the one passed in); and nsd expanding the placeholder with no session (the contract's
statement about the real service). Each case's detail says the same.

## The net file

The net family needs to know what the machine's backend promises, and a file on the memory card says
it: `sdmc:/edenprobe/net`, one `key value` per line, `#` to the end of a line a comment, and a line
it does not understand counted and named in the report and otherwise ignored.

| line | what it promises |
|---|---|
| `session <origin>` | an online session in the sense of reden's `docs/OnlineService.md` is configured against this backend, so the cases that ask one run |
| `account <16 hex digits>` | the account id its `/api/v1/config` publishes |
| `rewrite <source> <destination> <a.b.c.d>` | a rewrite it publishes, and the address the destination has |
| `blocked <16 hex digits>` | one entry of its `/api/v1/blocked` |
| `profile <16 hex digits> <url or -> <name>` | one entry of its `/api/v1/profiles`, the name running to the end of the line |
| `server <a.b.c.d> <port>` | a game server reachable over TCP and TLS; an address, not a name, so a server known by name is resolved by hand first |
| `server-name <name>` | the name to present to it, which its certificate has to carry as a subject alternative name for the verifying cases to pass |
| `server-cert <path>` | the certificate it presents, PEM, relative to `sdmc:/edenprobe` |

With no file, every case that needs a backend is skipped and the rest still run: loopback sockets, a
dotted address resolving to itself, a connection's defaults. Without a `session` line nothing asks
acc or friend for anything that reaches a server, which on a console would be the platform's own. A
console run against a deployed game server names `server`, `server-name` and, when no authority
signed the certificate, `server-cert`; the same file runs there and here. By hand, for a server at
203.0.113.7 whose certificate names `game.example.org`, copied to the card as
`sdmc:/edenprobe/game.pem`:

```
# sdmc:/edenprobe/net
server 203.0.113.7 443
server-name game.example.org
server-cert game.pem
```

And every key, as the fixture below writes it for a backend here (its ports are picked free at run
time; the session lines only make sense against an emulator configured for that backend):

```
session http://127.0.0.1:41234
account bb00000100000002
rewrite game.edenprobe.invalid localhost 127.0.0.1
blocked bb00000100000009
blocked bb0000010000000a
profile bb00000100000002 http://127.0.0.1:41234/avatar/{size}.png edenprobe
server 127.0.0.1 8443
server-name redenex
server-cert net-server.pem
```

## The fixture

**Here**, `tools/net-fixture.sh` does all of it and takes it down again:

```sh
EMULATOR=/path/to/eden-cli tools/net-fixture.sh
```

It builds redenex from `REDENEX` (default `~/WorkDir/accord/redenex`) and starts it on a free port
with a certificate made for the name `redenex` (as a subject alternative name). It starts reden's
reference online backend from `REDEN` through `tools/net-backend.py` on another free port, with one
rewrite, a profile and two blocked ids to publish, and asks it over its own protocol (discovery, a
token per scope, the three reads) what it publishes, writing the answers into the net file: an
expectation is only ever what the backend said. It puts the three `reden_online_*` settings in the
scratch `XDG_CONFIG_HOME` that `make run` uses (eden-cli's `eden/sdl2-config.ini`, group
`[Services]`), runs `make run MODE="net quick"` (which starts the emulator again after a crash),
prints passes, failures and skips per suite, and
stops both servers. The settings file is copied aside first and put back however the run ends, and
the net file and the certificate come off the scratch memory card, so a later plain `make run` skips
rather than fails against servers that are gone. redenex keeps its database on port 45432 (under
`~/.local/share/redenex`) and adopts one already running there; one that loses the race with another
redenex bringing that database up is started again, three tries in all, each with its own log
(`build/net/redenex-1.log` and on). Building it writes Go's caches under the home directory, as any
`go build` does.

It fails loudly rather than print a table: a server that does not come up, `make run` exiting
non-zero (no emulator there, a build error, a run with no summary), or output with no
`EDENPROBE DONE` line ends it with one line saying which and a non-zero exit, and the previous
report is deleted before the run, so nothing from an earlier run can pass for this one's.
`NET_CONFIG_DELAY=6` makes the backend hold `/api/v1/config` for six seconds, which is how the
sign-in wait is tested.
