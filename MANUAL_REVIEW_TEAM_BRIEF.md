# OpenEkklesia User-Manual Review — Team Brief

Source: an external reviewer (ChatGPT) read the in-app user manual and flagged
claims that overpromise or need explicit product decisions. Before reading,
know that the manual was already corrected in parallel: the vault is NOT
"encrypted" (claim removed), the RBAC table now lists the real roles
(Admin/Pastor/Finance/Guest, not Ministry Leader/Volunteer/Auditor), the
Planning chapter now describes the actual Roadmap+Gantt tool, graph
"simulation sliders" and facilities "warranty/repair logs" claims were removed,
and compliance wording was softened. The questions below are the remaining
genuine product decisions.

---

## Q1. Should Smart Bridge ever make a final eligibility decision?
**Today:** the engine hard-excludes candidates from *recommendations*
(3+ serving groups, schedule clash, elected/governance body, closed/seasonal
group, stale group >180 days, leader under pastoral care, At-Risk candidate +
understaffed team). It never edits rosters — every card shows score + reason,
and a pastor can add anyone directly via group management.
**Options:**
- **(a) Keep hard gates, keep advisory framing** (current) — simple, engine
  only suggests.
- **(b) Soften sensitive gates to "requires review"** — e.g. At-Risk candidates
  appear with a warning instead of vanishing. More humane, more UI work
  (review queue, override logging).

**Ask:** are the hard disqualifications OK as-is, or should any of them become
warnings that a pastor can consciously override?

## Q2. Should pastoral-care notes be hidden from *some* administrators?
**Today:** Admin and Pastor both have full read/write on care notes; Guest is
read-only. There is no third tier, and no "who viewed this note" audit.
**Options:**
- **(a) Keep one pastoral tier** (current) — church trusts its admins.
- **(b) Split administration from pastoral access** — e.g. a "church
  administrator" role that manages users/backups but cannot open care notes.

**Ask:** should pastoral confidentiality be a separate permission category, or
is the current Admin/Pastor/Finance/Guest model enough for your church?

## Q3. Are backups and JSON exports encrypted by default?
**Today:** SQLite snapshots, vault-folder copies, and the USB/floppy JSON
archives are all plain text. Encryption was explicitly out of scope for v1
(same as PIN security).
**Options:**
- **(a) Plain-text with a clear warning** on export (current).
- **(b) Optional password encryption** on DB export + JSON archives — moderate
  work (AES via QCryptographicHash/QtSsl or OpenSSL), key-recovery story
  needed ("what if the admin forgets the password").
- **(c) Encrypt by default** — strongest, but friction on every USB share.

**Ask:** is plain-text OK for the first release, or must backups/shares be
encrypted before you'd trust them?

## Q4. Serving-overload gate: hard "3 groups" or configurable?
**Today:** ≥3 active serving groups = barred from recommendations (constant in
the engine, `maxGroupsServing = 3`).
**Options:**
- **(a) Keep 3 as a hard gate** (current).
- **(b) Make it a setting** (e.g. 2–6, default 3) — one settings field, small
  wiring job.
- **(c) Make it a warning, not a gate.**

**Ask:** keep, configure, or warn?

## Q5. Are the 11 canonical ministries too rigid?
**Today:** 11 system blueprints exist as permanent, locked root nodes; child
teams inherit the blueprint; roots cannot be deleted/archived (historical data
depends on them).
**Options:**
- **(a) Keep locked** (current) — predictable structure, protects history.
- **(b) Allow rename/deactivate but not delete** — rename is trivial, but
  blueprint inheritance + archetype references assume canonical names.

**Ask:** do you ever need to rename or retire a top-level ministry?

## Q6. Should Ctrl+Q confirm before exiting?
**Today:** Ctrl+Q exits immediately; all state (graph positions, run sheets,
settings) already auto-saves, so nothing is lost — the risk is purely an
accidental close during a live service/meeting.
**Options:**
- **(a) Exit immediately** (current).
- **(b) Confirm on exit** unless the window was cleanly closed.

**Ask:** worth a confirm dialog?

## Q7. Scope of the accounting "compliance" story (info only)
Already softened in the manual ("full double-entry accounting engine",
"U.S. IRS-style statements"). No code change. Flagging so nobody markets this
as certified compliance — that would need a jurisdiction + audit review.

---

*Created 2026-08-25. Non-blocking: manual accuracy fixes proceed in parallel;
answers will be implemented after the team responds.*
