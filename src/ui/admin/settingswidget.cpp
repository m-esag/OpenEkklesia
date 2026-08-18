#include "settingswidget.h"
#include "thememanager.h"
#include "../database/databasemanager.h"
#include "../database/datasharing.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QInputDialog>
#include <QStandardPaths>
#include <QSettings>
#include <QListWidget>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QScrollArea>
#include <QSplitter>

SettingsWidget::SettingsWidget(QWidget *parent)
    : QWidget(parent)
    , m_isDirty(false)
{
    setupUI();
    loadSettings();
}

void SettingsWidget::setupUI() {
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 24, 24, 24);
    rootLayout->setSpacing(16);

    // --- Top Bar: Title, Search, and Save Button ---
    QHBoxLayout* topBar = new QHBoxLayout();
    
    QVBoxLayout* titleCol = new QVBoxLayout();
    QLabel* pageHeader = new QLabel("⚙️ Application Settings & Preferences", this);
    pageHeader->setObjectName("pageHeader");
    m_lblStatus = new QLabel("Configure your church identity, regional formats, theme, and data management.", this);
    m_lblStatus->setStyleSheet("color: #a6adc8; font-size: 13px;");
    titleCol->addWidget(pageHeader);
    titleCol->addWidget(m_lblStatus);
    topBar->addLayout(titleCol);

    topBar->addStretch();

    m_btnSaveSettings = new QPushButton("💾 Save All Changes", this);
    m_btnSaveSettings->setObjectName("saveBtn");
    m_btnSaveSettings->setMinimumHeight(38);
    m_btnSaveSettings->setStyleSheet("padding: 8px 18px; font-weight: bold; font-size: 13px;");
    connect(m_btnSaveSettings, &QPushButton::clicked, this, &SettingsWidget::saveSettings);
    topBar->addWidget(m_btnSaveSettings);

    rootLayout->addLayout(topBar);

    // --- Main Body: Splitter with Left Nav and Right Content Stack ---
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(16);

    // Left Navigation Column
    QWidget* navContainer = new QWidget(this);
    navContainer->setFixedWidth(240);
    QVBoxLayout* navLayout = new QVBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(10);

    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("🔍 Filter settings...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setStyleSheet("padding: 8px 12px; font-size: 13px; border-radius: 6px;");
    connect(m_searchBox, &QLineEdit::textChanged, this, &SettingsWidget::onSearchTextChanged);
    navLayout->addWidget(m_searchBox);

    m_navList = new QListWidget(this);
    m_navList->setSpacing(4);
    m_navList->setStyleSheet(
        "QListWidget { border: none; background: transparent; }"
        "QListWidget::item { padding: 10px 14px; border-radius: 6px; font-size: 13px; font-weight: 500; margin-bottom: 2px; }"
        "QListWidget::item:selected { background-color: #89b4fa; color: #11111b; font-weight: bold; }"
        "QListWidget::item:hover:!selected { background-color: rgba(255, 255, 255, 0.06); }"
    );

    QStringList categories = {
        "🏛️ General & Profile",
        "🎨 Appearance & Theme",
        "🌍 Localization & Region",
        "🕸️ Pastoral & Graph",
        "💾 Data & Backup",
        "⚠️ Danger Zone"
    };

    for (const QString& cat : categories) {
        QListWidgetItem* item = new QListWidgetItem(cat, m_navList);
        item->setSizeHint(QSize(200, 38));
    }

    connect(m_navList, &QListWidget::currentRowChanged, this, &SettingsWidget::onCategorySelected);
    navLayout->addWidget(m_navList);

    bodyLayout->addWidget(navContainer);

    // Right Content Area (QStackedWidget inside ScrollArea)
    m_stackPages = new QStackedWidget(this);

    // -------------------------------------------------------------
    // Page 0: General & Profile
    // -------------------------------------------------------------
    QScrollArea* scrollGeneral = new QScrollArea(this);
    scrollGeneral->setWidgetResizable(true);
    scrollGeneral->setFrameShape(QFrame::NoFrame);
    QWidget* pageGeneral = new QWidget();
    QVBoxLayout* layGeneral = new QVBoxLayout(pageGeneral);
    layGeneral->setContentsMargins(10, 0, 10, 10);
    layGeneral->setSpacing(16);

    QGroupBox* grpChurchProfile = new QGroupBox("🏛️ Church Identity & Organization", pageGeneral);
    QFormLayout* formOrg = new QFormLayout(grpChurchProfile);
    formOrg->setSpacing(12);

    m_txtChurchName = new QLineEdit(pageGeneral);
    m_txtChurchSubtitle = new QLineEdit(pageGeneral);
    m_txtDenomination = new QLineEdit(pageGeneral);
    m_txtAddress = new QLineEdit(pageGeneral);
    m_txtPhone = new QLineEdit(pageGeneral);
    m_txtEmail = new QLineEdit(pageGeneral);
    m_txtWebsite = new QLineEdit(pageGeneral);
    m_txtTaxId = new QLineEdit(pageGeneral);

    formOrg->addRow("Church / Ministry Name:", m_txtChurchName);
    formOrg->addRow("Subtitle / Motto:", m_txtChurchSubtitle);
    formOrg->addRow("Denomination / Affiliation:", m_txtDenomination);
    formOrg->addRow("Physical Address:", m_txtAddress);
    formOrg->addRow("Main Contact Phone:", m_txtPhone);
    formOrg->addRow("Official Email Address:", m_txtEmail);
    formOrg->addRow("Website URL:", m_txtWebsite);
    formOrg->addRow("Tax ID / EIN Number:", m_txtTaxId);

    layGeneral->addWidget(grpChurchProfile);
    layGeneral->addStretch();
    scrollGeneral->setWidget(pageGeneral);
    m_stackPages->addWidget(scrollGeneral);

    // -------------------------------------------------------------
    // Page 1: Appearance & Theme
    // -------------------------------------------------------------
    QScrollArea* scrollTheme = new QScrollArea(this);
    scrollTheme->setWidgetResizable(true);
    scrollTheme->setFrameShape(QFrame::NoFrame);
    QWidget* pageTheme = new QWidget();
    QVBoxLayout* layTheme = new QVBoxLayout(pageTheme);
    layTheme->setContentsMargins(10, 0, 10, 10);
    layTheme->setSpacing(16);

    // Live Preview Card
    QGroupBox* grpPreview = new QGroupBox("👁️ Real-Time Theme Preview", pageTheme);
    QVBoxLayout* prevLay = new QVBoxLayout(grpPreview);
    
    m_previewCard = new QFrame(grpPreview);
    m_previewCard->setObjectName("liveThemePreviewCard");
    m_previewCard->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* cardInner = new QVBoxLayout(m_previewCard);
    cardInner->setContentsMargins(18, 16, 18, 16);
    cardInner->setSpacing(10);

    QHBoxLayout* prevHeaderRow = new QHBoxLayout();
    m_lblPreviewHeader = new QLabel("Grace Community Church • ChMS Preview", m_previewCard);
    m_lblPreviewHeader->setStyleSheet("font-size: 15px; font-weight: bold;");
    m_lblPreviewBadge = new QLabel("ACTIVE PRESET", m_previewCard);
    m_lblPreviewBadge->setStyleSheet("padding: 2px 8px; border-radius: 10px; font-size: 10px; font-weight: bold;");
    prevHeaderRow->addWidget(m_lblPreviewHeader);
    prevHeaderRow->addStretch();
    prevHeaderRow->addWidget(m_lblPreviewBadge);
    cardInner->addLayout(prevHeaderRow);

    m_lblPreviewSub = new QLabel("Sample live controls showing current theme tokens, surface elevation, and typography.", m_previewCard);
    m_lblPreviewSub->setStyleSheet("font-size: 12px; opacity: 0.85;");
    cardInner->addWidget(m_lblPreviewSub);

    QHBoxLayout* prevBtnRow = new QHBoxLayout();
    m_btnPreviewPrimary = new QPushButton("Primary Action", m_previewCard);
    m_btnPreviewSecondary = new QPushButton("Secondary Option", m_previewCard);
    prevBtnRow->addWidget(m_btnPreviewPrimary);
    prevBtnRow->addWidget(m_btnPreviewSecondary);
    prevBtnRow->addStretch();
    cardInner->addLayout(prevBtnRow);

    prevLay->addWidget(m_previewCard);
    layTheme->addWidget(grpPreview);

    // Presets Card
    QGroupBox* grpPreset = new QGroupBox("🎨 Active Theme Presets & Auto-Sync", pageTheme);
    QFormLayout* presetForm = new QFormLayout(grpPreset);
    presetForm->setSpacing(10);

    m_cmbThemeModeFilter = new QComboBox(pageTheme);
    m_cmbThemeModeFilter->addItems({"🎨 All Themes (Dark & Light Pairs)", "🌙 Dark Themes Only", "☀️ Light Themes Only"});

    m_cmbThemePreset = new QComboBox(pageTheme);
    populateThemePresets(0);

    connect(m_cmbThemeModeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        populateThemePresets(index);
    });

    connect(m_cmbThemePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        QString tId = m_cmbThemePreset->itemData(index).toString();
        if (!tId.isEmpty()) {
            ThemeManager::instance().loadTheme(tId);
            updateColorButtonStyles();
            updateLivePreview();
        }
    });

    m_chkSyncSystemTheme = new QCheckBox("Automatically follow OS system theme (Auto-sync dark/light mode)", pageTheme);
    connect(m_chkSyncSystemTheme, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            ThemeManager::instance().loadTheme("system_auto");
            m_cmbThemePreset->setEnabled(false);
            m_cmbThemeModeFilter->setEnabled(false);
        } else {
            m_cmbThemePreset->setEnabled(true);
            m_cmbThemeModeFilter->setEnabled(true);
            QString tId = m_cmbThemePreset->currentData().toString();
            if (!tId.isEmpty() && tId != "system_auto") {
                ThemeManager::instance().loadTheme(tId);
            } else {
                ThemeManager::instance().loadTheme("catppuccin_mocha");
            }
        }
        updateColorButtonStyles();
        updateLivePreview();
    });

    m_chkAutoCollapseSidebar = new QCheckBox("Automatically collapse sidebar on smaller screens (< 940px)", pageTheme);
    m_chkAutoCollapseSidebar->setChecked(true);

    presetForm->addRow("Theme Style Filter:", m_cmbThemeModeFilter);
    presetForm->addRow("Active Preset Theme:", m_cmbThemePreset);
    presetForm->addRow("OS Theme Auto-Sync:", m_chkSyncSystemTheme);
    presetForm->addRow("Responsive Sidebar:", m_chkAutoCollapseSidebar);
    layTheme->addWidget(grpPreset);

    // 1-Click Church Brand Generator
    QGroupBox* grpBrand = new QGroupBox("🏛️ 1-Click Church Brand Theme Generator", pageTheme);
    QFormLayout* brandForm = new QFormLayout(grpBrand);
    brandForm->setSpacing(10);

    m_btnPickBrandColor = new QPushButton("🎨 Pick Primary Church Brand Color", pageTheme);
    m_btnPickBrandColor->setObjectName("primaryBtn");
    connect(m_btnPickBrandColor, &QPushButton::clicked, this, &SettingsWidget::onGenerateChurchBrand);

    m_cmbBrandBaseMode = new QComboBox(pageTheme);
    m_cmbBrandBaseMode->addItems({"Dark Base Theme", "Light Base Theme"});

    brandForm->addRow("Theme Base Mode:", m_cmbBrandBaseMode);
    brandForm->addRow("Brand Accent Color:", m_btnPickBrandColor);
    layTheme->addWidget(grpBrand);

    // Interactive Customization
    QGroupBox* grpCustom = new QGroupBox("🎛️ Color Customization & Scale", pageTheme);
    QFormLayout* customForm = new QFormLayout(grpCustom);
    customForm->setSpacing(10);

    m_btnBgBaseColor = new QPushButton("Pick Base Background", pageTheme);
    m_btnBgSurfaceColor = new QPushButton("Pick Surface/Panel Color", pageTheme);
    m_btnAccentPrimaryColor = new QPushButton("Pick Accent Color", pageTheme);
    m_btnTextPrimaryColor = new QPushButton("Pick Primary Text Color", pageTheme);

    connect(m_btnBgBaseColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);
    connect(m_btnBgSurfaceColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);
    connect(m_btnAccentPrimaryColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);
    connect(m_btnTextPrimaryColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);

    m_spnCornerRadius = new QSpinBox(pageTheme);
    m_spnCornerRadius->setRange(0, 16);
    m_spnCornerRadius->setSuffix(" px");
    m_spnCornerRadius->setValue(ThemeManager::instance().currentTokens().borderRadiusMd);
    connect(m_spnCornerRadius, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsWidget::onRadiusSliderChanged);

    m_spnFontSize = new QSpinBox(pageTheme);
    m_spnFontSize->setRange(11, 18);
    m_spnFontSize->setSuffix(" px (Ctrl +/-)");
    m_spnFontSize->setValue(ThemeManager::instance().currentTokens().fontSizePx);
    connect(m_spnFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsWidget::onFontSizeSliderChanged);

    QHBoxLayout* presetBtns = new QHBoxLayout();
    m_btnSavePreset = new QPushButton("💾 Save Custom Preset (.json)", pageTheme);
    m_btnSavePreset->setObjectName("primaryBtn");
    connect(m_btnSavePreset, &QPushButton::clicked, this, &SettingsWidget::onSaveCustomPreset);

    m_btnResetTheme = new QPushButton("🔄 Reset Default Theme", pageTheme);
    m_btnResetTheme->setObjectName("dangerBtn");
    connect(m_btnResetTheme, &QPushButton::clicked, this, &SettingsWidget::onResetDefaultTheme);

    presetBtns->addWidget(m_btnSavePreset);
    presetBtns->addWidget(m_btnResetTheme);
    presetBtns->addStretch();

    customForm->addRow("Main Window BG:", m_btnBgBaseColor);
    customForm->addRow("Surface / Panel:", m_btnBgSurfaceColor);
    customForm->addRow("Primary Accent:", m_btnAccentPrimaryColor);
    customForm->addRow("Text Color:", m_btnTextPrimaryColor);
    customForm->addRow("Corner Radius:", m_spnCornerRadius);
    customForm->addRow("UI Font Scale:", m_spnFontSize);
    customForm->addRow(presetBtns);
    layTheme->addWidget(grpCustom);

    layTheme->addStretch();
    scrollTheme->setWidget(pageTheme);
    m_stackPages->addWidget(scrollTheme);

    // -------------------------------------------------------------
    // Page 2: Localization & Region
    // -------------------------------------------------------------
    QScrollArea* scrollLocale = new QScrollArea(this);
    scrollLocale->setWidgetResizable(true);
    scrollLocale->setFrameShape(QFrame::NoFrame);
    QWidget* pageLocale = new QWidget();
    QVBoxLayout* layLocale = new QVBoxLayout(pageLocale);
    layLocale->setContentsMargins(10, 0, 10, 10);
    layLocale->setSpacing(16);

    QGroupBox* grpLocale = new QGroupBox("🌍 Regional Formats & Display", pageLocale);
    QFormLayout* formLocale = new QFormLayout(grpLocale);
    formLocale->setSpacing(12);

    m_cmbCurrency = new QComboBox(pageLocale);
    m_cmbCurrency->addItem("USD ($) - US Dollar", "$");
    m_cmbCurrency->addItem("EUR (€) - Euro", "€");
    m_cmbCurrency->addItem("GBP (£) - British Pound", "£");
    m_cmbCurrency->addItem("CAD ($) - Canadian Dollar", "CA$");
    m_cmbCurrency->addItem("AUD ($) - Australian Dollar", "A$");
    m_cmbCurrency->addItem("PHP (₱) - Philippine Peso", "₱");
    m_cmbCurrency->addItem("BRL (R$) - Brazilian Real", "R$");
    m_cmbCurrency->addItem("INR (₹) - Indian Rupee", "₹");

    m_cmbDateFormat = new QComboBox(pageLocale);
    m_cmbDateFormat->addItem("YYYY-MM-DD (ISO 8601 - e.g. 2026-08-18)", "yyyy-MM-dd");
    m_cmbDateFormat->addItem("MM/DD/YYYY (US Standard - e.g. 08/18/2026)", "MM/dd/yyyy");
    m_cmbDateFormat->addItem("DD/MM/YYYY (International - e.g. 18/08/2026)", "dd/MM/yyyy");
    m_cmbDateFormat->addItem("MMM D, YYYY (e.g. Aug 18, 2026)", "MMM d, yyyy");

    m_cmbStartOfWeek = new QComboBox(pageLocale);
    m_cmbStartOfWeek->addItem("Sunday (Traditional Church Calendar)", 0);
    m_cmbStartOfWeek->addItem("Monday (ISO Standard)", 1);

    m_cmbTimezone = new QComboBox(pageLocale);
    m_cmbTimezone->addItem("Local System Timezone (Default)", "local");
    m_cmbTimezone->addItem("UTC (Coordinated Universal Time)", "UTC");

    formLocale->addRow("Currency Symbol / Code:", m_cmbCurrency);
    formLocale->addRow("Default Date Format:", m_cmbDateFormat);
    formLocale->addRow("First Day of the Week:", m_cmbStartOfWeek);
    formLocale->addRow("Timezone Reference:", m_cmbTimezone);

    layLocale->addWidget(grpLocale);
    layLocale->addStretch();
    scrollLocale->setWidget(pageLocale);
    m_stackPages->addWidget(scrollLocale);

    // -------------------------------------------------------------
    // Page 3: Pastoral & Graph Thresholds
    // -------------------------------------------------------------
    QScrollArea* scrollPastoral = new QScrollArea(this);
    scrollPastoral->setWidgetResizable(true);
    scrollPastoral->setFrameShape(QFrame::NoFrame);
    QWidget* pagePastoral = new QWidget();
    QVBoxLayout* layPastoral = new QVBoxLayout(pagePastoral);
    layPastoral->setContentsMargins(10, 0, 10, 10);
    layPastoral->setSpacing(16);

    QGroupBox* grpThresholds = new QGroupBox("🕸️ Connection Graph & Care Analytics", pagePastoral);
    QFormLayout* formGraph = new QFormLayout(grpThresholds);
    formGraph->setSpacing(12);

    m_spnBurnoutThreshold = new QSpinBox(pagePastoral);
    m_spnBurnoutThreshold->setRange(2, 10);
    m_spnBurnoutThreshold->setValue(3);

    m_spnFamilyDominancePercent = new QSpinBox(pagePastoral);
    m_spnFamilyDominancePercent->setRange(30, 90);
    m_spnFamilyDominancePercent->setSuffix("%");
    m_spnFamilyDominancePercent->setValue(50);

    m_spnFollowUpDays = new QSpinBox(pagePastoral);
    m_spnFollowUpDays->setRange(1, 60);
    m_spnFollowUpDays->setSuffix(" days");
    m_spnFollowUpDays->setValue(14);

    formGraph->addRow("Volunteer Burnout Threshold (Group Count):", m_spnBurnoutThreshold);
    formGraph->addRow("Ministry Family Dominance Alert Percentage:", m_spnFamilyDominancePercent);
    formGraph->addRow("Default Pastoral Care Follow-up Window:", m_spnFollowUpDays);

    layPastoral->addWidget(grpThresholds);
    layPastoral->addStretch();
    scrollPastoral->setWidget(pagePastoral);
    m_stackPages->addWidget(scrollPastoral);

    // -------------------------------------------------------------
    // Page 4: Data & Backup
    // -------------------------------------------------------------
    QScrollArea* scrollData = new QScrollArea(this);
    scrollData->setWidgetResizable(true);
    scrollData->setFrameShape(QFrame::NoFrame);
    QWidget* pageData = new QWidget();
    QVBoxLayout* layData = new QVBoxLayout(pageData);
    layData->setContentsMargins(10, 0, 10, 10);
    layData->setSpacing(16);

    QGroupBox* grpDb = new QGroupBox("💾 Database Operations & Maintenance", pageData);
    QVBoxLayout* grpDbLay = new QVBoxLayout(grpDb);
    grpDbLay->setSpacing(12);

    m_lblDbPath = new QLabel("Database File Path: ...", pageData);
    m_lblDbPath->setWordWrap(true);
    m_lblDbPath->setStyleSheet("font-family: monospace; font-size: 12px; padding: 10px; border-radius: 6px; background-color: rgba(0, 0, 0, 0.2);");
    grpDbLay->addWidget(m_lblDbPath);

    QHBoxLayout* dbActions = new QHBoxLayout();
    m_btnBackupDb = new QPushButton("📦 Export Database Backup (.db)", pageData);
    m_btnBackupDb->setObjectName("primaryBtn");
    connect(m_btnBackupDb, &QPushButton::clicked, this, [this]() {
        QString defaultName = QString("open_ekklesia_backup_%1.db").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString savePath = QFileDialog::getSaveFileName(this, "Export Database Backup", defaultName, "SQLite Database (*.db)");
        if (!savePath.isEmpty()) {
            QSqlDatabase db = DatabaseManager::instance().database();
            QString srcPath = db.databaseName();
            if (QFile::copy(srcPath, savePath)) {
                QMessageBox::information(this, "Backup Successful", QString("Database successfully backed up to:\n%1").arg(savePath));
            } else {
                QMessageBox::critical(this, "Backup Failed", "Could not copy database file to specified destination.");
            }
        }
    });

    m_btnCompactDb = new QPushButton("🧹 Compact Database (VACUUM)", pageData);
    m_btnCompactDb->setObjectName("accentBtnOrange");
    connect(m_btnCompactDb, &QPushButton::clicked, this, [this]() {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.exec("PRAGMA incremental_vacuum;");
        if (q.exec("VACUUM;")) {
            QMessageBox::information(this, "Optimization Complete", "Database successfully compacted. Unused storage space has been reclaimed.");
        } else {
            QMessageBox::warning(this, "Optimization Note", "Incremental vacuum executed cleanly.");
        }
    });

    m_btnViewAuditLogs = new QPushButton("📋 View Audit Logs", pageData);
    m_btnViewAuditLogs->setObjectName("secondaryBtn");
    connect(m_btnViewAuditLogs, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("System Audit Log History");
        dlg.resize(650, 420);
        QVBoxLayout* v = new QVBoxLayout(&dlg);
        QListWidget* list = new QListWidget(&dlg);
        v->addWidget(list);

        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q("SELECT table_name, record_id, field_name, old_value, new_value, changed_at, changed_by FROM audit_log ORDER BY changed_at DESC LIMIT 100", db);
        while (q.next()) {
            QString itemStr = QString("[%1] %2 #%3 — %4: \"%5\" → \"%6\" (by %7)")
                                  .arg(q.value(5).toString(),
                                       q.value(0).toString(),
                                       q.value(1).toString(),
                                       q.value(2).toString(),
                                       q.value(3).toString(),
                                       q.value(4).toString(),
                                       q.value(6).toString());
            list->addItem(itemStr);
        }
        if (list->count() == 0) {
            list->addItem("No audit log entries recorded yet.");
        }
        dlg.exec();
    });

    m_btnSelectiveShare = new QPushButton("🔄 Selective Data Export / Import", pageData);
    m_btnSelectiveShare->setObjectName("primaryBtn");
    connect(m_btnSelectiveShare, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("Selective Scoped Data Sharing");
        dlg.resize(500, 320);
        QVBoxLayout* v = new QVBoxLayout(&dlg);
        v->setSpacing(12);

        QLabel* lblInfo = new QLabel("Export or import scoped portions of your church data (JSON format) without sharing sensitive financial or care records.", &dlg);
        lblInfo->setWordWrap(true);
        v->addWidget(lblInfo);

        QComboBox* cmbScope = new QComboBox(&dlg);
        cmbScope->addItem("👥 Membership Only (People, Households, Groups)", "membership");
        cmbScope->addItem("💰 Finance Only (Accounts, Funds, Contributions)", "finance");
        cmbScope->addItem("📋 Ministry Schedules (Services, Facilities, Plans)", "schedules");
        cmbScope->addItem("🕊️ Pastoral Care & History", "care_notes");
        cmbScope->addItem("🔒 Vault Documents & Metadata", "vault");
        v->addWidget(new QLabel("Select Data Scope:", &dlg));
        v->addWidget(cmbScope);

        QHBoxLayout* btnRow = new QHBoxLayout();
        QPushButton* btnExport = new QPushButton("📤 Export Scope to JSON", &dlg);
        QPushButton* btnImport = new QPushButton("📥 Import Scope from JSON", &dlg);
        btnExport->setObjectName("primaryBtn");
        btnImport->setObjectName("secondaryBtn");
        btnRow->addWidget(btnExport);
        btnRow->addWidget(btnImport);
        v->addLayout(btnRow);

        connect(btnExport, &QPushButton::clicked, &dlg, [this, cmbScope, &dlg]() {
            QString scope = cmbScope->currentData().toString();
            QString path = QFileDialog::getSaveFileName(&dlg, "Export Data Scope", QString("%1_export.json").arg(scope), "JSON Files (*.json)");
            if (!path.isEmpty()) {
                QSqlDatabase d = DatabaseManager::instance().database();
                QJsonObject doc = DataSharing::exportTables(d, DataSharing::tablesForScope(scope));
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(QJsonDocument(doc).toJson());
                    f.close();
                    QMessageBox::information(&dlg, "Export Complete", QString("Data scope '%1' exported successfully!").arg(scope));
                }
            }
        });

        connect(btnImport, &QPushButton::clicked, &dlg, [this, &dlg]() {
            QString path = QFileDialog::getOpenFileName(&dlg, "Import Data Scope", "", "JSON Files (*.json)");
            if (!path.isEmpty()) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                    f.close();
                    QSqlDatabase d = DatabaseManager::instance().database();
                    if (DataSharing::importDocument(d, doc.object())) {
                        QMessageBox::information(&dlg, "Import Complete", "Data imported successfully.");
                    } else {
                        QMessageBox::critical(&dlg, "Import Failed", "Failed to import JSON data payload.");
                    }
                }
            }
        });

        dlg.exec();
    });

    dbActions->addWidget(m_btnBackupDb);
    dbActions->addWidget(m_btnCompactDb);
    dbActions->addWidget(m_btnViewAuditLogs);
    dbActions->addWidget(m_btnSelectiveShare);
    grpDbLay->addLayout(dbActions);

    layData->addWidget(grpDb);
    layData->addStretch();
    scrollData->setWidget(pageData);
    m_stackPages->addWidget(scrollData);

    // -------------------------------------------------------------
    // Page 5: Danger Zone
    // -------------------------------------------------------------
    QScrollArea* scrollDanger = new QScrollArea(this);
    scrollDanger->setWidgetResizable(true);
    scrollDanger->setFrameShape(QFrame::NoFrame);
    QWidget* pageDanger = new QWidget();
    QVBoxLayout* layDanger = new QVBoxLayout(pageDanger);
    layDanger->setContentsMargins(10, 0, 10, 10);
    layDanger->setSpacing(16);

    QGroupBox* grpDanger = new QGroupBox("⚠️ Irreversible Administrative Actions", pageDanger);
    grpDanger->setStyleSheet("QGroupBox { border: 2px solid #f38ba8; border-radius: 8px; margin-top: 12px; padding-top: 16px; font-weight: bold; color: #f38ba8; }");
    QVBoxLayout* dangerLay = new QVBoxLayout(grpDanger);
    dangerLay->setSpacing(14);

    QLabel* lblDangerNotice = new QLabel("Actions in this section permanently alter or delete database records. Please ensure you have exported a database backup before executing any of these actions.", pageDanger);
    lblDangerNotice->setWordWrap(true);
    lblDangerNotice->setStyleSheet("color: #f38ba8; font-size: 13px;");
    dangerLay->addWidget(lblDangerNotice);

    // Purge Demo Data
    QHBoxLayout* rowPurge = new QHBoxLayout();
    QVBoxLayout* purgeText = new QVBoxLayout();
    QLabel* lblPurgeTitle = new QLabel("Purge Demo / Sample Records", pageDanger);
    lblPurgeTitle->setStyleSheet("font-weight: bold; font-size: 14px;");
    QLabel* lblPurgeDesc = new QLabel("Clears sample members, transactions, groups, and plans so your church starts with a clean database.", pageDanger);
    lblPurgeDesc->setStyleSheet("color: #a6adc8; font-size: 12px;");
    purgeText->addWidget(lblPurgeTitle);
    purgeText->addWidget(lblPurgeDesc);
    rowPurge->addLayout(purgeText);
    rowPurge->addStretch();

    m_btnPurgeDemoData = new QPushButton("🧹 Purge Demo Data", pageDanger);
    m_btnPurgeDemoData->setObjectName("dangerBtn");
    m_btnPurgeDemoData->setMinimumHeight(36);
    connect(m_btnPurgeDemoData, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString input = QInputDialog::getText(
            this,
            "Confirm Demo Data Purge",
            "This action clears all demo member, financial, and ministry records.\n\nType 'PURGE' to proceed:",
            QLineEdit::Normal,
            "",
            &ok
        );
        if (ok && input.trimmed() == "PURGE") {
            if (DatabaseManager::instance().purgeDemoData()) {
                QMessageBox::information(this, "Purge Complete", "All sample demo records have been cleared. Database is ready for church deployment.");
            } else {
                QMessageBox::critical(this, "Error", "Failed to purge sample demo data.");
            }
        } else if (ok) {
            QMessageBox::warning(this, "Cancelled", "Confirmation text did not match 'PURGE'. No data was modified.");
        }
    });
    rowPurge->addWidget(m_btnPurgeDemoData);
    dangerLay->addLayout(rowPurge);

    // Reset Preferences
    QHBoxLayout* rowReset = new QHBoxLayout();
    QVBoxLayout* resetText = new QVBoxLayout();
    QLabel* lblResetTitle = new QLabel("Reset All Settings to Defaults", pageDanger);
    lblResetTitle->setStyleSheet("font-weight: bold; font-size: 14px;");
    QLabel* lblResetDesc = new QLabel("Reverts all theme customizations, regional preferences, and church branding to standard defaults.", pageDanger);
    lblResetDesc->setStyleSheet("color: #a6adc8; font-size: 12px;");
    resetText->addWidget(lblResetTitle);
    resetText->addWidget(lblResetDesc);
    rowReset->addLayout(resetText);
    rowReset->addStretch();

    m_btnResetAppDefaults = new QPushButton("🔄 Reset Settings", pageDanger);
    m_btnResetAppDefaults->setObjectName("dangerBtn");
    m_btnResetAppDefaults->setMinimumHeight(36);
    connect(m_btnResetAppDefaults, &QPushButton::clicked, this, [this]() {
        auto res = QMessageBox::question(this, "Reset Settings", "Are you sure you want to reset all preferences to application defaults?", QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::Yes) {
            QSettings s("OpenEkklesia", "CHMS");
            s.clear();
            s.sync();
            ThemeManager::instance().loadTheme("catppuccin_mocha");
            loadSettings();
            QMessageBox::information(this, "Settings Reset", "All application settings have been restored to default values.");
            emit settingsSaved();
        }
    });
    rowReset->addWidget(m_btnResetAppDefaults);
    dangerLay->addLayout(rowReset);

    layDanger->addWidget(grpDanger);
    layDanger->addStretch();
    scrollDanger->setWidget(pageDanger);
    m_stackPages->addWidget(scrollDanger);

    // Attach Stack to body
    bodyLayout->addWidget(m_stackPages, 1);
    rootLayout->addLayout(bodyLayout);

    // Connect form changes to dirty tracking
    auto trackChanges = [this]() { onMarkDirty(); };
    connect(m_txtChurchName, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtChurchSubtitle, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtDenomination, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtAddress, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtPhone, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtEmail, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtWebsite, &QLineEdit::textChanged, this, trackChanges);
    connect(m_txtTaxId, &QLineEdit::textChanged, this, trackChanges);
    connect(m_cmbCurrency, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_cmbDateFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_cmbStartOfWeek, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_cmbTimezone, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_spnBurnoutThreshold, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnFamilyDominancePercent, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnFollowUpDays, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);

    m_navList->setCurrentRow(0);
}

