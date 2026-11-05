#include "thememanager.h"
#include "appsettings.h"
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

ThemeManager& ThemeManager::instance() {
    static ThemeManager inst;
    return inst;
}

QString ThemeManager::customThemesDir() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/themes";
    if (path.isEmpty()) {
        path = QDir::homePath() + "/.local/share/openekklesia/themes";
    }
    QDir().mkpath(path);
    return path;
}

void ThemeManager::initialize() {
    registerDefaultThemes();
    reloadCustomThemesFromDisk();

    // QStyleHints::colorSchemeChanged only exists since Qt 6.5; guard it so the
    // codebase still compiles against the Qt 6.x versions the README advertises.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qApp && QGuiApplication::styleHints()) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
            syncSystemTheme();
        });
    }
#endif

    AppSettings settings;
    // Default to following the OS light/dark scheme on first run ("system_auto"
    // resolves to Catppuccin Mocha/Latte in syncSystemTheme()); saved prefs win.
    QString savedTheme = settings.value("activeThemeId", "system_auto").toString();
    // loadTheme() itself re-applies interactive color tweaks persisted for this
    // exact theme (customThemeBaseId), so a previous session's customization is
    // restored there and a genuine preset switch discards it.
    if (!loadTheme(savedTheme)) {
        loadTheme("catppuccin_mocha");
    }
}

void ThemeManager::restoreColorCustomization() {
    AppSettings s;
    const QString json = s.value("customThemeJson").toString();
    if (json.isEmpty()) return;
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    if (obj.isEmpty()) return;

    m_currentTokens.bgBase = QColor(obj.value("bgBase").toString());
    m_currentTokens.bgSurface = QColor(obj.value("bgSurface").toString());
    m_currentTokens.bgOverlay = QColor(obj.value("bgOverlay").toString());
    m_currentTokens.bgInput = QColor(obj.value("bgInput").toString());
    m_currentTokens.textPrimary = QColor(obj.value("textPrimary").toString());
    m_currentTokens.textSecondary = QColor(obj.value("textSecondary").toString());
    m_currentTokens.textMuted = QColor(obj.value("textMuted").toString());
    m_currentTokens.accentPrimary = QColor(obj.value("accentPrimary").toString());
    m_currentTokens.accentSuccess = QColor(obj.value("accentSuccess").toString());
    m_currentTokens.accentWarning = QColor(obj.value("accentWarning").toString());
    m_currentTokens.accentDanger = QColor(obj.value("accentDanger").toString());
    m_currentTokens.borderColor = QColor(obj.value("borderColor").toString());
    m_currentTokens.borderRadiusMd = obj.value("borderRadiusMd").toInt(m_currentTokens.borderRadiusMd);

    // Record which preset this customization belongs to so it survives
    // re-applying that same preset (e.g. Settings "Save All Changes"). Legacy
    // saves predate this key, so assume the currently active preset.
    if (!s.contains("customThemeBaseId") && !m_activeThemeId.isEmpty()) {
        s.setValue("customThemeBaseId", m_activeThemeId);
    }
    s.setValue("themeIsCustom", true);
    if (qApp) {
        qApp->setPalette(generateQPalette());
        qApp->setStyleSheet(generateGlobalQSS());
    }
    emit themeChanged(m_currentTokens);
}

bool ThemeManager::isSystemDark() const {
    if (qApp) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
        return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
    }
    return true;
}

void ThemeManager::syncSystemTheme() {
    if (m_activeThemeId == "system_auto") {
        bool dark = isSystemDark();
        QString targetId = dark ? "catppuccin_mocha" : "catppuccin_latte";
        if (m_registeredThemes.contains(targetId)) {
            m_currentTokens = m_registeredThemes.value(targetId);
            if (qApp) {
                qApp->setPalette(generateQPalette());
                qApp->setStyleSheet(generateGlobalQSS());
            }
            emit themeChanged(m_currentTokens);
        }
    }
}

void ThemeManager::appendThemeOrder(const QString& id) {
    if (!m_themeOrder.contains(id)) {
        m_themeOrder.append(id);
    }
}

