# LudoShelf

<p align="center">
  <img src="ludo.png" alt="LudoShelf Logo" width="128"/>
</p>

<h3 align="center">Modern, High-Performance Game Library & Emulator Frontend</h3>

<p align="center">
  Built with for <b>Linux</b> with <b>C++20</b> and <b>Qt 6</b> for seamless platform support, fast ROM indexing, automated metadata lookup, multi-provider cover art acquisition, and flexible launch execution. Compiles for Windows with minimal changes.
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#architecture">Architecture</a> •
  <a href="#prerequisites">Prerequisites</a> •
  <a href="#building-from-source">Building</a> •
  <a href="#usage-guide">Usage Guide</a> •
  <a href="#license">License</a>
</p>

---

<img width="1920" height="1014" alt="ludoshelf_ss03" src="https://github.com/user-attachments/assets/6b6ae847-0129-4dd3-867f-a340f8cd6b49" />

---

## Key Features

* 🎮 **System & ROM Management**:
  * Organize games across 100+ console presets with platform-matching metadata keys.
  * Fast multi-threaded directory scanner supporting glob exclusion patterns and real-time filesystem watchers (`QFileSystemWatcher`).
  * Automatic multi-disc ROM detection supporting `.m3u` playlists, `.cue` sheets, and Dreamcast `.gdi` descriptor parsing.

* 🎨 **Automated Cover Art & Media Pipeline**:
  * **Multi-Tier Discovery**: Finds local artwork, reuses RetroArch thumbnail caches, or queries official Libretro repositories.
  * **Smart Cover Scorer**: Evaluates candidates up to 100 points based on resolution, region accuracy, and box art asset type.
  * **Procedural Placeholders**: Automatically generates pastel-themed fallback cover art seeded by game title hash when online art is unavailable.
  * **Content-Addressable Storage (CAS)**: Saves artwork using SHA-256 asset hashes to eliminate duplicate media files on disk.

* 🔍 **Metadata Enrichment & DAT Auditing**:
  * **Libretro Database (RDB) Integration**: Direct binary parser matching ROM CRC32 / SHA1 signatures against official Libretro databases.
  * **Compressed Archive Hashing**: Integrates `libarchive` to calculate hash signatures directly inside `.zip` and `.7z` archives without full extraction.
  * **DAT File Auditor**: Stream-parses Logiqx XML and ClrMamePro DAT files for verifying ROM set integrity against verified redumps.

* 🚀 **Flexible Execution & Emulator Launching**:
  * Native binaries, Flatpak (`flatpak run`), AppImage, Wine, or custom launchers.
  * Argument template expansion supporting placeholders like `{game.path}`, `{game.stem}`, `{system.short_name}`, and `{emulator.program}`.
  * Platform-aware shell escaping and configurable frontend window hide/minimize policies during gameplay.
  * Playtime tracking and session exit logging.

* 🖥️ **Responsive Desktop Interface**:
  * Dual-view library layouts: Customizable **Table View** and **Artwork Grid View** (supporting Portrait & Landscape box art aspects).
  * Fast proxy filtering (`QSortFilterProxyModel`) across titles, developers, systems, and favorite states.
  * Context menus for test launches, file revealing, artwork management, and metadata re-indexing.

* 💾 **Data Portability & Backup**:
  * **SQLite WAL Engine**: High-concurrency Write-Ahead Logging (`PRAGMA journal_mode=WAL;`) with transactional batch operations.
  * **Portable JSON Backups**: Two-way export/import embedding Base64 artwork blobs and integrity checksums for seamless library migration.

---

## Architecture & Technology Stack

| Layer | Technology | Details |
| :--- | :--- | :--- |
| **Core Framework** | **Qt 6 (6.2+)** | `Core`, `Gui`, `Widgets`, `Sql`, `Network`, `Concurrent`, `Test` |
| **Language Standard** | **C++20** | Enforced standard compilation with modern template & container semantics |
| **Persistence** | **SQLite 3** | WAL mode with prepared statements, schema migrations, and crash recovery |
| **Compression & Hashing** | **LibArchive & ZLIB** | Direct hash calculation inside `.zip` / `.7z` archives |
| **Metadata Engine** | **Libretro RDB** | Custom MsgPack binary parser for offline platform DAT databases |
| **Build System** | **CMake 3.16+** | Native cross-platform build support (GCC, Clang, MSVC) |