void SettingsWidget::onCategorySelected(int row) {
    if (row >= 0 && row < m_stackPages->count()) {
        m_stackPages->setCurrentIndex(row);
    }
}

void SettingsWidget::onSearchTextChanged(const QString& text) {
    QString q = text.trimmed().toLower();
    for (int i = 0; i < m_navList->count(); ++i) {
        QListWidgetItem* item = m_navList->item(i);
        if (q.isEmpty()) {
            item->setHidden(false);
        } else {
            bool matches = item->text().toLower().contains(q);
            // Search inside category keywords
            if (i == 0 && (q.contains("church") || q.contains("profile") || q.contains("email") || q.contains("phone") || q.contains("tax") || q.contains("address") || q.contains("denomination") || q.contains("name"))) matches = true;
            if (i == 1 && (q.contains("theme") || q.contains("color") || q.contains("dark") || q.contains("light") || q.contains("font") || q.contains("radius") || q.contains("brand") || q.contains("preview"))) matches = true;
            if (i == 2 && (q.contains("currency") || q.contains("date") || q.contains("week") || q.contains("timezone") || q.contains("locale") || q.contains("format"))) matches = true;
            if (i == 3 && (q.contains("graph") || q.contains("burnout") || q.contains("pastoral") || q.contains("care") || q.contains("dominance") || q.contains("follow"))) matches = true;
            if (i == 4 && (q.contains("backup") || q.contains("compact") || q.contains("vacuum") || q.contains("audit") || q.contains("share") || q.contains("export") || q.contains("database"))) matches = true;
            if (i == 5 && (q.contains("danger") || q.contains("purge") || q.contains("delete") || q.contains("demo") || q.contains("reset"))) matches = true;
            item->setHidden(!matches);
        }
    }
}

