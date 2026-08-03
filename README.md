# LudoShelf

<p align="center">
  <img src="ludo.png" alt="LudoShelf logo" width="128" />
</p>

<p align="center">
  A Linux-first game-library manager and emulator launcher built with C++20 and Qt 6.
</p>

LudoShelf indexes ROM folders into a local SQLite library, lets you configure
emulator launch profiles, enriches games with Libretro RDB metadata and cover
art, and provides backup, DAT-audit, and media-maintenance tools. It is an
early `0.2.0` project; the README describes the behavior implemented in this
repository, rather than a compatibility promise for future releases.
<p><img width="1920" height="1014" alt="ludoshelf_ss03" src="https://github.com/user-attachments/assets/6b6ae847-0129-4dd3-867f-a340f8cd6b49" /></p>
*Important: LudoShelf does not include or distribute game ROMs, BIOS files, emulator cores,
or other copyrighted game content. Users are responsible for ensuring that
their use of ROMs, emulator software, artwork, and metadata complies with
applicable law and the relevant rights holders' terms.*

## Contents

- [What it does](#what-it-does)
- [Requirements](#requirements)
- [Build, test, and run](#build-test-and-run)
- [Using LudoShelf](#using-ludoshelf)
- [Data, network, and privacy](#data-network-and-privacy)
- [Libretro RDB provisioning](#libretro-rdb-provisioning)
- [Architecture](#architecture)
- [Testing and continuous integration](#testing-and-continuous-integration)
- [Contributing and security](#contributing-and-security)
- [License](#license)

## What it does

### Library management

- Creates systems from a built-in set of platform presets or custom values.
- Scans configured ROM folders in the background, with recursive scanning,
  extension exclusions, filename-pattern exclusions, and optional symlink
  following.
- Watches configured scan roots and debounces follow-up rescans. A completed
  rescan marks files that disappeared from those roots as unavailable.
- Parses display titles and common region markers from filenames.
- Treats `.m3u`, `.cue`, and Dreamcast `.gdi` descriptors as disc entries and
  avoids listing their referenced track files as separate games.
- Provides table and artwork-grid views, plus title/developer search and
  status, region, and genre filters.

### Emulator launching and play history

- Supports native, Flatpak, AppImage, Wine, and custom launch profiles.
- Stores an enabled/default profile per system, ordered argument templates,
  a working directory, `KEY=VALUE` environment entries, output capture,
  detach mode, and window hide/minimize policy.
- Expands placeholders including `{game.path}`, `{game.title}`,
  `{system.short_name}`, and `{emulator.program}`. The **Test Launch
  Command** action shows the prepared command before use.
- Rejects disabled profiles and unavailable primary ROM files before launch.
- Records completed process sessions and play time. Detached launches are
  recorded as started, but cannot report a completion time or duration.

### Metadata, cover art, and ROM verification

- Reads Libretro RDB MessagePack records locally and matches supported ROMs by
  CRC32/SHA-1 and, where appropriate, unique title-and-region lookup.
- Hashes plain ROMs and supported archive members with `libarchive`; canonical
  transforms cover iNES and SMD inputs. CHD cue-sheet data tracks are handled
  through `chdman` when it is available. RVZ and CSO images use title lookup
  rather than exact content identification.
- Discovers cover art from local files and RetroArch thumbnail caches, and can
  fetch Libretro thumbnail candidates over HTTPS. Downloaded image data is
  bounded and decoded with size limits before use.
- Stores artwork as SHA-256-addressed media objects, generates placeholders
  when no art is chosen, and can audit/remove unreferenced managed media.

### DATs, backups, and diagnostics

- Imports Logiqx XML and ClrMamePro DAT files for a selected system and audits
  the collection against their hashes.
- Exports a versioned JSON library snapshot containing database rows and
  embedded managed media objects. Import validates the snapshot and media
  hashes, then replaces the library after the UI creates a database backup.
- Creates SQLite database backups, provides a media-storage audit, and shows
  database, media, and RDB availability diagnostics.

## Requirements

LudoShelf is built with:

| Dependency | Requirement | Purpose |
| --- | --- | --- |
| CMake | 3.16 or newer | Build configuration |
| Compiler | C++20-capable compiler | Application and tests |
| Qt | Qt 6: Core, Gui, Widgets, Sql, Network, Concurrent, Test | Desktop UI and services |
| SQLite | Qt SQLite driver | Local library database |
| libarchive | Development headers and library | Archive hashing and RDB extraction |
| ZLIB | Development headers and library | Metadata hashing support |

`chdman` is optional. It is only needed for exact metadata hashing of CHD
cue-sheet data tracks.

### Dependency installation

Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential cmake ninja-build qt6-base-dev \
  libarchive-dev zlib1g-dev
```

Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base libarchive zlib
```

macOS with Homebrew:

```sh
brew install cmake ninja qt@6 libarchive zlib
```

On Windows, install a Qt 6 desktop kit and use the supplied `vcpkg.json` to
install `libarchive` and `zlib` for the same architecture as that kit. Configure
CMake with the vcpkg toolchain and the Qt kit's prefix as needed by your local
Qt installation.

## Build, test, and run

From the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On macOS, point CMake at Homebrew Qt when it is not found automatically:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
```

The executable is `build/src/ludoshelf` on Unix-like systems. On a headless
environment, run tests with `QT_QPA_PLATFORM=offscreen`.

The build provisions the pinned Libretro database bundle when
`build/src/libretro-database-1.22.1/rdb` is absent. That first configuration
therefore needs network access unless the directory has already been supplied.

## Using LudoShelf

### Add and scan a system

1. Choose **File → Add System / Import Wizard** (`Ctrl+N`).
2. Select a preset or enter a system name and short name.
3. Select a ROM folder and configure the allowed extensions and recursive
   scanning option.
4. Set an emulator program and launch type. The wizard performs its discovery
   preview in the background.
5. Finish the wizard. New games are stored locally; background metadata and
   cover-art work proceeds as available.

Use **File → Rescan Current System ROM Folders** to refresh a system. A full,
successful scan reconciles files that have been removed from its configured
roots; a partial/unavailable root is not treated as evidence that all of its
games were deleted.

Use **File → Edit Current System Properties** to configure excluded extensions,
wildcard exclusions, symlink following, and automatic rescans for a scan root.

### Launch a game

Select a game and use **Play**, double-click it, or open its context menu. Use
**Configure System Emulator Profile** to edit the system profile, including
argument templates. A profile needs an enabled program and an available primary
ROM file before it can launch.

### Enrich and manage artwork

For a selected system, **Tools** provides actions to fetch missing cover art
and refresh ROM metadata. Cover management is also available from a game's
context menu. The application prefers local/user-selected artwork; it can use
local RetroArch thumbnail caches and Libretro thumbnail URLs when requested.

### Audit and back up

- **Tools → DAT Audit & Collection Verification** imports a DAT source for the
  selected system and reports matching/verification results.
- **Tools → Audit Media Storage** lists missing or orphaned managed objects and
  asks for confirmation before deleting orphaned managed files. External legacy
  image paths are not removed automatically.
- **File → Create Database Backup** creates a database backup in the
  application backup directory.
- **File → Export Library to JSON** writes a portable JSON snapshot. **Import
  Library from JSON** replaces existing library records after confirmation and
  a pre-import database backup.

## Data, network, and privacy

By default, LudoShelf stores its database, settings, managed artwork, cache,
backups, and fallback RDB bundle in Qt's writable application-data location.
Portable mode is enabled by a writable `ludoshelf_data` directory or a
`portable.dat` marker beside the executable; in that mode application-managed
data is stored in `ludoshelf_data` next to the executable. The application also
attempts a one-time import from the prior standard application-data location
when appropriate.

ROMs and emulator binaries remain at the paths selected by the user; LudoShelf
does not copy ROM data into its media store. It does not send ROM bytes or the
library database to metadata services. The following optional network actions
do make HTTPS requests:

- A missing RDB bundle is downloaded from GitHub at launch.
- Explicit cover-art retrieval can query GitHub's Libretro thumbnail resources.

Those services and the network path can observe normal request metadata and,
for cover retrieval, collection/title-derived URL paths. Keep this in mind when
using the feature on a privacy-sensitive network.

## Libretro RDB provisioning

LudoShelf uses the Libretro database v1.22.1 for offline metadata lookup. A
packaged install normally places `libretro-database-1.22.1/rdb` beside the
application executable. The metadata provider prefers that bundled copy.

If no usable bundled or application-managed RDB directory exists, startup
continues and LudoShelf downloads the official
[v1.22.1 ZIP](https://github.com/libretro/libretro-database/archive/refs/tags/v1.22.1.zip)
in the background. The downloader:

- streams the archive to temporary application storage;
- verifies its pinned SHA-256 digest;
- applies download, entry-count, archive-path, and extracted-size limits; and
- extracts to staging before replacing the managed RDB directory.

Until that work completes, RDB-backed metadata matching is unavailable; the
rest of the application remains usable. A download failure is shown in the
status bar and can be retried by restarting after network/storage issues are
resolved.

## Architecture

| Area | Main components | Responsibility |
| --- | --- | --- |
| Application state | `app/`, `database/`, SQLite WAL | Paths, migrations, backups, systems, games, files, sessions, and media records |
| Scanning | `scanning/` | Directory traversal, filtering, filename parsing, and disc descriptor grouping |
| Launching | `launch/` | Template expansion, profile validation, process execution, environment handling, and session signals |
| Metadata | `metadata/` | Hashing, RDB parsing/lookup, cache/repository, coordinator, and RDB bootstrapper |
| Covers and media | `covers/`, `media/` | Candidate discovery/scoring, HTTPS acquisition, content-addressed object storage, thumbnails, and cleanup |
| DAT verification | `dat/` | DAT parsing, hash service, and audit dialog support |
| Presentation | `models/`, `ui/` | Qt item models, filters, views, dialogs, and application workflows |

The main application target is defined in `src/CMakeLists.txt`; the root
`CMakeLists.txt` enables Qt code generation and adds the test suite.

## Testing and continuous integration

The current QTest suite covers filename parsing, disc grouping, launch-command
validation, media persistence/reconciliation, cover foundations, ROM metadata,
and JSON library backup behavior. Run it with:

```sh
ctest --test-dir build --output-on-failure
```

GitHub Actions is configured to build and run the test suite on Linux, Windows,
and macOS. It caches and verifies the same pinned Libretro database archive
used by source builds.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) for local validation expectations and
contribution safeguards. Do not commit ROMs, BIOS files, personal library
exports, generated build directories, tokens, or unredacted paths.

See [SECURITY.md](SECURITY.md) for private vulnerability-reporting guidance.
ROMs, archives, DAT files, artwork, metadata sidecars, and imports should be
treated as untrusted input.

## License

LudoShelf is available under the [MIT License](LICENSE).
