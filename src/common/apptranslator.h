#ifndef APPTRANSLATOR_H
#define APPTRANSLATOR_H

#include <QHash>
#include <QSet>
#include <QString>

// Central dictionary translator for the staged Spanish localization.
//
// Why not Qt tr()/QTranslator: the app carries ~30k LOC of hard-coded string
// literals, and a mechanical tr() extraction was rejected after two scripted
// multi-file edits corrupted the tree (see SPANISH_I18N_BRIEF.md §0). Instead,
// user-visible strings in translated surfaces are wrapped in AppTranslator::t()
// and looked up in a flat dictionary keyed by the exact English literal.
//
// Usage:
//   AppTranslator::t("Members")                     -> "Miembros" (es) / key (en)
//   AppTranslator::instance().setLanguage("es");    // called once at startup
//
// Language: AppSettings key "ui/language", values "en" (default) and "es".
// There is NO live retranslation in this phase — changing the language in
// Settings applies on restart, except the first-run SetupWizard, which
// retranslates its own widgets immediately.
//
// Dictionary file: assets/i18n/es.json — a flat JSON object mapping the exact
// English string to its Spanish translation. UTF-8, no BOM, no comments (JSON
// forbids them, so the phase-1 glossary table lives below and in the brief).
// Missing/blank values are treated as absent: t() returns the key unchanged
// and logs it once per key so coverage gaps stay discoverable. Translations
// MUST preserve every %1..%N placeholder (enforced by es_dictionary_validity).
//
// Glossary (formal "usted" register — keep consistent in later phases):
//   Members                Miembros            Households            Familias
//   Groups & Ministries    Grupos y Ministerios
//   Giving & Batches       Diezmos y Ofrendas  Document Vault        Bóveda de Documentos
//   Connection Graph       Mapa de Conexiones  Pastoral Analytics    Analítica Pastoral
//   Shepherd's Workbench   Banco del Pastor    Service Builder       Constructor de Servicios
//   Ministry Planning      Planificación Ministerial
//   Asset Inventory        Inventario de Activos   Facilities        Instalaciones
//   Settings               Configuración           Dashboard         Panel
//   Visitor                Visitante               Follow-up         Seguimiento
//
// File loading (chosen over qrc embedding so the dictionary stays editable
// without a rebuild; the header of es.json says the same). Resolution order,
// first hit wins:
//   1. explicitPath (the test suite passes the source-tree path)
//   2. OPENEKKLESIA_I18N_DIR env var (dir containing es.json)
//   3. <exe dir>/assets/i18n/es.json            (packaged layout)
//   4. <exe dir>/../assets/i18n/es.json         (dev build/ layout)
//   5. <exe dir>/../../assets/i18n/es.json      (dev build-windows/Release/ layout)
// Steps 3-5 also let the test binaries find the source assets without any
// compile-time path. Packaging copies es.json next to the executable
// (build-windows.ps1 staging, AppImage AppDir, Inno Setup ships the Release
// tree recursively).
class AppTranslator
{
public:
    static AppTranslator& instance();

    // "en" | "es"; any other value (including absent) resolves to "en".
    // Selecting "es" lazily loads the dictionary file on first use.
    void setLanguage(const QString& languageCode);
    QString language() const { return m_language; }

    // Translate key per the active language. "en" (or no dictionary) returns
    // the key unchanged with zero lookups; a missing Spanish entry returns
    // the key unchanged and warns once per key.
    static QString t(const QString& key);

    // Load the Spanish dictionary. With an empty path the resolution chain
    // from the class comment is used. Returns the file path that was loaded,
    // or an empty string when no dictionary could be read.
    QString loadDictionary(const QString& explicitPath = QString());
    QString dictionaryPath() const { return m_dictPath; }
    int entryCount() const { return m_es.size(); }

private:
    AppTranslator() = default;

    QString m_language = QStringLiteral("en");
    QHash<QString, QString> m_es;
    QSet<QString> m_warnedMissing;
    QString m_dictPath;
};

#endif // APPTRANSLATOR_H
