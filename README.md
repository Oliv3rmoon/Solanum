<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="Solanum icon">
</p>

<h1 align="center">Solanum</h1>

<p align="center">
  <strong>The store for PS5 homebrew. Install, update, roll back and uninstall apps from their own GitHub releases, each checked against a pinned SHA-256.</strong>
</p>

<p align="center">
  <a href="https://github.com/Oliv3rmoon/Solanum/releases/latest"><img src="https://img.shields.io/github/v/release/Oliv3rmoon/Solanum?style=for-the-badge&label=download&color=e5484d&labelColor=0d0d0f" alt="Download the latest release"></a>
  <a href="https://discord.gg/pSqgRU3YP9"><img src="https://img.shields.io/badge/discord-nightly-0d0d0f?style=for-the-badge&logo=discord&logoColor=e5484d" alt="Discord: nightly"></a>
  <a href="https://github.com/Oliv3rmoon/Solanum/stargazers"><img src="https://img.shields.io/github/stars/Oliv3rmoon/Solanum?style=for-the-badge&logo=github&logoColor=e5484d&color=e5484d&labelColor=0d0d0f" alt="GitHub stars"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-0d0d0f?style=for-the-badge&labelColor=0d0d0f&color=e5484d" alt="GPL-3.0-or-later"></a>
  <a href="https://buymeacoffee.com/oliv3rmoon"><img src="https://img.shields.io/badge/buy%20me%20a%20coffee-0d0d0f?style=for-the-badge&logo=buymeacoffee&logoColor=e5484d" alt="Buy me a coffee"></a>
</p>

