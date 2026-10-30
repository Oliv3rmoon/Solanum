<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="Solanum icon">
</p>

<h1 align="center">Solanum</h1>

<p align="center">
  <strong>Solanum, a Nightshade app: a store for open-source PS5 homebrew, by Oliv3rmoon. It installs, updates, rolls back and uninstalls the apps of an open catalog from their own GitHub releases, each checked against the SHA-256 the catalog pinned, in Nightshade's black and red.</strong>
</p>

<p align="center">
  <a href="https://github.com/Oliv3rmoon/Solanum/releases/latest"><img src="https://img.shields.io/github/v/release/Oliv3rmoon/Solanum?style=for-the-badge&label=download&color=e5484d&labelColor=0d0d0f" alt="Download the latest release"></a>
  <a href="https://discord.gg/pSqgRU3YP9"><img src="https://img.shields.io/badge/discord-nightly-0d0d0f?style=for-the-badge&logo=discord&logoColor=e5484d" alt="Discord: nightly"></a>
  <a href="https://buymeacoffee.com/oliv3rmoon"><img src="https://img.shields.io/badge/buy%20me%20a%20coffee-0d0d0f?style=for-the-badge&logo=buymeacoffee&logoColor=e5484d" alt="Buy me a coffee"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-0d0d0f?style=for-the-badge&labelColor=0d0d0f&color=e5484d" alt="GPL-3.0-or-later"></a>
</p>

