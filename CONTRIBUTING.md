# Contributing

Bug reports, fixes and testing of Solanum are all welcome. Ask for help on the
[nightly Discord](https://discord.gg/pSqgRU3YP9). To add an app to the store, open a pull request on
[Oliv3rmoon/Solanum-Catalog](https://github.com/Oliv3rmoon/Solanum-Catalog) instead.

**Reporting a bug**

- Include your firmware, your HEN and how you loaded it, and which app it was.
- Attach `/data/solanum/logs/solanum.log`, and for an install `updater.log` too. After a crash, the log has a
  `crash:` section with addresses.

**Changing the code**

- Every push and pull request is built by GitHub Actions (`.github/workflows/build.yml`), host checks included. A
  change should build there; `make host-checks` runs the checks on a computer.
- Keep the SPDX header (`GPL-3.0-or-later`) on new source files.
- Solanum installs only the catalog's apps from their own GitHub releases, checked against their checksums. It
  offers no game files and no way to install from any other address; changes that would are not taken.
- Never commit games, apps' release files, keys, `.env`, `build/` or `dist/`.
- Release tags are `MAJOR.MINOR.PATCH` (`0.1.0`), matching `NP_VERSION` in `src/nightshade.h` and the
  `contentVersion` of `sce_sys/param.json` (`00.001.000`). A tag pushed to Oliv3rmoon/Solanum builds and
  publishes the release (`PPSA76526.zip` and `SHA256SUMS`).