---

## Prerequisites

Before building LudoShelf, ensure you have installed:

* **C++20 Compatible Compiler** (GCC 10+, Clang 11+, or MSVC 2019+)
* **CMake 3.16** or higher
* **Qt 6.2+** Development Packages (`Core`, `Gui`, `Widgets`, `Sql`, `Network`, `Concurrent`)
* **LibArchive** development libraries
* **ZLIB** development libraries

### Installing Dependencies

#### Ubuntu / Debian:
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools \
                 libarchive-dev zlib1g-dev
```

#### Arch Linux:
```bash
sudo pacman -S base-devel cmake qt6-base libarchive zlib
```

#### macOS (via Homebrew):
```bash
brew install cmake qt@6 libarchive zlib
```

---

## Building from Source

1. **Clone the repository**:
   ```bash
   git clone https://github.com/ludoshelf/ludoshelf.git
   cd ludoshelf
   ```

2. **Configure with CMake**:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ```

3. **Build the executable**:
   ```bash
   cmake --build build -j$(nproc)
   ```

4. **Run LudoShelf**:
   ```bash
   ./build/src/ludoshelf
   ```

5. **Run Unit Tests**:
   ```bash
   ctest --test-dir build --output-on-failure
   ```

---

## Project Structure

```
ludoshelf/
├── CMakeLists.txt              # Root CMake build definition
├── vcpkg.json                  # Optional vcpkg dependency manifest
├── ludo.png                    # Application logo icon
├── src/
│   ├── app/                    # Application paths and JSON backup/restore service
│   ├── covers/                 # Cover art providers, title normalizer, and scoring engine
│   ├── database/               # SQLite database manager, schemas, and transactions
│   ├── dat/                    # Logiqx XML and ClrMamePro DAT parser & hash service
│   ├── domain/                 # Core domain data models (System, Game, GameFile, etc.)
│   ├── launch/                 # Process launch service and argument interpolator
│   ├── media/                  # CAS artwork storage manager & procedural cover generator
│   ├── metadata/               # ROM hashing, coordinator, and Libretro RDB provider
│   ├── models/                 # Qt item models (SystemListModel, GameTableModel, Proxy)
│   ├── resources/              # Qt Resource files (.qrc)
│   ├── scanning/               # Directory scanner, filename parser, & disc group detector
│   └── ui/                     # Qt Widgets UI, MainWindow, dialogs, and delegates
└── tests/                      # QTest automated test suite
```

---

## Usage Guide

### 1. Adding a Gaming System
1. Launch LudoShelf and click **File → Add System / Import Wizard...** (or `Ctrl+N`).
2. Select a preset (e.g. *Nintendo - Super Nintendo Entertainment System*) to automatically fill standard names and platform keys.
3. Select your ROM folder, specify file extensions (e.g. `sfc, zip`), and configure your preferred emulator binary (e.g. `retroarch`).
4. Complete the wizard to start automatic ROM scanning, metadata matching, and cover art resolution.

### 2. Launching Games
* Double-click any game card or table row to launch the configured emulator.
* Right-click a game and select **Test Launch Command...** to inspect expanded argument placeholders before execution.

### 3. DAT Auditing & Integrity Checks
* Select a system and choose **Tools → DAT Audit & Collection Verification...**.
* Load a Logiqx or ClrMamePro `.dat` file to audit your ROM set for missing entries, unverified dumps, or bad hashes.

### 4. Backups & Portability
* Use **File → Export Library to JSON...** to create a single self-contained JSON backup containing all metadata, game records, and embedded Base64 cover art blobs.
* Restore anytime via **File → Import Library from JSON...**.

---

## License

LudoShelf is distributed under the open-source **MIT License**. See `LICENSE` for details.

*Disclaimer: LudoShelf is an open-source library manager and frontend application. It does not include or distribute copyrighted game ROMs, BIOS files, or proprietary emulator cores.*
