# Releases

Every push to `main` that builds becomes a nightly, and every version tag a release, each with `edenprobe.nro`,
`edenprobe.apk` ([On Android](Android.md)) and a `.sha256` beside each. Only the CI uploads them. The tables and
commands below name the NRO; the APK is the same with `.apk`.

| What | Where |
|---|---|
| The newest nightly | `https://github.com/scarletnx/edenprobe/releases/download/nightly/edenprobe.nro` |
| One nightly (the newest 30 are kept) | `.../releases/download/nightly-<date>-<commit>/edenprobe.nro` |
| The newest version | `https://github.com/scarletnx/edenprobe/releases/latest/download/edenprobe.nro` |
| One version | `.../releases/download/v<version>/edenprobe.nro` |

```sh
base=https://github.com/scarletnx/edenprobe/releases/download/nightly
curl -fL -O "$base/edenprobe.nro" -O "$base/edenprobe.nro.sha256"
sha256sum -c edenprobe.nro.sha256
```

`nightly`'s files are replaced one after the other, so a check that fails while a nightly lands passes when they are
fetched again; a dated nightly and a version never change. A pull request's NRO and APK are its Build run's artifacts,
`edenprobe-<commit>.nro` and `edenprobe-<commit>.apk`, linked from a comment on the pull request (a fork's on the
run's page).

## The CI

- **`build.yml`**, on each push to `main`, each `v*` tag and each pull request: the NRO in devkitPro's container,
  pinned by tag and digest, checked to name its own commit, clean, and uploaded as the run's artifact; the APK with the
  runner's Gradle, JDK 21 and Android SDK, its native library checked to name the same commit, and uploaded the same
  way; then `make -f host.mk host-run`. A tag must be `v` and `VERSION`, or the build stops. A newer push to a pull request
  cancels its older build.
- **`publish.yml`**, after a Build run of a push passes: for `main`, the prerelease `nightly-<date>-<commit>` (never
  the latest) and the prerelease `nightly`, whose tag and files follow the newest; for a tag, its release, the
  latest (a tag with a `-` is a prerelease). Each release lists the changes since the one before.
- A version's release also gets a message on Discord, on the channel of the secret `DISCORD_WEBHOOK_RELEASES`;
  without it, nothing is posted there and the run says so.

To release a version: change `VERSION` on `main`, tag that commit `v<VERSION>` and push the tag. The scripts are in
`.github/scripts`; `make -f host.mk host-run` runs their tests against a fake GitHub and a fake Discord.
