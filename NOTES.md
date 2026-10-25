# Development Notes & Future Roadmap

> **Version:** `v0.8.0` (pre-release beta — re-versioned from `v1.5` language on
> 2026-09-10; `1.x` implied production readiness we do not claim yet).
> App version (`CMake PROJECT VERSION 0.8.0`) is independent of DB schema
> version (`kLatestSchemaVersion = 24`).

Status notes for future development sessions. Items here are intentionally
**not** part of the current sprint; they are reminders and decision points.

---

## 1. Theme engine — extract into a shared module (both products use it)

`src/ui/core/thememanager.*` is currently embedded in OpenEkklesia and is also
used by an unrelated **auto-repair shop** application. Before sharing it safely:

- [ ] **Decouple product-specific coupling**:
  - `QSettings("OpenEkklesia", "CHMS")` is hard-coded in 6+ places. Add a
    `ThemeManager::configure(orgName, appName)` or settings-prefix setter so
    each app uses its own settings scope.
  - `generateChurchBrandTheme()` / "Church Brand Theme" naming is product-
    specific; rename to a generic `generateBrandTheme(color, darkBase, label)`.
- [ ] **Extract as a reusable unit**: standalone folder + CMake interface/static
  target (or vendored copy), versioned independently, zero references to
  "OpenEkklesia"/"church"/"CHMS" inside the engine.
- [ ] **Ship the UI pieces as engine widgets** (currently hand-built inside
  OpenEkklesia Settings): `ThemePreviewWidget`, `ThemePickerWidget` (card
  gallery), `ThemeEditorWidget` (swatches + radius + font).
- [ ] **Complete the QSS template**: QScrollBar, QSplitter::handle, QToolTip,
  QProgressBar, QCalendarWidget popup, QDialogButtonBox, QMenu separators,
  disabled states for every control.
- [ ] **Color derivation helpers**: `tokens.hover()/pressed()/disabled()` and a
  WCAG-contrast `textOn(color)` helper; consider extra semantic tokens
  (accentInfo, translucent success/danger backgrounds, fontFamily token).
- [ ] **Tests**: token→QSS generation, contrast math, JSON theme export/import
  round-trip, font-size persistence, system-auto resolution.

Already done: font-size persistence, system-auto valid colors, theme ordering,
persisted color customization (startup restore), QComboBox dropdown + QMenu QSS,
palette roles (PlaceholderText/Link/Disabled), and routing most hard-coded
button styles through a consistent button taxonomy (primary/secondary/save/
danger/ghost/accent variants/nav/checkin/unlock).

## 2. Architecture — decision pending (Gemini's review)

Gemini's analysis of the current structure is broadly correct:

- Strengths: clean `src/` root, domain-based UI folders, explicit CMake.
- Pain points: ~90% of code in `src/ui/`, business logic (SQL, double-entry,
  CSV/PDF export, care scoring, graph physics) lives inside widget .cpp files,
  large monolithic files (`mainwindow.cpp` ~110 KB), and there is **no
  model/repository layer** — entities like Person/Group/Transaction are raw SQL.

Three options were proposed:

- **Option A — Evolutionary** (recommended for now): keep the folder layout,
  add `src/models/` (typed structs) + `src/services/` (ExportService,
  CareAnalyticsEngine, BackupService), and split the giant widgets into smaller
  dialogs/components.
- **Option B — Modular domains** (best for long-term): co-locate UI + logic +
  queries per feature (`modules/congregation`, `modules/finance`, ...).
- **Option C — Strict MVC/MVP**: separate `models/`, `services/`, `ui/` layers.

**Current stance:** adopt **Option A** incrementally as features are touched
(move big query blocks into services, introduce typed records), and revisit
**Option B** if/when the feature set grows substantially (multi-user sync,
expanded reports, mobile/API). Do not do a big-bang restructure.

## 3. Dead / pending settings (stored but not yet wired)

- `churchDenomination`, `churchAddress` (partially wired into tax statements),
  `churchPhone`, `churchEmail`, `churchWebsite`, `churchTaxId` — should appear
  on the printed directory and vault report headers.
