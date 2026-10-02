#ifndef TOUCHHELPER_H
#define TOUCHHELPER_H

#include <QWidget>

namespace TouchHelper {

// True when the OS reports at least one touchscreen (e.g. Dell Latitude 7320
// Detachable panel). Active pens (PN7320A) act as a precise mouse by default,
// so no separate pen handling is needed for buttons/forms.
bool hasTouchscreen();

// True when the OS reports an active pen/stylus device (e.g. Dell PN7320A).
// The pen works as a precise mouse with zero changes; this only gates
// pen-specific affordances (radial menu, ink notes).
bool hasStylus();

// "touchMode" QSettings override: "auto" (default) follows hasTouchscreen(),
// "on"/"off" force the behavior. Lets a desktop without touch test the layout.
bool touchModeEnabled();

// App-wide one-time setup: mouse synthesis for untouched widgets + kinetic
// scroller tuning. Safe to call once from main().
void installAppWide();

// Enable QScroller kinetic scrolling on every scrollable child of `root`
// (lists, tables, trees, scroll areas). Graph canvas is skipped — it owns
// its drag/pinch gestures.
void enableKineticScrolling(QWidget* root);

} // namespace TouchHelper

#endif // TOUCHHELPER_H
