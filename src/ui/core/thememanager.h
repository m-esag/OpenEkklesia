#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QStringList>

struct ThemeTokens {
    QString id;
    QString name;
    bool isDark = true;
    bool isCustom = false;

    // Backgrounds
    QColor bgBase;
    QColor bgSurface;
    QColor bgOverlay;
    QColor bgInput;

    // Text
    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;

    // Accents & Status
    QColor accentPrimary;
    QColor accentSuccess;
    QColor accentWarning;
    QColor accentDanger;

    // Borders & Styling
    QColor borderColor;
    int borderRadiusSm = 4;
    int borderRadiusMd = 8;
    int fontSizePx = 13;
};

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    void initialize();
    bool loadTheme(const QString& themeId);
    bool applyCustomTokens(const ThemeTokens& tokens);
    bool saveCustomPreset(const QString& presetName, const ThemeTokens& tokens);

    void zoomIn();
    void zoomOut();
    void setFontSize(int px);

    // Drop any persisted color/radius customization (used by "Reset" actions).
    void clearCustomizationOverride();

    bool generateBrandTheme(const QColor& brandColor, bool darkBase = true,
                             const QString& label = QString());
    // Backwards-compatible alias (product-specific naming; prefer generateBrandTheme).
    bool generateChurchBrandTheme(const QColor& brandColor, bool darkBase = true);

    QString activeThemeId() const;
    ThemeTokens currentTokens() const;

    QString generateGlobalQSS() const;
    QPalette generateQPalette() const;

    QStringList availableThemes() const;
    ThemeTokens getThemeTokens(const QString& themeId) const;

    QString customThemesDir() const;
    void reloadCustomThemesFromDisk();

    bool isSystemDark() const;
    void syncSystemTheme();

    // WCAG 2.1 Contrast Guardian & Accessibility
    static double relativeLuminance(const QColor& color);
    static double contrastRatio(const QColor& c1, const QColor& c2);
    static QString contrastRating(double ratio);
    static QColor textOn(const QColor& c);
    static QColor hoverOn(const QColor& c);
    static QColor pressedOn(const QColor& c);
    static QColor disabledTextOn(const QColor& c);

signals:
    void themeChanged(const ThemeTokens& tokens);

private:
    ThemeManager() = default;
    ~ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void registerDefaultThemes();
    void appendThemeOrder(const QString& id);
    void restoreColorCustomization();

    QString m_activeThemeId;
    ThemeTokens m_currentTokens;
    QMap<QString, ThemeTokens> m_registeredThemes;
    QStringList m_themeOrder; // preserves intentional registration order
};

#endif // THEMEMANAGER_H