void ThemeManager::registerDefaultThemes() {
    // --- PAIR 1: Catppuccin Pastel Pair ---
    ThemeTokens mocha;
    mocha.id = "catppuccin_mocha";
    mocha.name = "🌙 Catppuccin Mocha (Dark Pastel)";
    mocha.isDark = true;
    mocha.bgBase = QColor("#181825");
    mocha.bgSurface = QColor("#1e1e2e");
    mocha.bgOverlay = QColor("#313244");
    mocha.bgInput = QColor("#11111b");
    mocha.textPrimary = QColor("#cdd6f4");
    mocha.textSecondary = QColor("#a6adc8");
    mocha.textMuted = QColor("#6c7086");
    mocha.accentPrimary = QColor("#89b4fa");
    mocha.accentSuccess = QColor("#a6e3a1");
    mocha.accentWarning = QColor("#f9e2af");
    mocha.accentDanger = QColor("#f38ba8");
    mocha.borderColor = QColor("#313244");
    mocha.borderRadiusMd = 8;
    m_registeredThemes.insert(mocha.id, mocha);
    appendThemeOrder(mocha.id);

    ThemeTokens latte;
    latte.id = "catppuccin_latte";
    latte.name = "☀️ Catppuccin Latte (Light Pastel)";
    latte.isDark = false;
    latte.bgBase = QColor("#eff1f5");
    latte.bgSurface = QColor("#e6e9ef");
    latte.bgOverlay = QColor("#ccd0da");
    latte.bgInput = QColor("#ffffff");
    latte.textPrimary = QColor("#4c4f69");
    latte.textSecondary = QColor("#5c5f77");
    latte.textMuted = QColor("#5d627b"); // was #8c8fa1 (2.6:1) -> #73778b (3.9:1) -> 4.9:1
    latte.accentPrimary = QColor("#1e66f5");
    latte.accentSuccess = QColor("#40a02b");
    latte.accentWarning = QColor("#df8e1d");
    latte.accentDanger = QColor("#d20f39");
    latte.borderColor = QColor("#bcc0cc");
    latte.borderRadiusMd = 8;
    m_registeredThemes.insert(latte.id, latte);
    appendThemeOrder(latte.id);

    // System Auto is resolved to the Catppuccin pair (mocha/latte) so it also
    // carries real color values for any consumer (e.g. a future preview swatch).
    ThemeTokens autoTok = m_registeredThemes.value(isSystemDark() ? "catppuccin_mocha" : "catppuccin_latte");
    autoTok.id = "system_auto";
    autoTok.name = "🌓 System Auto (Follow OS Dark/Light)";
    autoTok.isDark = isSystemDark();
    m_registeredThemes.insert(autoTok.id, autoTok);
    appendThemeOrder(autoTok.id);

    // --- PAIR 2: Warm Brown Coffee Pair ---
    ThemeTokens warmEspresso;
    warmEspresso.id = "warm_espresso_dark";
    warmEspresso.name = "🌙 Dark Espresso (Warm Brown)";
    warmEspresso.isDark = true;
    warmEspresso.bgBase = QColor("#1e1b18");
    warmEspresso.bgSurface = QColor("#28231f");
    warmEspresso.bgOverlay = QColor("#38312b");
    warmEspresso.bgInput = QColor("#141210");
    warmEspresso.textPrimary = QColor("#f5ebe0");
    warmEspresso.textSecondary = QColor("#d7c4b7");
    warmEspresso.textMuted = QColor("#9a887b");
    warmEspresso.accentPrimary = QColor("#e5a968");
    warmEspresso.accentSuccess = QColor("#8dbb81");
    warmEspresso.accentWarning = QColor("#e3bc63");
    warmEspresso.accentDanger = QColor("#e57c68");
    warmEspresso.borderColor = QColor("#3d352e");
    warmEspresso.borderRadiusMd = 8;
    m_registeredThemes.insert(warmEspresso.id, warmEspresso);
    appendThemeOrder(warmEspresso.id);

    ThemeTokens warmCaramel;
    warmCaramel.id = "warm_caramel_light";
    warmCaramel.name = "☀️ Light Caramel (Warm Brown)";
    warmCaramel.isDark = false;
    warmCaramel.bgBase = QColor("#fdf8f4");
    warmCaramel.bgSurface = QColor("#f4e9df");
    warmCaramel.bgOverlay = QColor("#e6d7c9");
    warmCaramel.bgInput = QColor("#ffffff");
    warmCaramel.textPrimary = QColor("#3d2612");
    warmCaramel.textSecondary = QColor("#58402c");
    warmCaramel.textMuted = QColor("#6e5947"); // was #8d7561 (~3.8:1, below AA) -> ~5.4:1
    warmCaramel.accentPrimary = QColor("#8c4a16");
    warmCaramel.accentSuccess = QColor("#2e6f40");
    warmCaramel.accentWarning = QColor("#b46d0e");
    warmCaramel.accentDanger = QColor("#b71c1c");
    warmCaramel.borderColor = QColor("#d8c3b2");
    warmCaramel.borderRadiusMd = 8;
    m_registeredThemes.insert(warmCaramel.id, warmCaramel);
    appendThemeOrder(warmCaramel.id);

    // --- PAIR 3: Ocean & Sky Blue Pair ---
    ThemeTokens oceanDeep;
    oceanDeep.id = "ocean_deep_dark";
    oceanDeep.name = "🌙 Ocean Deep (Dark Blue)";
    oceanDeep.isDark = true;
    oceanDeep.bgBase = QColor("#0f172a");
    oceanDeep.bgSurface = QColor("#1e293b");
    oceanDeep.bgOverlay = QColor("#334155");
    oceanDeep.bgInput = QColor("#090d16");
    oceanDeep.textPrimary = QColor("#f8fafc");
    oceanDeep.textSecondary = QColor("#cbd5e1");
    oceanDeep.textMuted = QColor("#64748b");
    oceanDeep.accentPrimary = QColor("#38bdf8");
    oceanDeep.accentSuccess = QColor("#4ade80");
    oceanDeep.accentWarning = QColor("#fbbf24");
    oceanDeep.accentDanger = QColor("#f87171");
    oceanDeep.borderColor = QColor("#334155");
    oceanDeep.borderRadiusMd = 8;
    m_registeredThemes.insert(oceanDeep.id, oceanDeep);
    appendThemeOrder(oceanDeep.id);

    ThemeTokens skyAzure;
    skyAzure.id = "sky_azure_light";
    skyAzure.name = "☀️ Sky Azure (Light Blue)";
    skyAzure.isDark = false;
    skyAzure.bgBase = QColor("#f0f9ff");
    skyAzure.bgSurface = QColor("#e0f2fe");
    skyAzure.bgOverlay = QColor("#bae6fd");
    skyAzure.bgInput = QColor("#ffffff");
    skyAzure.textPrimary = QColor("#0c4a6e");
    skyAzure.textSecondary = QColor("#0369a1");
    skyAzure.textMuted = QColor("#64748b"); // was #38bdf8 (1.9:1, failed AA) -> 4.2:1
    skyAzure.accentPrimary = QColor("#0284c7");
    skyAzure.accentSuccess = QColor("#16a34a");
    skyAzure.accentWarning = QColor("#d97706");
    skyAzure.accentDanger = QColor("#dc2626");
    skyAzure.borderColor = QColor("#93c5fd");
    skyAzure.borderRadiusMd = 8;
    m_registeredThemes.insert(skyAzure.id, skyAzure);
    appendThemeOrder(skyAzure.id);

    // --- PAIR 4: Emerald Forest Green Pair ---
    ThemeTokens emeraldPine;
    emeraldPine.id = "emerald_pine_dark";
    emeraldPine.name = "🌙 Emerald Pine (Dark Green)";
    emeraldPine.isDark = true;
    emeraldPine.bgBase = QColor("#0b1d17");
    emeraldPine.bgSurface = QColor("#142e26");
    emeraldPine.bgOverlay = QColor("#1f4237");
    emeraldPine.bgInput = QColor("#07130f");
    emeraldPine.textPrimary = QColor("#ecfdf5");
    emeraldPine.textSecondary = QColor("#a7f3d0");
    emeraldPine.textMuted = QColor("#7fb69c"); // was #5eead4 (bright teal read as accent) -> desaturated sage, 6.3:1
    emeraldPine.accentPrimary = QColor("#34d399");
    emeraldPine.accentSuccess = QColor("#4ade80");
    emeraldPine.accentWarning = QColor("#facc15");
    emeraldPine.accentDanger = QColor("#f87171");
    emeraldPine.borderColor = QColor("#1f4237");
    emeraldPine.borderRadiusMd = 8;
    m_registeredThemes.insert(emeraldPine.id, emeraldPine);
    appendThemeOrder(emeraldPine.id);

    ThemeTokens sageMeadow;
    sageMeadow.id = "sage_meadow_light";
    sageMeadow.name = "☀️ Sage Meadow (Light Green)";
    sageMeadow.isDark = false;
    sageMeadow.bgBase = QColor("#f0fdf4");
    sageMeadow.bgSurface = QColor("#dcfce7");
    sageMeadow.bgOverlay = QColor("#bbf7d0");
    sageMeadow.bgInput = QColor("#ffffff");
    sageMeadow.textPrimary = QColor("#064e3b");
    sageMeadow.textSecondary = QColor("#047857");
    sageMeadow.textMuted = QColor("#4d7c0f"); // was #34d399 (1.8:1, failed AA) -> 4.6:1
    sageMeadow.accentPrimary = QColor("#059669");
    sageMeadow.accentSuccess = QColor("#15803d");
    sageMeadow.accentWarning = QColor("#ca8a04");
    sageMeadow.accentDanger = QColor("#dc2626");
    sageMeadow.borderColor = QColor("#86efac");
    sageMeadow.borderRadiusMd = 8;
    m_registeredThemes.insert(sageMeadow.id, sageMeadow);
    appendThemeOrder(sageMeadow.id);

    // --- PAIR 5: Midnight Amethyst Purple Pair ---
    ThemeTokens amethystDark;
    amethystDark.id = "midnight_amethyst_dark";
    amethystDark.name = "🌙 Midnight Amethyst (Dark Purple)";
    amethystDark.isDark = true;
    amethystDark.bgBase = QColor("#170f24");
    amethystDark.bgSurface = QColor("#231738");
    amethystDark.bgOverlay = QColor("#342352");
    amethystDark.bgInput = QColor("#0f0918");
    amethystDark.textPrimary = QColor("#f3e8ff");
    amethystDark.textSecondary = QColor("#e9d5ff");
    amethystDark.textMuted = QColor("#8f7fb0"); // was #a855f7 (purple accent as muted) -> desaturated grey-purple, 4.7:1
    amethystDark.accentPrimary = QColor("#c084fc");
    amethystDark.accentSuccess = QColor("#4ade80");
    amethystDark.accentWarning = QColor("#facc15");
    amethystDark.accentDanger = QColor("#f87171");
    amethystDark.borderColor = QColor("#342352");
    amethystDark.borderRadiusMd = 8;
    m_registeredThemes.insert(amethystDark.id, amethystDark);
    appendThemeOrder(amethystDark.id);

    ThemeTokens lavenderLight;
    lavenderLight.id = "soft_lavender_light";
    lavenderLight.name = "☀️ Soft Lavender (Light Purple)";
    lavenderLight.isDark = false;
    lavenderLight.bgBase = QColor("#faf5ff");
    lavenderLight.bgSurface = QColor("#f3e8ff");
    lavenderLight.bgOverlay = QColor("#e9d5ff");
    lavenderLight.bgInput = QColor("#ffffff");
    lavenderLight.textPrimary = QColor("#3b0764");
    lavenderLight.textSecondary = QColor("#581c87");
    lavenderLight.textMuted = QColor("#9333ea");
    lavenderLight.accentPrimary = QColor("#7e22ce");
    lavenderLight.accentSuccess = QColor("#15803d");
    lavenderLight.accentWarning = QColor("#c2410c");
    lavenderLight.accentDanger = QColor("#b91c1c");
    lavenderLight.borderColor = QColor("#d8b4fe");
    lavenderLight.borderRadiusMd = 8;
    m_registeredThemes.insert(lavenderLight.id, lavenderLight);
    appendThemeOrder(lavenderLight.id);

    // --- PAIR 6: Nordic Frost Pair ---
    ThemeTokens nord;
    nord.id = "nord_slate";
    nord.name = "🌙 Nordic Slate (Dark Frost)";
    nord.isDark = true;
    nord.bgBase = QColor("#2e3440");
    nord.bgSurface = QColor("#3b4252");
    nord.bgOverlay = QColor("#434c5e");
    nord.bgInput = QColor("#242933");
    nord.textPrimary = QColor("#eceff4");
    nord.textSecondary = QColor("#d8dee9");
    nord.textMuted = QColor("#8f97a8"); // was #4c566a (1.4:1, failed AA) -> 3.4:1
    nord.accentPrimary = QColor("#88c0d0");
    nord.accentSuccess = QColor("#a3be8c");
    nord.accentWarning = QColor("#ebcb8b");
    nord.accentDanger = QColor("#bf616a");
    nord.borderColor = QColor("#4c566a");
    nord.borderRadiusMd = 8;
    m_registeredThemes.insert(nord.id, nord);
    appendThemeOrder(nord.id);

    ThemeTokens nordicSnow;
    nordicSnow.id = "nordic_snow_light";
    nordicSnow.name = "☀️ Nordic Snow (Light Frost)";
    nordicSnow.isDark = false;
    nordicSnow.bgBase = QColor("#f8fafc");
    nordicSnow.bgSurface = QColor("#f1f5f9");
    nordicSnow.bgOverlay = QColor("#e2e8f0");
    nordicSnow.bgInput = QColor("#ffffff");
    nordicSnow.textPrimary = QColor("#0f172a");
    nordicSnow.textSecondary = QColor("#334155");
    nordicSnow.textMuted = QColor("#64748b");
    nordicSnow.accentPrimary = QColor("#0284c7");
    nordicSnow.accentSuccess = QColor("#059669");
    nordicSnow.accentWarning = QColor("#d97706");
    nordicSnow.accentDanger = QColor("#e11d48");
    nordicSnow.borderColor = QColor("#cbd5e1");
    nordicSnow.borderRadiusMd = 8;
    m_registeredThemes.insert(nordicSnow.id, nordicSnow);
    appendThemeOrder(nordicSnow.id);

    // --- PAIR 7: High Contrast Accessibility Pair ---
    ThemeTokens hcDark;
    hcDark.id = "high_contrast_dark";
    hcDark.name = "👁️ High-Contrast Dark";
    hcDark.isDark = true;
    hcDark.bgBase = QColor("#000000");
    hcDark.bgSurface = QColor("#121212");
    hcDark.bgOverlay = QColor("#242424");
    hcDark.bgInput = QColor("#000000");
    hcDark.textPrimary = QColor("#ffffff");
    hcDark.textSecondary = QColor("#e0e0e0");
    hcDark.textMuted = QColor("#b0b0b0");
    hcDark.accentPrimary = QColor("#ffff00");
    hcDark.accentSuccess = QColor("#00ff00");
    hcDark.accentWarning = QColor("#ffaa00");
    hcDark.accentDanger = QColor("#ff3333");
    hcDark.borderColor = QColor("#ffffff");
    hcDark.borderRadiusMd = 4;
    m_registeredThemes.insert(hcDark.id, hcDark);
    appendThemeOrder(hcDark.id);

    ThemeTokens hcLight;
    hcLight.id = "high_contrast_light";
    hcLight.name = "👁️ High-Contrast Light";
    hcLight.isDark = false;
    hcLight.bgBase = QColor("#ffffff");
    hcLight.bgSurface = QColor("#f0f0f0");
    hcLight.bgOverlay = QColor("#e0e0e0");
    hcLight.bgInput = QColor("#ffffff");
    hcLight.textPrimary = QColor("#000000");
    hcLight.textSecondary = QColor("#222222");
    hcLight.textMuted = QColor("#555555");
    hcLight.accentPrimary = QColor("#0000d6");
    hcLight.accentSuccess = QColor("#008000");
    hcLight.accentWarning = QColor("#b8860b");
    hcLight.accentDanger = QColor("#d90000");
    hcLight.borderColor = QColor("#000000");
    hcLight.borderRadiusMd = 4;
    m_registeredThemes.insert(hcLight.id, hcLight);
    appendThemeOrder(hcLight.id);

    // --- PAIR 8: Sunset Crimson & Solar Coral Pair ---
    ThemeTokens sunsetCrimson;
    sunsetCrimson.id = "sunset_crimson_dark";
    sunsetCrimson.name = "🌙 Sunset Crimson (Dark Ruby)";
    sunsetCrimson.isDark = true;
    sunsetCrimson.bgBase = QColor("#1a0c10");
    sunsetCrimson.bgSurface = QColor("#281318");
    sunsetCrimson.bgOverlay = QColor("#381c23");
    sunsetCrimson.bgInput = QColor("#12080a");
    sunsetCrimson.textPrimary = QColor("#ffe4e6");
    sunsetCrimson.textSecondary = QColor("#fecdd3");
    sunsetCrimson.textMuted = QColor("#fda4af");
    sunsetCrimson.accentPrimary = QColor("#f43f5e");
    sunsetCrimson.accentSuccess = QColor("#4ade80");
    sunsetCrimson.accentWarning = QColor("#fbbf24");
    sunsetCrimson.accentDanger = QColor("#ff4d4d");
    sunsetCrimson.borderColor = QColor("#381c23");
    sunsetCrimson.borderRadiusMd = 8;
    m_registeredThemes.insert(sunsetCrimson.id, sunsetCrimson);
    appendThemeOrder(sunsetCrimson.id);

    ThemeTokens solarCoral;
    solarCoral.id = "solar_coral_light";
    solarCoral.name = "☀️ Solar Coral (Light Rose)";
    solarCoral.isDark = false;
    solarCoral.bgBase = QColor("#fff1f2");
    solarCoral.bgSurface = QColor("#ffe4e6");
    solarCoral.bgOverlay = QColor("#fecdd3");
    solarCoral.bgInput = QColor("#ffffff");
    solarCoral.textPrimary = QColor("#4c0519");
    solarCoral.textSecondary = QColor("#881337");
    solarCoral.textMuted = QColor("#9f1239");
    solarCoral.accentPrimary = QColor("#e11d48");
    solarCoral.accentSuccess = QColor("#16a34a");
    solarCoral.accentWarning = QColor("#d97706");
    solarCoral.accentDanger = QColor("#be123c");
    solarCoral.borderColor = QColor("#fda4af");
    solarCoral.borderRadiusMd = 8;
    m_registeredThemes.insert(solarCoral.id, solarCoral);
    appendThemeOrder(solarCoral.id);

    // --- PAIR 9: Cypress Teal & Seafoam Pair ---
    ThemeTokens cypressTeal;
    cypressTeal.id = "cypress_teal_dark";
    cypressTeal.name = "🌙 Cypress Teal (Dark Cyan)";
    cypressTeal.isDark = true;
    cypressTeal.bgBase = QColor("#081c24");
    cypressTeal.bgSurface = QColor("#102a36");
    cypressTeal.bgOverlay = QColor("#1a3847");
    cypressTeal.bgInput = QColor("#041016");
    cypressTeal.textPrimary = QColor("#ecfeff");
    cypressTeal.textSecondary = QColor("#cffaff");
    cypressTeal.textMuted = QColor("#a5f3fc");
    cypressTeal.accentPrimary = QColor("#06b6d4");
    cypressTeal.accentSuccess = QColor("#34d399");
    cypressTeal.accentWarning = QColor("#facc15");
    cypressTeal.accentDanger = QColor("#f87171");
    cypressTeal.borderColor = QColor("#1a3847");
    cypressTeal.borderRadiusMd = 8;
    m_registeredThemes.insert(cypressTeal.id, cypressTeal);
    appendThemeOrder(cypressTeal.id);

    ThemeTokens seafoamBreeze;
    seafoamBreeze.id = "seafoam_breeze_light";
    seafoamBreeze.name = "☀️ Seafoam Breeze (Light Cyan)";
    seafoamBreeze.isDark = false;
    seafoamBreeze.bgBase = QColor("#f0fdfa");
    seafoamBreeze.bgSurface = QColor("#ccfbf1");
    seafoamBreeze.bgOverlay = QColor("#99f6e4");
    seafoamBreeze.bgInput = QColor("#ffffff");
    seafoamBreeze.textPrimary = QColor("#042f2e");
    seafoamBreeze.textSecondary = QColor("#134e4a");
    seafoamBreeze.textMuted = QColor("#0f766e");
    seafoamBreeze.accentPrimary = QColor("#0f766e");
    seafoamBreeze.accentSuccess = QColor("#15803d");
    seafoamBreeze.accentWarning = QColor("#d97706");
    seafoamBreeze.accentDanger = QColor("#b91c1c");
    seafoamBreeze.borderColor = QColor("#5eead4");
    seafoamBreeze.borderRadiusMd = 8;
    m_registeredThemes.insert(seafoamBreeze.id, seafoamBreeze);
    appendThemeOrder(seafoamBreeze.id);

    // --- PAIR 10: Autumn Amber & Harvest Copper Pair ---
    ThemeTokens autumnAmber;
    autumnAmber.id = "autumn_amber_dark";
    autumnAmber.name = "🌙 Autumn Amber (Dark Copper)";
    autumnAmber.isDark = true;
    autumnAmber.bgBase = QColor("#1c130d");
    autumnAmber.bgSurface = QColor("#2a1d13");
    autumnAmber.bgOverlay = QColor("#3c2a1c");
    autumnAmber.bgInput = QColor("#130c08");
    autumnAmber.textPrimary = QColor("#ffedd5");
    autumnAmber.textSecondary = QColor("#fed7aa");
    autumnAmber.textMuted = QColor("#fdba74");
    autumnAmber.accentPrimary = QColor("#f97316");
    autumnAmber.accentSuccess = QColor("#4ade80");
    autumnAmber.accentWarning = QColor("#facc15");
    autumnAmber.accentDanger = QColor("#ef4444");
    autumnAmber.borderColor = QColor("#3c2a1c");
    autumnAmber.borderRadiusMd = 8;
    m_registeredThemes.insert(autumnAmber.id, autumnAmber);
    appendThemeOrder(autumnAmber.id);

    ThemeTokens harvestCopper;
    harvestCopper.id = "harvest_copper_light";
    harvestCopper.name = "☀️ Harvest Copper (Light Amber)";
    harvestCopper.isDark = false;
    harvestCopper.bgBase = QColor("#fff7ed");
    harvestCopper.bgSurface = QColor("#ffedd5");
    harvestCopper.bgOverlay = QColor("#fed7aa");
    harvestCopper.bgInput = QColor("#ffffff");
    harvestCopper.textPrimary = QColor("#431407");
    harvestCopper.textSecondary = QColor("#7c2d12");
    harvestCopper.textMuted = QColor("#9a3412");
    harvestCopper.accentPrimary = QColor("#c2410c");
    harvestCopper.accentSuccess = QColor("#15803d");
    harvestCopper.accentWarning = QColor("#ca8a04");
    harvestCopper.accentDanger = QColor("#b91c1c");
    harvestCopper.borderColor = QColor("#fdba74");
    harvestCopper.borderRadiusMd = 8;
    m_registeredThemes.insert(harvestCopper.id, harvestCopper);
    appendThemeOrder(harvestCopper.id);

    // --- PAIR 11: Midnight Onyx & Minimal Pearl Pair ---
    ThemeTokens midnightOnyx;
    midnightOnyx.id = "midnight_onyx_dark";
    midnightOnyx.name = "🌙 Midnight Onyx (Monochrome Dark)";
    midnightOnyx.isDark = true;
    midnightOnyx.bgBase = QColor("#121214");
    midnightOnyx.bgSurface = QColor("#1c1c20");
    midnightOnyx.bgOverlay = QColor("#28282e");
    midnightOnyx.bgInput = QColor("#0a0a0c");
    midnightOnyx.textPrimary = QColor("#fafafa");
    midnightOnyx.textSecondary = QColor("#e4e4e7");
    midnightOnyx.textMuted = QColor("#a1a1aa");
    midnightOnyx.accentPrimary = QColor("#e4e4e7");
    midnightOnyx.accentSuccess = QColor("#4ade80");
    midnightOnyx.accentWarning = QColor("#fbbf24");
    midnightOnyx.accentDanger = QColor("#f87171");
    midnightOnyx.borderColor = QColor("#3f3f46");
    midnightOnyx.borderRadiusMd = 4;
    m_registeredThemes.insert(midnightOnyx.id, midnightOnyx);
    appendThemeOrder(midnightOnyx.id);

    ThemeTokens minimalPearl;
    minimalPearl.id = "minimal_pearl_light";
    minimalPearl.name = "☀️ Minimal Pearl (Monochrome Light)";
    minimalPearl.isDark = false;
    minimalPearl.bgBase = QColor("#fafafa");
    minimalPearl.bgSurface = QColor("#f4f4f5");
    minimalPearl.bgOverlay = QColor("#e4e4e7");
    minimalPearl.bgInput = QColor("#ffffff");
    minimalPearl.textPrimary = QColor("#09090b");
    minimalPearl.textSecondary = QColor("#18181b");
    minimalPearl.textMuted = QColor("#3f3f46");
    minimalPearl.accentPrimary = QColor("#18181b");
    minimalPearl.accentSuccess = QColor("#16a34a");
    minimalPearl.accentWarning = QColor("#d97706");
    minimalPearl.accentDanger = QColor("#dc2626");
    minimalPearl.borderColor = QColor("#d4d4d8");
    minimalPearl.borderRadiusMd = 4;
    m_registeredThemes.insert(minimalPearl.id, minimalPearl);
    appendThemeOrder(minimalPearl.id);

    // --- PAIR 12: Cosmic Nebula & Aurora Pair ---
    ThemeTokens cosmicNebula;
    cosmicNebula.id = "cosmic_nebula_dark";
    cosmicNebula.name = "🌙 Cosmic Nebula (Dark Deep Space)";
    cosmicNebula.isDark = true;
    cosmicNebula.bgBase = QColor("#0b0c16");
    cosmicNebula.bgSurface = QColor("#141628");
    cosmicNebula.bgOverlay = QColor("#202340");
    cosmicNebula.bgInput = QColor("#06070e");
    cosmicNebula.textPrimary = QColor("#f1f5f9");
    cosmicNebula.textSecondary = QColor("#e2e8f0");
    cosmicNebula.textMuted = QColor("#94a3b8");
    cosmicNebula.accentPrimary = QColor("#a855f7");
    cosmicNebula.accentSuccess = QColor("#34d399");
    cosmicNebula.accentWarning = QColor("#fbbf24");
    cosmicNebula.accentDanger = QColor("#f43f5e");
    cosmicNebula.borderColor = QColor("#202340");
    cosmicNebula.borderRadiusMd = 8;
    m_registeredThemes.insert(cosmicNebula.id, cosmicNebula);
    appendThemeOrder(cosmicNebula.id);

    ThemeTokens auroraSky;
    auroraSky.id = "aurora_sky_light";
    auroraSky.name = "☀️ Aurora Sky (Light Pastel Sky)";
    auroraSky.isDark = false;
    auroraSky.bgBase = QColor("#f5f3ff");
    auroraSky.bgSurface = QColor("#ede9fe");
    auroraSky.bgOverlay = QColor("#ddd6fe");
    auroraSky.bgInput = QColor("#ffffff");
    auroraSky.textPrimary = QColor("#1e1b4b");
    auroraSky.textSecondary = QColor("#312e81");
    auroraSky.textMuted = QColor("#4338ca");
    auroraSky.accentPrimary = QColor("#6d28d9");
    auroraSky.accentSuccess = QColor("#059669");
    auroraSky.accentWarning = QColor("#d97706");
    auroraSky.accentDanger = QColor("#e11d48");
    auroraSky.borderColor = QColor("#c4b5fd");
    auroraSky.borderRadiusMd = 8;
    m_registeredThemes.insert(auroraSky.id, auroraSky);
    appendThemeOrder(auroraSky.id);
}