void SettingsWidget::onMarkDirty() {
    m_isDirty = true;
    m_btnSaveSettings->setText("💾 Save Changes (Unsaved)");
    m_btnSaveSettings->setStyleSheet("padding: 8px 18px; font-weight: bold; font-size: 13px; background-color: #fab387; color: #11111b;");
}

void SettingsWidget::populateThemePresets(int filterIndex) {
    m_cmbThemePreset->blockSignals(true);
    m_cmbThemePreset->clear();

    QString currentId = ThemeManager::instance().activeThemeId();

    for (const QString& tId : ThemeManager::instance().availableThemes()) {
        ThemeTokens tok = ThemeManager::instance().getThemeTokens(tId);
        // filterIndex 0: All Themes, 1: Dark Themes Only, 2: Light Themes Only
        if (filterIndex == 1 && !tok.isDark && tId != "system_auto") continue;
        if (filterIndex == 2 && tok.isDark && tId != "system_auto") continue;

        m_cmbThemePreset->addItem(tok.name, tId);
    }

    int idx = m_cmbThemePreset->findData(currentId);
    if (idx >= 0) {
        m_cmbThemePreset->setCurrentIndex(idx);
    } else if (m_cmbThemePreset->count() > 0) {
        m_cmbThemePreset->setCurrentIndex(0);
    }

    m_cmbThemePreset->blockSignals(false);
}

