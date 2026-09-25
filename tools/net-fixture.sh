#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the net family under an emulator against backends on this machine, and takes them down
# again. Two of them, because the contract has two halves:
#
#   - the online session (reden_online_host): reden's reference backend, through
#     tools/net-backend.py, on a free port. OAuth discovery, tokens, config, profiles, blocked.
#   - the game server: redenex, built from its tree, on another free port with a certificate made
#     here, for the TCP connect, the TLS handshake and the status document.
#
# What the probe expects is not written here. The script asks the backend over its own protocol
# (discovery, a token per scope, the three API reads) and writes what it answered into
# sdmc:/edenprobe/net, so an expectation is only ever what the backend published.
#
#   tools/net-fixture.sh                         the emulator make run uses by default
#   EMULATOR=/path/to/eden-cli tools/net-fixture.sh
#   NET_CONFIG_DELAY=6 tools/net-fixture.sh      the backend answers /api/v1/config 6 s late,
#                                                inside the fork's 15 s sign-in budget
#
# It exits non-zero, with one line saying what failed, when a server does not come up, when
# make run fails (an emulator that is not there, a build error) or when this run's output has
# no summary line: a table printed from an earlier run's report would be worse than none.
#
# REDEN and REDENEX point at the two trees (default ~/WorkDir/accord/{reden,redenex}). The
# emulator's settings live in the scratch XDG_CONFIG_HOME make run uses (build/xdg/config), in
# eden-cli's file eden/sdl2-config.ini, group [Services]. The file there before is copied aside
# first thing and put back whatever way the run ends (none there before, none after), and the net
# file and certificate are taken off the scratch memory card, so a later plain make run skips
# instead of failing against servers that are gone.

set -euo pipefail

PROBE=$(cd "$(dirname "$0")/.." && pwd)
REDEN=${REDEN:-$HOME/WorkDir/accord/reden}
REDENEX=${REDENEX:-$HOME/WorkDir/accord/redenex}
WORK=$PROBE/build/net
XDG=$PROBE/build/xdg
SDMC=$XDG/data/eden/sdmc/edenprobe
CONFIG_DIR=$XDG/config/eden
CONFIG=$CONFIG_DIR/sdl2-config.ini
CLIENT_ID=edenprobe
CLIENT_SECRET=edenprobe-$(od -An -N6 -tx1 /dev/urandom | tr -d ' \n')

say() { printf 'net-fixture: %s\n' "$*"; }
die() { printf 'net-fixture: FAILED: %s\n' "$*" >&2; exit 1; }

free_port() {
    python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}

mkdir -p "$WORK/tls" "$SDMC" "$CONFIG_DIR"
rm -f "$WORK"/redenex-*.log "$WORK/redenex.log"

# The settings file as it is now, before anything can fail: cleanup restores exactly this. A copy
# left by an earlier run that was killed is not trusted over the file as it stands.
SAVED=$WORK/sdl2-config.ini.saved
rm -f "$SAVED"
HAD_CONFIG=no
if [ -f "$CONFIG" ]; then
    cp -p "$CONFIG" "$SAVED"
    HAD_CONFIG=yes
fi
WROTE_CONFIG=no

BACKEND_PORT=$(free_port)
SERVER_PORT=$(free_port)
while [ "$SERVER_PORT" = "$BACKEND_PORT" ]; do SERVER_PORT=$(free_port); done

PIDS=()
cleanup() {
    for pid in "${PIDS[@]}"; do
        kill -INT "$pid" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do
        for _ in $(seq 50); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
        kill -TERM "$pid" 2>/dev/null || true
    done
    rm -f "$SDMC/net" "$SDMC/net-server.pem"
    if [ "$HAD_CONFIG" = yes ]; then
        mv -f "$SAVED" "$CONFIG"
        say "servers stopped; emulator settings put back as they were; net file removed"
    elif [ "$WROTE_CONFIG" = yes ]; then
        rm -f "$CONFIG"
        say "servers stopped; the settings file this run wrote removed; net file removed"
    else
        say "servers stopped; emulator settings untouched; net file removed"
    fi
}
trap cleanup EXIT

say "building redenex from $REDENEX"
(cd "$REDENEX" && go build -o "$WORK/redenex" ./cmd/redenex)

# The server's own certificate, made here: self-signed, so no authority vouches for it, and named
# for the name the probe presents.
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 7 \
    -keyout "$WORK/tls/key.pem" -out "$WORK/tls/cert.pem" -subj "/CN=redenex" \
    -addext "subjectAltName=DNS:redenex" 2>/dev/null

say "online backend on 127.0.0.1:$BACKEND_PORT, log $WORK/backend.log"
python3 -u "$PROBE/tools/net-backend.py" --backend "$REDEN/docs/examples/online_backend.py" \
    --port "$BACKEND_PORT" --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" \
    --config-delay "${NET_CONFIG_DELAY:-0}" --game-server-cert "$WORK/tls/cert.pem" \
    >"$WORK/backend.log" 2>&1 &
BACKEND_PID=$!
PIDS+=("$BACKEND_PID")

# redenex shares its PostgreSQL on 45432 with any other redenex on this machine, and one that
# starts while another is bringing that database up can lose the race and exit. Three tries,
# each from its own directory, since the protocol library writes a log/ wherever it starts.
cd "$WORK"
for attempt in 1 2 3; do
    say "redenex on 127.0.0.1:$SERVER_PORT, attempt $attempt, log $WORK/redenex-$attempt.log"
    "$WORK/redenex" -address 127.0.0.1 -port "$SERVER_PORT" -cert "$WORK/tls/cert.pem" \
        -key "$WORK/tls/key.pem" -nat-check=false >"$WORK/redenex-$attempt.log" 2>&1 &
    REDENEX_PID=$!
    PIDS+=("$REDENEX_PID")
    up=no
    for _ in $(seq 120); do
        if curl -skf -o /dev/null "https://127.0.0.1:$SERVER_PORT/service_status_hac.json"; then
            up=yes
            break
        fi
        kill -0 "$REDENEX_PID" 2>/dev/null || break
        sleep 0.5
    done
    [ "$up" = yes ] && break
    kill -INT "$REDENEX_PID" 2>/dev/null || true
    [ "$attempt" = 3 ] || sleep 3