Solanum, a Nightshade app named after the genus at the heart of the nightshade family, is a PS5 homebrew
store. It lists the 56 apps of the open catalog at
[Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog), installs and updates each one
from its own GitHub releases after checking the download against the SHA-256 the catalog pinned for it, shows what
changed before an update, rolls back, installs older versions, uninstalls, and opens what it installed. Its installs
are those of
[Nightshade](https://github.com/Oliv3rmoon/Nightshade)'s Settings > Apps, and it is built on
[Dulcamara](https://github.com/Oliv3rmoon/Dulcamara)'s frontend, which is
[Mandrake](https://github.com/Oliv3rmoon/Mandrake)'s. The current version is **0.3.0**; what changed is in the
[changelog](docs/CHANGELOG.md).

## What you get

- **The catalog's apps.** 56 open-source apps: media players and clients for your own servers, emulators, engine
  ports that need your own game files, games with their own assets, and tools. Browse them by category (the
  Nightshade family first) or search them by name, author or tag (`#emulator`, `#ps1`). Each has a page with what it
  does, its author, its stars on GitHub, its tags, its license, an **attested** badge when its builds are attested
  by its own CI, its newest version, when it came out and its notes, and a QR code to its source.
- **Every release pinned.** The catalog's workflow downloads each app's newest release, checks it, and records its
  SHA-256, size and content version. Solanum checks every download against that pin, and against the release's
  checksum file or GitHub's digest when there is one, as it arrives and again before it is unpacked; one that
  doesn't match is never unpacked. A release with no checksum file installs too.
- **Install, update, open.** Install puts an app in `/data/homebrew/<title ID>` beside Solanum, or on an extended
  drive or a USB drive (Install on, or Settings); Update replaces an older version's files and keeps your own; Open
  closes Solanum and starts the app.
- **More, on an app's page:** **Versions** installs any of its releases, an older one too; **Roll back** puts the
  version before back in one press, from the ZIP Solanum kept, without downloading it; **Skip** an update, or
  **Hold** an app's updates; **Uninstall** removes its folder and its home-screen entry, and its data folder when
  you tick it.
- **Updates.** One tab lists every installed app with a newer release, with its changelog beside it, and **Update
  all**. Switch on *Update apps by themselves* (Settings > Store) and they update after each refresh, except the
  ones you hold or skipped.
- **Downloads.** Installs, updates and uninstalls wait in a queue and run up to three at once; an install cut off by
  the network goes on from where it stopped the next time.
- **Installed.** Where each app lives and how much room it takes, each drive's free room, and what Solanum keeps.
  Apps you installed by hand, anywhere ShadowMountPlus scans, are found and looked after from then on.
- **Custom catalogs.** Add another catalog (a GitHub `owner/name` or an HTTPS `catalog.json`) in Settings >
  Catalogs. Its apps are marked third-party, never attested, ask before they install, and are checked against
  GitHub alone.
- **Background music.** Put WAV files in `/data/solanum/music` and they play behind the store, in the order of their
  names or shuffled, with a volume of their own (Settings > Interface); they fade out as an app opens.
- **Ten languages:** English, French, Portuguese, Spanish, Japanese, German, Dutch, Turkish, Italian and Polish. And
  a USB keyboard works as well as the controller.
- **Open source only.** The catalog lists open-source homebrew with no game files, each linking its source and its
  license. Solanum never offers game files, and downloads only the apps' own GitHub releases.
- **Nothing to pay for.** No ads, no account, no tracking, no server of its own. Free software under the GPL.

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
| L1 / R1 | The tabs: Browse, Updates, Installed, Downloads, About |
| D-pad, left stick | Move |
| Cross | Open an app's page; on the page, Install, Update, Open or More |
| Triangle | Search (Browse); More (an app's page); Update all (Updates); Clear the cache (Installed) |
| Circle | Back; during an install, cancel it (until its files are being put in place) |
| Square | Ask for the catalogs and the newest releases again |
| L2 / R2, right stick | Scroll the release notes |
| OPTIONS | Settings: language, sounds, music, where new apps go, installs at once, keeping versions, automatic updates, custom catalogs, Solanum's own updates, closing it |

On the search keyboard, L1 switches capitals. A **USB keyboard** works too: the arrows move, Enter chooses, Escape
or Backspace goes back, Tab and Shift+Tab change tab, `/` searches, F5 refreshes, F10 opens Settings, and typing
types (in Browse, typing starts a search).

A new app appears on the home screen once the homebrew loader has looked again (ShadowMountPlus's next scan, or
a restart). Until then Open says so; afterwards Open works from Solanum and the home screen alike.

## Background music

Copy WAV files (16-bit PCM, mono or stereo, 8 to 192 kHz) into `/data/solanum/music` over FTP; Solanum makes the
folder. Settings > Interface switches the music on or off, shuffles it and sets its volume. Without shuffle the files
play in the order of their names (`2.wav` before `10.wav`); shuffled, every file plays once before any plays again.
The folder is read again when the list comes round, so a file you add is played in the next round. MP3 and OGG files
are not played, because Solanum has no decoder for them: convert them to WAV first. The music fades out when an app
opens. Nothing is downloaded or sent: Solanum only reads the files you put there.

## How an install works

It is Nightshade's Settings > Apps, for every app of the catalog:

1. Solanum reads `catalog.json` from Solanum-Catalog (and keeps a copy, so it opens offline too), with each app's
   newest release as the catalog's workflow pinned it: its tag, file, size, SHA-256 and content version. It shows
   them all without asking GitHub's API, and asks it (one request an app, of the 60 an hour GitHub allows an address)
   only for the apps installed on the console, for a release newer than the pin. Drafts and pre-releases never
   count.
2. A release's file is checked against its pin, and against the line the release's checksum file has for it and
   GitHub's own digest of the file when there are; all must agree. A release newer than the pin installs only with a
   checksum file or a digest; a file that changed after it was pinned never installs. The download's address is made
   from the repository, the tag and the name, never taken from anywhere else.
3. Install sends `self-updater.elf` (the boilerplate's self-update helper, with Nightshade's "install") from
   Solanum's folder to the payload loader. Solanum downloads the ZIP from GitHub, checks it as it arrives and
   hands it to the helper, which checks it again, unpacks the app's folder (the one holding `eboot.bin` and
   `sce_sys/param.json`) and checks that it is the title and version asked for. Only then does it put the folder
   beside Solanum's or in the folder chosen for it (`<drive>/homebrew`, made when it isn't there yet), or, for an
   update, put the new files in place of the old ones (keeping the files it replaced in
   `/data/solanum/previous-version/` until the next update). Anything that doesn't match is refused, and nothing is
   changed.
4. The ZIP it installed is kept in `/data/solanum/cache/keep/<title ID>/` with the one before it, so Roll back hands
   the one before to the helper again, checked the same way, without the network.
5. Uninstall asks the helper to remove the app's one installed folder (never the running app, never an image, never
   a folder that holds other apps), to take the title off the home screen through the console's app-install
   service, and, when you tick it, to remove the data folder the catalog names for it (one folder of its own directly
   in `/data`).
6. Open sends `app-launch.elf` to the payload loader; Solanum closes, and the payload opens the app.

Each download is kept in `/data/solanum/cache/` while it arrives, so a cut-off one goes on with an HTTP range
next time; a copy that doesn't match its checksum is deleted, never resumed. Your decisions (holds, skipped
releases, how each app came to be installed) are in `/data/solanum/apps.ini`, custom catalogs in
`/data/solanum/catalogs.ini`. The logs are in `/data/solanum/logs/` (`solanum.log`, `updater.log`,
`app-launch.log`).