void ThemeManager::zoomIn() {
    int nextPx = qMin(m_currentTokens.fontSizePx + 1, 18);
    setFontSize(nextPx);
}

void ThemeManager::zoomOut() {
    int nextPx = qMax(m_currentTokens.fontSizePx - 1, 11);
    setFontSize(nextPx);
}

void ThemeManager::setFontSize(int px) {
    m_currentTokens.fontSizePx = qBound(11, px, 18);
    AppSettings().setValue("fontSizePx", m_currentTokens.fontSizePx);
    applyCustomTokens(m_currentTokens);
}

bool ThemeManager::generateBrandTheme(const QColor& brandColor, bool darkBase, const QString& label) {
    const bool isChurch = label.isEmpty() || label == QStringLiteral("Church Brand Theme");
    const QString displayLabel = isChurch ? QStringLiteral("Church Brand Theme") : label;
    ThemeTokens tok;
    tok.id = isChurch ? QStringLiteral("church_brand") : QStringLiteral("brand_theme");
    tok.name = displayLabel;
    tok.isDark = darkBase;
    tok.isCustom = true;

    tok.accentPrimary = brandColor;
    tok.accentSuccess = QColor("#a6e3a1");
    tok.accentWarning = QColor("#f9e2af");
    tok.accentDanger = QColor("#f38ba8");

    if (darkBase) {
        tok.bgBase = brandColor.darker(350);
        tok.bgSurface = brandColor.darker(250);
        tok.bgOverlay = brandColor.darker(180);
        tok.bgInput = brandColor.darker(400);
        tok.textPrimary = QColor("#ffffff");
        tok.textSecondary = QColor("#e0e0e0");
        tok.textMuted = QColor("#a0a0a0");
        tok.borderColor = brandColor.darker(160);
    } else {
        tok.bgBase = QColor("#f8f9fa");
        tok.bgSurface = QColor("#ffffff");
        tok.bgOverlay = QColor("#e9ecef");
        tok.bgInput = QColor("#ffffff");
        tok.textPrimary = QColor("#212529");
        tok.textSecondary = QColor("#495057");
        tok.textMuted = QColor("#6c757d");
        tok.borderColor = brandColor.lighter(170);
    }
    tok.borderRadiusMd = 8;
    return saveCustomPreset(displayLabel, tok);
}