void SettingsWidget::updateLivePreview() {
    ThemeTokens t = ThemeManager::instance().currentTokens();

    m_previewCard->setStyleSheet(QString(
        "#liveThemePreviewCard { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: %3px; "
        "}"
    ).arg(t.bgSurface.name(), t.borderColor.name(), QString::number(t.borderRadiusMd)));

    m_lblPreviewHeader->setStyleSheet(QString("color: %1; font-size: 15px; font-weight: bold;").arg(t.textPrimary.name()));
    m_lblPreviewSub->setStyleSheet(QString("color: %1; font-size: 12px;").arg(t.textSecondary.name()));
    m_lblPreviewBadge->setStyleSheet(QString("background-color: %1; color: %2; padding: 3px 8px; border-radius: 8px; font-size: 10px; font-weight: bold;")
                                         .arg(t.accentPrimary.name(), (t.accentPrimary.lightness() > 128 ? "#11111b" : "#ffffff")));

    m_btnPreviewPrimary->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; border-radius: %3px; padding: 6px 14px;")
                                           .arg(t.accentPrimary.name(), (t.accentPrimary.lightness() > 128 ? "#11111b" : "#ffffff"), QString::number(t.borderRadiusSm)));

    m_btnPreviewSecondary->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 6px 14px;")
                                             .arg(t.bgOverlay.name(), t.textPrimary.name(), t.borderColor.name(), QString::number(t.borderRadiusSm)));
}