- `dateFormat`, `startOfWeek`, `timezone` — stored, not applied app-wide.
- `followUpDays` — stored; the Shepherd queue uses follow-up dates but does not
  yet auto-suggest the configured window.
- `churchLogo` — stored + previewed; not yet rendered on printed documents.
- `fiscalYearStart` was **removed** (was dead); calendar-year YTD is current.
  A real July–June fiscal year would require budget-schema changes — decide
  whether to add it.

## 4. Shepherd / pastoral module backlog

- Registries still to add: sacraments log, sermons log, discipleship
  relationships (mentor/mentee + milestones), outreach tracking.
- Prayer list is present; consider structured request/answer + due reminders.
- "Lead by example" (pastor accountability): deliberately NOT a tracker —
  private personal notes only.

## 5. Security (deferred by design decision)

- PIN auth + plaintext `users.pin` and Planning Center credentials in QSettings
  are accepted for now (single local operator machines). Revisit if multi-user
  network sync or mobile access is added.
- Planning Center sync shells out to `curl` with `-u appId:secret` (credentials
  briefly visible in `ps`). Replace with `QNetworkAccessManager` (HTTP Basic
  Auth header) once the `qt6-network-devel` package is available — the runtime
  lib is installed but the headers/cmake config are not.

## 6. Misc / polish backlog

- Command palette (Ctrl+K) is not module-aware (disabled modules still appear).
- A few hard-coded colors remain in the graph side panels (legend, details,
  roster) — migrate to tokens when convenient.
- `-Wconversion` warnings under strict audit flags (cosmetic).
- ~~No CI or git history hygiene review~~ — **DONE (2026-10)**: GitHub Actions
  builds and tests on Linux + Windows, and the repository history was purged
  of build artifacts. See §6.1.

### 6.1 Branch flow and release state (2026-10)

Three long-lived branches, promoted in this order:

| Branch | Purpose | Who works here |
|---|---|---|
| `Unstable` | alpha development and verification | Beam |
| `Testing` | integration branch, CI-verified | after a review |
| `master` | released code | after a final review |

Promotion is done with a **pull request**, not a direct push. The
`Promotion` workflow (`.github/workflows/promotion.yml`) builds the *merge
commit* and runs the test suite before the PR can be merged; `ci.yml`
runs the full matrix (Linux + Windows) on every push.

> Branch protection and rulesets are **not** available on private repos for
> free plans ("Upgrade to GitHub Pro or make this repository public"), so the
> `Promotion` workflow check is the only automated gate. Read its result
> before merging a promotion PR.

All three branches currently point at the same commit, and the tree is
identical across them.

### 6.2 Shipped in 2026-10 (so you do not re-investigate it)

- **System tray + reminders** (`src/ui/core/traymanager.*`). A
  `QSystemTrayIcon` with a context menu (Show, Refresh, enable toggle,
  Quit) and a 15-minute refresh timer. Five independently switchable
  scopes in QSettings under `tray/`: `services`, `ministryEvents`,
  `ministryTasks`, `facilityReservations`, `pastoralFollowUps` — the
  pastoral scope is **off by default** because care data is sensitive.
  Notifications are deduplicated per `type:id:dueAt` so repeated timer
  cycles never re-toast. On Windows the AppUserModelID is set so the
  Action Center accepts the notification; the installer's Start Menu shortcut
  carries that ID, so installed builds get native toasts, while a loose
  (unshortcutted) EXE still falls back to a balloon.
- **Start at login** (`src/ui/core/startuplaunchmanager.*`) using the
  platform-native mechanism: `HKCU\...\Run` on Windows, a LaunchAgent
  plist on macOS, an XDG autostart `.desktop` on Linux.
- **Restore hardening.** `restoreFromBackup()` validates the candidate
  file (SQLite magic header, `PRAGMA integrity_check`, a `schema_version`
  table) *before* touching the live database, keeps a
  `.pre-restore-YYYYMMDD.bak` safety copy, re-applies the connection
  pragmas on reopen, and runs `migrateSchema()` so an older backup is
  brought forward.