bool ThemeManager::generateChurchBrandTheme(const QColor& brandColor, bool darkBase) {
    // Product-specific alias retained for binary/source compatibility.
    return generateBrandTheme(brandColor, darkBase, QStringLiteral("Church Brand Theme"));
}

void ThemeManager::reloadCustomThemesFromDisk() {
    QDir dir(customThemesDir());
    QStringList files = dir.entryList({"*.json"}, QDir::Files);
    for (const QString& file : files) {
        QFile f(dir.absoluteFilePath(file));
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                QJsonObject json = doc.object();
                ThemeTokens t;
                t.id = json.value("id").toString(QFileInfo(file).baseName());
                t.name = json.value("name").toString(t.id);
                t.isDark = json.value("isDark").toBool(true);
                t.isCustom = true;

                t.bgBase = QColor(json.value("bgBase").toString("#181825"));
                t.bgSurface = QColor(json.value("bgSurface").toString("#1e1e2e"));
                t.bgOverlay = QColor(json.value("bgOverlay").toString("#313244"));
                t.bgInput = QColor(json.value("bgInput").toString("#11111b"));

                t.textPrimary = QColor(json.value("textPrimary").toString("#cdd6f4"));
                t.textSecondary = QColor(json.value("textSecondary").toString("#a6adc8"));
                t.textMuted = QColor(json.value("textMuted").toString("#6c7086"));

                t.accentPrimary = QColor(json.value("accentPrimary").toString("#89b4fa"));
                t.accentSuccess = QColor(json.value("accentSuccess").toString("#a6e3a1"));
                t.accentWarning = QColor(json.value("accentWarning").toString("#f9e2af"));
                t.accentDanger = QColor(json.value("accentDanger").toString("#f38ba8"));

                t.borderColor = QColor(json.value("borderColor").toString("#313244"));
                t.borderRadiusMd = json.value("borderRadiusMd").toInt(8);

                m_registeredThemes.insert(t.id, t);
                appendThemeOrder(t.id);
            }
            f.close();
        }
    }
}