void SettingsWidget::loadSettings() {
    QSettings s("OpenEkklesia", "CHMS");

    m_txtChurchName->setText(s.value("churchName", "OpenEkklesia Church").toString());
    m_txtChurchSubtitle->setText(s.value("churchSubtitle", "Church Management & Pastoral Analytics").toString());
    m_txtDenomination->setText(s.value("churchDenomination", "Non-Denominational / General").toString());
    m_txtAddress->setText(s.value("churchAddress", "123 Grace Ave, Springfield").toString());
    m_txtPhone->setText(s.value("churchPhone", "(555) 019-2834").toString());
    m_txtEmail->setText(s.value("churchEmail", "office@openekklesia.org").toString());
    m_txtWebsite->setText(s.value("churchWebsite", "https://openekklesia.org").toString());
    m_txtTaxId->setText(s.value("churchTaxId", "XX-XXXXXXX").toString());

    m_chkAutoCollapseSidebar->setChecked(s.value("autoCollapseSidebar", true).toBool());

    int curIdx = m_cmbCurrency->findData(s.value("currencySymbol", "$").toString());
    if (curIdx >= 0) m_cmbCurrency->setCurrentIndex(curIdx);

    int dateIdx = m_cmbDateFormat->findData(s.value("dateFormat", "yyyy-MM-dd").toString());
    if (dateIdx >= 0) m_cmbDateFormat->setCurrentIndex(dateIdx);

    int weekIdx = m_cmbStartOfWeek->findData(s.value("startOfWeek", 0).toInt());
    if (weekIdx >= 0) m_cmbStartOfWeek->setCurrentIndex(weekIdx);

    int tzIdx = m_cmbTimezone->findData(s.value("timezone", "local").toString());
    if (tzIdx >= 0) m_cmbTimezone->setCurrentIndex(tzIdx);

    m_spnBurnoutThreshold->setValue(s.value("burnoutThreshold", 3).toInt());
    m_spnFamilyDominancePercent->setValue(s.value("familyDominancePercent", 50).toInt());
    m_spnFollowUpDays->setValue(s.value("followUpDays", 14).toInt());

    populateThemePresets(m_cmbThemeModeFilter->currentIndex());

    bool isSystemAuto = (ThemeManager::instance().activeThemeId() == "system_auto");
    m_chkSyncSystemTheme->blockSignals(true);
    m_chkSyncSystemTheme->setChecked(isSystemAuto);
    m_chkSyncSystemTheme->blockSignals(false);
    m_cmbThemePreset->setEnabled(!isSystemAuto);
    m_cmbThemeModeFilter->setEnabled(!isSystemAuto);

    updateColorButtonStyles();
    updateLivePreview();

    QSqlDatabase db = DatabaseManager::instance().database();
    m_lblDbPath->setText(QString("Active Database Location:\n%1").arg(db.databaseName()));

    m_isDirty = false;
    m_btnSaveSettings->setText("💾 Save All Changes");
    m_btnSaveSettings->setStyleSheet("padding: 8px 18px; font-weight: bold; font-size: 13px;");
}

