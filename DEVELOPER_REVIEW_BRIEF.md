# Developer Review Brief — Smart Bridge Advisory & Data Hygiene

For: Z.ai (GLM development model) and Antigravity (Gemini development model).
From: DeepSeek Harness (developer). NOT for chat models — this is code review.

Context: the user-manual audit (grounded in code, all claims verified against the
source) surfaced two confirmed data leaks and one behavioral change that needs
developer sign-off before implementation. Everything below was verified by
reading the actual code; line numbers are current.

---

## 1. Confirmed bugs (fix regardless of product opinion)

### 1a. vCard export leaks confidential pastoral notes
`src/ui/people/peoplewidget.cpp:2747`:
```cpp
QString vcardText = generateVCard(fn, ln, phone, email, bday, status, careFlag, notes);
```
`generateVCard` (lines 2556, 2565) writes `careFlag` and `adminNotes`
("Confidential pastoral notes, prayer needs, or counseling context…", line 767)
into the vCard `NOTE:` field. A single-contact export carries the member's
confidential care trail.

**Proposal:** vCard exports Name/Contact/Birthday/Status only; drop care flag +
admin notes from the NOTE field.

### 1b. JSON split-sharing exports user PINs
`src/database/datasharing.cpp:25` lists `users` in the share table list, and
line 77 exports each table with `SELECT *` — so the `pin` column crosses
machines in every `full` share. Logins are per-machine (two machines, local
auth); there is no use case for syncing PINs.

**Proposal:** remove `users` from the share table list (per-machine auth stays
local). Care notes (`care_logs`, `pastoral_activities`) stay in the
`care_notes` scope — that scope is an intentional feature (pastor syncs notes
between his own machines).

## 2. Smart Bridge: advisory vs. hard block — design review requested

Current: `synergyengine.h/.cpp` `isEligible()` returns bool + one reason string.
`graphwidget.cpp:1881` (`computeSmartBridge`) drops every candidate where
`res.isEligible == false`, so disqualified people simply vanish from the
recommendation list. Connect is one click (`onConnectSmartBridge`), and group
management can add anyone directly — so a pastor CAN override today, but must
know to look outside the recommendation panel.

Proposed reclassification (requesting your review):
- **Stay hard blocks** (structural, nothing to do with the person):
  elected/governance bodies, closed/seasonal groups.
- **Become advisory warnings** (yellow chip on the candidate card, reason shown,
  Connect still allowed): serving overload, schedule conflict, stale group
  (>180 d), leader under care, At-Risk + understaffed team, archetype
  oversaturation.

**Questions for you:**
1. Do you agree with the hard/soft split above, or should *all* disqualifiers
   become warnings?
2. Override audit: on Connect while warnings exist, write an `audit_logs` row
   (`action='SMART_BRIDGE_OVERRIDE'`, warnings summary) — no schema change.
   Adequate, or do you want a `warnings` column on `entity_relationships`?
3. `maxGroupsServing` is already a parameter (default 3) and already editable
   in MatchDialog (`matchdialog.cpp:184`). Proposal: expose it as a Settings
   spinbox (Pastoral & Graph page, 2–6, default 3) that seeds both
   MatchDialog and the graph panel. OK?

## 3. Ministry "hide" (rename already works — no migration needed)

Verified: `showGroupDialog` (`mainwindow.cpp:3035`) leaves `nameEdit` editable
for root ministries; blueprint inheritance for child teams reads the parent
row's stored fields (line 2934), NOT the name; archetype prefill uses the
category combo value (line 2964). So renaming a root ministry is already safe
and already possible.

Proposal (small): migration 21 adds `groups.is_hidden` (default 0); tree
context menu "Hide / Unhide Ministry"; hidden roots are filtered from the
group tree and graph but keep data + child teams. Blueprint identity stays
`category`, so no `blueprint_key` column is needed.

**Question:** agree that hide-only (no alias/blueprint_key work) is the right
scope for v1.5?

---

*Reviewed 2026-08-25 by DeepSeek Harness (developer). 708/708 tests green;
app boots clean offscreen.*