QStringList ThemeManager::availableThemes() const {
    // Return themes in the intentional registration order (default pair first,
    // System Auto near the top) rather than alphabetical map order.
    QStringList ordered;
    for (const QString& id : m_themeOrder) {
        if (m_registeredThemes.contains(id)) {
            ordered.append(id);
        }
    }
    // Include any themes not tracked in the order list (defensive).
    for (const QString& id : m_registeredThemes.keys()) {
        if (!ordered.contains(id)) {
            ordered.append(id);
        }
    }
    return ordered;
}

ThemeTokens ThemeManager::getThemeTokens(const QString& themeId) const {
    return m_registeredThemes.value(themeId, m_currentTokens);
}

QString ThemeManager::activeThemeId() const {
    return m_activeThemeId;
}

ThemeTokens ThemeManager::currentTokens() const {
    return m_currentTokens;
}

bool ThemeManager::loadTheme(const QString& themeId) {
    if (!m_registeredThemes.contains(themeId)) return false;

    AppSettings settings;
    // Interactive color tweaks belong to the preset they were created on, so
    // re-applying that preset (e.g. every Settings save) must preserve them.
    // Only a genuine switch to a different preset abandons them.
    const QString customJson = settings.value("customThemeJson").toString();
    const bool hasCustomization =
        settings.value("themeIsCustom", false).toBool() &&
        !customJson.isEmpty() &&
        !QJsonDocument::fromJson(customJson.toUtf8()).object().isEmpty();
    const QString customizationOwner =
        settings.value("customThemeBaseId",
                       settings.value("activeThemeId").toString()).toString();
    const bool keepCustomization = hasCustomization && customizationOwner == themeId;

    m_activeThemeId = themeId;
    if (themeId == "system_auto") {
        bool dark = isSystemDark();
        QString targetId = dark ? "catppuccin_mocha" : "catppuccin_latte";
        m_currentTokens = m_registeredThemes.value(targetId);
    } else {
        m_currentTokens = m_registeredThemes.value(themeId);
    }

    // Restore the user's font-size preference, which is theme-independent and
    // should survive both restarts and theme switches.
    m_currentTokens.fontSizePx = qBound(11, settings.value("fontSizePx", 13).toInt(), 18);

    settings.setValue("activeThemeId", themeId);

    if (keepCustomization) {
        // Re-apply the saved tweaks on top of the preset. restoreColorCustomization()
        // also persists themeIsCustom=true and refreshes palette/QSS/signal.
        restoreColorCustomization();
        settings.setValue("isDarkMode", m_currentTokens.isDark);
        return true;
    }

    // Selecting a different preset shows that preset's true colors: abandon any
    // saved interactive color tweaks.
    settings.setValue("themeIsCustom", false);
    settings.remove("customThemeJson");
    settings.remove("customThemeBaseId");
    settings.setValue("isDarkMode", m_currentTokens.isDark);

    if (qApp) {
        qApp->setPalette(generateQPalette());
        qApp->setStyleSheet(generateGlobalQSS());
    }

    emit themeChanged(m_currentTokens);
    return true;
}