void SettingsWidget::saveSettings() {
    QSettings s("OpenEkklesia", "CHMS");

    s.setValue("churchName", m_txtChurchName->text().trimmed());
    s.setValue("churchSubtitle", m_txtChurchSubtitle->text().trimmed());
    s.setValue("churchDenomination", m_txtDenomination->text().trimmed());
    s.setValue("churchAddress", m_txtAddress->text().trimmed());
    s.setValue("churchPhone", m_txtPhone->text().trimmed());
    s.setValue("churchEmail", m_txtEmail->text().trimmed());
    s.setValue("churchWebsite", m_txtWebsite->text().trimmed());
    s.setValue("churchTaxId", m_txtTaxId->text().trimmed());

    s.setValue("autoCollapseSidebar", m_chkAutoCollapseSidebar->isChecked());
    s.setValue("currencySymbol", m_cmbCurrency->currentData().toString());
    s.setValue("dateFormat", m_cmbDateFormat->currentData().toString());
    s.setValue("startOfWeek", m_cmbStartOfWeek->currentData().toInt());
    s.setValue("timezone", m_cmbTimezone->currentData().toString());

    s.setValue("burnoutThreshold", m_spnBurnoutThreshold->value());
    s.setValue("familyDominancePercent", m_spnFamilyDominancePercent->value());
    s.setValue("followUpDays", m_spnFollowUpDays->value());

    if (m_chkSyncSystemTheme->isChecked()) {
        ThemeManager::instance().loadTheme("system_auto");
    } else {
        QString activeThemeId = m_cmbThemePreset->currentData().toString();
        if (!activeThemeId.isEmpty()) {
            ThemeManager::instance().loadTheme(activeThemeId);
        }
    }

    s.sync();

    m_isDirty = false;
    m_btnSaveSettings->setText("💾 Save All Changes");
    m_btnSaveSettings->setStyleSheet("padding: 8px 18px; font-weight: bold; font-size: 13px;");

    QMessageBox::information(this, "Settings Saved", "Application settings have been updated successfully.");
    emit settingsSaved();
}

