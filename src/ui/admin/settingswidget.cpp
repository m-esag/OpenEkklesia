#include "settingswidget.h"
#include "thememanager.h"
#include "../database/databasemanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
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

SettingsWidget::SettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    loadSettings();
}

void SettingsWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(20);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* pageHeader = new QLabel("⚙️ Application Settings", this);
    pageHeader->setObjectName("pageHeader");
    headerLayout->addWidget(pageHeader);
    headerLayout->addStretch();

    m_btnSaveSettings = new QPushButton("💾 Save All Settings", this);
    m_btnSaveSettings->setObjectName("saveBtn");
    connect(m_btnSaveSettings, &QPushButton::clicked, this, &SettingsWidget::saveSettings);
    headerLayout->addWidget(m_btnSaveSettings);

    mainLayout->addLayout(headerLayout);

    QTabWidget* tabs = new QTabWidget(this);

    // --- Tab 1: Organization & Identity ---
    QWidget* tabOrg = new QWidget(this);
    QVBoxLayout* orgLay = new QVBoxLayout(tabOrg);
    orgLay->setContentsMargins(15, 15, 15, 15);
    orgLay->setSpacing(12);

    QGroupBox* grpOrg = new QGroupBox("🏛️ Church Profile", this);
    QFormLayout* formOrg = new QFormLayout(grpOrg);
    formOrg->setSpacing(12);

    m_txtChurchName = new QLineEdit(this);
    m_txtChurchSubtitle = new QLineEdit(this);

    formOrg->addRow("Church / Ministry Name:", m_txtChurchName);
    formOrg->addRow("Subtitle / Motto:", m_txtChurchSubtitle);

    orgLay->addWidget(grpOrg);
    orgLay->addStretch();
    tabs->addTab(tabOrg, "🏛️ Organization");

    // --- Tab 2: Interface & Custom Theme Editor ---
    QWidget* tabUI = new QWidget(this);
    QVBoxLayout* uiLay = new QVBoxLayout(tabUI);
    uiLay->setContentsMargins(15, 15, 15, 15);
    uiLay->setSpacing(12);

    // 1. Presets & OS Auto Sync Card
    QGroupBox* grpPreset = new QGroupBox("🎨 Active Theme Presets & OS Auto-Sync", this);
    QFormLayout* presetForm = new QFormLayout(grpPreset);
    presetForm->setSpacing(10);

    m_cmbThemeModeFilter = new QComboBox(this);
    m_cmbThemeModeFilter->addItems({"🎨 All Themes (Dark & Light Pairs)", "🌙 Dark Themes Only", "☀️ Light Themes Only"});

    m_cmbThemePreset = new QComboBox(this);
    populateThemePresets(0);

    connect(m_cmbThemeModeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        populateThemePresets(index);
    });

    connect(m_cmbThemePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        QString tId = m_cmbThemePreset->itemData(index).toString();
        if (!tId.isEmpty()) {
            ThemeManager::instance().loadTheme(tId);
            updateColorButtonStyles();
        }
    });

    m_chkSyncSystemTheme = new QCheckBox("Automatically follow OS system theme (Auto-sync dark/light mode)", this);
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
    });

    m_chkAutoCollapseSidebar = new QCheckBox("Automatically collapse sidebar on smaller screens (< 940px)", this);
    m_chkAutoCollapseSidebar->setChecked(true);

    presetForm->addRow("Theme Style Filter:", m_cmbThemeModeFilter);
    presetForm->addRow("Active Preset Theme:", m_cmbThemePreset);
    presetForm->addRow("OS Theme Auto-Sync:", m_chkSyncSystemTheme);
    presetForm->addRow("Responsive Sidebar:", m_chkAutoCollapseSidebar);
    uiLay->addWidget(grpPreset);

    // 2. Church Brand Theme Generator Card
    QGroupBox* grpBrand = new QGroupBox("🏛️ 1-Click Church Brand Theme Generator", this);
    QFormLayout* brandForm = new QFormLayout(grpBrand);
    brandForm->setSpacing(10);

    m_btnPickBrandColor = new QPushButton("🎨 Pick Primary Church Brand Color", this);
    m_btnPickBrandColor->setObjectName("primaryBtn");
    connect(m_btnPickBrandColor, &QPushButton::clicked, this, &SettingsWidget::onGenerateChurchBrand);

    m_cmbBrandBaseMode = new QComboBox(this);
    m_cmbBrandBaseMode->addItems({"Dark Base Theme", "Light Base Theme"});

    brandForm->addRow("Theme Base Mode:", m_cmbBrandBaseMode);
    brandForm->addRow("Brand Accent Color:", m_btnPickBrandColor);
    uiLay->addWidget(grpBrand);

    // 3. Interactive Theme Customization & UI Font Zoom Card
    QGroupBox* grpCustom = new QGroupBox("🎛️ Color Customization & Accessibility Zoom", this);
    QFormLayout* customForm = new QFormLayout(grpCustom);
    customForm->setSpacing(10);

    m_btnBgBaseColor = new QPushButton("Pick Base Background", this);
    m_btnBgSurfaceColor = new QPushButton("Pick Surface/Panel Color", this);
    m_btnAccentPrimaryColor = new QPushButton("Pick Accent Color", this);
    m_btnTextPrimaryColor = new QPushButton("Pick Primary Text Color", this);

    connect(m_btnBgBaseColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);
    connect(m_btnBgSurfaceColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);
    connect(m_btnAccentPrimaryColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);
    connect(m_btnTextPrimaryColor, &QPushButton::clicked, this, &SettingsWidget::onPickColor);

    m_spnCornerRadius = new QSpinBox(this);
    m_spnCornerRadius->setRange(0, 16);
    m_spnCornerRadius->setSuffix(" px");
    m_spnCornerRadius->setValue(ThemeManager::instance().currentTokens().borderRadiusMd);
    connect(m_spnCornerRadius, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsWidget::onRadiusSliderChanged);

    m_spnFontSize = new QSpinBox(this);
    m_spnFontSize->setRange(11, 18);
    m_spnFontSize->setSuffix(" px (Ctrl +/-)");
    m_spnFontSize->setValue(ThemeManager::instance().currentTokens().fontSizePx);
    connect(m_spnFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsWidget::onFontSizeSliderChanged);

    QHBoxLayout* presetBtns = new QHBoxLayout();
    m_btnSavePreset = new QPushButton("💾 Save Custom Preset (.json)", this);
    m_btnSavePreset->setObjectName("primaryBtn");
    connect(m_btnSavePreset, &QPushButton::clicked, this, &SettingsWidget::onSaveCustomPreset);

    m_btnResetTheme = new QPushButton("🔄 Reset Defaults", this);
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

    uiLay->addWidget(grpCustom);
    uiLay->addStretch();
    tabs->addTab(tabUI, "🎨 Appearance & Theme Editor");

    // --- Tab 3: Pastoral & Graph Thresholds ---
    QWidget* tabGraph = new QWidget(this);
    QVBoxLayout* graphLay = new QVBoxLayout(tabGraph);
    graphLay->setContentsMargins(15, 15, 15, 15);
    graphLay->setSpacing(12);

    QGroupBox* grpThresholds = new QGroupBox("🕸️ Connection Graph & Care Thresholds", this);
    QFormLayout* formGraph = new QFormLayout(grpThresholds);
    formGraph->setSpacing(12);

    m_spnBurnoutThreshold = new QSpinBox(this);
    m_spnBurnoutThreshold->setRange(2, 10);
    m_spnBurnoutThreshold->setValue(3);

    m_spnFamilyDominancePercent = new QSpinBox(this);
    m_spnFamilyDominancePercent->setRange(30, 90);
    m_spnFamilyDominancePercent->setSuffix("%");
    m_spnFamilyDominancePercent->setValue(50);

    formGraph->addRow("Volunteer Burnout Alert Threshold (Group Count):", m_spnBurnoutThreshold);
    formGraph->addRow("Ministry Family Dominance Alert Percentage:", m_spnFamilyDominancePercent);

    graphLay->addWidget(grpThresholds);
    graphLay->addStretch();
    tabs->addTab(tabGraph, "🕸️ Graph & Pastoral Thresholds");

    // --- Tab 4: Database & System Backup ---
    QWidget* tabDb = new QWidget(this);
    QVBoxLayout* dbLay = new QVBoxLayout(tabDb);
    dbLay->setContentsMargins(15, 15, 15, 15);
    dbLay->setSpacing(12);

    QGroupBox* grpDb = new QGroupBox("💾 Database Storage & Administrative Operations", this);
    QVBoxLayout* grpDbLay = new QVBoxLayout(grpDb);
    grpDbLay->setSpacing(12);

    m_lblDbPath = new QLabel("Database File Path: ...", this);
    m_lblDbPath->setWordWrap(true);
    m_lblDbPath->setStyleSheet("font-family: monospace; font-size: 12px; padding: 10px; border-radius: 6px;");
    grpDbLay->addWidget(m_lblDbPath);

    QHBoxLayout* dbActions = new QHBoxLayout();
    m_btnBackupDb = new QPushButton("📦 Export Database Backup (.db)", this);
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

    m_btnViewAuditLogs = new QPushButton("📋 View Administrative Audit Logs", this);
    m_btnViewAuditLogs->setObjectName("secondaryBtn");
    connect(m_btnViewAuditLogs, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("System Audit Log History");
        dlg.resize(600, 400);
        QVBoxLayout* v = new QVBoxLayout(&dlg);
        QListWidget* list = new QListWidget(&dlg);
        v->addWidget(list);

        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q("SELECT table_name, record_id, field_name, old_value, new_value, changed_at, changed_by FROM audit_log ORDER BY changed_at DESC LIMIT 100", db);
        while (q.next()) {
            QString itemStr = QString("[%1] %2 #%3 — %4: \"%5\" → \"%6\" (by %7)")
                                  .arg(q.value(5).toString(),  // changed_at
                                       q.value(0).toString(),  // table_name
                                       q.value(1).toString(),  // record_id
                                       q.value(2).toString(),  // field_name
                                       q.value(3).toString(),  // old_value
                                       q.value(4).toString(),  // new_value
                                       q.value(6).toString()); // changed_by
            list->addItem(itemStr);
        }
        if (list->count() == 0) {
            list->addItem("No audit log entries recorded yet.");
        }
        dlg.exec();
    });

    m_btnPurgeDemoData = new QPushButton("🧹 Purge Sample / Demo Data", this);
    m_btnPurgeDemoData->setObjectName("dangerBtn");
    connect(m_btnPurgeDemoData, &QPushButton::clicked, this, [this]() {
        auto res = QMessageBox::warning(
            this,
            "Confirm Demo Data Purge",
            "Are you sure you want to purge all sample demo records (members, contributions, transactions, groups, service plans)?\n\n"
            "This action resets the database for real ministry deployment and cannot be undone.",
            QMessageBox::Yes | QMessageBox::No
        );
        if (res == QMessageBox::Yes) {
            if (DatabaseManager::instance().purgeDemoData()) {
                QMessageBox::information(this, "Purge Complete", "All sample demo records have been cleared. Database is ready for real church deployment.");
            } else {
                QMessageBox::critical(this, "Error", "Failed to purge sample demo data.");
            }
        }
    });

    m_btnCompactDb = new QPushButton("🧹 Compact Database & Reclaim Space", this);
    m_btnCompactDb->setObjectName("accentBtnOrange");
    connect(m_btnCompactDb, &QPushButton::clicked, this, [this]() {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.exec("PRAGMA incremental_vacuum;");
        if (q.exec("VACUUM;")) {
            QMessageBox::information(this, "Optimization Complete", "Database successfully compacted. Unused pages and storage space have been reclaimed.");
        } else {
            QMessageBox::warning(this, "Optimization Note", "Incremental vacuum executed cleanly.");
        }
    });

    dbActions->addWidget(m_btnBackupDb);
    dbActions->addWidget(m_btnCompactDb);
    dbActions->addWidget(m_btnViewAuditLogs);
    dbActions->addWidget(m_btnPurgeDemoData);
    dbActions->addStretch();
    grpDbLay->addLayout(dbActions);

    dbLay->addWidget(grpDb);
    dbLay->addStretch();
    tabs->addTab(tabDb, "💾 Database & Backup");

    mainLayout->addWidget(tabs);
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

