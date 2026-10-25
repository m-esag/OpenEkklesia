
#include <QDir>
#include "appsettings.h"

namespace {

QSettings::Format resolveFormat()
{
    const QString dir = qEnvironmentVariable("OPENEKKLESIA_SETTINGS_DIR");
    if (dir.isEmpty()) {
        return QSettings::NativeFormat;
    }
    // The INI scope path must be installed before the base constructor below
    // resolves the settings file location.
    QDir().mkpath(dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    return QSettings::IniFormat;
}

} // namespace

AppSettings::AppSettings()
    : QSettings(resolveFormat(), QSettings::UserScope,
                QStringLiteral("OpenEkklesia"), QStringLiteral("CHMS"))
{
}
