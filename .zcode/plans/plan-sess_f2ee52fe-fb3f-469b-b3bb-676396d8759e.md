# Giving Module: Simple Entry + Pastoral Connection (Unified Plan, Phases 1–2)

Implements the agreed synthesis: Plan B's stripped entry form as the chassis, Plan A's pastoral signals as the engine. Tax-statement PDFs are deleted per user decision (Planning Center covers the rare statement need). Deviation from the source document: `payment_method`/`check_number` columns are kept (nullable, PC-import provenance + history) but removed from the entry form — non-destructive.

## Step 0 — Safety net
- `git init`, add `.gitignore` (`build/`, `build_release/`, `AppDir/`, `*.db`, `linuxdeploy-*.AppImage`, `deb_package/`), baseline commit of current state. Makes the tax-code deletion and migration reversible.

## Phase 1 — Simple Sunday entry + anonymous gifts

### 1. Migration 10 (`databasemanager.cpp`, bump `kLatestSchemaVersion` 9→10)
Rebuild `contributions` via create-new/copy/drop/rename in a transaction (SQLite can't relax NOT NULL in place):
- `person_id` → **nullable** (anonymous plate offerings)
- `payment_method` → nullable (relaxed; no longer written by the entry form)
- add `note TEXT`
- keep `check_number`, `external_id`, all else
- recreate the 3 indexes (`idx_contributions_external_id` unique-partial, `_date`, `_person`)
- update `createTables()` fresh-install definition to the same shape; follow existing pragma-guarded migration style

### 2. Entry form strip (`contributionswidget.cpp`)
- Remove Payment Method + Check Number rows, `m_paymentCombo`, `m_checkNumberEdit`, `onPaymentMethodChanged`; clean `applyRole`
- Add optional **Note** field (`m_noteEdit`)
- **Anonymous**: "— Anonymous —" entry in person combo bound to NULL; validation accepts it (typed-name fallback still resolves real names); `member_history`/audit logging skipped for anonymous
- INSERT binds note, omits payment fields
- Delete tax-statement code: `onExportTaxStatements`, `m_btnExportPDF`, `m_progressBar` (+ header decls)

### 3. Nullable-safe displays (4 sites)
- `refreshHistoryModel`: LEFT JOIN people, `COALESCE(name,'— Anonymous —')`, method `COALESCE '—'`, add Note column
- GnuCash CSV per-gift branch: donor 'Anonymous', method `COALESCE '—'`
- `PeopleWidget::updateGivingSummary` recent-gifts rows: method `COALESCE '—'`

## Phase 2 — Pastoral connection

### 4. First-gift milestone (`onSaveContribution`)
Pre-insert `COUNT(*)==0` check → `logMemberHistory(personId, "First Gift", ...)` (named donors only).

### 5. Household giving metric (`peoplewidget.cpp`)
- 4th `makeMetric("Household (with spouse)")` in the giving card (row at :745-747, members :196-199)
- In `updateGivingSummary`: spouse lookup via `entity_relationships` (both directions, `relationship_type='Spouse'`, no reusable helper exists — write the small query); sum both person ids; "—" when no spouse
- Fix existing mislabel: "YTD Total Giving" metric actually sums all-time → rename "Total Giving" (its text is also read by the member-PDF painter at :776 — keep the label populated)

### 6. Shepherd "Giving Care" tab (`shepherdwidget.cpp/.h`)
- New 4th tab mirroring the existing pattern (widget member + setupUI section + `refreshLapsedGivers()` called from `refreshAll()`)
- Query: `membership_status IN ('Member','Regular Attender')` AND gave in ≥3 distinct months in the 6-month window ending 60 days ago AND nothing in the last 60 days
- Double-click → existing `navigateToMember(int)` signal for jump-to-profile

### 7. PC import skip count (`planningcenterdialog.cpp` :252-254)
Append log line when `sum.donationsSkipped > 0` (currently never displayed).

## Verification
- `cmake --build build_release` clean; `ctest` passes
- Migration rehearsal: copy live DB (`~/.local/share/OpenEkklesia/open_ekklesia.db`) to /tmp, apply migration SQL via sqlite3, verify column shape/indexes/row counts (104 people, 4 contributions), re-run = no-op
- Note for user: `datasharing.cpp` generic import writes contributions by explicit column list — unaffected by rebuild

## Out of scope (documented, later)
Deposit batches (their Phase 3), pledges, envelope numbers, recurring-giver detection, `funds.gnu_account` mapping, PCO incremental import.