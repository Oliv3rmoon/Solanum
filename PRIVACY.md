# Privacy policy

Solanum is free, open-source software (GPL-3.0-or-later) with no ads, no accounts, no analytics and no
telemetry. The developer receives no data from your console. This policy lists everything the app stores and every
connection it can make.

Last updated: 2026-10-30.

## What stays on your console

Everything lives under `/data/solanum/` (and the app under `/data/homebrew/PPSA76526/`):

- `settings.ini`: the settings; `apps.ini`: what you decided for each app (holds, skipped releases) and how it was
  installed; `catalogs.ini`: the custom catalogs you added.
- `cache/catalog.json`, `cache/catalog-*.json`: copies of the catalogs, for starts without a network;
  `cache/*.zip.part`: a download while it arrives; `cache/keep/<title ID>/`: the ZIPs of each app's installed version
  and the one before it, for Roll back (Installed > Clear the cache deletes them).
- `previous-version/`: the files of the version an update replaced, until the next update of that app.
- `music/`: the WAV files you put there for Background music. Solanum only reads them; it never writes to the
  folder, and nothing in it is sent anywhere.
- `logs/solanum.log`, `logs/updater.log`, `logs/app-launch.log`: diagnostic logs.

The apps it installs go in `/data/homebrew/` beside it, or in the `homebrew` folder of the drive you choose. None of this is sent anywhere by the app. You can delete it
at any time with an FTP client.

## Connections the app makes

| To | When | What is sent | Why |
| --- | --- | --- | --- |
| raw.githubusercontent.com | When it starts, and when you refresh | A request for `Oliv3rmoon/Solanum-Catalog`'s `catalog.json` | The list of apps, with each one's pinned release |
| The custom catalogs you added (raw.githubusercontent.com, or the HTTPS address you typed) | The same | A request for that `catalog.json` | Their apps |
| api.github.com | When it starts, and when you refresh | A request for the latest release of each installed app's repository, and of each app the catalog pinned nothing for | A release newer than the catalog's pin |
| api.github.com | When you open an app's Versions | A request for that repository's releases | The list to choose from |
| github.com (and the file host it sends downloads to) | The same, and when you install or update an app | A request for the release's checksum file; installing downloads the release's ZIP | Checking and installing the app |
| api.github.com and github.com | If "Check for updates at start" is on, or when you check from Settings | A request for the latest release of `Oliv3rmoon/Solanum`, then its `SHA256SUMS` and `PPSA76526.zip` | Solanum's own update |

These servers receive the usual network metadata (your IP address, and a User-Agent naming Solanum or the
boilerplate's update kit) and are governed by their own privacy policies. Nothing else is sent, no game is
downloaded, and the app talks to no server run by the developer.

## Children

The app collects no personal data and has no social features.

## Contact

Open an issue at <https://github.com/Oliv3rmoon/Solanum/issues>.
