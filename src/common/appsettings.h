#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>
#include <QStringList>
#include <QVariant>

// Central access point for the application's stored preferences.
//
// Storage is a JSON document at the first resolvable-writable location:
//   1. OPENEKKLESIA_SETTINGS_DIR env (test harness hermeticity)
//   2. <exe dir>/data/settings.ini  when a .portable marker exists
//   3. <APPDATA>/<org>/settings.json otherwise
//
// Why not QSettings: on Windows build 26300 both QSettings backends
// (registry and INI) fail with AccessError for this application while plain
// file I/O from the same process works — silently breaking every preference
// write (module toggles, themes, church profile). This store writes with
// QSaveFile and verifies each commit.
//
// Key semantics match QSettings: "/" in a key is a group separator, stored
// here as literal "group/key" strings in a flat JSON map.
class AppSettings
{
public:
    AppSettings();

    void setValue(const QString& key, const QVariant& value);
    QVariant value(const QString& key, const QVariant& fallback = QVariant()) const;
    void remove(const QString& key);
    bool contains(const QString& key) const;
    QStringList allKeys() const;
    void clear();

    void sync() {}

    // Absolute path of the JSON store file, resolved with the same precedence
    // used for reads/writes. Exposed so Settings can display the location.
    static QString storagePath();

private:
    struct Store;
    static Store& store();
};

// Inline helper for one-shot preference reads.
inline QVariant appSetting(const QString& key, const QVariant& fallback = QVariant()) {
    AppSettings s;
    return s.value(key, fallback);
}

#endif // APPSETTINGS_H
