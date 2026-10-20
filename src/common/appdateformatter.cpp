#include "appdateformatter.h"

#include <QSettings>

namespace {
QSettings appSettings()
{
    return QSettings("OpenEkklesia", "CHMS");
}
}

QString AppDateFormatter::formatDate(const QDate &date) {
    if (!date.isValid())
        return QString();
    const QString fmt = appSettings().value("dateFormat", "yyyy-MM-dd").toString();
    return date.toString(fmt);
}

QString AppDateFormatter::formatDateTime(const QDateTime &dt) {
    if (!dt.isValid())
        return QString();
    return formatDate(dt.date()) + " " + dt.time().toString("h:mm AP");
}