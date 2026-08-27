# Fix: Graph module renders empty canvas (scene corruption)

Root cause is memory corruption in the QGraphicsScene from widget/item lifetime bugs in the recent graph rework — NOT data, schema, or filters (all verified healthy). Fix the lifetime bugs; the nodes will render again.

## 1. Stop the Smart-Bridge panel widget churn (`graphwidget.cpp`)
`clearGhostEdges()` (:1623-1636) + `clearLayoutRecursively()` (:42-54) delete child widgets of `m_smartBridgeLayout` while it remains the installed layout of the live `m_smartBridgeSection` — runs on every person-node click.
Fix: rebuild the section contents into an inner container widget; on refresh, remove/dispose that container with `deleteLater()` instead of deleting children of the installed layout. Alternatively clear via the QLayoutItem take + `widget()->deleteLater()` pattern. One structural change, no behavior change.

## 2. QPointer-guard raw item pointers across `exec()` loops
- `showNodeContextMenu` (:2663-2726): hold `QPointer<NodeItem> node`; bail if null after `menu.exec()`.
- Edge-sever path (:2440-2494): same for `EdgeItem*`; no use of `edge` after the confirm dialog and `delete`.
- Sweep the whole file for other capture-then-`exec()`-then-dereference patterns (QMessageBox/question/dialog exec) and guard each.

## 3. Never clear the scene from inside an item's handler stack
`onGroupSuggest` (:1616) and `onQuickAssignGroup` (:2511) call `loadGraph()` (which `m_scene->clear()`s and deletes every item) while the originating NodeItem handler is still running.
Fix: defer the reload with `QTimer::singleShot(0, this, [this]{ loadGraph(); });` so it runs after the stack unwinds. Audit for other in-handler `loadGraph()` calls and defer them too.

## 4. Build hygiene (no deletions without user confirmation)
- The canonical binary is `build/OpenEkklesia`; `build_release/OpenEkklesia` (09:51) and `dist/*.AppImage` are stale/pre-rework. I will NOT delete them; I'll report paths so you (or Gemini) can clean up, and rebuild only the canonical `build/` for verification.
- Recommendation to pass to Gemini: commit the uncommitted migration-18 bump (`databasemanager.{cpp,h}`) so HEAD matches the live DB. I will not commit their WIP myself.

## Verification
1. Build the canonical target: `cmake --build build` — clean compile.
2. Run the test suite (`ctest` in build/); note the pre-existing ICS/TRIGGER test failure belongs to Gemini's WIP, not this fix.
3. ASan validation: configure `build-asan` (Debug + `-fsanitize=address -fno-omit-frame-pointer`), launch under xvfb, script the trigger sequence — open graph, click a person node (exercises clearGhostEdges), open a node context menu, run the suggest/quick-assign flow, close the app (the teardown abort). Before the fix ASan should flag the invalid free/use-after-free; after the fix, clean run.
4. Manual smoke via xvfb screenshot: graph canvas shows nodes (105 people / 19 groups) after the interaction sequence.

## Out of scope
- Smart Bridge v2 (migration 19, SynergyEngine, archetypes) — plan-doc only, untouched.
- Any redesign of the graph's features — lifetime fixes only, preserving Gemini's UX as committed.

## Files touched
- `src/ui/ministry/graphwidget.cpp` (all fixes; currently committed-clean, so no collision with Gemini's uncommitted WIP in databasemanager/mainwindow/CMakeLists/iconmanager)
- Possibly `src/ui/ministry/graphwidget.h` if the section-container refactor needs a member
- Commit: only these files, explicit paths, leaving Gemini's WIP untouched.