bool ThemeManager::applyCustomTokens(const ThemeTokens& tokens) {
    m_currentTokens = tokens;

    // Persist the interactive color/radius customization immediately so the
    // live tweaks are also the saved state ("Save All Changes" model).
    QJsonObject obj;
    obj["bgBase"] = m_currentTokens.bgBase.name();
    obj["bgSurface"] = m_currentTokens.bgSurface.name();
    obj["bgOverlay"] = m_currentTokens.bgOverlay.name();
    obj["bgInput"] = m_currentTokens.bgInput.name();
    obj["textPrimary"] = m_currentTokens.textPrimary.name();
    obj["textSecondary"] = m_currentTokens.textSecondary.name();
    obj["textMuted"] = m_currentTokens.textMuted.name();
    obj["accentPrimary"] = m_currentTokens.accentPrimary.name();
    obj["accentSuccess"] = m_currentTokens.accentSuccess.name();
    obj["accentWarning"] = m_currentTokens.accentWarning.name();
    obj["accentDanger"] = m_currentTokens.accentDanger.name();
    obj["borderColor"] = m_currentTokens.borderColor.name();
    obj["borderRadiusMd"] = m_currentTokens.borderRadiusMd;
    AppSettings s;
    s.setValue("customThemeJson", QJsonDocument(obj).toJson(QJsonDocument::Compact));
    s.setValue("themeIsCustom", true);
    // Remember which preset these tweaks were applied to, so re-applying that
    // preset keeps them (and switching to another one discards them).
    if (!m_activeThemeId.isEmpty()) {
        s.setValue("customThemeBaseId", m_activeThemeId);
    }

    if (qApp) {
        qApp->setPalette(generateQPalette());
        qApp->setStyleSheet(generateGlobalQSS());
    }

    emit themeChanged(m_currentTokens);
    return true;
}

void ThemeManager::clearCustomizationOverride() {
    AppSettings s;
    s.remove("customThemeJson");
    s.remove("customThemeBaseId");
    s.remove("themeIsCustom");
}

bool ThemeManager::saveCustomPreset(const QString& presetName, const ThemeTokens& tokens) {
    QString safeId = QString(presetName).toLower().replace(" ", "_");
    ThemeTokens t = tokens;
    t.id = safeId;
    t.name = presetName;
    t.isCustom = true;

    QJsonObject json;
    json["id"] = t.id;
    json["name"] = t.name;
    json["isDark"] = t.isDark;
    json["bgBase"] = t.bgBase.name();
    json["bgSurface"] = t.bgSurface.name();
    json["bgOverlay"] = t.bgOverlay.name();
    json["bgInput"] = t.bgInput.name();
    json["textPrimary"] = t.textPrimary.name();
    json["textSecondary"] = t.textSecondary.name();
    json["textMuted"] = t.textMuted.name();
    json["accentPrimary"] = t.accentPrimary.name();
    json["accentSuccess"] = t.accentSuccess.name();
    json["accentWarning"] = t.accentWarning.name();
    json["accentDanger"] = t.accentDanger.name();
    json["borderColor"] = t.borderColor.name();
    json["borderRadiusMd"] = t.borderRadiusMd;

    QString filePath = QString("%1/%2.json").arg(customThemesDir(), safeId);
    QFile f(filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(json).toJson());
        f.close();
        m_registeredThemes.insert(t.id, t);
        appendThemeOrder(t.id);
        loadTheme(t.id);
        return true;
    }
    return false;
}

QPalette ThemeManager::generateQPalette() const {
    QPalette pal;
    pal.setColor(QPalette::Window, m_currentTokens.bgBase);
    pal.setColor(QPalette::WindowText, m_currentTokens.textPrimary);
    pal.setColor(QPalette::Base, m_currentTokens.bgInput);
    pal.setColor(QPalette::AlternateBase, m_currentTokens.bgSurface);
    pal.setColor(QPalette::ToolTipBase, m_currentTokens.bgOverlay);
    pal.setColor(QPalette::ToolTipText, m_currentTokens.textPrimary);
    pal.setColor(QPalette::Text, m_currentTokens.textPrimary);
    pal.setColor(QPalette::Button, m_currentTokens.bgSurface);
    pal.setColor(QPalette::ButtonText, m_currentTokens.textPrimary);
    pal.setColor(QPalette::Highlight, m_currentTokens.accentPrimary);
    pal.setColor(QPalette::PlaceholderText, m_currentTokens.textMuted);
    pal.setColor(QPalette::Link, m_currentTokens.accentPrimary);
    pal.setColor(QPalette::LinkVisited, m_currentTokens.accentPrimary);

    pal.setColor(QPalette::Mid, m_currentTokens.bgOverlay);
    pal.setColor(QPalette::Midlight, m_currentTokens.bgSurface);
    pal.setColor(QPalette::Dark, m_currentTokens.borderColor);
    pal.setColor(QPalette::Light, m_currentTokens.bgBase);
    pal.setColor(QPalette::Shadow, m_currentTokens.isDark ? QColor(0, 0, 0, 160) : QColor(0, 0, 0, 40));

    pal.setColor(QPalette::HighlightedText, textOn(m_currentTokens.accentPrimary));

    // Disabled state: muted text on the surface color for legibility.
    pal.setColor(QPalette::Disabled, QPalette::Text, m_currentTokens.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, m_currentTokens.textMuted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, m_currentTokens.textMuted);
    return pal;
}

