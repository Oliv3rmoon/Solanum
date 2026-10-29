<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="Solanum icon">
</p>

<h1 align="center">Solanum</h1>

<p align="center">
  <strong>Solanum, a Nightshade app: a store for open-source PS5 homebrew, by Oliv3rmoon. It installs and updates the apps of an open catalog from their own GitHub releases, each checked against its checksums, in Nightshade's black and red.</strong>
</p>

<p align="center">
  <a href="https://github.com/Oliv3rmoon/Solanum/releases/latest"><img src="https://img.shields.io/github/v/release/Oliv3rmoon/Solanum?style=for-the-badge&label=download&color=e5484d&labelColor=0d0d0f" alt="Download the latest release"></a>
  <a href="https://discord.gg/pSqgRU3YP9"><img src="https://img.shields.io/badge/discord-nightly-0d0d0f?style=for-the-badge&logo=discord&logoColor=e5484d" alt="Discord: nightly"></a>
  <a href="https://buymeacoffee.com/oliv3rmoon"><img src="https://img.shields.io/badge/buy%20me%20a%20coffee-0d0d0f?style=for-the-badge&logo=buymeacoffee&logoColor=e5484d" alt="Buy me a coffee"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-0d0d0f?style=for-the-badge&labelColor=0d0d0f&color=e5484d" alt="GPL-3.0-or-later"></a>
</p>