## Add an app to the catalog

Open a pull request on [Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog): one entry in
`catalog.json`'s `apps`, as its README says. Open-source homebrew only, with an OSI license and a GitHub release
whose ZIP holds the app's folder; a checksum file is welcome but no longer needed, since the catalog's workflow
pins every release. Solanum shows it once the pull request is in.

## Custom catalogs

Settings > Catalogs > Add a catalog takes a GitHub repository (`owner/name`, or its page's address), whose
`catalog.json` at the root is read from raw.githubusercontent.com, or the HTTPS address of any `catalog.json` in
Solanum-Catalog's format; eight at most. Their apps come after Solanum's, each in a section named after its catalog,
and are third-party always: marked so, never attested or in the Nightshade family, no data folder to remove, a
question before each one's first install, and an app that Solanum's catalog lists too is shown from Solanum's. Their
downloads still come only from the apps' own GitHub releases, checked against what GitHub says (the release's
checksum file or GitHub's digest), never against what the custom catalog says.

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
make host-checks   # the store (tests/store_check.c, on the live catalog, the pinned one and the apps' real
                   # releases in tests/fixtures), the install helper's whole conversation with installs on any
                   # drive and uninstalls (tests/helper_check.cpp), Solanum's own update
                   # (tests/update_release_check.c), the background music's playlist, WAV reader and mixer
                   # (tests/music_check.c), and the translations (tests/test_i18n.py)
make desktop       # the app in a window on this computer (SDL2), its store answering from tests/fixtures
```

| Path | What |
|---|---|
| `src/main.c` | start-up, the main loop, notices, what the screens share |
| `src/catalog.c` | reading `catalog.json` (with its pins, tags and data folders) and merging custom catalogs; bad entries are left out with a log line |
| `src/apps.c` | where an app is, what a release offers (checked against the catalog's pins), the launch conversation, search, the Konami code, tips |
| `src/store.c` | the store's thread, the job queue (installs, updates, versions, roll backs, uninstalls), Versions, storage, Open |
| `src/resume.c` | downloads that go on where they stopped |
| `src/music.c`, `src/playlist.c`, `src/wavstream.c`, `src/mixer.c` | the background music: its thread, the playlist (name order or shuffle), the WAV reader and the mixer that puts the interface's clicks into it |
| `src/keep.c`, `src/prefs.c`, `src/custom.c` | the kept ZIPs for Roll back, the player's holds and skips, the custom catalogs |
| `src/i18n.c`, `i18n/` | the ten languages (`tools/i18n.py` writes `src/i18n_table.inc` from `i18n/<language>.json`) |
| `src/ui/` | Browse, Updates, Installed, Downloads, About, the app pages and their menus, the keyboard, Settings |
| `src/platform/keys.c` | the USB keyboard |
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
