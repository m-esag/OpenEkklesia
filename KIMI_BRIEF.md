# OpenEkklesia — Insight Request (forward to Kimi)

Short brief for an external architecture/product review. All code is C++20 / Qt 6.10 / SQLite (WAL), offline-first, single-folder desktop app for a church office + pastor. Full context lives in `NOTES.md`; this is the condensed "where we are / where we're stuck" version.

---

## 1. Project posture (so suggestions don't duplicate shipped work)

- **Two users** (church admin + pastor), two machines, no network — data exchanged via **USB file export/import** (selective "share scopes" as JSON: full / membership / care_notes / schedules / finance / governance). A shared/central DB transport is consciously deferred.
- **Roles**: only full-access vs read-only (Guest). Modules can be toggled on/off per install.
- **Recently shipped** (commits f7764af ← 3869d06 ← 990e3d9 ← 73095a7 …):
  - Ministry Match recommendation engine (people→groups: age/gender/skills/serving-load/attendance scoring, per-group declared "match profile").
  - Service Builder Tier 1–3: persistent run sheets tied to dated services, volunteer smart assignment (role→skill ranking), start-time timelines + overrun guard, coverage check, richer PDF, post-service attendance, element usage stats, iCal volunteer export.
  - Group hierarchy (cycle-safe reparenting, drag-drop, graph parent edges).
  - Planning module: edit/status support, relational team/owner links (migration 13), governance share scope.
  - Giving module Phase 1–2: anonymous gifts, import dedupe (`external_id`), schema v13.
- **Headless test harness**: 412 checks, plain asserts, runs the real widgets offscreen.

## 2. Open questions — insight requested

### Q1. Planning module: calendar anchoring (highest priority)
Tasks use an **abstract grid** — `start_day 1–20` (Gantt) and `start_month 1–5` (Roadmap JAN–MAY/JUN) — with **no real calendar anchor**. Consequences:
- "What's slipping" (In-Progress past planned end) and dashboard overdue/upcoming indicators **cannot be computed** against real dates.
- No recurrence (weekly/monthly tasks), no cross-year planning, no tie to the `services` dates.

Options we see:
1. Add real `start_date`/`end_date` columns, derive the grid from them (bigger change, best long-term).
2. Keep the grid but add a "planning period" anchor (e.g., target date = next major event; grid days = relative offsets).
3. Two modes: quick template grid + optional real-date override.

**Ask:** which model best fits a small church ministry roadmap? How would you reconcile "abstract template" vs "dated commitments" without over-engineering? Any prior art (ministry planning, church operations) worth borrowing?

### Q2. Milestone checkpoints
Proposed: child table `task_milestones` (task_id, label, due, done). **Ask:** worth the extra table at this scale, or fold milestones into the task grid (e.g., multiple Gantt bars)? What's the minimum viable checkpoint model?

### Q3. Recommendation engine criteria expansion
Current: age band, gender, skills (member_skills), serving load (group count), 90-day attendance, group's declared match profile. Just added: `groups.recruitment_status` (Open / Elected / Seasonal / Closed) — governance bodies (Board/Deacons/Elders) should be excluded from general suggestion.
**Ask:** what other criteria would materially improve person→ministry fit for a church (spiritual gifts? serving history depth? family dynamics? capacity vs growth targets?)? How should Elected/Closed groups interact with suggestions?

### Q4. Smart-features T1 backlog — prioritization framework
Cheap SQL-only candidates: attendance drop-off alerts, follow-up-due list, birthday/anniversary feed, per-group "who's missing" (roster vs attendance), fuzzy duplicate-person detection.
**Ask:** which 2–3 would you build first and why? Any that should be avoided (e.g., duplicate detection false positives at church scale)?

### Q5. Volunteer scheduling fairness
Current heuristics: rank by role-skill fit, penalize team load + 30-day serve count, block same-day double-booking, warn on same-household scheduling. No no-show tracking, no preferences, no rotation fairness metric.
**Ask:** a lightweight fairness/rotation model (e.g., serve-count balancing, "rest" weeks, preference fields) that fits an offline SQLite app? Or is the current heuristic adequate at this scale?

### Q6. Giving module direction
Shipped: anonymous gifts (nullable person), import dedupe, pastoral signals. Open: year-end tax statements, budget vs actual (funds), the ledger is a separate optional module (defaults OFF; GnuCash workflow).
**Ask:** what's the minimal sensible giving/reporting surface for a small church before the ledger becomes necessary? Any pitfalls in the anonymous-gift model for reconciliation?

### Q7. Two-machine sync (long horizon)
Current: USB JSON scopes. Future options we've discussed: shared SQLite over a file share, proper sync (CRDT-ish), or a lightweight central server.
**Ask:** the most pragmatic evolution from file-export to real sync for a 2-machine, offline-tolerant church office? What's the smallest step that doesn't paint us into a corner?

### Q8. Architecture stance validation
Per `NOTES.md` §2: adopting **Option A (evolutionary)** — keep folders, add `src/models/` + `src/services/`, split giant widgets — and revisiting modular domains only if the feature set grows. 90% of code currently lives in `src/ui/`; business logic (SQL, exports, scoring) is inside widget files.
**Ask:** sanity-check this stance for a 2-user desktop app. Any specific "when to stop tolerating SQL-in-widgets" threshold?

### Q9. Theme engine reuse
The named-token QSS theme engine is shared with an unrelated auto-repair shop app. Decoupling plan in `NOTES.md` §1 (settings-prefix setter, generic brand theme, standalone CMake unit, shared preview/editor widgets).
**Ask:** validate the decoupling priorities — anything that should be done before extraction (e.g., token schema freeze, snapshot tests for the QSS output)?

### Q10. Security posture
PIN auth + plaintext `users.pin` + Planning Center credentials in QSettings are accepted for now (trusted local operators, single desktop each). `NOTES.md` §5 says revisit if multi-user sync or mobile access arrives.
**Ask:** at what trigger do we need to harden (hash PINs, encrypted settings), and is there a zero-effort improvement worth doing now (e.g., salted-hash the PIN during the next schema migration)?

## 3. Constraints to respect in any suggestion
- Qt 6.10 / C++20 / SQLite (WAL, FK on), no cloud, no accounts.
- Two non-technical users; every feature must be understandable from one dialog.
- Thematic: everything ships in the Linux AppImage; small binary, offline.
- Schema migrations are cheap to run now (v13), harder later — but don't add columns "just in case."

## 4. What we'd most value back
1. A direct answer to **Q1** (calendar anchoring) with a concrete recommendation.
2. Prioritized Q3 criteria.
3. Any **landmine** you see in the current architecture or data model that we haven't asked about.
