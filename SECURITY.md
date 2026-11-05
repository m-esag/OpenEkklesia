# Security Policy

## Scope: what OpenEkklesia is (and is not)

OpenEkklesia is an **offline-first, single-congregation desktop application**
built for small churches (1–2 administrators). Its security posture is scoped
to that reality:

- **Authentication is a 4-digit PIN gate** (seeded defaults `1234` / `5555` /
  `7777` / `0000`, all printed on the login screen by design and changeable in
  Settings → Access PINs). PINs are stored in the local `users` table and are
  not a substitute for OS-level account security.
- **The database is a local SQLite file** with no network listener, no cloud
  sync, and no telemetry. Whoever can read the machine's user profile can read
  the data; disk-level protection (BitLocker/LUKS) is the right layer for that.
- **No encryption of data at rest.** The README's honesty note applies: do not
  treat OpenEkklesia as an encrypted records system.
- **Not tax/IRS compliance software.** OpenEkklesia owns donor-level giving
  records and can emit contribution statements; the general ledger stays in a
  dedicated bookkeeping package (e.g. GnuCash).

## Supported versions

Security fixes land on the latest release tag. As a pre-1.0 project we do not
backport to older tags.

## Reporting a vulnerability

Please **do not open a public issue** for something exploitable. Use GitHub's
*Report a vulnerability* flow (Security tab → Private vulnerability reporting)
so fixes can land before disclosure. Include the version, OS, and the minimum
steps to reproduce.

For non-sensitive bugs, regular issues are welcome.