void SettingsWidget::updateColorButtonStyles() {
    ThemeTokens t = ThemeManager::instance().currentTokens();

    auto applyBtnStyle = [](QPushButton* btn, const QColor& col) {
        btn->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; border: 1px solid #45475a; border-radius: 4px; padding: 6px;")
                               .arg(col.name(), (col.lightness() > 128 ? "#11111b" : "#ffffff")));
        btn->setText(QString("%1").arg(col.name().toUpper()));
    };

    applyBtnStyle(m_btnBgBaseColor, t.bgBase);
    applyBtnStyle(m_btnBgSurfaceColor, t.bgSurface);
    applyBtnStyle(m_btnAccentPrimaryColor, t.accentPrimary);
    applyBtnStyle(m_btnTextPrimaryColor, t.textPrimary);
    m_spnCornerRadius->setValue(t.borderRadiusMd);
}

void SettingsWidget::onPickColor() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    ThemeTokens t = ThemeManager::instance().currentTokens();
    QColor initialCol;

    if (btn == m_btnBgBaseColor) initialCol = t.bgBase;
    else if (btn == m_btnBgSurfaceColor) initialCol = t.bgSurface;
    else if (btn == m_btnAccentPrimaryColor) initialCol = t.accentPrimary;
    else if (btn == m_btnTextPrimaryColor) initialCol = t.textPrimary;

    QColor chosen = QColorDialog::getColor(initialCol, this, "Pick Custom Theme Color");
    if (!chosen.isValid()) return;

    if (btn == m_btnBgBaseColor) t.bgBase = chosen;
    else if (btn == m_btnBgSurfaceColor) t.bgSurface = chosen;
    else if (btn == m_btnAccentPrimaryColor) t.accentPrimary = chosen;
    else if (btn == m_btnTextPrimaryColor) t.textPrimary = chosen;

    ThemeManager::instance().applyCustomTokens(t);
    updateColorButtonStyles();
    updateLivePreview();
    onMarkDirty();
}

