# Release Notes — OpenEkklesia v0.8.8

**Tag:** `v0.8.8`
**Date:** 2026-11-10
**Status:** Pre-release (post-RC polish). Offline-first desktop for small
churches (1–2 admins). GPL-3.0.

## Highlights since v0.8.5-rc1

### Spanish translation (phase 1 + 2)
- **Español selectable at first run and in Settings** (restart applies it):
  first-run wizard language row with live retranslation, Settings → General
  language selector, and translated main-window chrome, login, dialogs, and
  page headers. 280+ entry dictionary, formal *usted* register
  (`assets/i18n/es.json` ships in all artifacts).

### Rewritten in-app manual
- Complete coverage of the current product: first-run wizard, default PINs
  (with change guidance), settings guide, data locations, backups, Planning
  Center import, troubleshooting. Honesty contract enforced by tests: no
  "IRS-style"/AAA claims; every section carries an
  `[Available]`/`[Configurable]`/`[Advisory]` status badge.

### Security audit fixes (source-only audit, coordinator-scoped)
- **Vault containment**: stored document paths are canonicalized and required
  to stay inside the vault directory on open/export/delete.
- **Restore gate**: backup files must carry a valid `schema_version`
  (1..supported); foreign/crafted SQLite files are rejected before the live
  database is touched.
- **CSV import validation**: malformed email/phone rows skipped and counted;
  in-file and database duplicate detection; unknown statuses normalize to
  Visitor; skip counts reported in the summary.
- **Planning Center pagination cap**: hard 200-page limit per fetch aborts
  cleanly instead of following an unbounded `links.next` chain.
- Plaintext-PIN storage and unsupervised kiosk check-in documented as
  accepted risks in SECURITY.md (coordinator decisions).

