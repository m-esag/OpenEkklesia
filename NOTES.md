# Development Notes & Future Roadmap

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
- No CI or git history hygiene review; consider adding GitHub Actions build +
  test once the repo is pushed.
