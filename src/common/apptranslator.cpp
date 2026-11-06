#include "apptranslator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

AppTranslator& AppTranslator::instance()
{
    static AppTranslator inst;
    return inst;
}

void AppTranslator::setLanguage(const QString& languageCode)
{
    // Absent/unknown values fall back to English rather than half-localized UI.
    m_language = (languageCode == QStringLiteral("es")) ? QStringLiteral("es")
                                                        : QStringLiteral("en");
    if (m_language == QStringLiteral("es") && m_es.isEmpty())
        loadDictionary();
}

QString AppTranslator::t(const QString& key)
{
    AppTranslator& self = instance();
    if (self.m_language != QStringLiteral("es") || self.m_es.isEmpty())
        return key; // English: zero lookups, zero allocations beyond the key

    const QString translated = self.m_es.value(key);
    if (translated.isEmpty()) {
        // Missing key (blank values were dropped at load time): degrade to the
        // English text so a coverage gap can never break the UI, but say so —
        // once per key, so a long session does not flood the log.
        if (!self.m_warnedMissing.contains(key)) {
            self.m_warnedMissing.insert(key);
            qWarning() << "AppTranslator: missing es.json entry for key:" << key;
        }
        return key;
    }
    return translated;
}

QString AppTranslator::loadDictionary(const QString& explicitPath)
{
    QStringList candidates;
    if (!explicitPath.isEmpty()) {
        candidates << explicitPath;
    } else {
        const QString envDir = qEnvironmentVariable("OPENEKKLESIA_I18N_DIR");
        if (!envDir.isEmpty())
            candidates << envDir + QStringLiteral("/es.json");

        const QString exeDir = QCoreApplication::applicationDirPath();
        candidates << exeDir + QStringLiteral("/assets/i18n/es.json")
                   << exeDir + QStringLiteral("/../assets/i18n/es.json")
                   << exeDir + QStringLiteral("/../../assets/i18n/es.json");
    }

    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "AppTranslator: invalid dictionary JSON at" << path
                       << "-" << parseError.errorString();
            continue;
        }

        QHash<QString, QString> loaded;
        const QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString value = it.value().toString();
            // Blank translations are invalid: treat them as missing so the
            // English text (and its missing-entry warning) is used instead.
            if (!value.isEmpty())
                loaded.insert(it.key(), value);
        }

        m_es = loaded;
        m_warnedMissing.clear();
        m_dictPath = path;
        return path;
    }

    // No dictionary found: Spanish degrades to English keys, which is the
    // documented safe behavior. Warn once per attempt so a mispackaged build
    // is diagnosable without spamming the log on every t() call.
    static bool warnedNoFile = false;
    if (!warnedNoFile) {
        warnedNoFile = true;
        qWarning() << "AppTranslator: es.json not found (checked"
                   << candidates.size() << "locations); Spanish falls back to English";
    }
    return QString();
}
