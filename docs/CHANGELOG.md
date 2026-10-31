# Changelog

## 0.4.0

ProsperoStore's catalog's apps, and what Solanum needs to install them all: 154 apps in the catalog, up from 56.

- **98 more apps.** Solanum-Catalog now lists the apps of darkxex/ps5-homebrew-catalog that have a public release,
  except six it leaves out by its rules: PKG Folderizer, Alembic and Prospero Multi Tools (they install or convert
  game dumps and packages), DOOM Native Port (its release carries the shareware WAD), The Simpsons Game (its release
  is a PC builder, with no app in it) and Stremio by Sp9nky (only an `.ffpfsc` image, which the catalog can't open
  to check); and five left out by choice: ProsperoStore, ProsperoEden (Mirror), ProsperoTV, ProsperoLight and
  SwanStationPS5. The eight title IDs reserved there have no release yet. Every new release was pinned by the catalog's workflow and its ZIP looked
  into: one app folder of its title ID, and no game data, ROMs, BIOS or keys. Each one-line summary is the
  catalog's own.
- **The catalog's new line.** Decompilations, recompilations, engine ports and PC-game launchers that run a game
  you own are listed, marked **Needs your own game files** on their page and in the list. An app whose repository
  states no license says **No license**; emulators under non-commercial licenses show theirs. Backup tools (PKG
  installers, backup loaders, dump converters) and releases with game files stay out.
