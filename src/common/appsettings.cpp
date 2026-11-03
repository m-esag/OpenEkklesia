#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>

namespace {

QString settingsFilePath()
{
    const QString envDir = qEnvironmentVariable("OPENEKKLESIA_SETTINGS_DIR");
    if (!envDir.isEmpty())
        return envDir + "/OpenEkklesia/CHMS.json";

#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    const QString portableMarker =
        QCoreApplication::applicationDirPath() + "/.portable";
    if (QFile::exists(portableMarker))
        return QCoreApplication::applicationDirPath() + "/data/settings.json";
#endif

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appData + "/settings.json";
}

// Resilient JSON settings store. Backed by QSaveFile (write to temp, atomic
// rename) so a crash mid-write never corrupts the store. Loads lazily once
// per process; every write commits immediately.
class JsonSettingsStore
{
public:
    static JsonSettingsStore& instance()
    {
        static JsonSettingsStore store;
        return store;
    }

    void setValue(const QString& key, const QVariant& value)
    {
        load();
        if (value.isValid())
            m_values.insert(key, value);
        else
            m_values.remove(key);
        save();
    }

    QVariant value(const QString& key, const QVariant& fallback) const
    {
        load();
        return m_values.value(key, fallback);
    }

    void remove(const QString& key)
    {
        load();
        m_values.remove(key);
        save();
    }

    bool contains(const QString& key)
    {
        load();
        return m_values.contains(key);
    }

    QStringList keys() const
    {
        load();
        return m_values.keys();
    }

    void clear()
    {
        m_values.clear();
        save();
    }

private:
    JsonSettingsStore() { load(); }

    void load() const
    {
        if (m_loaded) return;
        m_loaded = true;
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly)) return;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return;
        const QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            m_values.insert(it.key(), it.value().toVariant());
    }

    void save() const
    {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning() << "AppSettings: cannot write" << m_path << f.errorString();
            return;
        }
        QJsonObject obj;
        for (auto it = m_values.begin(); it != m_values.end(); ++it)
            obj.insert(it.key(), QJsonValue::fromVariant(it.value()));
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        if (!f.commit())
            qWarning() << "AppSettings: commit failed for" << m_path;
    }

    QString m_path = settingsFilePath();
    mutable QMap<QString, QVariant> m_values;
    mutable bool m_loaded = false;
};

} // namespace

AppSettings::AppSettings() = default;

void AppSettings::setValue(const QString& key, const QVariant& value)
{
    JsonSettingsStore::instance().setValue(key, value);
}

QVariant AppSettings::value(const QString& key, const QVariant& fallback) const
{
    return JsonSettingsStore::instance().value(key, fallback);
}

void AppSettings::remove(const QString& key)
{
    JsonSettingsStore::instance().remove(key);
}

bool AppSettings::contains(const QString& key) const
{
    return JsonSettingsStore::instance().contains(key);
}

QStringList AppSettings::allKeys() const
{
    return JsonSettingsStore::instance().keys();
}

void AppSettings::clear()
{
    JsonSettingsStore::instance().clear();
}