Solanum, a Nightshade app named after the genus at the heart of the nightshade family, is a PS5 homebrew
store. It lists the apps of the open catalog at
[Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog), installs and updates each one
from its own GitHub releases after checking the download against the release's checksum file, shows what
changed before an update, and opens what it installed. Its installs are those of
[Nightshade](https://github.com/Oliv3rmoon/Nightshade)'s Settings > Apps, and it is built on
[Dulcamara](https://github.com/Oliv3rmoon/Dulcamara)'s frontend, which is
[Mandrake](https://github.com/Oliv3rmoon/Mandrake)'s. The current version is **0.1.0**; what changed is in the
[changelog](docs/CHANGELOG.md).

## What you get

- **The catalog's apps.** Browse them by category, the Nightshade family first, or search by name. Each has a
  page with what it does, its license, an **attested** badge when its builds are attested by its own CI, its
  newest version and when it came out, and the release's notes.
- **Install, update, open.** Install puts an app in `/data/homebrew/<title ID>` beside Solanum; Update replaces
  an older version's files and keeps your own; Open closes Solanum and starts the app.
- **Checked before anything is unpacked.** Every download is checked against the SHA-256 its release's
  checksum file (usually `SHA256SUMS`) gives it, as it arrives and again before it is unpacked. One that
  doesn't match is never unpacked, and Solanum says so.
- **Downloads that pick up where they stopped.** An install cut off by the network goes on from where it
  stopped the next time.
- **Updates.** One tab lists every installed app with a newer release, with its changelog beside it.
- **Installed.** Where each app lives and how much room it takes.
- **Open source only.** The catalog lists open-source homebrew, each linking its source and its license.
  Solanum never offers game files, has no "add any URL", and downloads only the catalog's apps' own
  releases.
- **Nothing to pay for.** No ads, no account, no tracking. Free software under the GPL.

## Install

1. Download `PPSA76526.zip` from the [latest release](https://github.com/Oliv3rmoon/Solanum/releases/latest) and
   unzip it.
2. Copy its `PPSA76526` folder to `/data/homebrew/` on the PS5 over FTP, as you install the other Nightshade
   apps, and set its permissions to `777` (in FileZilla: right-click the folder, *File permissions*, `777`,
   recurse into subdirectories). Without them the PS5 says *"Can't start the game or app"*.
3. Restart ShadowMountPlus (or the console). **Solanum** appears on the home screen.

Its title ID, `PPSA76526`, spells SOLAN on a phone keypad; its data lives in `/data/solanum/`.

Solanum needs what the other Nightshade apps need: a HEN with the payload loader on port 9021 (etaHEN, or
another with an ELF loader there), which takes Solanum's install helper and its launch payload, and `/data`
unlocked so it can see the apps already installed (LegacyJB, etaHEN's *Legacy CMD server*, or the Lapy helper it
carries; see Mandrake's README,
[Unlocking /data](https://github.com/Oliv3rmoon/Mandrake#unlocking-data-so-swanstationps5-can-see-your-games)).

## Using it

| Button | |
| --- | --- |
| L1 / R1 | The tabs: Browse, Updates, Installed, About |
| D-pad, left stick | Move |
| Cross | Open an app's page; on the page, Install, Update or Open |
| Circle | Back; during an install, cancel it (until its files are being put in place) |
| Triangle | Search (Browse) |
| Square | Ask for the catalog and the newest releases again |
| L2 / R2, right stick | Scroll the release notes |
| OPTIONS | Settings: the interface sounds, Solanum's own updates, closing it |

A new app appears on the home screen once the homebrew loader has looked again (ShadowMountPlus's next scan, or
a restart). Until then Open says so; afterwards Open works from Solanum and the home screen alike.

## How an install works

It is Nightshade's Settings > Apps, for every app of the catalog:

1. Solanum reads `catalog.json` from Solanum-Catalog (and keeps a copy, so it opens offline too), then asks
   `api.github.com` for each app's newest release: one request an app, of the 60 an hour GitHub allows an
   address. Drafts and pre-releases never count.
2. The release must have the file the catalog names (`{version}` in its name is the tag without a leading `v`)
   and its SHA-256: the line the checksum file has for it, or else GitHub's own digest of the file. When both
   are there they must agree; with neither, nothing is offered. The download's address is made from the
   repository, the tag and the name, never taken from anywhere else.
3. Install sends `self-updater.elf` (the boilerplate's self-update helper, with Nightshade's "install") from
   Solanum's folder to the payload loader. Solanum downloads the ZIP from GitHub, checks it as it arrives and
   hands it to the helper, which checks it again, unpacks the app's folder (the one holding `eboot.bin` and
   `sce_sys/param.json`) and checks that it is the title and version asked for. Only then does it put the folder
   beside Solanum's, or, for an update, put the new files in place of the old ones (keeping the version it
   replaced in `/data/solanum/previous-version/` until the next update). Anything that doesn't match is
   refused, and nothing is changed.
4. Open sends `app-launch.elf` to the payload loader; Solanum closes, and the payload opens the app.

Each download is kept in `/data/solanum/cache/` while it arrives, so a cut-off one goes on with an HTTP range
next time; a copy that doesn't match its checksum is deleted, never resumed. The logs are in
`/data/solanum/logs/` (`solanum.log`, `updater.log`, `app-launch.log`).

## Add an app to the catalog

Open a pull request on [Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog): one entry in
`catalog.json`'s `apps`, as its README says. Open-source homebrew only, released on GitHub with a checksum file.
Solanum shows it once the pull request is in.

## Updates

When it starts (and from **Settings > System > Check for updates now**) Solanum asks GitHub for the newest release
of [Oliv3rmoon/Solanum](https://github.com/Oliv3rmoon/Solanum/releases) that is not a pre-release. When its tag is a
newer version and it carries `PPSA76526.zip` and `SHA256SUMS`, both this repository's own files, the ZIP is checked
against `SHA256SUMS` and installed into the folder the app runs from; the new version runs from the next start.
A release built by this repository's workflow can be checked with
`gh attestation verify PPSA76526.zip -R Oliv3rmoon/Solanum`.

## Requests, bugs and support

- **Ask for a fix or a feature:** [open an issue](https://github.com/Oliv3rmoon/Solanum/issues/new/choose), or
  ask in **[nightly on Discord](https://discord.gg/pSqgRU3YP9)**.
- **Bugs:** open an issue with `/data/solanum/logs/solanum.log` (and `updater.log` for an install), copied right
  after the problem.
- **An app that doesn't work:** its own repository is the place; Solanum only installs its releases.
- **If my work helps you,** you can [buy me a coffee](https://buymeacoffee.com/oliv3rmoon).

## Building from source

The PS5 app builds on Linux or WSL (Ubuntu 24.04); GitHub Actions builds every push
([.github/workflows/build.yml](.github/workflows/build.yml)):

```bash
sudo apt install clang-18 lld-18 llvm-18 libclang-rt-18-dev make ninja-build ccache cmake pkg-config \
  python3 python3-venv tar unzip wget zip zstd
git clone https://github.com/Oliv3rmoon/Solanum.git
cd Solanum
make                            # -> dist/PPSA76526/, with self-updater.elf and app-launch.elf
python3 tools/make_release.py   # optional: build, and pack PPSA76526.zip as a release has it
```

`make` builds the install helper and the launch payload with the PS5 payload SDK, links the RADV Vulkan driver
(`tools/fetch-radv.sh`), compiles `src/` and the kit, and signs `eboot.bin`.

The host checks need no PS5 SDK, only a C and C++ compiler:

```bash
make host-checks   # the store (tests/store_check.c, on the live catalog and the apps' real releases in
                   # tests/fixtures), the install helper's whole conversation (tests/helper_check.cpp),
                   # and Solanum's own update (tests/update_release_check.c)
make desktop       # the app in a window on this computer (SDL2), its store answering from tests/fixtures
```

| Path | What |
|---|---|
| `src/main.c` | start-up, the main loop, notices, what the screens share |
| `src/catalog.c` | reading `catalog.json`; bad entries are left out with a log line |
| `src/apps.c` | where an app is, what a release offers, the launch conversation, the Konami code, tips |
| `src/store.c` | the store's thread, installs and Open (Nightshade's Settings > Apps) |
| `src/resume.c` | downloads that go on where they stopped |
| `src/ui/` | Browse, Updates, Installed, About, the app pages, the search keyboard, Settings |
| `kit/` | the self-update kit: Nightshade's copy of the boilerplate's (see `kit/README.md`) |
| `helper/` | the install helper and the launch payload, Nightshade's |
| `src/platform/` | Mandrake's PS5 platform layer and Vulkan screen |
| `tools/render-home-art.py` | the icon and the home-screen pictures |
| `docs/boilerplate/` | the PS5 app boilerplate everything is built on |

## Thanks

- **[Nightshade](https://github.com/Oliv3rmoon/Nightshade)**, whose Settings > Apps, update helper and launch
  payload Solanum's installs and Open button are.
- **Mihawk**, whose [PS5 RetroArch](https://github.com/mihawk-99/PS5_RetroArch) forwarder's launch payload is the
  one Solanum sends, and who ported RADV to the PS5.
- **[Dulcamara](https://github.com/Oliv3rmoon/Dulcamara)** and **[Mandrake (formerly Nightshade Station)](https://github.com/Oliv3rmoon/Mandrake)**,
  and the work they are made of: **[SwanStationPS5](https://github.com/darkxex/SwanStationPS5)** by **darkxex**,
  **[PSXS5](https://github.com/SynoPiia/PSXS5)** by **SynoPiia**, and **BlackBearReloaded**'s
  ps5-native-app-boilerplate, whose self-update kit and helper are under all of it.
- **[ps5-payload-dev](https://github.com/ps5-payload-dev)** for the SDK and PacBrew, and everyone in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Every author whose app is in the catalog.

## Legal

Solanum is free software under the GNU General Public License, version 3 or later ([LICENSE](LICENSE)). Every
component keeps its own licence, listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and the app folder
carries their texts in `licenses/`. Each app it installs is its authors', under its own license. It is an
unofficial project, not endorsed by Sony. It includes and downloads no game, key or system file.
