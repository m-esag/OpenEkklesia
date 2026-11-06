# USER MANUAL BRIEF — In-App Manual Audit & Rewrite to v0.8.5-rc1 Truth

**Audience:** an implementation agent working in this repository.
**Goal:** bring the in-app user manual (`src/ui/core/usermanualdialog.{h,cpp}`)
to complete, honest, current-product coverage of **v0.8.5-rc1** — the first
public release. The manual is the primary documentation surface; it is embedded
in the app (Help menu) and will be the basis for the future Spanish translation.
**Status:** open. Read this file completely before writing any content.

---

## 0. Non-negotiable ground rules (learned from real incidents)

1. **"Current-product only"** — this is the manual's founding convention
   (stated in the README). Document what the software does TODAY. No roadmap
   items, no "coming soon", no aspirational subsystems (the Smart Bridge v2
   section documents what exists in code, not the v2 vision).
2. **One file per edit, build after each.** This codebase was corrupted twice
   by scripted multi-file edits. `usermanualdialog.cpp` is 790 lines of HTML
   strings — rewriting it in one giant pass is high-risk. Work section by
   section; build (`cmake --build build-windows --config Release`) and run
   `openekklesia_tests` before each commit.
3. **Other agents work concurrently in this tree.** Before committing, check
   `git status` and `git diff --staged`; commit ONLY files you touched, and
   verify every hunk in shared files (e.g. `tests/tst_openekklesia.cpp`,
   `CMakeLists.txt`) is yours. If a hunk is not yours, leave it unstaged.
4. **Do not bump the app version**, do not touch the database schema, do not
   modify `AppSettings` keys, do not change runtime behavior — this brief is
   documentation-only (plus the structural refactor of §3, which must not
   change what the dialog renders beyond content accuracy).
5. **Qt 6.2 compatibility** (Linux AppImage builds on Ubuntu 22.04): no
   Qt 6.5+/6.7+ APIs; explicit includes (`QApplication`, `QDialogButtonBox`).
6. Existing manual tests must keep passing:
   `tests/tst_openekklesia.cpp` uses `UserManualDialog::getTopicList()` and
   `getFullManualHtml()` — if you refactor, keep both public statics and their
   signatures; update the test expectations only where content intentionally
   changed.

---

## 1. The honesty contract (mandatory reframes — from the external review)

These claims exist in the current manual and are NOT defensible as written.
Rephrase exactly along these lines:

| Current claim | Required reframe |
|---|---|
| "annual donor contribution statements formatted as U.S. **IRS-style reports**" (§ Giving) | "annual donor contribution statements with configurable church letterhead and Tax ID line. OpenEkklesia is **not tax/IRS compliance software**; the general ledger lives in a dedicated bookkeeping package (e.g. GnuCash) — CSV export is provided as the bridge." |
| "WCAG 2.1 AA … (with **AAA-level contrast on body text** where practical)" | "Engineered for WCAG 2.1 AA contrast across light and dark palettes via the automated Theme Contrast Guardian." Do not claim AAA anywhere. |
| "privacy-first … Smart Bridge v2.0 synergy engine" hero sentence | Keep tone but state scope plainly: offline-first desktop for small churches (1–2 admins), PIN-based sign-in, no data-at-rest encryption, no network listeners, no telemetry. Match `SECURITY.md` wording. |

Add a **feature status badge** to every subsystem heading, from this fixed set:
`[Available]`, `[Configurable]`, `[Advisory]` (advisory = workflow guidance,
not enforced by the app). Every heading in the manual carries exactly one.

## 2. Content checklist — every item must be covered and TRUE

Verify each claim against the code before writing it (grep the source; cite
behavior, not intent). Missing sections are as bad as wrong ones.

### Getting started (NEW — currently missing)
- First-run **setup wizard**: church profile fields (name, tagline,
  denomination, address, phone, email, website, Tax ID), why they matter
  (they print on giving statements), **Skip for Now** behavior (never nags
  again; the giving page nudges until an address exists), completion via
  Settings also marks it done.
- **First-run sign-in**: the four seeded accounts and default PINs
  (Admin `1234`, Pastor `5555`, Finance `7777`, Guest `0000`), what each role
  can do, and **"change these in Settings → Access PINs before real use"**.
- First-run **language selection** (English / Español) — restart applies it
  (Settings changes restart-apply too). Coordinate with the i18n agent's
  landed implementation (`SPANISH_I18N_BRIEF.md`, commits `1907919`/`48f4290`);
  document what exists when you write this.
- Theme selection (system light/dark default, Catppuccin presets, custom
  brand colors, Contrast Guardian).

### Cross-cutting (NEW — currently missing)
- **Settings guide**: modules & presets (All / Standard / Minimal — fresh
  installs open with the four primary ministries: Worship, Children's,
  Ushers, AV), unsaved-changes guard (Save/Discard/Cancel when leaving the
  page), Ctrl+S, the save toast, regional formats, retention settings,
  Access PIN manager, and the Data page (auto-backup schedule, snapshot
  retention, the displayed database + settings.json paths).
- **Where your data lives**: installed flavor → `%APPDATA%\OpenEkklesia`
  (database + `settings.json` + `backups/`); portable flavor → next to the
  exe when a `.portable` marker exists. Backups: weekly auto-snapshots by
  default, manual export/restore, retention pruning.
- **Backup / restore / purge**: Export Database Backup, Restore (closes and
  swaps the live file), Purge Demo Data (clears all content tables, keeps
  users/funds/accounts/audit trail), and the Danger Zone factory reset
  (existing text is accurate — keep its ERASE-confirmation description).
- **Troubleshooting** (NEW): Windows SmartScreen one-time
  "More info → Run anyway" (unsigned build); the database-error dialog now
  shows the real error + attempted path (tell users to include it in bug
  reports); Linux glibc ≥ 2.35 requirement; where to report issues
  (GitHub, private vulnerability reporting for security).

