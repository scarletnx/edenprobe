#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: MIT

"""The online backend the net fixture signs the emulator in to.

It is reden's own reference backend (docs/examples/online_backend.py, the contract in
docs/OnlineService.md made executable), loaded from the reden tree at run time and given a
little more to publish than it ships with: one rewrite, a profile for its account and a blocked
list that is not empty, so every case of the net family has something to expect. Nothing of that
file is copied here; it is imported, and only its tables are replaced.
"""

import argparse
import importlib.util
import sys
import time

# Nothing is left in the reden tree this is loaded from.
sys.dont_write_bytecode = True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--backend", required=True, help="path to reden's online_backend.py")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--client-id", required=True)
    parser.add_argument("--client-secret", required=True)
    parser.add_argument("--config-delay", type=float, default=0.0,
                        help="seconds to hold every /api/v1/config answer, to test a slow sign-in")
    args = parser.parse_args()

    spec = importlib.util.spec_from_file_location("online_backend", args.backend)
    backend = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(backend)

    account = backend.ACCOUNT_ID.lower()
    backend.CLIENT_ID = args.client_id
    backend.CLIENT_SECRET = args.client_secret
    # A name no resolver anywhere answers (RFC 6761 puts .invalid out of reach), rewritten onto one
    # every machine does: only the rewrite can make it resolve.
    backend.REWRITES.clear()
    backend.REWRITES["game.edenprobe.invalid"] = "localhost"
    backend.PROFILES.clear()
    backend.PROFILES[account] = {
        "username": "edenprobe",
        "avatar_url": f"http://127.0.0.1:{args.port}/avatar/{{size}}.png",
    }
    backend.BLOCKED[:] = ["bb00000100000009", "bb0000010000000a"]

    if args.config_delay > 0:
        answer = backend.Handler.do_GET

        def slow_config(handler):
            if handler.path.startswith("/api/v1/config"):
                time.sleep(args.config_delay)
            answer(handler)

        backend.Handler.do_GET = slow_config

    sys.argv = [args.backend, "--host", "127.0.0.1", "--port", str(args.port)]
    backend.main()


if __name__ == "__main__":
    main()
