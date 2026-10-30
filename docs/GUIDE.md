# Solanum guide

How to use Solanum, its background music, how an install works, how to add an app to the catalog, custom catalogs, how Solanum updates itself, and how to build it from source.

Back to the [README](../README.md).

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

Copy music files into `/data/solanum/music` over FTP; Solanum makes the folder. WAV (16-bit PCM), MP3 and OGG
Vorbis play, mono or stereo at any sample rate; each is decoded as it plays and converted to the console's 48 kHz
stereo. Settings > Interface switches the music on or off, shuffles it and sets its volume. Without shuffle the files
play in the order of their names (`2.wav` before `10.wav`); shuffled, every file plays once before any plays again.
The folder is read again when the list comes round, so a file you add is played in the next round. Other kinds
(FLAC, M4A, Opus) are not played: convert them to MP3, OGG or WAV first. The music fades out when an app opens.
Nothing is downloaded or sent: Solanum only reads the files you put there.

## How an install works

It is Nightshade's Settings > Apps, for every app of the catalog:

1. Solanum reads `catalog.json` from Solanum-Catalog (and keeps a copy, so it opens offline too), with each app's
   newest release as the catalog's workflow pinned it: its tag, file, size, SHA-256 and content version. It shows
   them all without asking GitHub's API, and asks it (one request an app, of the 60 an hour GitHub allows an address)
   only for the apps installed on the console, for a release newer than the pin. Drafts never count, and neither do
   pre-releases, except for an app whose catalog entry has the `newest` channel (one that publishes only
   pre-releases, or other platforms' releases between its own): its release is the newest one in GitHub's list that
   has its file, and its page says when that is a pre-release.
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

Two apps of the catalog can have one title ID: a fork that kept its original's (Physalis and ProsperoTV, for one). A
console holds one of them. Solanum tells which is installed by the name in its `param.json` (the name the catalog's
workflow read in each app's release), shows the other as not installed, with a note on its page, and installs it
only after asking: in place of the one there, in its folder, where it keeps the saved data. The kept versions and the
holds and skips of the one it replaces are let go, so Roll back never puts the other app back.

Each download is kept in `/data/solanum/cache/` while it arrives, so a cut-off one goes on with an HTTP range
next time; a copy that doesn't match its checksum is deleted, never resumed. Your decisions (holds, skipped
releases, how each app came to be installed) are in `/data/solanum/apps.ini`, custom catalogs in
`/data/solanum/catalogs.ini`. The logs are in `/data/solanum/logs/` (`solanum.log`, `updater.log`,
`app-launch.log`).

## Add an app to the catalog

Open a pull request on [Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog): one entry in
`catalog.json`'s `apps`, as its README says (so does the website's
[Submit your app](https://oliv3rmoon.github.io/Solanum-Catalog/submit/) page). It needs a public repository and a
GitHub release whose ZIP holds the app's folder; a checksum file is welcome but not needed, since the catalog's
workflow pins every release. Its license is shown as it is, or as **No license** when the repository states none.
No game data, ROMs, BIOS or keys in a release, and nothing whose main purpose is installing or loading game dumps or
backups; a port that runs a game you own is welcome, marked **Needs your own game files**. Solanum shows it once the
pull request is in.

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
## Building from source

The PS5 app builds on Linux or WSL (Ubuntu 24.04); GitHub Actions builds every push
([.github/workflows/build.yml](../.github/workflows/build.yml)):

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
                   # (tests/update_release_check.c), the background music's playlist, file reader (WAV, MP3, OGG) and mixer
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
| `src/music.c`, `src/playlist.c`, `src/audiostream.c`, `src/mixer.c` | the background music: its thread, the playlist (name order or shuffle), the reader that decodes WAV, MP3 and OGG as they play, and the mixer that puts the interface's clicks into it |
| `src/codecs.c`, `third_party/dr_libs/`, `third_party/stb_vorbis/` | the MP3 and Ogg Vorbis decoders (dr_mp3 and stb_vorbis, vendored) |
| `src/keep.c`, `src/prefs.c`, `src/custom.c` | the kept ZIPs for Roll back, the player's holds and skips, the custom catalogs |
| `src/i18n.c`, `i18n/` | the ten languages (`tools/i18n.py` writes `src/i18n_table.inc` from `i18n/<language>.json`) |
| `src/ui/` | Browse, Updates, Installed, Downloads, About, the app pages and their menus, the keyboard, Settings |
| `src/platform/keys.c` | the USB keyboard |
| `kit/` | the self-update kit: Nightshade's copy of the boilerplate's (see `kit/README.md`) |
| `helper/` | the install helper and the launch payload, Nightshade's |
| `src/platform/` | Mandrake's PS5 platform layer and Vulkan screen |
| `tools/render-home-art.py` | the icon and the home-screen pictures |
| `docs/boilerplate/` | the PS5 app boilerplate everything is built on |