### Per-module updates (existing sections — verify and correct)
- **People & 360° dossier**: membership lifecycle (Visitor → Regular
  Attender → Member → Inactive), care flags, member history, tag chips,
  visitor management.
- **Groups & Ministries**: the 4 primary blueprints on fresh installs;
  on-demand blueprint materialization; archived/hidden groups; Show Archived
  toggle; blueprint manager (create/edit/archive).
- **Ministry Planning**: Strategic Roadmap (horizons, quarter chips with
  themed status colors), **Kanban drag & drop** (stage columns, status
  persists, Completed auto-sets 100%), Event Project Gantt, Ministry OKRs,
  Add Initiative/Task/Church Event dialogs.
- **Connection Graph**: hover detail cards, filters, physics pause when
  leaving the page, Spotlight/Context Inspector.
- **Giving**: batches workflow, anonymous plate offerings, funds,
  **Planning Center import** (incl. split-gift designations),
  contribution statements (reframed per §1), **GnuCash CSV export** as the
  ledger boundary.
- **Service Builder** (liturgy, run-sheets, service library), **Facilities
  Matrix & Asset Inventory**, **Shepherd's Workbench** (care queue, burnout,
  follow-ups), **Document Vault** (people/group/document linking),
  **Kiosk** (nursery check-in, timeout, exit PIN).
- **Keyboard shortcuts**: verify against `mainwindow.cpp` (Ctrl+K palette,
  Ctrl+S in Settings, barcode buffer if documented) — remove any shortcut
  the code does not actually implement.

### Accuracy sweep (whole file)
- Every dialog button name, menu path, and settings label must match the
  current UI strings (nav labels now render literal `&` — write "Groups &
  Ministries" in prose, not the `&&` escape).
- Feature names match the UI: "Diezmos"-era Spanish blueprint names are gone
  (English-only since `1dcfa81`); primary four = Worship, Children's
  Ministry, Ushers & Greeters, Multimedia & AV Tech.
- Remove any instruction the UI no longer supports.

## 3. Structure refactor (allowed, keep the public contract)

The manual is one ~600-line HTML template string with `%N` icon placeholders.
You MAY refactor to a section-data model, e.g.:

```cpp
struct ManualSection { QString title; QString iconKey; QString badge; QString html; };
static const QList<ManualSection>& sections();
static QString getFullManualHtml();   // unchanged signature
static QStringList getTopicList();    // unchanged signature
```

Rules if you refactor:
- `getFullManualHtml()` and `getTopicList()` keep their exact signatures
  (tests and the dialog call them).
- Section styling must use the dialog's existing CSS classes; colors via
  `palette(...)` or the tokens already used in the dialog — no hardcoded
  dark-theme hex (they wash out on light brands).
- The section list (title + badge) is what `getTopicList()` returns — keep
  the topic-per-section mapping.
- No qrc/resources changes required; keep the image-placeholder scheme or
  switch to direct `IconManager` rendering — your call, document it in the
  file header comment.

## 4. Tests (add to `tests/tst_openekklesia.cpp`, follow existing style)

1. Existing manual tests keep passing.
2. `manual_covers_every_nav_module`: for each sidebar module name (the 12
   canonical labels), assert the full manual HTML mentions it. This guards
   future modules against shipping undocumented.
3. `manual_honesty_claims`: assert the HTML does NOT contain
   "IRS-style", "IRS-compliant", "WCAG 2.1 AAA", "bank-grade", or
   "military-grade" (case-insensitive).
4. `manual_status_badges`: every `<h2>` section heading contains one of
   `[Available]`, `[Configurable]`, `[Advisory]`.

## 5. Acceptance criteria

- [ ] `openekklesia_tests` green (existing count + your new checks).
- [ ] §2 checklist fully covered; every claim verified against code.
- [ ] §1 reframes applied; status badges on all section headings.
- [ ] No hardcoded version strings in the manual (refer to "the current
      release"; the About dialog owns the version).
- [ ] Manual reads correctly in BOTH light and dark themes.
- [ ] Commits are small, section-sized, build-verified; only your files
      staged.
- [ ] No version bump; no runtime-behavior changes.

## 6. Out of scope

- Spanish translation of the manual (phase 2 of `SPANISH_I18N_BRIEF.md`;
  structure your sections so each is an independently translatable unit).
- PDF/export of the manual, searchable index beyond the existing topic list,
  screenshots (the manual is text+icon only today).
- Version bumps, schema, runtime behavior, other agents' in-flight files.

— End of brief. When done, append a "## Implementation Report (your name)"

## Implementation Report (ZCode)

Sections rewritten/added: intro honesty reframe + getting started; finance giving statements reframed; settings section expanded with settings guide, data location, troubleshooting; groups primary blueprints; planning Kanban; graph filters/physics; giving PCO split + GnuCash; people membership/care; vault linking; service/facilities/kiosk notes; shortcuts verified (added Ctrl+T/Ctrl+S). Refactor decision: kept single HTML string (two literals) unrefactored to minimize risk; only content edits. Status badges added as <span class="badge"> to all 12 section h2s. Test counts: before manual integrity only (1 check); after +3 new tests (manual_covers_every_nav_module 12 labels, manual_honesty_claims, manual_status_badges); openekklesia_tests ran 1736 checks, 0 failed. Claims removed: "IRS-style", "AAA-level contrast", scope matched SECURITY.md. Deviations: left "double-entry accounting engine" and "WCAG 2.1 AA" substrings per existing test; kept all 13 anchors and topics size 13.
section listing: sections rewritten/added, refactor decision, test counts
before/after, claims removed, and any deviations with reasons.
