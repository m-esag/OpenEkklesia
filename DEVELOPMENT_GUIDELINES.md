# OpenEkklesia — Development Guidelines

**Purpose:** A shared, scannable reference for OpenEkklesia's architecture, UI
conventions, theming, testing, and multi-agent coordination, so decisions are
objective across agents and sprints. This documents the *de-facto* system
currently in the code (verified against `src/`, `tests/`, and `CMakeLists.txt`)
**formalized** — it is not a new architecture or design system to adopt.

**Palette source of truth:** `ThemeManager` named tokens (`ThemeTokens`) in
`src/ui/core/thememanager.h/.cpp`; base themes are Catppuccin Mocha (dark) and
Latte (light), plus Warm Espresso / Warm Caramel / Ocean Deep / Sky Azure and
church-brand-generated themes. No per-widget hex is "the system" — tokens are.

---

## 1. Color — semantic tokens (never raw hex for surfaces/text)

All UI color must come from `ThemeManager::instance().currentTokens()`
(`ThemeTokens`). Representative defaults (authoritative values live in
`thememanager.cpp::registerDefaultThemes`):

| Token | Dark (Mocha) | Light (Latte) | Role |
|---|---|---|---|
| `bgBase` | `#181825` | `#eff1f5` | Page background |
| `bgSurface` | `#1e1e2e` | `#e6e9ef` | Card / panel / input surface |
| `bgOverlay` / `bgInput` | darker / surface | darker / surface | Modals, inputs |
| `textPrimary` | `#cdd6f4` | `#4c4f69` | Body / headings |
| `textSecondary` | muted | muted | Labels, hints |
| `textMuted` | muted | muted | Placeholders, disabled |
| `accentPrimary` | `#89b4fa` | `#1e66f5` | Brand / interactive / links |
| `accentSuccess` | green | green | Positive / connected |
| `accentWarning` | `#f9e2af`-family | amber | Warnings, over-time |
| `accentDanger` | `#f38ba8`-family | red | Errors, delete, at-risk |
| `borderColor` | `#313244` | `#bcc0cc` | Borders, dividers |

### Rules
1. **Never hardcode surface/text hex** (`#cdd6f4`, `#a6adc8`, `#1e1e2e`, …) in
   new code — the app-wide sweep to tokens is already done; don't reintroduce.
   Semantic accents (danger/warning/success) may use the theme's fixed accent
   colors where they must read identically in both palettes.
2. In QSS strings prefer **palette-relative** values: `palette(text)`,
   `palette(base)`, `palette(mid)`, `palette(highlight)`,
   `palette(highlighted-text)`, `palette(placeholderText)` — they auto-adapt to
   light/dark. Use token hex only for things `palette()` can't express.
3. Text on filled accents uses `textOn(t.accentPrimary)` (computed by the theme
   engine), not a hardcoded `#11111b`/white guess.
4. Every new surface/text pairing must be checked in **both dark and light**
   (theme gallery) — the WCAG contrast guardian
   (`ThemeManager::contrastRatio`) enforces the AA target; do not ship a pair
   it flags.
5. `ThemeTokens.borderRadiusSm = 4`, `borderRadiusMd = 8`; `fontSizePx = 13`.

---

## 2. Typography
- **Family:** Qt default stack — no custom font is bundled.
- **Body:** `font-size: 13px` (token default). Compact/labels: `11px`.
  Micro-labels/eyebrows: `10px`. Dense tables: `11–12px`.
- **Headers:** page headers `20–26px` bold; section headers `14–16px` bold;
  card titles `13–14px` bold.
- **Weights:** `font-weight: bold` for titles/headers and `500` for buttons;
  avoid light weights (contrast).
- Dates via `AppDateFormatter::formatDate()` (respects the `dateFormat`
  setting) — never format dates ad hoc.

---

## 3. Buttons & controls — objectName catalog

The QSS engine styles by object name. Use these names — never invent a new
"button style" without reusing one:

| objectName | Use |
|---|---|
| `primaryBtn` | Primary action (Add / Save / Export main) — filled accent |
| `saveBtn` | Save — same treatment as `primaryBtn` |
| `secondaryBtn` | Secondary action — surface fill |
| `dangerBtn` | Destructive (Delete / Purge) — danger fill |
| `ghostDangerBtn` | Quiet destructive (Remove) |
| `accentBtnCyan` / `accentBtnOrange` | Specialized accents (rare) |
| `navButton` | Sidebar / module navigation |
| `cardWidget` | Card / panel surface |
| `pageHeader` / `detailHeader` | Page and detail-panel titles |
| `searchEdit` | Search field |

### Rules
1. Every interactive control gets a **hover** state and a **tooltip** when
   icon-only (`setToolTip`).
2. Icon buttons use `IconManager::instance().getIcon(name, color, size)` —
   Lucide set; **14px** for buttons/rows, **16px** for headers/tree items.
3. **No emoji for functional icons** — emoji is decorative only (badges, ⚡✨
   labels). Functional affordances are Lucide icons.
4. Read-only (Guest) mode: disable the control and set the standard tooltip
   `"Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."`
5. Destructive actions confirm with `QMessageBox::question` before executing.

---

## 4. Layout & spacing
- Page content margins: `30px` (`setContentsMargins(30,30,30,30)`), spacing
  `12–15px` between sections.
- Forms: `QFormLayout` with `addRow("Label:", field)`; label text in sentence
  case with trailing colon.
