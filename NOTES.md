# Development Notes & Future Roadmap

> **Version:** `v0.8.0` (pre-release beta — re-versioned from `v1.5` language on
> 2026-09-10; `1.x` implied production readiness we do not claim yet).
> App version (`CMake PROJECT VERSION 0.8.0`) is independent of DB schema
> version (`kLatestSchemaVersion = 22`).

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
- ~~No CI or git history hygiene review~~ — DONE: GitHub Actions is live
  (`.github/workflows/ci.yml`, Linux + Windows) and the repo history was
  cleaned up in 2026-10.

### 6.1 Branch flow (Unstable -> Testing -> master)

Three long-lived branches, promoted in that order:

| Branch | Purpose | Who works here |
|---|---|---|
| `Unstable` | alpha development and verification | Beam |
| `Testing` | integration branch, CI-verified | after a review |
| `master` | released code | after a final review |

Promotion is done with a **pull request**, not a direct push, so the
`Promotion` workflow (`.github/workflows/promotion.yml`) builds the
*merge commit* and runs the test suite before the PR can be merged.

> Note: GitHub branch protection and rulesets are **not** available on
> private repos for free plans ("Upgrade to GitHub Pro or make this
> repository public to enable this feature"). The `Promotion` workflow
> check is therefore the only automated gate — do not merge a promotion
> PR without reading its result.

A second worktree exists for `Unstable` so two people can work at the
same time without sharing a checkout:

```
C:/Users/Manuel/Downloads/OpenEkklesia          [Testing]
C:/Users/Manuel/Downloads/OpenEkklesia-unstable [Unstable]
```

`git worktree list` shows them. Each builds independently
(`cmake -S . -B build-windows -DCMAKE_PREFIX_PATH=<qt>`, then
`cmake --build build-windows --config Release`).

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
