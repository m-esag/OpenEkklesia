#include "appsettings.h"

#include <QDir>

namespace {

QSettings::Format resolveFormat()
{
    const QString dir = qEnvironmentVariable("OPENEKKLESIA_SETTINGS_DIR");
    if (dir.isEmpty()) {
        return QSettings::NativeFormat;
    }
    // Route every preference read/write to a scratch INI store (used by the
    // test harness). QSettings::setPath mutates a process-global table and is
    // not reentrant, so install it exactly once; this also keeps the hot path
    // free of repeated mkpath/setPath calls. The environment variable is read
    // once for the same reason, so set it before the first AppSettings use.
    static const bool pathInstalled = [] {
        const QString settingsDir = qEnvironmentVariable("OPENEKKLESIA_SETTINGS_DIR");
        QDir().mkpath(settingsDir);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
        return true;
    }();
    Q_UNUSED(pathInstalled)
    return QSettings::IniFormat;
}

} // namespace

AppSettings::AppSettings()
    : QSettings(resolveFormat(), QSettings::UserScope,
                QStringLiteral("OpenEkklesia"), QStringLiteral("CHMS"))
{
}