- Cards: `QFrame` with `setObjectName("cardWidget")`; inner margins `12px`,
  spacing `8–10px`.
- Toolbars/header rows: `QHBoxLayout`, `addStretch()` before action buttons.
- Tables: `QTableWidget`/`QTableView` with `verticalHeader()->setVisible(false)`,
  alternating row colors, `QHeaderView::Stretch` on the primary column.

---

## 5. Dark / light theming
- Themes: named token sets in `ThemeManager`; `isDark` flag; `system_auto`
  follows the OS; users can customize colors (persisted) and generate a
  church-brand theme from a brand color.
- Widgets that paint themselves (graph, custom `paintEvent`) must re-read
  tokens on `ThemeManager::instance().themeChanged` and restyle chrome
  (see `GraphWidget::restyleChrome` pattern).
- QSS built by `generateGlobalQSS()` from tokens — do not append a second
  stylesheet with hardcoded colors that bypasses tokens.

---

## 6. Architecture & data conventions
- Module layout: `src/ui/<domain>/` (core, people, ministry, finance, admin),
  `src/database/`, `src/common/` (shared, theme-neutral helpers),
  `src/integrations/` (PCO).
- DB access: `DatabaseManager::instance().database()`; singleton in
  `src/database/databasemanager.cpp`. SQLite **WAL**, `PRAGMA foreign_keys=ON`,
  `date('now','localtime')` for today.
- **Prepared statements with bound values** (`prepare` + `addBindValue`) — never
  concatenate user input into SQL. (Known legacy violations exist in a few
  delete paths; do not add new ones.)
- Schema changes go through **numbered migrations** 1–24 in `migrateSchema()`;
  bump `kLatestSchemaVersion` (currently 24 — always re-grep before adding) and
  add the next number. Fresh DBs run every migration from 0. `ADD COLUMN` must
  use constant defaults (no non-constant `DEFAULT`).
- `entity_relationships` is the polymorphic connective tissue
  (`source_type/source_id → target_type/target_id`, free-text
  `relationship_type`); tag automated links with `connection_source`
  (`'smart_bridge'`, `'ministry_match'`).
- New shared helpers that must stay product-neutral (themes, formatting,
  letterhead) go in `src/common/` — the theme engine is reused by the
  auto-repair project (see `NOTES.md` roadmap).

---

## 7. Testing (tests/tst_openekklesia.cpp)
- Plain `CHECK` / `CHECK_EQ` harness (no QtTest). One kebab-case test name per
  scenario, registered in the `tests[]` table in `main()`.
- Tests **seed their own data** after the harness purges demo data: people
  ids `9001+`, groups `9101+`, households `9201+`, assets `9301+`. Never rely
  on demo rows or on another test's leftovers.
- The harness `chdir`s to `/tmp/oe_test_work` so legacy-DB adoption never
  copies the workspace `open_ekklesia.db`; run from a clean dir with
  `XDG_DATA_HOME=$PWD/smoke-data`.
- Keep the suite green before committing (`build/openekklesia_tests`):
  **currently 700+ checks, must stay 0 failed.**
- The manual integrity test (`user_manual_integrity`) asserts the in-app manual
  matches reality — the manual is *current-product only*; update the test when
  the manual changes, never weaken it to hide drift.
- Tests must tolerate QSettings-configurable values (duration thresholds,
  `smartBridgeMinScore`, …) — assert behavior, not the default numbers.

---

## 8. Multi-agent coordination (how we share the repo)
- **File ownership:** Gemini owns People + Smart Bridge/Giving/Vault work.
  Do not edit their in-flight files (`peoplewidget.*`, `vaultwidget.*`,
  `commandpalette.*`, `inventorywidget.*`, `facilitieswidget.*`,
  `planningwidget.*`, `mainwindow.h`, `tests/`, `CMakeLists.txt`) while they
  are mid-edit — coordinate first, then commit shared work with clear messages.
- **Never commit:** `tools/`, `dist/` build artifacts (AppImage rebuilds show
  as modified — leave unstaged), `squashfs-root/`, `*.db`, backup files,
  swap files (`.kate-swp`).
- **Schema version:** always re-grep `kLatestSchemaVersion` before adding a
  migration — parallel agents bump it.
- **AppImage:** after any UI change, rebuild
  (`./build-linux-appimage.sh`) so the user's test binary matches; verify with
  an offscreen boot (`APPIMAGE_EXTRACT_AND_RUN=1 … -platform offscreen`,
  expect exit 124 = healthy timeout).
- **Honesty rules:** no claims of encryption, tax/IRS "compliance", WCAG AAA,
  or features that don't exist. Manual, tooltips, and README must match code;
  aspirational items live in `NOTES.md`/ROADMAP-style docs, never in the manual.

---

## 9. Known inconsistencies (flagged, not yet fixed)
- A few `DELETE` paths still concatenate numeric ids into SQL instead of
  binding parameters (`mainwindow.cpp` delete-group path) — do not copy this
  pattern.
- Legacy QSettings keys (`pcoAppId` vs `pco/appId`) have fallback readers; new
  keys are the canonical dotted ones.
- Some inline styles still carry raw accent hex (`#89b4fa`, `#f38ba8`) for
  semantic accents — acceptable only for fixed accents, not surfaces/text.

---

*Reference doc — update it alongside any intentional convention change.
Questions on a specific value should be resolved against the code, not the prose.*