Solanum, named after the genus at the heart of the nightshade family, is a Nightshade app by **[Oliv3rmoon](https://github.com/Oliv3rmoon)**: a PS5 homebrew store for the 154 apps of the open catalog at [Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog), which is also on the web, with a page for each app: **<https://oliv3rmoon.github.io/Solanum-Catalog/>**. The latest release is **0.3.0**, a full release; what changed is in the [changelog](docs/CHANGELOG.md).

## Why Solanum

- **A catalog you can read.** 154 apps, among them ProsperoStore's catalog's apps with a public release (but for backup tools, a bundled game WAD, two releases with nothing installable to check, and a few left out by choice): media players and clients for your own servers, emulators, decompilations, recompilations and engine ports that run your own game files, games with their own assets, AI apps and tools. Browse them by category (the Nightshade family first) or search by name, author or tag (`#emulator`, `#recomp`). Each has a page with what it does, its author, its stars on GitHub, its license (or **No license**), an **attested** badge when its own CI attests its builds, **Needs your own game files** for a port, **Pre-release** when its newest release is one, its newest version and notes, and a QR code to its source.
- **Every release pinned.** The catalog's workflow downloads each app's newest release, checks it, and records its SHA-256, size and content version. Solanum checks every download against that pin, and against the release's checksum file or GitHub's digest when there is one, as it arrives and again before it is unpacked. One that doesn't match is never unpacked.
- **Install, update, open.** Install puts an app in `/data/homebrew/<title ID>` beside Solanum, or on an extended or USB drive. Update replaces an older version's files and keeps your own. Open closes Solanum and starts the app.
- **Undo anything.** **Versions** installs any release, an older one too; **Roll back** puts the version before back in one press, without downloading it; **Skip** an update or **Hold** an app's updates; **Uninstall** removes the app's folder and its home-screen entry, and its data folder when you tick it.
- **Updates in one place.** One tab lists every installed app with a newer release, with its changelog beside it, and **Update all**. Switch on *Update apps by themselves* (Settings > Store) and they update after each refresh, except the ones you hold or skipped. Downloads wait in a queue, run up to three at once, and go on from where they stopped.
- **Your catalogs, your music.** Add another catalog (a GitHub `owner/name` or an HTTPS `catalog.json`) in Settings > Catalogs; its apps are marked third-party and checked against GitHub alone. Put WAV, MP3 or OGG files in `/data/solanum/music` and they play behind the store, in order or shuffled, and fade out as an app opens.
- **Free, open and quiet.** Ten languages, and a USB keyboard works as well as the controller. No ads, no account, no tracking and no server of its own; free software under the GPL. Solanum never offers game files: no release in the catalog carries game data, ROMs, BIOS or keys, and no backup tools are listed.

## Solanum and ProsperoStore

ProsperoStore is another PS5 homebrew store. Here is where they differ. A dash means the other app's README doesn't list it.

| | Solanum | ProsperoStore |
| --- | --- | --- |
| Catalog | Solanum-Catalog: open on GitHub, 154 apps, also on the web | prosperostore.net, signed |
| Ports that need your own game files | Marked on the app's page and in the list | — |
| Two apps of one title ID | Tells which is installed, asks before replacing it | — |
| What checks a download | A SHA-256 pinned for every release, plus the release's checksum file or GitHub's digest | The signed catalog |
| Install, update, uninstall | Yes | Yes |
| Open an app from the store | Yes | No |
| Older versions, Roll back, Skip or Hold an update | Yes | — |
| Update apps by themselves | Yes (Settings > Store) | — |
| Custom catalogs | Yes, up to eight | Yes, up to eight |
| Likes and comments on each app | No | Yes |
| Background music | Your own WAV, MP3 or OGG files | Its own song, or your own `background.mp3` |
| Title ID | `PPSA76526` | `PPSA99000` |

## Install

1. Download `PPSA76526.zip` from the [latest release](https://github.com/Oliv3rmoon/Solanum/releases/latest) and unzip it.
2. Copy its `PPSA76526` folder to `/data/homebrew/` on the PS5 over FTP, as you install the other Nightshade apps, and set its permissions to `777` (in FileZilla: right-click the folder, *File permissions*, `777`, recurse into subdirectories). Without them the PS5 says *"Can't start the game or app"*.
3. Restart ShadowMountPlus (or the console). **Solanum** appears on the home screen.

Its title ID, `PPSA76526`, spells SOLAN on a phone keypad; its data lives in `/data/solanum/`.

Solanum needs what the other Nightshade apps need: a HEN with the payload loader on port 9021 (etaHEN, or another with an ELF loader there), which takes Solanum's install helper and its launch payload, and `/data` unlocked so it can see the apps already installed (LegacyJB, etaHEN's *Legacy CMD server*, or the Lapy helper it carries; see [Unlocking /data](https://github.com/Oliv3rmoon/Mandrake/blob/main/docs/GUIDE.md#unlocking-data-so-swanstationps5-can-see-your-games)). Once running, Solanum updates itself from this repository's releases.

## Docs

- **[Guide](docs/GUIDE.md):** [using it](docs/GUIDE.md#using-it) (the controls and the USB keyboard), [background music](docs/GUIDE.md#background-music), [how an install works](docs/GUIDE.md#how-an-install-works), [custom catalogs](docs/GUIDE.md#custom-catalogs), [updates](docs/GUIDE.md#updates) and [building from source](docs/GUIDE.md#building-from-source).
- **[Changelog](docs/CHANGELOG.md)**, [Privacy policy](PRIVACY.md), [Third-party notices](THIRD_PARTY_NOTICES.md) and [Contributing](CONTRIBUTING.md).
- **Add your app to the catalog:** open a pull request on [Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog), or see the website's [Submit your app](https://oliv3rmoon.github.io/Solanum-Catalog/submit/) page. A public repository and a GitHub release whose ZIP holds the app's folder, with no game files in it. [The guide](docs/GUIDE.md#add-an-app-to-the-catalog) has the details.

## Requests, bugs and support

- **Ask for a fix or a feature:** [open an issue](https://github.com/Oliv3rmoon/Solanum/issues/new/choose), or ask in **[nightly on Discord](https://discord.gg/pSqgRU3YP9)**.
- **Bugs:** open an issue with `/data/solanum/logs/solanum.log` (and `updater.log` for an install), copied right after the problem.
- **An app that doesn't work:** its own repository is the place; Solanum only installs its releases.
- **If my work helps you,** you can [buy me a coffee](https://buymeacoffee.com/oliv3rmoon).

## The Nightshade family

Solanum is one of seven repositories in the Nightshade family, all by [Oliv3rmoon](https://github.com/Oliv3rmoon): the same black-and-red look, one Discord.

| App | What it is |
| --- | --- |
| [**Nightshade**](https://github.com/Oliv3rmoon/Nightshade) | The Switch emulator for PS5 homebrew |
| [**Physalis**](https://github.com/Oliv3rmoon/Physalis) | Live TV, movies and Plex on the PS5 |
| [**Datura**](https://github.com/Oliv3rmoon/Datura) | Game streaming from your PC to the PS5 |
| [**Mandrake**](https://github.com/Oliv3rmoon/Mandrake) | PS1 emulation, with two emulators |
| [**Dulcamara**](https://github.com/Oliv3rmoon/Dulcamara) | RPG Maker 2000 and 2003 games, and Ren'Py visual novels |
| **Solanum** (this repo) | A store for PS5 homebrew, with its catalog [on the web](https://oliv3rmoon.github.io/Solanum-Catalog/) |
| [**Belladonna**](https://github.com/Oliv3rmoon/Belladonna) | Nightshade's PS5 Vulkan driver |

## Built on

The open [Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog) and its [website](https://oliv3rmoon.github.io/Solanum-Catalog/), on the frontend of [Dulcamara](https://github.com/Oliv3rmoon/Dulcamara), which is [Mandrake](https://github.com/Oliv3rmoon/Mandrake)'s. Its installs are those of [Nightshade](https://github.com/Oliv3rmoon/Nightshade)'s Settings > Apps.

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

## License

Solanum is free software under the GNU General Public License, version 3 or later ([LICENSE](LICENSE)). Every
component keeps its own licence, listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and the app folder
carries their texts in `licenses/`. Each app it installs is its authors', under its own license.

## Disclaimer

Solanum is an unofficial project, not endorsed by Sony. It includes and downloads no game, key or system file.

## Help it grow

Like Solanum? **[Star it](https://github.com/Oliv3rmoon/Solanum)** and **[join the Discord](https://discord.gg/pSqgRU3YP9)**.