### UI polish
- **Control chrome**: themed checkbox/radio indicators, slim themed
  scrollbars, button variant completion (`ledgerEntryBtn`), global touch-target
  minimums, `&`-mnemonic rendering fixes ("Giving_ Batches" → "Giving &
  Batches").
- **Animation-free by policy**: Ctrl+K palette invisibility bug fixed by
  removing the fade (and the written policy prevents regression); instant
  palette display.
- **Adaptive text fitting**: dashboard scrolls at narrow widths (no more
  clipped KPI cards/care panels); KPI labels wrap; People filter row and
  Connection Graph toolbars wrap via FlowLayout; People 360° dossier gains a
  full-width expand toggle; Ministry Planning tables themed; **Kanban drag &
  drop** across stage columns; roadmap selected-cell washout fixed.
- Compact sidebar mode removed (superseded by full sidebar collapse).
- Groups: fresh installs open with the 4 primary ministries (English-only
  blueprint names); Groups tree duplicate-render bug fixed; graph hover-card
  clipping fixed; database-error dialog reports the real error and path.

### Build & packaging
- Single canonical CMake source list shared by app/tests/probes (tests now
  compile the full app surface).
- Version automation: `project(VERSION)` is the single source; binaries embed
  a git `build id`; `scripts/set-version.sh` one-command bumps.
- Installer payload excludes all developer state; portable ZIP verified clean;
  AppImage tooling cached for offline rebuilds.
- ~1,980 automated checks green on Windows (MSVC/Qt 6.8) and Linux
  (GCC/Qt 6.2) at release time.

---

# Release Notes — OpenEkklesia v0.8.5-rc1

**Tag:** `v0.8.5-rc1`
**Date:** 2026-11-05
**Status:** Release candidate. Offline-first desktop for small churches
(1–2 admins). First public release; licensed GPL-3.0.

## Highlights since v0.8.0-beta.1

### Honesty & first-run experience
- **First-run setup wizard** collects the church profile before login; the
  profile prints on giving statements, so no placeholder data ships anymore
  ("123 Grace Ave, Springfield" and friends are gone — blank fields and a
  neutral "My Church" fallback instead).
- Giving page shows a completion nudge until the profile has an address.
- Theme defaults to **system light/dark** on first run (Catppuccin Mocha/Latte).
- Database-error dialog now reports the real `QSqlError` text and the attempted
  database path — no more silent generic failures.

### Resilient settings store
- QSettings is replaced by a **JSON settings store** with atomic writes
  (`QSaveFile`), fixing silent preference loss where the Qt backends fail.

### Groups & ministries
- Fresh installs open with the **four primary ministries** (Worship,
  Children's, Ushers, AV); the other seven blueprints stay in the registry for
  Ministry Match and materialize on demand.
- Blueprint names are **English-only**; existing databases migrate via a
  one-time idempotent rename.
- Fixed: Groups tree duplicated on every refresh (lost `clear()` during the
  bulk-member-fetch rewrite).

### Ministry Planning
- Strategic Roadmap: fixed the selected-cell washout that hid status chips
  (item views now use a translucent accent overlay, app-wide).
- Tables follow the app-wide table conventions; status chip colors derive from
  the active theme.
- **Kanban board: drag & drop** between stage columns, persisted to the
  database (Completed auto-sets progress to 100%).

### Distribution
- **Windows installer** (Inno Setup): machine-wide install, Start Menu
  shortcut with toast-capable AppUserModelID, optional desktop icon and
  auto-start, proper uninstaller. A portable ZIP is built alongside. Neither
  ships developer state.
- Linux AppImage built on a glibc 2.35 baseline (Ubuntu 22.04+ / Fedora / Mint
  21+), SQLite driver bundled.
- One canonical CMake source list shared by the app, tests, and probes — the
  test suite now compiles the full app surface (~1370 checks).

### Known limitations (unchanged by design)
- PIN-based auth only; no data-at-rest encryption; bookkeeping lives in an
  external package (GnuCash) — see `SECURITY.md`.

---

# Release Notes — OpenEkklesia v0.8.0-beta.1

**Tag:** `v0.8.0-beta.1` → commit `0f175a9` (branch `Testing`)
**Date:** 2026-10-16
**Status:** Pre-release beta, single-operator offline-first desktop app.

## What this release contains

- The main-window minimum-size fix (`ed45c10`): the 1000×620 floor is decoupled
  from page content via `IgnoreMinStackedLayout`, eliminating the ~1770px
  minimum-width blowout from dense Groups/Settings pages.
- A mutation-verified regression test (`test_mainWindowMinimumDecoupledFromContent`)
  locking in the floor, the stacked-layout pin, and the bounded
  `minimumSizeHint()` (≤ 1100px). Dead min-size probe rig removed.
- Maintenance carried by the shipped Windows artifact (`d8ce4be`, `dab6570`):
  outcome-accurate restore-migration logging (regression-tested) and a
  tree-wide unused-variable cleanup. No schema or behavior change beyond the
  log messages.

## Shipped artifacts

Naming convention from this release on: `OpenEkklesia-VERSION-SHORTCOMMIT-PLATFORM`.

| Artifact | Status | SHA256 |
|---|---|---|
| `OpenEkklesia-0.8.0-beta.1-0f175a9-x86_64.AppImage` (dist-linux/) | **Shipped** — rebuilt from the tagged commit | `7d9a8c4c8d66a2cd624d48364223c67a1b95fb01d030b9b89cb027506623a9c7` |
| `OpenEkklesia-0.8.0-beta.1-dab6570-win64/` (dist-windows/) | **Shipped** — exe rebuilt from `dab6570` (tag + `d8ce4be` notes/gitignore + `dab6570` maintenance), `windeployqt`-deployed (Qt 6.8.0 msvc2022_64) plus `vc_redist.x64.exe` 14.51.36247.0. Self-contained: launches with no Qt on `PATH` (see Verification). Distribute the folder or its `.zip`; never a bare `.exe`. | exe `571959fad9f046de33702572e38e105974740101babbecc37f84856605083380` |
| `OpenEkklesia-0.8.0-beta.1-dab6570-win64.zip` (dist-windows/) | **Shipped** — distributable zip of the deploy folder above (44,482,285 bytes) | `a78c57dcd1bbbfa07bf2c3b0e1dce823ba9765e19e46c4cee0ee9acb7a16841b` |
| `OpenEkklesia-0f175a9-win64/` (dist-windows/) | Preserved reference (commit-named, never versioned): the tagged commit's exe with its own `windeployqt` payload. Superseded by the `dab6570` folder; known-good Windows fallback. Not a release artifact. | exe `6d721ce8b5524c5aab402bcce880ae2ff1d1c9ae705898851459c1127eb8bcbf` |
| `OpenEkklesia-ed45c10-x86_64.AppImage` (dist-linux/) | Preserved reference build (pre-tagging, never versioned — commit-named only). Not a release artifact. | `95f7f64b96bc42f85e12f2257fa5e2edfd2bcbd92843a732f0e64c6a96517e9c` |

Removed from the tree: the stale Oct 3 `dist/OpenEkklesia-x86_64.AppImage`
(SHA256 `5f0ae19ffc0e50f919843c813d436ff06fe9585115803b911ab8163a5bb9f0e5`) —
superseded by the rebuilt Linux artifact and not shipped.

Windows notes: the shipped Windows artifact is the `windeployqt`-deployed
`OpenEkklesia-0.8.0-beta.1-dab6570-win64/` folder (or its `.zip`) —
self-contained, no Qt installation or `PATH` entry required. A bare `.exe`
without its sibling Qt DLLs will not start on a clean machine; never distribute
it alone. The `dist-windows/OpenEkklesia/` deploy folder (Oct 8) **predates the
minimum-size fix and is not part of this release**; do not ship it.

## Known caveats (read before relying on this build)

1. **Security deferrals (accepted for this beta, documented here):**
   - `users.pin` and Planning Center credentials are stored **in plaintext**
     in QSettings. Accepted for the single-operator, offline-first beta.
   - Planning Center sync shells out to `curl -u appId:secret`, so credentials
     are visible in the process list while a sync runs. It is an optional
     integration; migrating it to QNetworkAccessManager is queued for the
     maintenance batch.
2. **Restore is DB-only.** "Restore from backup" validates and restores the
   SQLite database but does **not** swap back the accompanying `_vault`
   document folder. If documents are missing after a restore, manually restore
   the matching `auto_backup_YYYYMMDD_vault` folder from the same backup date.
3. **Not every backup on disk is restorable — by design.** The restore
   validator rejects files that fail the size/SQLite-header/integrity/
   `schema_version` checks (e.g. the pre-`schema_version` Aug 17 backup and any
   0-byte file). This rejection is intentional; do not promise all on-disk
   backups can be restored.
4. **Downgrade caveat.** Schema is v24; older binaries open a v24 database
   without blocking but silently skip columns they don't know. Edits made on an
   old binary during a downgrade window will not populate v24 columns — avoid
   editing (especially financial) records while downgraded.

## Verification summary

- **Windows:** `ctest -C Release` in `build-windows/` (Qt 6.8.0 msvc2022_64):
  **1078 checks, 0 failed** at `0f175a9`; **1104 checks, 0 failed** at
  `dab6570` (the shipped Windows build), matching Linux exactly.
- **Linux:** fresh build in Ubuntu 22.04 WSL (Qt 6.2.4, GCC 11.4 — same
  baseline as `Dockerfile.build`): **1078 checks, 0 failed**, exact parity.
- **Visual acceptance** at 1000×620 on Groups and Settings: pass, no clipped
  controls; sidebar stays at 220px by default (auto-collapse gates at <940px).
- **Smoke:** Linux AppImage run in a sandboxed fresh profile — fresh DB
  created, migrations 0→24 applied end-to-end, backup scheduler fired.
  Windows: the shipped `dab6570` deploy folder launched with `C:\Qt` stripped
  from `PATH` (`where Qt6Core.dll` → not found): process up, main window
  rendered, **9 Qt6 modules loaded — all 9 resolved from the deploy folder,
  0 from `C:\Qt`**; test instance force-closed afterwards. The launch created
  no new backup (newest `backups/` entries predate the run).

## Rollback

- **Data:** automatic weekly/daily backups (`backups/auto_backup_YYYYMMDD.db` +
  matching `_vault` folder, retention 10) and manual "Backup Database"; restore
  validates, writes a `.pre-restore-*.bak` safety copy, then staged-swaps and
  forward-migrates to v24. Downgrade is soft-compatible (see caveat 4).
- **Process:** `git checkout v0.8.0-beta.1` (or `ed45c10` for the pre-test
  tree) and rebuild; the preserved `OpenEkklesia-ed45c10-x86_64.AppImage` is a
  known-good pre-release fallback binary, and the preserved
  `OpenEkklesia-0f175a9-win64/` folder is the Windows equivalent.

## Not yet verified (beta disclosure)

Live Linux desktop rendering (Wayland/X11, HiDPI scaling), interactive login,
Planning Center sync against the live service, a real restore of the aged v6
backup into the v24 binary, the older-distro glibc compatibility claim
(by construction, not by test), and bare-machine VC++ runtime behavior (the
`vc_redist.x64.exe` shipped in the Windows folder was not install-tested; the
dev machine already has the runtime).
