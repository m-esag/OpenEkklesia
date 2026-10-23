# ⛪ OpenEkklesia — Modern Church Management System (ChMS)

> **Version `0.8.0` — pre-release beta.** Offline-first desktop for 1–2 admins.
> No encryption, tax/IRS compliance, or WCAG AAA claims — see in-app manual
> (current-product only) and `NOTES.md` for the roadmap.

OpenEkklesia is a modern, high-performance C++20 / Qt6 desktop software application designed for church administration, congregation management, financial accounting, worship service planning, and pastoral care tracking.

---

## 🏗️ Architecture & Project Structure

The project follows a clean, domain-driven modular structure:

```
OpenEkklesia/
├── CMakeLists.txt              # Primary CMake Build Configuration
├── .clangd                     # Language Server Configuration
├── .gitattributes              # Line-ending normalisation (LF in the repo)
├── README.md                   # Architecture & Developer Documentation
├── NOTES.md                    # Future roadmap & decision log
├── DEVELOPMENT_GUIDELINES.md   # Code conventions every contributor follows
├── .github/workflows/          # CI (Linux + Windows) and the promotion gate
├── tests/                      # Data-layer integration tests (ctest)
└── src/
    ├── main.cpp                # Application Entry Point & Bootstrap
    ├── common/                 # Shared app utilities
    │   ├── appcurrency.h       # Currency symbol helper (Settings → Localization)
    │   ├── appdateformatter.h / .cpp   # Date formatting per the date-format setting
    │   ├── churchprofile.h / .cpp       # Church metadata & PDF letterhead
    │   ├── tableutil.h         # Shared table conventions
    │   ├── givingstatementgenerator.h / .cpp  # Donor contribution statements
    │   └── icalgenerator.h / .cpp        # Shared iCalendar (RFC 5545) writer
    ├── database/               # Persistence & data sharing
    │   ├── databasemanager.h / .cpp   # SQLite, migrations, backup/restore
    │   └── datasharing.h / .cpp       # Scoped export/import ("split sharing")
    ├── integrations/           # Third-party service integrations
    │   ├── planningcenterclient.h / .cpp
    │   └── planningcenterdialog.h / .cpp
    └── ui/                     # Graphical User Interface Modules
        ├── core/               # App shell, navigation, command palette, kiosk, tray & theme engine
        │   ├── mainwindow.h / .cpp
        │   ├── commandpalette.h / .cpp
        │   ├── kioskwindow.h / .cpp
        │   ├── traymanager.h / .cpp     # System tray icon & reminder collection
        │   ├── startuplaunchmanager.h / .cpp  # Start-at-login (Windows/macOS/Linux)
        │   ├── touchhelper.h / .cpp      # Touchscreen & pen detection
        │   └── thememanager.h / .cpp
        ├── people/             # Congregation directory & pastoral analytics
        │   ├── peoplewidget.h / .cpp
        │   └── pastoralanalyticswidget.h / .cpp
        ├── finance/            # Giving, double-entry ledger, transactions & inventory
        │   ├── contributionswidget.h / .cpp
        │   ├── inventorywidget.h / .cpp
        │   └── transactiondialog.h / .cpp
        ├── ministry/           # Service builder, planning, facilities, graph & Shepherd
        │   ├── servicebuilderwidget.h / .cpp
        │   ├── planningwidget.h / .cpp
        │   ├── facilitieswidget.h / .cpp
        │   ├── graphwidget.h / .cpp
        │   └── shepherdwidget.h / .cpp
        └── admin/              # Settings & document vault
            ├── settingswidget.h / .cpp
            └── vaultwidget.h / .cpp
```

---

## 🚀 Building & Running

### Requirements
- **C++ Compiler**: GCC 13+ or Clang 16+ supporting C++20/C++26
- **Build System**: CMake 3.16+
- **Framework**: Qt 6.x (Modules: `Core`, `Widgets`, `Sql`, `Charts`, `PrintSupport`)

### Build Steps

```bash
# 1. Configure the build with CMake
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 2. Compile the executable
cmake --build build

# 3. Launch OpenEkklesia
./build/OpenEkklesia
```

On Windows, `build-windows.ps1` does all of the above plus runs the test
suite, and on Linux `build-linux-appimage.sh` produces the AppImage.

### Running the tests

```bash
ctest --test-dir build --output-on-failure   # or run the test binary directly
```

The suite constructs real widgets, so it needs a display-less Qt platform
plugin: set `QT_QPA_PLATFORM=offscreen` on a headless machine.

---

## 🌿 Branch flow

Three long-lived branches, promoted in this order:

```
Unstable   →   Testing   →   master
 (alpha)      (CI-verified)   (released)
```

Promotion is done with a **pull request**. The `Promotion` workflow builds
the *merge commit* and runs the test suite, and `ci.yml` runs the full
build matrix (Linux + Windows) on every push — read both results before
merging.

> GitHub branch protection is not available on private repositories for free
> plans, so the `Promotion` check is the only automated gate. Treat it as
> mandatory.

See `DEVELOPMENT_GUIDELINES.md` for the conventions to follow and `NOTES.md`
for the roadmap and current release state.

---

## 🗄️ Database Architecture

OpenEkklesia uses an embedded SQLite relational database (WAL mode, foreign-key
enforced, mmap + memory cached) with automatic versioned migrations and weekly
backup snapshots:

- `people` / `households`: Member profiles, household groupings, membership lifecycle (Visitor → Regular Attender → Member → Inactive), spiritual gifts, tags, and contact details.
- `groups`: Ministries, small groups, serving teams, subgroup hierarchy, and meeting metadata.
- `entity_relationships`: Polymorphic connections (spouse, parent/child, household, and group membership/leadership) that power the Connection Graph.
- `accounts` / `transactions` / `splits` / `account_budgets`: Double-entry chart of accounts and fiscal budgets.
- `funds` / `contributions`: Tithes, offerings, fund categories, and payment methods.
- `assets` / `checkout_logs`: Facilities and equipment inventory with serial numbers, conditions, and checkout history.
- `services` / `service_volunteers`: Worship service plans and volunteer scheduling.
- `attendance`: Kiosk self-service check-in ledger.
- `care_logs` / `member_history`: Pastoral care logs with follow-up flags, plus a unified activity/care timeline.
- `ministry_tasks`: Planning initiatives and ministry roadmap tasks.
- `facility_reservations`: Room and equipment bookings.
- `board_motions`: Governance board minutes, motions, and votes.
- `vault_documents`: Document metadata, categories, and meeting records.
- `users`: Sign-in accounts with role-based access (admin, pastor, finance, readonly) and PINs.
- `member_skills`, `graph_layout`, `audit_log`, `schema_version`: Supporting skills, graph node-position cache, audit trail, and migration tracking.

> 🔐 On launch, OpenEkklesia prompts for a user PIN validated against `users`;
> the resolved role drives role-based access restrictions across modules
> (read-only Guest access is also available).

---

## 🎨 Theme Engine

Features a dynamic theme engine (`ThemeManager`) supporting:
- Dark Mode / Modern Themes (Catppuccin Macchiato inspired)
- Light Mode / System Theme Sync
- Dynamic Accent Color Palette
- Persistent Theme State via `QSettings`

---

## 📜 License
Developed with care for OpenEkklesia.
