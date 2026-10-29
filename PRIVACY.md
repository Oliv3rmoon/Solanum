# Privacy policy

Solanum is free, open-source software (GPL-3.0-or-later) with no ads, no accounts, no analytics and no
telemetry. The developer receives no data from your console. This policy lists everything the app stores and every
connection it can make.

Last updated: 2026-10-29.

## What stays on your console

Everything lives under `/data/solanum/` (and the app under `/data/homebrew/PPSA76526/`):

- `settings.ini`: the settings.
- `cache/catalog.json`: a copy of the catalog, for starts without a network; `cache/*.zip.part`: a download while it
  arrives, deleted once its app is installed.
- `previous-version/`: the files of the version an update replaced, until the next update of that app.
- `logs/solanum.log`, `logs/updater.log`, `logs/app-launch.log`: diagnostic logs.

The apps it installs go in `/data/homebrew/` beside it. None of this is sent anywhere by the app. You can delete it
at any time with an FTP client.

## Connections the app makes

| To | When | What is sent | Why |
| --- | --- | --- | --- |
| raw.githubusercontent.com | When it starts, and when you refresh | A request for `Oliv3rmoon/Solanum-Catalog`'s `catalog.json` | The list of apps |
| api.github.com | When it starts, and when you refresh | A request for the latest release of each app's repository, as the catalog names it | Each app's newest version, notes and checksum |
| github.com (and the file host it sends downloads to) | The same, and when you install or update an app | A request for the release's checksum file; installing downloads the release's ZIP | Checking and installing the app |
| api.github.com and github.com | If "Check for updates at start" is on, or when you check from Settings | A request for the latest release of `Oliv3rmoon/Solanum`, then its `SHA256SUMS` and `PPSA76526.zip` | Solanum's own update |

These servers receive the usual network metadata (your IP address, and a User-Agent naming Solanum or the
boilerplate's update kit) and are governed by their own privacy policies. Nothing else is sent, no game is
downloaded, and the app talks to no server run by the developer.

## Children

The app collects no personal data and has no social features.

## Contact

Open an issue at <https://github.com/Oliv3rmoon/Solanum/issues>.
