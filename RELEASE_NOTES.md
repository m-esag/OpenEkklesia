# Release Notes — OpenEkklesia v0.8.0-beta.1

**Tag:** `v0.8.0-beta.1` → commit `0f175a9` (branch `Testing`)
**Date:** 2026-10-16
**Status:** Pre-release beta, single-operator offline-first desktop app.

## What this release contains

- The main-window minimum-size fix (`ed45c10`): the 1000×620 floor is decoupled
  from page content via `IgnoreMinStackedLayout`, eliminating the ~1770px
  minimum-width blowout from dense Groups/Settings pages.
- A mutation-verified regression test (`test_mainWindowMinimumDecoupledFromContent`)
  locking in the floor, the stacked-layout pin, and the bounded
  `minimumSizeHint()` (≤ 1100px). Dead min-size probe rig removed.

## Shipped artifacts

Naming convention from this release on: `OpenEkklesia-VERSION-SHORTCOMMIT-PLATFORM`.

| Artifact | Status | SHA256 |
|---|---|---|
| `OpenEkklesia-0.8.0-beta.1-0f175a9-x86_64.AppImage` (dist-linux/) | **Shipped** — rebuilt from the tagged commit | `7d9a8c4c8d66a2cd624d48364223c67a1b95fb01d030b9b89cb027506623a9c7` |
| `OpenEkklesia-0.8.0-beta.1-0f175a9-win64.exe` (dist-windows/) | Shipped, **not rebuilt** — code-identical to the tagged commit (delta vs `ed45c10` is tests/gitignore only) | `6d721ce8b5524c5aab402bcce880ae2ff1d1c9ae705898851459c1127eb8bcbf` |
| `OpenEkklesia-ed45c10-x86_64.AppImage` (dist-linux/) | Preserved reference build (pre-tagging, never versioned — commit-named only). Not a release artifact. | `95f7f64b96bc42f85e12f2257fa5e2edfd2bcbd92843a732f0e64c6a96517e9c` |

Removed from the tree: the stale Oct 3 `dist/OpenEkklesia-x86_64.AppImage`
(SHA256 `5f0ae19ffc0e50f919843c813d436ff06fe9585115803b911ab8163a5bb9f0e5`) —
superseded by the rebuilt Linux artifact and not shipped.

Windows notes: the shipped `.exe` is not fully self-contained — it was smoke-run
with Qt 6.8.0 runtime DLLs reachable; bundle `windeployqt` output before
distributing beyond the dev machine. The `dist-windows/OpenEkklesia/` deploy
folder (Oct 8) **predates the minimum-size fix and is not part of this
release**; do not ship it.

## Known caveats (read before relying on this build)

1. **Security deferrals (accepted for this beta, documented here):**
   - `users.pin` and Planning Center credentials are stored **in plaintext**
     in QSettings. Accepted for the single-operator, offline-first beta.
   - Planning Center sync shells out to `curl -u appId:secret`, so credentials
     are visible in the process list while a sync runs. It is an optional
     integration; migrating it to QNetworkAccessManager is queued for the
     maintenance batch.
2. **Restore is DB-only.** "Restore from backup" validates and restores the
   SQLite database but does **not** swap back the accompanying `_vault`
   document folder. If documents are missing after a restore, manually restore
   the matching `auto_backup_YYYYMMDD_vault` folder from the same backup date.
3. **Not every backup on disk is restorable — by design.** The restore
   validator rejects files that fail the size/SQLite-header/integrity/
   `schema_version` checks (e.g. the pre-`schema_version` Aug 17 backup and any
   0-byte file). This rejection is intentional; do not promise all on-disk
   backups can be restored.
4. **Downgrade caveat.** Schema is v24; older binaries open a v24 database
   without blocking but silently skip columns they don't know. Edits made on an
   old binary during a downgrade window will not populate v24 columns — avoid
   editing (especially financial) records while downgraded.

## Verification summary

- **Windows:** `ctest -C Release` in `build-windows/` (Qt 6.8.0 msvc2022_64):
  **1078 checks, 0 failed** at `0f175a9`.
- **Linux:** fresh build in Ubuntu 22.04 WSL (Qt 6.2.4, GCC 11.4 — same
  baseline as `Dockerfile.build`): **1078 checks, 0 failed**, exact parity.
- **Visual acceptance** at 1000×620 on Groups and Settings: pass, no clipped
  controls; sidebar stays at 220px by default (auto-collapse gates at <940px).
- **Smoke:** Windows shipped exe launched offscreen (DB opened, reached login
  loop); Linux AppImage run in a sandboxed fresh profile — fresh DB created,
  migrations 0→24 applied end-to-end, backup scheduler fired.

## Rollback

- **Data:** automatic weekly/daily backups (`backups/auto_backup_YYYYMMDD.db` +
  matching `_vault` folder, retention 10) and manual "Backup Database"; restore
  validates, writes a `.pre-restore-*.bak` safety copy, then staged-swaps and
  forward-migrates to v24. Downgrade is soft-compatible (see caveat 4).
- **Process:** `git checkout v0.8.0-beta.1` (or `ed45c10` for the pre-test
  tree) and rebuild; the preserved `OpenEkklesia-ed45c10-x86_64.AppImage` is a
  known-good pre-release fallback binary.

## Not yet verified (beta disclosure)

Live Linux desktop rendering (Wayland/X11, HiDPI scaling), interactive login,
Planning Center sync against the live service, a real restore of the aged v6
backup into the v24 binary, and the older-distro glibc compatibility claim
(by construction, not by test).
