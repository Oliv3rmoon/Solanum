# Changelog

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
