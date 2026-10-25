#include "touchhelper.h"
#include "appsettings.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QGraphicsView>
#include <QInputDevice>
#include <QScroller>
#include <QSettings>

namespace TouchHelper {

bool hasTouchscreen() {
    const auto devices = QInputDevice::devices();
    for (const QInputDevice* d : devices) {
        if (d && d->type() == QInputDevice::DeviceType::TouchScreen)
            return true;
    }
    return false;
}

bool hasStylus() {
    const auto devices = QInputDevice::devices();
    for (const QInputDevice* d : devices) {
        if (d && d->type() == QInputDevice::DeviceType::Stylus)
            return true;
    }
    return false;
}

bool touchModeEnabled() {
    AppSettings s;
    const QString mode = s.value("touchMode", "auto").toString().toLower();
    if (mode == "on" || mode == "1" || mode == "true")
        return true;
    if (mode == "off" || mode == "0" || mode == "false")
        return false;
    return hasTouchscreen();
}

void installAppWide() {
    // Widgets that ignore touch still get working taps via mouse synthesis.
    QApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, true);
    QApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents, false);
}

void enableKineticScrolling(QWidget* root) {
    if (!root)
        return;
    // Touch-drag scrolling only (not mouse) so rubber-band select, text
    // selection and graph dragging keep working with a mouse/pen.
    const auto areas = root->findChildren<QAbstractScrollArea*>();
    for (QAbstractScrollArea* area : areas) {
        if (!area)
            continue;
        // Graph canvas owns its gestures — don't steal its viewport.
        if (qobject_cast<QGraphicsView*>(area))
            continue;
        QObject* vp = area->viewport();
        if (vp)
            QScroller::grabGesture(vp, QScroller::TouchGesture);
    }
    // Also cover the root itself if it is scrollable.
    if (auto* self = qobject_cast<QAbstractScrollArea*>(root)) {
        if (!qobject_cast<QGraphicsView*>(self) && self->viewport())
            QScroller::grabGesture(self->viewport(), QScroller::TouchGesture);
    }
}

} // namespace TouchHelper