done
[ "$up" = yes ] || die "redenex did not come up in three tries; see $WORK/redenex-{1,2,3}.log"

for _ in $(seq 60); do
    curl -sf -o /dev/null "http://127.0.0.1:$BACKEND_PORT/.well-known/openid-configuration" &&
        break
    kill -0 "$BACKEND_PID" 2>/dev/null || die "the online backend exited; see $WORK/backend.log"
    sleep 0.5
done
curl -sf -o /dev/null "http://127.0.0.1:$BACKEND_PORT/.well-known/openid-configuration" ||
    die "the online backend never answered; see $WORK/backend.log"

say "asking the backend what it publishes"
cp "$WORK/tls/cert.pem" "$SDMC/net-server.pem"
python3 - "$BACKEND_PORT" "$CLIENT_ID" "$CLIENT_SECRET" "$SERVER_PORT" >"$SDMC/net" <<'PY'
import base64, json, socket, sys, urllib.parse, urllib.request

port, client_id, secret, server_port = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
origin = f"http://127.0.0.1:{port}"

def get(url, token=None):
    request = urllib.request.Request(url)
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)

# RFC 8414 discovery, then the client credentials grant per scope (RFC 6749 section 4.4), with
# each half form-encoded before it is joined, as the contract says.
endpoint = get(f"{origin}/.well-known/openid-configuration")["token_endpoint"]
basic = base64.b64encode(
    f"{urllib.parse.quote_plus(client_id)}:{urllib.parse.quote_plus(secret)}".encode()).decode()

def token(scope):
    request = urllib.request.Request(
        endpoint, data=urllib.parse.urlencode(
            {"grant_type": "client_credentials", "scope": scope}).encode(),
        headers={"Authorization": f"Basic {basic}",
                 "Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)["access_token"]

config = get(f"{origin}/api/v1/config", token("config"))
account = config["account_id"].lower()
profiles = get(f"{origin}/api/v1/profiles?ids={account}", token("profile"))
blocked = get(f"{origin}/api/v1/blocked", token("friends"))

print("# written by tools/net-fixture.sh from what the backend answered")
print(f"session {origin}")
print(f"account {account}")
for rewrite in config.get("rewrites", []):
    address = socket.getaddrinfo(rewrite["destination"], None, socket.AF_INET)[0][4][0]
    print(f"rewrite {rewrite['source']} {rewrite['destination']} {address}")
for one in blocked:
    print(f"blocked {one.lower()}")
for profile in profiles:
    url = profile.get("avatar_url") or "-"
    print(f"profile {profile['account_id'].lower()} {url} {profile.get('username', '')}")
print(f"server 127.0.0.1 {server_port}")
print("server-name redenex")
print("server-cert net-server.pem")
PY
sed 's/^/  net: /' "$SDMC/net"

# The emulator's side: the three settings, and nothing else, in the file eden-cli reads. They
# are Category::Network settings, which the config file keeps in its [Services] group
# (frontend_common/config.cpp, ReadNetworkValues).
WROTE_CONFIG=yes
cat >"$CONFIG" <<INI
[Services]
reden_online_host\\default=false
reden_online_host=http://127.0.0.1:$BACKEND_PORT
reden_online_client_id\\default=false
reden_online_client_id=$CLIENT_ID
reden_online_client_secret\\default=false
reden_online_client_secret=$CLIENT_SECRET
INI

# The old report goes first, and the run has to say it finished: its exit status, and the summary
# line the program prints on the debug channel at the end of this run.
REPORT=$SDMC/report.jsonl
rm -f "$REPORT"
set +e
make -C "$PROBE" --no-print-directory run MODE="net quick" ${EMULATOR:+EMULATOR="$EMULATOR"} \
    >"$WORK/run.log" 2>&1
status=$?
set -e
{ grep -aE 'EDENPROBE DONE|^mode file|make run:| error: |\*\*\* ' "$WORK/run.log" || true; } |
    sed 's/.*\(EDENPROBE DONE\)/\1/' | head -20
[ "$status" = 0 ] || die "make run exited $status; see $WORK/run.log"
grep -aq 'EDENPROBE DONE' "$WORK/run.log" ||
    die "this run printed no EDENPROBE DONE line; see $WORK/run.log"
[ -s "$REPORT" ] || die "this run left no report at $REPORT"
python3 - "$REPORT" <<'PY'
import collections, json, sys
counts = collections.OrderedDict()
failed = []
for line in open(sys.argv[1], encoding="utf-8"):
    item = json.loads(line)
    if item.get("type") != "case" or item["suite"] not in (
            "nifm", "dns", "bsd", "ssl", "acc", "friend"):
        continue
    suite = counts.setdefault(item["suite"], collections.Counter())
    suite[item["result"]] += 1
    if item["result"] == "fail":
        failed.append(f"  {item['suite']}: {item['name']}")
print("net-fixture: suite    pass fail skip")
for suite, count in counts.items():
    print(f"net-fixture: {suite:8} {count['pass']:4} {count['fail']:4} {count['skip']:4}")
if failed:
    print("net-fixture: failing:")
    print("\n".join(failed))
PY
say "report: $REPORT"