- **Portable data directory.** `OPENEKKLESIA_DATA_DIR` (a directory) or a
  `.portable` marker next to the binary keeps the database beside the
  launch binary instead of in the XDG user-data location — for USB
  deployments.
- **iCalendar.** `src/common/icalgenerator.*` is the single RFC 5545 writer:
  UTF-8-safe 75-octet folding, escaping, stable per-event UIDs, all-day and
  overnight handling. The Service Builder, Shepherd and Facilities modules all
  delegate to `eventIcal()` / `volunteerServiceIcal()` (Facilities adds a
  15-minute display alarm). Covered by `ical_generator_rfc_compliance`.
- **History retention pruning.** `DatabaseManager::pruneHistory(auditKeepDays,
  historyKeepDays)` deletes `audit_log` / `member_history` rows older than the
  given windows in one transaction (UTC cutoffs, `<= 0` = keep forever).
  `initialize()` runs it only for an existing database, after migrations and
  the auto-backup snapshot, reading QSettings `auditLogRetentionDays` (365) and
  `memberHistoryRetentionDays` (730). Both are editable on Settings → Data,
  Backup & Integrations (0 renders as “Keep forever”, admin-only).
- **Double-entry ledger entry.** Finance → Contributions → “New Ledger
  Transaction” (`ledgerEntryBtn`) opens `TransactionDialog`; saves go through
  `TransactionDialog::saveTransaction()` which rejects an empty description,
  fewer than two splits, unknown accounts and any unbalanced total, then writes
  the `transactions` row plus its `splits` in one checked transaction. Success
  emits `ContributionsWidget::ledgerChanged()`, which MainWindow connects to
  `updateDashboardStats()` so the “Total Assets” KPI (SUM of ASSET-account
  splits) refreshes immediately. Guest/read-only roles get a disabled button.
  Covered by `ledger_entry_and_total_assets`.
- **Unsaved-edit prompts beyond person switching.** The People module now also
  prompts on search/status/pathway filter changes and on programmatic
  drill-through from analytics/dashboard (Save/Discard/Cancel); Cancel restores
  the previous filter widgets with blocked signals and aborts the navigation.
  Covered by `people_confirm_discard_edits`.
- **Planning Center designation → fund mapping.** The Giving sync requests
  `include=designations,funds` (one retry without `include` if the live API
  rejects it), parses the to-many designation relationship plus the sideloaded
  `Designation`/`Fund` names, and maps each imported gift to a real fund:
  existing `funds.fund_name` matched case-insensitively, unknown non-empty
  designations create a fund, and only genuinely unknown gifts fall back to
  "Imported (Unmapped)". Re-syncs stay idempotent (external-id dedupe, no
  duplicate funds). Covered by `pco_donation_designation_parsing` and
  `pco_giving_import_designation_funds`.
- **Windows installer + portable ZIP.** `build-windows-installer.ps1` validates
  the deployed Release tree and drives Inno Setup 6 (`installer/OpenEkklesia.iss`,
  exit codes 0/1/2/3); `-Portable` emits a ZIP, `-InstallTool` can fetch the
  compiler. The installer creates Start Menu/desktop shortcuts carrying the
  `OpenEkklesia.ChMS` AppUserModelID, an uninstall entry, and ships the whole
  deployed runtime (tests/benchmarks/PDBs excluded). Icon assets are generated
  by `installer/make-icon.py` into `assets/openekklesia.ico`.
- **Hermetic test settings.** `src/common/appsettings.*` is the single
  preference entry point; `OPENEKKLESIA_SETTINGS_DIR` redirects it to a scratch
  INI store. The suite now never reads or writes the developer's real registry,
  so a saved graph filter (e.g. "Isolated") can no longer turn the graph layout
  tests red, and `graph_filter_persistence` verifies real save/restore without
  polluting the user profile. The test/benchmark harness also falls back from
  the OS temp dir to a writable scratch directory when a sandboxed launcher
  denies temp writes.