// WCAG 2.1 Relative Luminance and Contrast Ratio calculations
double ThemeManager::relativeLuminance(const QColor& c) {
    auto channelLum = [](double srgbNorm) {
        return (srgbNorm <= 0.04045) ? (srgbNorm / 12.92) : std::pow((srgbNorm + 0.055) / 1.055, 2.4);
    };
    double r = channelLum(c.redF());
    double g = channelLum(c.greenF());
    double b = channelLum(c.blueF());
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double ThemeManager::contrastRatio(const QColor& c1, const QColor& c2) {
    double l1 = relativeLuminance(c1);
    double l2 = relativeLuminance(c2);
    double lighter = qMax(l1, l2);
    double darker = qMin(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

QString ThemeManager::contrastRating(double ratio) {
    if (ratio >= 7.0) {
        return QString("🟢 AAA Pass (%1:1)").arg(ratio, 0, 'f', 1);
    } else if (ratio >= 4.5) {
        return QString("🟡 AA Pass (%1:1)").arg(ratio, 0, 'f', 1);
    } else {
        return QString("🔴 Fail (%1:1 < 4.5:1)").arg(ratio, 0, 'f', 1);
    }
}

QColor ThemeManager::textOn(const QColor& c) {
    // Pick whichever canonical text color has the higher measured contrast.
    // A single lightness threshold misjudges saturated accents: orange
    // #f97316, teal #06b6d4 and lime #00ff00 all read as "light" by
    // QColor::lightness() yet need dark text to clear WCAG AA.
    const QColor dark("#11111b");
    const QColor light("#ffffff");
    return contrastRatio(c, dark) >= contrastRatio(c, light) ? dark : light;
}

QColor ThemeManager::hoverOn(const QColor& c) {
    return (c.lightness() > 128) ? c.darker(112) : c.lighter(115);
}

QColor ThemeManager::pressedOn(const QColor& c) {
    return (c.lightness() > 128) ? c.darker(122) : c.lighter(125);
}

QColor ThemeManager::disabledTextOn(const QColor& c) {
    return textOn(c).lighter(150);
}

QString ThemeManager::generateGlobalQSS() const {
    const auto& t = m_currentTokens;

    const QString primaryBtnTextColor = textOn(t.accentPrimary).name();
    const QString dangerBtnTextColor  = textOn(t.accentDanger).name();
    const QString successBtnTextColor = textOn(t.accentSuccess).name();

    // Harmonized accent variants derived from the theme's primary hue so the
    // cyan/orange/pink accent buttons stay on-palette in both dark and light.
    const int hue = t.accentPrimary.hue() >= 0 ? t.accentPrimary.hue() : 0;
    const QColor accentCyan   = QColor::fromHsv((hue + 40) % 360, 200, 225);
    const QColor accentOrange = QColor::fromHsv((hue + 120) % 360, 210, 215);
    const QColor accentPink   = QColor::fromHsv((hue + 300) % 360, 190, 230);

    // Subtle accent-tinted background for the active sidebar nav item.
    QColor navActiveBg = t.accentPrimary;
    navActiveBg.setAlpha(28);

    // Hover states: slightly darker (or lighter for dark accents).
    const QColor primaryHover = (t.accentPrimary.lightness() > 128)
                                    ? t.accentPrimary.darker(112) : t.accentPrimary.lighter(115);
    const QColor dangerHover  = (t.accentDanger.lightness() > 128)
                                    ? t.accentDanger.darker(112)  : t.accentDanger.lighter(115);
    const QColor surfaceHover = (t.bgSurface.lightness() > 128)
                                    ? t.bgSurface.darker(104)     : t.bgSurface.lighter(112);
    const QColor cyanHover    = (accentCyan.lightness() > 128)
                                    ? accentCyan.darker(110)      : accentCyan.lighter(115);
    const QColor orangeHover  = (accentOrange.lightness() > 128)
                                    ? accentOrange.darker(110)    : accentOrange.lighter(115);
    const QColor pinkHover    = (accentPink.lightness() > 128)
                                    ? accentPink.darker(110)      : accentPink.lighter(115);

    // Pressed states for tactile click feedback.
    const QColor primaryPressed = (t.accentPrimary.lightness() > 128)
                                      ? t.accentPrimary.darker(122) : t.accentPrimary.lighter(125);
    const QColor dangerPressed  = (t.accentDanger.lightness() > 128)
                                      ? t.accentDanger.darker(122)  : t.accentDanger.lighter(125);
    const QColor surfacePressed = (t.bgSurface.lightness() > 128)
                                      ? t.bgSurface.darker(112)     : t.bgSurface.lighter(118);

    QMap<QString, QString> tokenMap = {
        {"--bg-base", t.bgBase.name()},
        {"--bg-surface", t.bgSurface.name()},
        {"--bg-overlay", t.bgOverlay.name()},
        {"--bg-input", t.bgInput.name()},
        {"--text-primary", t.textPrimary.name()},
        {"--text-secondary", t.textSecondary.name()},
        {"--text-muted", t.textMuted.name()},
        {"--accent-primary", t.accentPrimary.name()},
        {"--accent-success", t.accentSuccess.name()},
        {"--accent-warning", t.accentWarning.name()},
        {"--accent-danger", t.accentDanger.name()},
        {"--border-color", t.borderColor.name()},
        {"--radius-sm", QString::number(t.borderRadiusSm) + "px"},
        {"--radius-md", QString::number(t.borderRadiusMd) + "px"},
        {"--font-size", QString::number(t.fontSizePx) + "px"},
        {"--text-on-primary", primaryBtnTextColor},
        {"--text-on-danger", dangerBtnTextColor},
        {"--text-on-success", successBtnTextColor},
        {"--accent-cyan", accentCyan.name()},
        {"--accent-orange", accentOrange.name()},
        {"--accent-pink", accentPink.name()},
        {"--text-on-cyan", textOn(accentCyan).name()},
        {"--text-on-orange", textOn(accentOrange).name()},
        {"--text-on-pink", textOn(accentPink).name()},
        {"--nav-active-bg", navActiveBg.name(QColor::HexArgb)},
        {"--surface-hover", surfaceHover.name()},
        {"--primary-hover", primaryHover.name()},
        {"--danger-hover", dangerHover.name()},
        {"--cyan-hover", cyanHover.name()},
        {"--orange-hover", orangeHover.name()},
        {"--pink-hover", pinkHover.name()},
        {"--surface-pressed", surfacePressed.name()},
        {"--primary-pressed", primaryPressed.name()},
        {"--danger-pressed", dangerPressed.name()},
        // Translucent accent used by the item-view ::item:selected rule: an
        // overlay instead of the opaque palette highlight so custom cell
        // backgrounds (status chips) stay visible through the selection.
        {"--selection-overlay", [&t] {
             QColor overlay = t.accentPrimary;
             overlay.setAlpha(70);
             return overlay.name(QColor::HexArgb);
         }()}
    };

    QString qss = R"(
QMainWindow, QDialog { background-color: var(--bg-base); color: var(--text-primary); font-family: 'Segoe UI', system-ui, sans-serif; font-size: var(--font-size); }
QWidget#mainContent { background-color: var(--bg-base); }
QWidget { color: var(--text-primary); font-size: var(--font-size); }
QLabel { color: var(--text-primary); font-size: var(--font-size); }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QTextEdit, QPlainTextEdit {
  background-color: var(--bg-input); border: 1px solid var(--border-color); border-radius: var(--radius-md); color: var(--text-primary); padding: 6px 10px; font-size: var(--font-size);
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateEdit:focus {
  border: 1px solid var(--accent-primary);
}
QPushButton {
  background-color: var(--bg-surface); border: 1px solid var(--border-color); color: var(--text-primary); padding: 8px 16px; border-radius: var(--radius-md); font-weight: bold; font-size: var(--font-size);
}
QPushButton:hover { background-color: var(--surface-hover); }
QPushButton:pressed { background-color: var(--surface-pressed); }
QPushButton:disabled {
  background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color);
}
QPushButton#primaryBtn, QPushButton#saveBtn {
  background-color: var(--accent-primary); color: var(--text-on-primary); border: none;
}
QPushButton#primaryBtn:hover, QPushButton#saveBtn:hover { background-color: var(--primary-hover); }
QPushButton#primaryBtn:pressed, QPushButton#saveBtn:pressed { background-color: var(--primary-pressed); }
QPushButton#primaryBtn:disabled, QPushButton#saveBtn:disabled {
  background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color);
}
QPushButton#secondaryBtn {
  background-color: var(--bg-surface); color: var(--text-primary); border: 1px solid var(--border-color);
}
QPushButton#secondaryBtn:hover { background-color: var(--surface-hover); }
QPushButton#secondaryBtn:pressed { background-color: var(--surface-pressed); }
QPushButton#secondaryBtn:disabled {
  background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color);
}
QPushButton#dangerBtn {
  background-color: var(--accent-danger); color: var(--text-on-danger); border: none;
}
QPushButton#dangerBtn:hover { background-color: var(--danger-hover); }
QPushButton#dangerBtn:pressed { background-color: var(--danger-pressed); }
QPushButton#dangerBtn:disabled {
  background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color);
}
QPushButton#ghostDangerBtn, QPushButton#dangerGhostBtn {
  background: transparent; color: var(--accent-danger); border: none; font-weight: bold;
}
QPushButton#ghostDangerBtn:hover, QPushButton#dangerGhostBtn:hover { text-decoration: underline; }
QPushButton#ghostDangerBtn:disabled, QPushButton#dangerGhostBtn:disabled { color: var(--text-muted); text-decoration: none; }
QPushButton#accentBtnCyan   { background-color: var(--accent-cyan); color: var(--text-on-cyan); border: none; }
QPushButton#accentBtnCyan:hover   { background-color: var(--cyan-hover); }
QPushButton#accentBtnCyan:disabled { background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color); }
QPushButton#accentBtnOrange { background-color: var(--accent-orange); color: var(--text-on-orange); border: none; }
QPushButton#accentBtnOrange:hover { background-color: var(--orange-hover); }
QPushButton#accentBtnOrange:disabled { background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color); }
QPushButton#accentBtnPink   { background-color: var(--accent-pink); color: var(--text-on-pink); border: none; }
QPushButton#accentBtnPink:hover   { background-color: var(--pink-hover); }
QPushButton#accentBtnPink:disabled { background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color); }
QPushButton#navButton {
  background: transparent; border: none; text-align: left; padding: 9px 12px; border-radius: 6px; color: var(--text-primary); font-weight: 500;
}
QPushButton#navButton:hover { background-color: var(--bg-surface); }
QPushButton#navButton[active="true"] { background-color: var(--nav-active-bg); color: var(--accent-primary); font-weight: bold; }
QPushButton#checkinBtn {
  background-color: var(--accent-success); color: var(--text-on-success); border: none; border-radius: 10px; font-size: 18px;
}
QPushButton#checkinBtn:disabled { background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color); }
QPushButton#unlockBtn {
  background-color: var(--accent-danger); color: var(--text-on-danger); border: none; border-radius: 8px; font-size: 15px;
}
QPushButton#unlockBtn:disabled { background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color); }
QPushButton#sectionHeaderBtn {
  font-size: 11px; font-weight: bold; color: var(--accent-primary); text-align: left; background: transparent; border: none; padding: 4px 0px;
}
QGroupBox {
  background-color: var(--bg-surface); border: 1px solid var(--border-color); border-radius: var(--radius-md); margin-top: 12px; font-weight: bold; color: var(--accent-primary);
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: var(--accent-primary); }
QTableView, QTableWidget, QListView, QListWidget, QTreeWidget {
  background-color: var(--bg-input); alternate-background-color: var(--bg-base); border: 1px solid var(--border-color); gridline-color: var(--border-color); color: var(--text-primary); border-radius: var(--radius-md); font-size: var(--font-size);
}
QHeaderView::section {
  background-color: var(--bg-surface); color: var(--text-primary); font-weight: bold; border: none; border-bottom: 2px solid var(--accent-primary); padding: 6px; font-size: var(--font-size);
}
/* Table consistency: unify font sizing across modules regardless of
   per-widget font-size overrides embedded in code; keep header slightly
   smaller than body for hierarchy without a different family/weight. */