void SettingsWidget::loadSettings() {
    QSettings s("OpenEkklesia", "CHMS");

    m_txtChurchName->setText(s.value("churchName", "OpenEkklesia Church").toString());
    m_txtChurchSubtitle->setText(s.value("churchSubtitle", "Church Management & Pastoral Analytics").toString());

    m_chkAutoCollapseSidebar->setChecked(s.value("autoCollapseSidebar", true).toBool());
    
    m_spnBurnoutThreshold->setValue(s.value("burnoutThreshold", 3).toInt());
    m_spnFamilyDominancePercent->setValue(s.value("familyDominancePercent", 50).toInt());

    populateThemePresets(m_cmbThemeModeFilter->currentIndex());

    bool isSystemAuto = (ThemeManager::instance().activeThemeId() == "system_auto");
    m_chkSyncSystemTheme->blockSignals(true);
    m_chkSyncSystemTheme->setChecked(isSystemAuto);
    m_chkSyncSystemTheme->blockSignals(false);
    m_cmbThemePreset->setEnabled(!isSystemAuto);
    m_cmbThemeModeFilter->setEnabled(!isSystemAuto);

    updateColorButtonStyles();

    QSqlDatabase db = DatabaseManager::instance().database();
    m_lblDbPath->setText(QString("Active Database Location:\n%1").arg(db.databaseName()));
}

void SettingsWidget::saveSettings() {
    QSettings s("OpenEkklesia", "CHMS");

    s.setValue("churchName", m_txtChurchName->text().trimmed());
    s.setValue("churchSubtitle", m_txtChurchSubtitle->text().trimmed());

    s.setValue("autoCollapseSidebar", m_chkAutoCollapseSidebar->isChecked());

    s.setValue("burnoutThreshold", m_spnBurnoutThreshold->value());
    s.setValue("familyDominancePercent", m_spnFamilyDominancePercent->value());

    if (m_chkSyncSystemTheme->isChecked()) {
        ThemeManager::instance().loadTheme("system_auto");
    } else {
        QString activeThemeId = m_cmbThemePreset->currentData().toString();
        if (!activeThemeId.isEmpty()) {
            ThemeManager::instance().loadTheme(activeThemeId);
        }
    }

    s.sync();

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
}

void SettingsWidget::onRadiusSliderChanged(int value) {
    ThemeTokens t = ThemeManager::instance().currentTokens();
    t.borderRadiusMd = value;
    ThemeManager::instance().applyCustomTokens(t);
}

void SettingsWidget::onFontSizeSliderChanged(int value) {
    ThemeManager::instance().setFontSize(value);
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
}