- **Sanitizer verification (2026-10, MSVC ASan+UBSan).** The full suite was
  rebuilt and run under `-DOE_ENABLE_SANITIZERS=ON`
  (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`); the CI `asan` job covers the
  Linux GCC path on every push.
- **CI sanitizer job.** `ci.yml` has an `asan` job (Ubuntu GCC, ASan + UBSan,
  `-DOE_ENABLE_SANITIZERS=ON`) building and running the suite with
  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, in addition to the local
  `OE_ENABLE_SANITIZERS` option.
- **Qt 6.4 / MSVC 14.44 portability.** The `QCheckBox::checkStateChanged`
  API (Qt 6.7+) is used through a `QT_VERSION`-guarded helper with a
  `stateChanged(int)` fallback; the in-app manual was split across two string
  literals because it exceeded the 16,384 character limit for a single literal.
- **Repository hygiene.** Build outputs are no longer tracked (the history
  was purged), `.gitattributes` normalises line endings so a Windows edit
  no longer rewrites every line of a file, and the test suite fails fast
  on the first failing `CHECK` instead of letting follow-on failures pile
  up.

### 6.3 Still open

- **Installer setup EXE not built in this environment.** Inno Setup 6 is not
  installed here, so `build-windows-installer.ps1` was verified in `-Portable`
  mode (ZIP contents, deployed runtime); run the script once on a machine with
  Inno Setup to produce and smoke-test
  `dist\OpenEkklesia-0.8.0-win64-setup.exe`.
- **Hard-coded seed identities.** The seeded admin/pastor/finance/readonly
  users and the demo household ids are fixed product seed data; don't build
  assumptions about "household 1" into new features.
- A native Windows toast (Start Menu shortcut carrying the AppUserModelID).
- macOS notification delivery needs a signed bundle.
- `QSystemTrayIcon` cannot be compiled into the offscreen test target on
  every toolchain; the tray's *queries* are the testable part, and
  `collectReminders()` is public for that reason.


## 7. Smart features roadmap (agreed 2025 direction)

### 7.1 "Ministry Match" — recommendation engine (BUILT, commit 7b3dc6e)
Prompt-driven person→group recommendations. All criteria below already exist
in the schema; no new tables required for v1.

- Age band from `people.date_of_birth` (e.g. Youth 13–25, Seniors 60+).
- `people.gender`, `membership_status`, `care_flag` filters.
- Skills from `member_skills` mapped per-group (e.g. "music"→Worship Team,
  "teaching"→Sunday School); a group can declare required skills.
- Serving history: Person→Group `entity_relationships` roles + `service_volunteers`
  (last served, count) → burnout guard reusing graph burnout threshold
  (QSettings `burnoutThreshold`), plus not-already-in-group exclusion.
- `groups.max_capacity` vs current roster → skip full groups.
- Engagement: `attendance` count over last 90 days as a match booster.
- Scoring: weighted sum → match %, with human-readable "why" reasons.
- UI: "✨ Suggest People" button on the group detail panel → dialog with
  criterion knobs → ranked table (match %, color-coded, reasons) → one-click
  "Add selected" (dedup-safe bulk insert). Implemented in
  src/ui/ministry/ministrymatch.{h,cpp} + matchdialog.{h,cpp}; headless
  scoring is unit-tested (+33 checks).

### 7.2 Other smart features (priority order)
- T1 (SQL-only, cheap): engagement drop-off alerts; follow-up-due list
  (`pastoral_activities.follow_up_date`); birthday/anniversary feed;
  per-group "who's missing" (roster vs attendance); fuzzy duplicate detection.
- T2 (moderate): volunteer-gap matching (roles needed vs skills); composite
  group health score (burnout + family dominance + capacity + attendance);
  Visitor→Attender→Member pathway nudges; giving year-over-year deltas.
- T3 (heavier, later): churn-risk flags from attendance decline; external
  SMS/email (out of scope for offline builds).

### 7.3 Built this round (commit 5237262)
- Cycle-safe group parent selector + drag-drop reparenting (groups page).
- Graph: Group→Group "Parent of" dashed edges; category-colored group nodes;
  larger group hubs scaling with member count; clustered layout nests
  subgroups around their parent.
