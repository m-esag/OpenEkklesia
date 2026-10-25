#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QSettings>
#include <QString>
#include <QVariant>

// Central access point for the application's stored preferences.
//
// Production keeps preferences in the platform-native store (the Windows
// registry, ~/.config/OpenEkklesia.conf on Unix, ...). Setting the
// OPENEKKLESIA_SETTINGS_DIR environment variable redirects every read and
// write to a self-contained INI file under that directory instead, which lets
// the test suite run hermetically: a test can neither be influenced by the
// developer's saved preferences nor overwrite them.
class AppSettings : public QSettings {
public:
    AppSettings();
};

// Inline helper for one-shot preference reads.
inline QVariant appSetting(const QString& key, const QVariant& fallback = QVariant()) {
    return AppSettings().value(key, fallback);
}

#endif // APPSETTINGS_H