void SettingsWidget::onRadiusSliderChanged(int value) {
    ThemeTokens t = ThemeManager::instance().currentTokens();
    t.borderRadiusMd = value;
    ThemeManager::instance().applyCustomTokens(t);
    updateLivePreview();
    onMarkDirty();
}

void SettingsWidget::onFontSizeSliderChanged(int value) {
    ThemeManager::instance().setFontSize(value);
    onMarkDirty();
}

void SettingsWidget::onGenerateChurchBrand() {
    QColor chosen = QColorDialog::getColor(QColor("#1B365D"), this, "Pick Primary Church Brand Color");
    if (!chosen.isValid()) return;

    bool darkBase = (m_cmbBrandBaseMode->currentIndex() == 0);
    if (ThemeManager::instance().generateChurchBrandTheme(chosen, darkBase)) {
        populateThemePresets(m_cmbThemeModeFilter->currentIndex());
        int idx = m_cmbThemePreset->findData("church_brand");
        if (idx >= 0) m_cmbThemePreset->setCurrentIndex(idx);

        updateColorButtonStyles();
        updateLivePreview();
        onMarkDirty();
        QMessageBox::information(this, "Church Brand Generated", "Custom Church Brand Theme has been generated and applied successfully!");
    }
}

void SettingsWidget::onSaveCustomPreset() {
    bool ok;
    QString presetName = QInputDialog::getText(this, "Save Custom Theme Preset",
                                                "Enter a name for your custom theme preset:", QLineEdit::Normal,
                                                "My Custom Church Theme", &ok);
    if (ok && !presetName.trimmed().isEmpty()) {
        ThemeTokens t = ThemeManager::instance().currentTokens();
        if (ThemeManager::instance().saveCustomPreset(presetName.trimmed(), t)) {
            populateThemePresets(m_cmbThemeModeFilter->currentIndex());
            int idx = m_cmbThemePreset->findData(ThemeManager::instance().activeThemeId());
            if (idx >= 0) m_cmbThemePreset->setCurrentIndex(idx);

            updateLivePreview();
            QMessageBox::information(this, "Preset Saved", QString("Custom theme preset '%1' has been saved!").arg(presetName));
        }
    }
}

void SettingsWidget::onResetDefaultTheme() {
    ThemeManager::instance().loadTheme("catppuccin_mocha");
    populateThemePresets(m_cmbThemeModeFilter->currentIndex());
    int idx = m_cmbThemePreset->findData("catppuccin_mocha");
    if (idx >= 0) m_cmbThemePreset->setCurrentIndex(idx);
    updateColorButtonStyles();
    updateLivePreview();
    onMarkDirty();
}
