# OpenEkklesia Settings — Team Brief (3 questions)

Context: an audit of the Settings module found 18 settings stored in the UI but
read nowhere else (church profile, locale, pastoral, backup, kiosk). Those are
being wired/cleaned regardless of your answers. The three questions below are
genuine product decisions where your context beats any model's guess.

---

## Q1. Fiscal year for giving/budget reports
**Today:** everything is calendar-year (YTD = Jan 1 → now).
**Options:**
- **(a) Keep calendar-year** — simplest; "YTD Income" matches what most small
  churches quote casually.
- **(b) Configurable fiscal year start** (e.g. July 1) — needs a small budget
  schema change so budgets can be per-fiscal-year, and YTD recalculates from
  the configured start. Moderate effort, affects the dashboard + giving reports.

**Ask:** does your church run on a July–June (or other) fiscal year, or is
Jan–Dec fine?

## Q2. Date format & timezone settings
The Localization page stores `dateFormat`, `startOfWeek`, `timezone` — but the
app currently ignores them (every date is shown as yyyy-MM-dd, local time).
**Options:**
- **(a) Apply them app-wide** — a shared date formatter + week-start-aware
  calendar. Moderate effort; touches every date display.
- **(b) Remove them from Settings** — stop showing controls that do nothing
  until they're real.

**Ask:** do you actually need different date formats / week starts / timezones
(e.g. church in another country, reports for an overseas board), or is the
current format fine?

## Q3. Which printed outputs matter most (priority for logo/address wiring)
The church profile (logo, name, address, phone, email, website, tax ID) is
configured but appears on nothing. I can wire it into any combination of:
- **Service run sheet PDF** (Service Builder export)
- **Donor receipts / contribution statements** (giving)
- **Vault report / board documents** (governance)
- **Connection graph audit export**

**Ask:** rank the 1–3 you actually hand out — so the church logo lands where
it's seen.

---

*Created 2026-08-25. Non-blocking: dead-settings wiring and cleanup proceed in
parallel with this brief.*