- **Apps that publish only pre-releases.** An app whose catalog entry has the `newest` channel is offered its newest
  release with its file, pre-release or not (GitHub's list, one request, as releases/latest is for the others), and
  its page says **Pre-release**. Versions lists its pre-releases too. 20 apps use it, among them SuperTuxKart,
  Source Port, SudokuZen, Mortalfin and the pre-release-only ports; James Pond 2 and Codex PS5 use it to skip the
  releases they publish for other platforms. Its release, and every release Versions lists, is checked as
  releases/latest's is: the release's checksum file must name the file and agree with GitHub's digest and the pin.
- **Two apps of one title ID.** vkQuake has Dulcamara's title ID. A console holds one app of a title ID, so Solanum
  tells which is installed by the name in its `param.json` (the catalog's workflow records each release's): the only
  app whose pinned release has that name, or else whose own name it is. A name no app has, none, or one two apps
  have is no app's: Solanum can't tell, so neither app is updated, rolled back or uninstalled, and the page says so.
  The app that isn't there shows as not installed, with **Installing it replaces ...**; Install, and Versions too,
  ask before putting it in place of the one there, and never over an app mounted from an image. A ZIP kept for Roll
  back is offered only to the app whose repository it came from, so Roll back never puts the other app back, even
  after it was swapped outside Solanum; holds and skips are each app's own. An older Solanum leaves the second app of
  a title ID out of its lists.
- **No endless updates.** Solanum remembers the release it put in place (`apps.ini`), so a release whose tag names no
  version (`v0.2.0-beta.1`) isn't offered, or installed by itself, again once it is installed.
- **Downloads** names each job's own app, not the first app of its title ID.
- About and the Browse hint say what the catalog lists now, in all ten languages. `tests/store_check.c` checks the
  new fields, which app a shared title ID's folder is (and that a name it can't place is no app's), that no install
  replaces another app unasked, Roll back after an outside switch, the checks of GitHub's list, and that the whole
  catalog is read with nothing left out.

## 0.3.0

Background music: your own files, played behind the store.

- **Background music.** Put WAV, MP3 or OGG files in `/data/solanum/music` (Solanum makes the folder) and they play
  one after another behind the store. Settings > Interface has **Background music** (on or off), **Shuffle music** and **Music
  volume** (10 to 100 %). The music fades in, and fades out as an app opens, so the app starts in silence.
- **The order.** Without shuffle, the files play in the order of their names, numbers by value (`2.wav` before
  `10.wav`). Shuffled, every file plays once before any plays again, and a new round never opens with the file
  just played. Changing the setting mid-song keeps that song playing. The folder is read again each time the list
  comes round and when music is switched on, so files copied meanwhile are played; with none, switching it on says so.
- **What it plays.** WAV (16-bit PCM), MP3 and Ogg Vorbis files, mono or stereo, at any sample rate (8 to 192 kHz;
  MP3 up to 48), each converted as it plays to the console's 16-bit stereo at 48 kHz. A file is decoded as it plays,
  not loaded whole, and a file that can't be played is skipped with a line in the log. FLAC, M4A and the like are
  left alone. The decoders are [dr_mp3](https://github.com/mackron/dr_libs) and
  [stb_vorbis](https://github.com/nothings/stb), vendored in `third_party/`; stb_vorbis carries a small patch,
  because a damaged Ogg header could make it write past a block (a bit-flipped file crashed the app), and gets one
  fixed block of memory instead of the heap.
- **A thread of its own** keeps the console's audio queue 64 ms ahead, so a slow frame of the interface never cuts
  the music, and the interface's clicks are mixed into it instead of queued beside it.
- The settings are in all ten languages, and `tests/music_check.c` checks the playlist, the file reader (WAV, MP3
  and OGG, from small samples in `tests/fixtures/music`, and Ogg headers that claim billions of bytes) and the mixer.

## 0.2.0

The store grows up: a catalog of 56 apps, every release pinned by its SHA-256, and everything a store should do with
what it installed.

- **A catalog nine times as big,** and the machinery to keep it big. Solanum-Catalog's workflow now downloads each
  app's newest release every six hours, checks it (GitHub's digest, the release's checksum file, an app folder of
  the right title ID, no game files, the attestation of an attested app) and pins its SHA-256, size and content
  version in the catalog. Solanum checks every download against that pin, so a release with no checksum file
  installs too, and a file replaced after it was pinned is refused. Fifty open-source apps joined the Nightshade
  family: media players and clients for your own servers, emulators, engine ports that need your own game files,
  games with their own assets, and tools; each with an OSI license and no game files, ROMs or keys.
- **A catalog of any size costs GitHub nothing.** Every app's release comes from the catalog's pin; GitHub's API is
  asked only for the apps installed on the console (a release newer than the pin), and no more once its hourly limit
  is reached.
- **Uninstall:** an app's folder and its home-screen entry, and its data folder too when you tick it (the catalog
  names it). **Apps installed by hand** anywhere ShadowMountPlus scans are found, and Solanum looks after them from
  then on.
- **Versions:** install any release of an app, an older one too; **Roll back** puts the version before back in one
  press, from the ZIP Solanum kept, without downloading it; **Skip** an update, or **Hold** an app's updates; and
  **update apps by themselves** after each refresh, if you switch it on. Updates are decided by the content version
  the pin records, whatever the tag says.
- **Any install location ShadowMountPlus scans:** internal storage, an extended drive or a USB drive, chosen for
  each install or in Settings, with a free-space check first.
- **Custom catalogs:** add a GitHub owner/name or an HTTPS catalog.json in Settings > Catalogs. Their apps are marked
  third-party, never attested, ask before they install, and are checked against GitHub alone.
- **Ten languages:** English, French, Portuguese, Spanish, Japanese, German, Dutch, Turkish, Italian and Polish,
  taken from the console's or chosen in Settings.
- **Downloads:** a queue that runs up to three installs, updates and uninstalls at once, with Cancel; **Update
  all**; each drive's free room and what Solanum keeps, with Clear the cache.
- **A USB keyboard** moves, chooses and types; **search by tag** (#emulator); each app's **stars**, author and tags,
  and a **QR code** to its source.
- The install helper puts a new app in the folder chosen for it (install-to) and removes apps (remove); several can
  work at once, each sending its payload from a buffer of its own.

## 0.1.0

The first Solanum, a Nightshade app: a store for open-source PS5 homebrew.

- **The catalog.** The apps of [Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog),
  by category with the Nightshade family first, and a search. An entry the catalog lists badly is left out, the
  rest are shown; a copy of the catalog is kept for starts without a network.
- **Install, update, open.** Nightshade's Settings > Apps for every app of the catalog: the newest release from
  the app's own repository (no drafts or pre-releases), its ZIP checked against the release's checksum file as it
  arrives and again by the install helper before anything is unpacked, the app put beside Solanum in
  `/data/homebrew/<title ID>` or over its own files, and opened by the launch payload once Solanum closes.
- **Downloads that go on where they stopped,** with an HTTP range; a copy that doesn't match is never resumed.
- **Updates** lists the installed apps with a newer release, with their changelogs; **Installed** says where each
  app lives and how big it is; each app has a page with its license, an attested badge, its newest version and
  date, and the release's notes.
- **Tips** from the catalog on the loading screens, and its hidden lines behind two easter eggs.
- **Nightshade's look,** its own icon and home-screen pictures, and its own updates from
  [Oliv3rmoon/Solanum](https://github.com/Oliv3rmoon/Solanum/releases).
- Built on Dulcamara 0.1.0's frontend (Mandrake's), with Nightshade's self-update kit, install helper and launch
  payload.
