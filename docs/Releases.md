# Releases

Every push to `main` that builds becomes a nightly, and every version tag a release, each holding
`edenprobe.nro` and `edenprobe.nro.sha256`. The CI builds them; nobody uploads a file by hand.

## Getting the NRO

| What | Where |
|---|---|
| The newest nightly | `https://github.com/scarletnx/edenprobe/releases/download/nightly/edenprobe.nro` |
| One nightly (the newest 30 are kept) | `.../releases/download/nightly-<date>-<commit>/edenprobe.nro` |
| The newest version | `https://github.com/scarletnx/edenprobe/releases/latest/download/edenprobe.nro` |
| One version | `.../releases/download/v<version>/edenprobe.nro` |

The `.sha256` beside each is in the form `sha256sum -c` reads:

```sh
base=https://github.com/scarletnx/edenprobe/releases/download/nightly
curl -fL -O "$base/edenprobe.nro" -O "$base/edenprobe.nro.sha256"
sha256sum -c edenprobe.nro.sha256
```

The `nightly` files are replaced one after the other when a new nightly lands, so a check that
fails at that moment passes when both are fetched again; a dated nightly and a version never
change. Whichever you take, a run's report names it: the opening note gives the version and the
build, and the folder is named after the commit (see [Versions](./Versions.md)).

A pull request's NRO is not released: it is its Build run's artifact, `edenprobe-<commit>.nro`,
linked from a comment on the pull request (a fork's run lists it on the run's page).

## How the CI publishes

- **`.github/workflows/build.yml`**, on each push to `main`, each `v*` tag and each pull request:
  builds the NRO in devkitPro's `devkitpro/devkita64` container, pinned by tag and digest; checks
  that the build names its own commit, clean; uploads it as the run's artifact; and runs
  `make -f host.mk host-run`. A tag must read `v` followed by `VERSION`, or the build stops.
- **`.github/workflows/publish.yml`**, after a Build run of a push passes: for `main`, the
  prerelease `nightly-<date>-<commit>` (never the latest) and the prerelease `nightly`, whose tag
  and files follow the newest; for a tag, its release, the latest (a tag with a `-` is a
  prerelease). Each release lists what changed since the one before it.
- **`.github/workflows/cards.yml`** and **`cards-watch.yml`**: a Discord card for each nightly,
  pull request and issue, edited as it changes; a message for each version. The webhooks are the
  secrets `DISCORD_WEBHOOK_NIGHTLY`, `DISCORD_WEBHOOK_PRS`, `DISCORD_WEBHOOK_ISSUES` and
  `DISCORD_WEBHOOK_RELEASES`; without one, nothing is posted there and the run says so. Each
  card's message id is kept in the `discord-cards` branch.

To release a version: change `VERSION` on `main`, then tag that commit `v<VERSION>` and push the
tag. The scripts are in `.github/scripts`, and `make -f host.mk host-run` runs their tests against
a fake GitHub and a fake Discord.