QTableView, QTableWidget { font-size: var(--font-size); }
QTableView QHeaderView::section, QTableWidget QHeaderView::section { font-size: calc(var(--font-size) - 1px); }
QTableView::item, QTableWidget::item { padding: 4px 6px; }
/* Selection = translucent accent OVERLAY, not the opaque palette highlight.
   Cells with custom backgrounds (planning status chips, gantt marks) were
   hidden behind the flat system blue whenever clicked — the overlay lets the
   chip color read through while keeping selection obvious. No color rule:
   selected items keep their own foreground (chips use dark on-color text). */
QTableView::item:selected, QTableWidget::item:selected, QListView::item:selected,
QListWidget::item:selected, QTreeView::item:selected, QTreeWidget::item:selected {
  background-color: var(--selection-overlay);
}
QTabWidget::pane { border: 1px solid var(--border-color); background-color: var(--bg-surface); border-radius: var(--radius-md); }
QTabBar::tab { background-color: var(--bg-input); color: var(--text-secondary); padding: 10px 20px; border-top-left-radius: 6px; border-top-right-radius: 6px; font-weight: bold; border: 1px solid var(--border-color); border-bottom: none; font-size: var(--font-size); }
QTabBar::tab:selected { background-color: var(--bg-surface); color: var(--text-primary); border-bottom: 3px solid var(--accent-primary); }
QFrame#cardWidget, QFrame#hintPanel {
  background-color: var(--bg-surface); border: 1px solid var(--border-color); border-radius: var(--radius-md);
}
QCheckBox, QRadioButton {
  color: var(--text-primary); font-weight: bold; font-size: var(--font-size);
}
QComboBox {
  padding: 6px 28px 6px 10px;
}
QComboBox:hover {
  border: 1px solid var(--accent-primary);
}
QComboBox:on {
  border: 1px solid var(--accent-primary);
}
QComboBox:disabled {
  background-color: var(--bg-input); color: var(--text-muted); border: 1px solid var(--border-color);
}
QComboBox::drop-down {
  subcontrol-origin: padding; subcontrol-position: top right; width: 26px; border: none;
}
QComboBox::down-arrow {
  image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid var(--text-secondary); width: 0px; height: 0px; margin-right: 8px;
}
QComboBox::down-arrow:hover {
  border-top: 5px solid var(--text-primary);
}
QComboBox::down-arrow:on {
  border-top: none; border-bottom: 5px solid var(--accent-primary);
}
QComboBox::down-arrow:disabled {
  border-top: 5px solid var(--text-muted);
}
QComboBox QAbstractItemView {
  background-color: var(--bg-surface); color: var(--text-primary); border: 1px solid var(--border-color); selection-background-color: var(--accent-primary); selection-color: var(--text-on-primary); border-radius: var(--radius-md); padding: 4px; outline: none;
}
QComboBox QAbstractItemView::item {
  min-height: 24px; padding: 4px 8px; border-radius: 4px;
}
QComboBox QAbstractItemView::item:selected {
  background-color: var(--accent-primary); color: var(--text-on-primary);
}
QDateEdit::drop-down {
  subcontrol-origin: padding; subcontrol-position: top right; width: 26px; border: none;
}
QDateEdit::down-arrow {
  image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid var(--text-secondary); width: 0px; height: 0px; margin-right: 8px;
}
QDateEdit::down-arrow:hover {
  border-top: 5px solid var(--text-primary);
}
QDateEdit::down-arrow:disabled {
  border-top: 5px solid var(--text-muted);
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
  subcontrol-origin: border; subcontrol-position: top right; width: 18px; border-left: 1px solid var(--border-color); border-bottom: 1px solid var(--border-color); background: var(--bg-surface); border-top-right-radius: var(--radius-md);
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover {
  background: var(--surface-hover);
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
  image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 4px solid var(--text-secondary); width: 0px; height: 0px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
  subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; border-left: 1px solid var(--border-color); background: var(--bg-surface); border-bottom-right-radius: var(--radius-md);
}
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
  background: var(--surface-hover);
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
  image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 4px solid var(--text-secondary); width: 0px; height: 0px;
}
QCalendarWidget QWidget { alternate-background-color: var(--bg-surface); background-color: var(--bg-surface); }
QCalendarWidget QAbstractItemView:enabled { color: var(--text-primary); selection-background-color: var(--accent-primary); selection-color: var(--text-on-primary); }
QCalendarWidget QMenu { background-color: var(--bg-surface); color: var(--text-primary); }
QCalendarWidget QSpinBox { background-color: var(--bg-input); color: var(--text-primary); }
QMenuBar {
  background-color: var(--bg-base); color: var(--text-primary); padding: 2px 6px; border-bottom: 1px solid var(--border-color); font-size: var(--font-size);
}
QMenuBar::item {
  background: transparent; padding: 5px 10px; border-radius: var(--radius-sm); color: var(--text-primary); font-weight: 500;
}
QMenuBar::item:selected {
  background-color: var(--bg-surface); color: var(--accent-primary);
}
QMenuBar::item:pressed {
  background-color: var(--surface-pressed); color: var(--accent-primary);
}
QMenu {
  background-color: var(--bg-surface); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-md); padding: 4px; font-size: var(--font-size);
}
QMenu::item {
  color: var(--text-primary); padding: 6px 24px 6px 26px; border-radius: var(--radius-sm);
}
QMenu::item:selected {
  background-color: var(--accent-primary); color: var(--text-on-primary);
}
QMenu::item:disabled {
  color: var(--text-muted);
}
QMenu::icon {
  padding-left: 6px;
}
QMenu::separator {
  height: 1px; background-color: var(--border-color); margin: 4px 8px;
}
QToolTip {
  background-color: var(--bg-overlay); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); padding: 6px 8px; font-size: var(--font-size);
}
QProgressBar {
  background-color: var(--bg-input); border: 1px solid var(--border-color); border-radius: var(--radius-md); color: var(--text-primary); text-align: center;
}
QProgressBar::chunk {
  background-color: var(--accent-primary); border-radius: var(--radius-md);
}
QSplitter::handle { background-color: var(--border-color); }
QSplitter::handle:hover { background-color: var(--accent-primary); }
QDialogButtonBox QPushButton { min-width: 80px; }
QScrollBar:vertical, QScrollBar:horizontal { background-color: var(--bg-base); border: none; margin: 0; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background-color: var(--bg-input); border-radius: var(--radius-sm); min-height: 24px; min-width: 24px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background-color: var(--surface-hover); }
QScrollBar::add-line, QScrollBar::sub-line { border: none; background: none; width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)";

    for (auto it = tokenMap.cbegin(); it != tokenMap.cend(); ++it) {
        qss.replace(QString("var(%1)").arg(it.key()), it.value());
    }
    return qss;
}

