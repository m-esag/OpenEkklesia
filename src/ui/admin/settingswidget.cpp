#include "settingswidget.h"
#include "thememanager.h"
#include "../integrations/planningcenterdialog.h"
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
#include <QPainter>
#include <QFileInfo>
#include <QPixmap>
#include <QIcon>
#include <QListView>
#include <QLineEdit>

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
    m_lblStatus->setStyleSheet("color: palette(placeholderText); font-size: 13px;");
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
        "🧩 Modules & Features",
        "🎵 Service & Liturgy Defaults",
        "🌍 Localization & Region",
        "🕸️ Pastoral & Graph",
        "🖥️ Kiosk & Security",
        "💾 Data, Backup & Integrations",
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

    // Church logo (used on printed documents and statements).
    m_logoPreview = new QLabel(pageGeneral);
    m_logoPreview->setFixedSize(96, 96);
    m_logoPreview->setAlignment(Qt::AlignCenter);
    m_logoPreview->setText("No logo\nselected");
    m_logoPreview->setStyleSheet("border: 1px dashed #45475a; border-radius: 8px; font-size: 11px; color: palette(placeholderText);");
    m_btnPickLogo = new QPushButton("🖼️ Upload Church Logo", pageGeneral);
    m_btnPickLogo->setObjectName("primaryBtn");
    connect(m_btnPickLogo, &QPushButton::clicked, this, &SettingsWidget::onPickLogo);

    QWidget* logoRow = new QWidget(pageGeneral);
    QHBoxLayout* logoRowLay = new QHBoxLayout(logoRow);
    logoRowLay->setContentsMargins(0, 0, 0, 0);
    logoRowLay->setSpacing(12);
    logoRowLay->addWidget(m_logoPreview);
    QVBoxLayout* logoBtns = new QVBoxLayout();
    logoBtns->addWidget(m_btnPickLogo);
    QLabel* logoHint = new QLabel("PNG/JPG, shown on printed reports, statements, and documents.", pageGeneral);
    logoHint->setWordWrap(true);
    logoHint->setStyleSheet("font-size: 11px; opacity: 0.7;");
    logoBtns->addWidget(logoHint);
    logoBtns->addStretch();
    logoRowLay->addLayout(logoBtns);
    logoRowLay->addStretch();
    formOrg->addRow("Church Logo:", logoRow);

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

    // Live Preview Card (mini app mock: sidebar + content)
    QGroupBox* grpPreview = new QGroupBox("👁️ Real-Time Theme Preview", pageTheme);
    QVBoxLayout* prevLay = new QVBoxLayout(grpPreview);

    m_previewCard = new QFrame(grpPreview);
    m_previewCard->setObjectName("liveThemePreviewCard");
    m_previewCard->setFrameShape(QFrame::StyledPanel);
    m_previewCard->setMinimumHeight(150);
    QHBoxLayout* prevOuter = new QHBoxLayout(m_previewCard);
    prevOuter->setContentsMargins(0, 0, 0, 0);
    prevOuter->setSpacing(0);

    // Thin navigation sidebar strip
    m_prevSidebar = new QWidget(m_previewCard);
    m_prevSidebar->setFixedWidth(44);
    prevOuter->addWidget(m_prevSidebar);

    // Main content column
    QWidget* prevContent = new QWidget(m_previewCard);
    QVBoxLayout* cardInner = new QVBoxLayout(prevContent);
    cardInner->setContentsMargins(16, 14, 16, 14);
    cardInner->setSpacing(8);

    QHBoxLayout* prevHeaderRow = new QHBoxLayout();
    m_lblPreviewHeader = new QLabel("Grace Community Church", prevContent);
    m_lblPreviewHeader->setStyleSheet("font-size: 15px; font-weight: bold;");
    m_lblPreviewBadge = new QLabel("ACTIVE PRESET", prevContent);
    m_lblPreviewBadge->setStyleSheet("padding: 2px 8px; border-radius: 10px; font-size: 10px; font-weight: bold;");
    prevHeaderRow->addWidget(m_lblPreviewHeader);
    prevHeaderRow->addStretch();
    prevHeaderRow->addWidget(m_lblPreviewBadge);
    cardInner->addLayout(prevHeaderRow);

    m_lblPreviewSub = new QLabel("Sample controls rendered with the current theme tokens.", prevContent);
    m_lblPreviewSub->setStyleSheet("font-size: 12px; opacity: 0.85;");
    cardInner->addWidget(m_lblPreviewSub);

    m_prevInput = new QLineEdit(prevContent);
    m_prevInput->setPlaceholderText("Search members...");
    m_prevInput->setMaximumWidth(250);
    cardInner->addWidget(m_prevInput);

    m_prevRow1 = new QLabel("•  Jane Smith  —  Member  (At-Risk)", prevContent);
    m_prevRow2 = new QLabel("•  John Doe    —  Leader", prevContent);
    m_prevRow1->setStyleSheet("font-size: 11px; padding: 3px 8px; border-radius: 4px;");
    m_prevRow2->setStyleSheet("font-size: 11px; padding: 3px 8px; border-radius: 4px;");
    cardInner->addWidget(m_prevRow1);
    cardInner->addWidget(m_prevRow2);

    QHBoxLayout* prevBtnRow = new QHBoxLayout();
    m_btnPreviewPrimary = new QPushButton("Primary Action", prevContent);
    m_btnPreviewSecondary = new QPushButton("Secondary", prevContent);
    prevBtnRow->addWidget(m_btnPreviewPrimary);
    prevBtnRow->addWidget(m_btnPreviewSecondary);
    prevBtnRow->addStretch();
    cardInner->addLayout(prevBtnRow);

    prevOuter->addWidget(prevContent, 1);
    prevLay->addWidget(m_previewCard);
    layTheme->addWidget(grpPreview);

    // Theme Gallery (visual preset cards)
    QGroupBox* grpPreset = new QGroupBox("🎨 Theme Gallery — click a preset to apply", pageTheme);
    QVBoxLayout* presetForm = new QVBoxLayout(grpPreset);
    presetForm->setSpacing(10);

    QHBoxLayout* galleryToolbar = new QHBoxLayout();
    m_cmbThemeModeFilter = new QComboBox(pageTheme);
    m_cmbThemeModeFilter->addItems({"🎨 All Themes", "🌙 Dark Themes", "☀️ Light Themes"});
    m_chkSyncSystemTheme = new QCheckBox("Follow OS system theme (auto dark/light)", pageTheme);
    m_chkAutoCollapseSidebar = new QCheckBox("Auto-collapse sidebar on small screens", pageTheme);
    m_chkAutoCollapseSidebar->setChecked(true);
    galleryToolbar->addWidget(new QLabel("Show:", pageTheme));
    galleryToolbar->addWidget(m_cmbThemeModeFilter);
    galleryToolbar->addSpacing(18);
    galleryToolbar->addWidget(m_chkSyncSystemTheme);
    galleryToolbar->addWidget(m_chkAutoCollapseSidebar);
    galleryToolbar->addStretch();
    presetForm->addLayout(galleryToolbar);

    m_themeGallery = new QListWidget(pageTheme);
    m_themeGallery->setObjectName("themeGallery");
    m_themeGallery->setViewMode(QListView::IconMode);
    m_themeGallery->setIconSize(QSize(120, 68));
    m_themeGallery->setGridSize(QSize(138, 108));
    m_themeGallery->setResizeMode(QListView::Adjust);
    m_themeGallery->setMovement(QListView::Static);
    m_themeGallery->setWrapping(true);
    m_themeGallery->setSpacing(8);
    m_themeGallery->setMinimumHeight(250);
    m_themeGallery->setUniformItemSizes(true);
    presetForm->addWidget(m_themeGallery);

    populateThemePresets(0);

    connect(m_cmbThemeModeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        populateThemePresets(index);
    });

    connect(m_themeGallery, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (!current) return;
        const QString tId = current->data(Qt::UserRole).toString();
        if (!tId.isEmpty() && tId != ThemeManager::instance().activeThemeId()) {
            ThemeManager::instance().loadTheme(tId);
            updateColorButtonStyles();
            updateLivePreview();
            onMarkDirty(); // so "Save All Changes" gives feedback
        }
    });

    connect(m_chkSyncSystemTheme, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            ThemeManager::instance().loadTheme("system_auto");
        } else {
            QListWidgetItem* cur = m_themeGallery->currentItem();
            QString tId = cur ? cur->data(Qt::UserRole).toString() : QString();
            if (!tId.isEmpty() && tId != "system_auto") {
                ThemeManager::instance().loadTheme(tId);
            } else {
                ThemeManager::instance().loadTheme("catppuccin_mocha");
            }
        }
        m_themeGallery->setEnabled(!checked);
        updateColorButtonStyles();
        updateLivePreview();
    });

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

    m_lblContrastTextBg = new QLabel(pageTheme);
    m_lblContrastTextBg->setStyleSheet("font-size: 11px; font-weight: bold;");
    m_lblContrastButtonAccent = new QLabel(pageTheme);
    m_lblContrastButtonAccent->setStyleSheet("font-size: 11px; font-weight: bold;");
    m_lblContrastWarning = new QLabel(pageTheme);
    m_lblContrastWarning->setStyleSheet("font-size: 11px; color: #f38ba8; font-weight: bold;");
    m_lblContrastWarning->setVisible(false);

    customForm->addRow("Window Background:", m_btnBgBaseColor);
    customForm->addRow("Cards & Panels:", m_btnBgSurfaceColor);
    customForm->addRow("Accent Color:", m_btnAccentPrimaryColor);
    customForm->addRow("Text Color:", m_btnTextPrimaryColor);
    customForm->addRow("Text Contrast (WCAG):", m_lblContrastTextBg);
    customForm->addRow("Button Contrast (WCAG):", m_lblContrastButtonAccent);
    customForm->addRow("", m_lblContrastWarning);
    customForm->addRow("Corner Radius:", m_spnCornerRadius);
    customForm->addRow("UI Font Scale:", m_spnFontSize);
    customForm->addRow(presetBtns);
    layTheme->addWidget(grpCustom);

    layTheme->addStretch();
    scrollTheme->setWidget(pageTheme);
    m_stackPages->addWidget(scrollTheme);

    // -------------------------------------------------------------
    // Page 2: Modules & Features
    // -------------------------------------------------------------
    QScrollArea* scrollModules = new QScrollArea(this);
    scrollModules->setWidgetResizable(true);
    scrollModules->setFrameShape(QFrame::NoFrame);
    QWidget* pageModules = new QWidget();
    QVBoxLayout* layModules = new QVBoxLayout(pageModules);
    layModules->setContentsMargins(10, 0, 10, 10);
    layModules->setSpacing(16);

    QGroupBox* grpModPresets = new QGroupBox("⚡ Quick Module Presets", pageModules);
    QHBoxLayout* layModPresets = new QHBoxLayout(grpModPresets);
    QPushButton* btnModAll = new QPushButton("✨ Enable All Modules", pageModules);
    QPushButton* btnModStandard = new QPushButton("⛪ Standard Church (Roster + Finance + Groups)", pageModules);
    QPushButton* btnModMinimal = new QPushButton("🌱 Minimal (Roster + Liturgy Only)", pageModules);
    btnModAll->setObjectName("secondaryBtn");
    btnModStandard->setObjectName("secondaryBtn");
    btnModMinimal->setObjectName("secondaryBtn");
    layModPresets->addWidget(btnModAll);
    layModPresets->addWidget(btnModStandard);
    layModPresets->addWidget(btnModMinimal);
    layModPresets->addStretch();
    layModules->addWidget(grpModPresets);

    QGroupBox* grpModCong = new QGroupBox("👥 Congregation & Community", pageModules);
    QVBoxLayout* layModCong = new QVBoxLayout(grpModCong);
    m_chkModPeople = new QCheckBox("People & Membership Directory (Demographics, Timeline, Care Logs)", pageModules);
    m_chkModGroups = new QCheckBox("Small Groups & Discipleship (Life groups, serving teams, chemistry analytics)", pageModules);
    m_chkModGraph = new QCheckBox("Relational Connection Graph (Visual demographic map & network clusters)", pageModules);
    m_chkModAnalytics = new QCheckBox("Pastoral Analytics (Radar charts, engagement metrics, care follow-ups)", pageModules);
    m_chkModShepherd = new QCheckBox("🛐 Shepherd's Workbench (Today's follow-up queue, member care timeline, prayer list)", pageModules);
    layModCong->addWidget(m_chkModPeople);
    layModCong->addWidget(m_chkModGroups);
    layModCong->addWidget(m_chkModGraph);
    layModCong->addWidget(m_chkModAnalytics);
    layModCong->addWidget(m_chkModShepherd);
    layModules->addWidget(grpModCong);

    QGroupBox* grpModMin = new QGroupBox("🗓️ Ministry & Services", pageModules);
    QVBoxLayout* layModMin = new QVBoxLayout(grpModMin);
    m_chkModService = new QCheckBox("Service Liturgy Builder (Order of service, music cues, volunteer scheduling)", pageModules);
    m_chkModPlanning = new QCheckBox("Ministry Planning & Roadmaps (Milestones, timelines, project boards)", pageModules);
    m_chkModFacilities = new QCheckBox("Facilities & Room Bookings (Facility calendar, room reservation matrix)", pageModules);
    layModMin->addWidget(m_chkModService);
    layModMin->addWidget(m_chkModPlanning);
    layModMin->addWidget(m_chkModFacilities);
    layModules->addWidget(grpModMin);

    QGroupBox* grpModFin = new QGroupBox("💰 Finance, Operations & Kiosk", pageModules);
    QVBoxLayout* layModFin = new QVBoxLayout(grpModFin);
    m_chkModGiving = new QCheckBox("Giving & Sunday Batches (Offering batches, multi-fund splits, GnuCash export)", pageModules);
    m_chkModInventory = new QCheckBox("Asset & Equipment Inventory (Barcodes, item locations, check-outs)", pageModules);
    m_chkModVault = new QCheckBox("Private Document Vault (Membership forms, meeting minutes, legal records)", pageModules);
    m_chkModKiosk = new QCheckBox("Self-Service Nursery Check-In Kiosk (Full-screen checkin & barcode badging)", pageModules);
    layModFin->addWidget(m_chkModGiving);
    layModFin->addWidget(m_chkModInventory);
    layModFin->addWidget(m_chkModVault);
    layModFin->addWidget(m_chkModKiosk);
    layModules->addWidget(grpModFin);

    connect(btnModAll, &QPushButton::clicked, this, [this]() {
        m_chkModPeople->setChecked(true);
        m_chkModGroups->setChecked(true);
        m_chkModGraph->setChecked(true);
        m_chkModAnalytics->setChecked(true);
        m_chkModShepherd->setChecked(true);
        m_chkModService->setChecked(true);
        m_chkModPlanning->setChecked(true);
        m_chkModFacilities->setChecked(true);
        m_chkModGiving->setChecked(true);
        m_chkModInventory->setChecked(true);
        m_chkModVault->setChecked(true);
        m_chkModKiosk->setChecked(true);
        onMarkDirty();
    });

    connect(btnModStandard, &QPushButton::clicked, this, [this]() {
        m_chkModPeople->setChecked(true);
        m_chkModGroups->setChecked(true);
        m_chkModGraph->setChecked(false);
        m_chkModAnalytics->setChecked(true);
        m_chkModShepherd->setChecked(true);
        m_chkModService->setChecked(true);
        m_chkModPlanning->setChecked(false);
        m_chkModFacilities->setChecked(false);
        m_chkModGiving->setChecked(true);
        m_chkModInventory->setChecked(false);
        m_chkModVault->setChecked(true);
        m_chkModKiosk->setChecked(true);
        onMarkDirty();
    });

    connect(btnModMinimal, &QPushButton::clicked, this, [this]() {
        m_chkModPeople->setChecked(true);
        m_chkModGroups->setChecked(false);
        m_chkModGraph->setChecked(false);
        m_chkModAnalytics->setChecked(false);
        m_chkModShepherd->setChecked(false);
        m_chkModService->setChecked(true);
        m_chkModPlanning->setChecked(false);
        m_chkModFacilities->setChecked(false);
        m_chkModGiving->setChecked(false);
        m_chkModInventory->setChecked(false);
        m_chkModVault->setChecked(false);
        m_chkModKiosk->setChecked(false);
        onMarkDirty();
    });

    auto trackMod = [this]() { onMarkDirty(); };
    connect(m_chkModPeople, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModGroups, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModGraph, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModAnalytics, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModShepherd, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModService, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModPlanning, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModFacilities, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModGiving, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModInventory, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModVault, &QCheckBox::toggled, this, trackMod);
    connect(m_chkModKiosk, &QCheckBox::toggled, this, trackMod);

    layModules->addStretch();
    scrollModules->setWidget(pageModules);
    m_stackPages->addWidget(scrollModules);

    // -------------------------------------------------------------
    // Page 3: Service & Liturgy Defaults
    // -------------------------------------------------------------
    QScrollArea* scrollService = new QScrollArea(this);
    scrollService->setWidgetResizable(true);
    scrollService->setFrameShape(QFrame::NoFrame);
    QWidget* pageService = new QWidget();
    QVBoxLayout* layService = new QVBoxLayout(pageService);
    layService->setContentsMargins(10, 0, 10, 10);
    layService->setSpacing(16);

    QGroupBox* grpServiceDefaults = new QGroupBox("🎵 Sunday Service Planning & Calendar Defaults", pageService);
    QFormLayout* formService = new QFormLayout(grpServiceDefaults);
    formService->setSpacing(12);

    m_timeDefaultService = new QTimeEdit(QTime(10, 0), pageService);
    m_timeDefaultService->setDisplayFormat("hh:mm AP");

    m_spnTargetDuration = new QSpinBox(pageService);
    m_spnTargetDuration->setRange(30, 240);
    m_spnTargetDuration->setSuffix(" minutes");
    m_spnTargetDuration->setValue(75);

    m_spnIcalAlarmMins = new QSpinBox(pageService);
    m_spnIcalAlarmMins->setRange(0, 720);
    m_spnIcalAlarmMins->setSuffix(" mins prior");
    m_spnIcalAlarmMins->setValue(60);

    formService->addRow("Default Service Start Time:", m_timeDefaultService);
    formService->addRow("Target Liturgy Run Time:", m_spnTargetDuration);
    formService->addRow("Volunteer Calendar Reminder Alarm:", m_spnIcalAlarmMins);

    layService->addWidget(grpServiceDefaults);
    layService->addStretch();
    scrollService->setWidget(pageService);
    m_stackPages->addWidget(scrollService);

    // -------------------------------------------------------------
    // Page 4: Localization & Region
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
    // Page 5: Pastoral & Graph Thresholds
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

    m_spnInactivityDays = new QSpinBox(pagePastoral);
    m_spnInactivityDays->setRange(14, 365);
    m_spnInactivityDays->setSuffix(" days");
    m_spnInactivityDays->setValue(90);

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

    m_spnSmartBridgeThreshold = new QSpinBox(pagePastoral);
    m_spnSmartBridgeThreshold->setRange(20, 90);
    m_spnSmartBridgeThreshold->setSuffix("%");
    m_spnSmartBridgeThreshold->setValue(40);

    m_chkGraphPhysicsDamping = new QCheckBox("Enable Live Force-Directed Physics Animation", pagePastoral);
    m_chkGraphPhysicsDamping->setChecked(true);

    formGraph->addRow("Pastoral Inactivity Alert Window:", m_spnInactivityDays);
    formGraph->addRow("Volunteer Burnout Threshold (Group Count):", m_spnBurnoutThreshold);
    formGraph->addRow("Ministry Family Dominance Alert Percentage:", m_spnFamilyDominancePercent);
    formGraph->addRow("Default Pastoral Care Follow-up Window:", m_spnFollowUpDays);
    formGraph->addRow("Smart Bridge Minimum Match Threshold:", m_spnSmartBridgeThreshold);
    formGraph->addRow("Graph Dynamics:", m_chkGraphPhysicsDamping);

    layPastoral->addWidget(grpThresholds);
    layPastoral->addStretch();
    scrollPastoral->setWidget(pagePastoral);
    m_stackPages->addWidget(scrollPastoral);

    // -------------------------------------------------------------
    // Page 6: Kiosk & Nursery Security
    // -------------------------------------------------------------
    QScrollArea* scrollKiosk = new QScrollArea(this);
    scrollKiosk->setWidgetResizable(true);
    scrollKiosk->setFrameShape(QFrame::NoFrame);
    QWidget* pageKiosk = new QWidget();
    QVBoxLayout* layKiosk = new QVBoxLayout(pageKiosk);
    layKiosk->setContentsMargins(10, 0, 10, 10);
    layKiosk->setSpacing(16);

    QGroupBox* grpKiosk = new QGroupBox("🖥️ Self-Service Kiosk & Child Safety", pageKiosk);
    QFormLayout* formKiosk = new QFormLayout(grpKiosk);
    formKiosk->setSpacing(12);

    m_spnKioskTimeout = new QSpinBox(pageKiosk);
    m_spnKioskTimeout->setRange(15, 300);
    m_spnKioskTimeout->setSuffix(" seconds");
    m_spnKioskTimeout->setValue(45);

    m_chkKioskRequirePin = new QCheckBox("Require 4-digit Security PIN for Child Pickup & Badging", pageKiosk);
    m_chkKioskRequirePin->setChecked(true);

    formKiosk->addRow("Inactivity Screen Reset Timeout:", m_spnKioskTimeout);
    formKiosk->addRow("Nursery Guardian Release Mode:", m_chkKioskRequirePin);

    layKiosk->addWidget(grpKiosk);
    layKiosk->addStretch();
    scrollKiosk->setWidget(pageKiosk);
    m_stackPages->addWidget(scrollKiosk);

    // -------------------------------------------------------------
    // Page 7: Data, Backup & Integrations
    // -------------------------------------------------------------
    QScrollArea* scrollData = new QScrollArea(this);
    scrollData->setWidgetResizable(true);
    scrollData->setFrameShape(QFrame::NoFrame);
    QWidget* pageData = new QWidget();
    QVBoxLayout* layData = new QVBoxLayout(pageData);
    layData->setContentsMargins(10, 0, 10, 10);
    layData->setSpacing(16);

    QGroupBox* grpDb = new QGroupBox("💾 Database Operations & Lifecycle", pageData);
    QVBoxLayout* grpDbLay = new QVBoxLayout(grpDb);
    grpDbLay->setSpacing(12);

    m_lblDbPath = new QLabel("Database File Path: ...", pageData);
    m_lblDbPath->setWordWrap(true);
    m_lblDbPath->setStyleSheet("font-family: monospace; font-size: 12px; padding: 10px; border-radius: 6px; background-color: rgba(0, 0, 0, 0.2);");
    grpDbLay->addWidget(m_lblDbPath);

    QFormLayout* formLifecycle = new QFormLayout();
    formLifecycle->setSpacing(10);

    m_cmbAutoBackupSchedule = new QComboBox(pageData);
    m_cmbAutoBackupSchedule->addItem("Weekly (Recommended - Automated 7-Day Snapshots)", "weekly");
    m_cmbAutoBackupSchedule->addItem("Daily (High Frequency Backup)", "daily");
    m_cmbAutoBackupSchedule->addItem("Monthly (Low Frequency)", "monthly");
    m_cmbAutoBackupSchedule->addItem("Disabled (Manual Export Only)", "disabled");

    m_spnBackupRetention = new QSpinBox(pageData);
    m_spnBackupRetention->setRange(3, 50);
    m_spnBackupRetention->setSuffix(" snapshots");
    m_spnBackupRetention->setValue(10);

    formLifecycle->addRow("Automated Backup Schedule:", m_cmbAutoBackupSchedule);
    formLifecycle->addRow("Backup Snapshot Retention:", m_spnBackupRetention);
    grpDbLay->addLayout(formLifecycle);

    QHBoxLayout* pcoRow = new QHBoxLayout();
    m_lblPcoStatusBadge = new QLabel("⚪ Planning Center Online: Configured via API Dialog", pageData);
    m_lblPcoStatusBadge->setStyleSheet("padding: 4px 10px; border-radius: 6px; font-weight: bold; background-color: rgba(255,255,255,0.06);");
    pcoRow->addWidget(m_lblPcoStatusBadge);
    pcoRow->addStretch();
    grpDbLay->addLayout(pcoRow);

    QHBoxLayout* dbActions = new QHBoxLayout();
    m_btnBackupDb = new QPushButton("📦 Export Database Backup (.db)", pageData);
    m_btnBackupDb->setObjectName("primaryBtn");
    connect(m_btnBackupDb, &QPushButton::clicked, this, [this]() {
        QString defaultName = QString("open_ekklesia_backup_%1.db").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString savePath = QFileDialog::getSaveFileName(this, "Export Database Backup", defaultName, "SQLite Database (*.db)");
        if (!savePath.isEmpty()) {
            if (DatabaseManager::instance().exportBackup(savePath)) {
                QMessageBox::information(this, "Backup Successful", QString("Database successfully backed up to:\n%1").arg(savePath));
            } else {
                QMessageBox::critical(this, "Backup Failed", "Could not copy database file to specified destination.");
            }
        }
    });

    m_btnRestoreDb = new QPushButton("📥 Restore Database from Backup", pageData);
    m_btnRestoreDb->setObjectName("secondaryBtn");
    connect(m_btnRestoreDb, &QPushButton::clicked, this, [this]() {
        QString openPath = QFileDialog::getOpenFileName(this, "Restore Database from Backup", QString(), "SQLite Database (*.db)");
        if (openPath.isEmpty()) return;

        const auto res = QMessageBox::warning(
            this,
            "Confirm Database Restore",
            "This will REPLACE all current data with the contents of the selected backup file.\n\n"
            "Make sure you have an up-to-date export of the current data first. Continue?",
            QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;

        if (DatabaseManager::instance().restoreFromBackup(openPath)) {
            QMessageBox::information(
                this, "Restore Complete",
                "The database was restored from the selected backup.\n\n"
                "Please restart OpenEkklesia so all screens reload the restored data.");
        } else {
            QMessageBox::critical(this, "Restore Failed",
                                  "Could not restore the database from that file. It may be corrupted or in use.");
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

        QLabel* lblInfo = new QLabel("Export or import scoped portions of your church data as a portable JSON file (e.g. on a USB drive) so the office and pastor can share exactly the records they need.", &dlg);
        lblInfo->setWordWrap(true);
        v->addWidget(lblInfo);

        QComboBox* cmbScope = new QComboBox(&dlg);
        cmbScope->addItem("🗄️ Full Database (All Tables)", "full");
        cmbScope->addItem("👥 Membership Only (People, Households, Groups)", "membership");
        cmbScope->addItem("💰 Finance Only (Accounts, Funds, Contributions)", "finance");
        cmbScope->addItem("📋 Ministry Schedules (Services, Facilities, Plans)", "schedules");
        cmbScope->addItem("🕊️ Pastoral Care & History", "care_notes");
        cmbScope->addItem("🔒 Governance & Documents (Board Motions, Vault)", "governance");
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

    m_btnPlanningCenter = new QPushButton("🔌 Planning Center Sync Wizard", pageData);
    m_btnPlanningCenter->setObjectName("primaryBtn");
    connect(m_btnPlanningCenter, &QPushButton::clicked, this, [this]() {
        PlanningCenterDialog dlg(this);
        dlg.exec();
    });

    dbActions->addWidget(m_btnBackupDb);
    dbActions->addWidget(m_btnRestoreDb);
    dbActions->addWidget(m_btnCompactDb);
    dbActions->addWidget(m_btnViewAuditLogs);
    dbActions->addWidget(m_btnSelectiveShare);
    dbActions->addWidget(m_btnPlanningCenter);
    grpDbLay->addLayout(dbActions);

    layData->addWidget(grpDb);
    layData->addStretch();
    scrollData->setWidget(pageData);
    m_stackPages->addWidget(scrollData);

    // -------------------------------------------------------------
    // Page 6: Danger Zone
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
    lblPurgeDesc->setStyleSheet("color: palette(placeholderText); font-size: 12px;");
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
    lblResetDesc->setStyleSheet("color: palette(placeholderText); font-size: 12px;");
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
    connect(m_timeDefaultService, &QTimeEdit::timeChanged, this, trackChanges);
    connect(m_spnTargetDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnIcalAlarmMins, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_cmbCurrency, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_cmbDateFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_cmbStartOfWeek, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_cmbTimezone, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_spnInactivityDays, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnBurnoutThreshold, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnFamilyDominancePercent, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnFollowUpDays, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_spnSmartBridgeThreshold, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_chkGraphPhysicsDamping, &QCheckBox::toggled, this, trackChanges);
    connect(m_spnKioskTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_chkKioskRequirePin, &QCheckBox::toggled, this, trackChanges);
    connect(m_cmbAutoBackupSchedule, QOverload<int>::of(&QComboBox::currentIndexChanged), this, trackChanges);
    connect(m_spnBackupRetention, QOverload<int>::of(&QSpinBox::valueChanged), this, trackChanges);
    connect(m_chkAutoCollapseSidebar, &QCheckBox::toggled, this, trackChanges);

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
            if (i == 0 && (q.contains("church") || q.contains("profile") || q.contains("email") || q.contains("phone") || q.contains("tax") || q.contains("address") || q.contains("denomination") || q.contains("name") || q.contains("logo"))) matches = true;
            if (i == 1 && (q.contains("theme") || q.contains("color") || q.contains("dark") || q.contains("light") || q.contains("font") || q.contains("radius") || q.contains("brand") || q.contains("preview") || q.contains("contrast") || q.contains("gallery"))) matches = true;
            if (i == 2 && (q.contains("module") || q.contains("feature") || q.contains("toggle") || q.contains("plugin") || q.contains("preset"))) matches = true;
            if (i == 3 && (q.contains("service") || q.contains("liturgy") || q.contains("worship") || q.contains("time") || q.contains("duration") || q.contains("alarm") || q.contains("ical") || q.contains("calendar"))) matches = true;
            if (i == 4 && (q.contains("currency") || q.contains("date") || q.contains("week") || q.contains("timezone") || q.contains("locale") || q.contains("format") || q.contains("symbol"))) matches = true;
            if (i == 5 && (q.contains("graph") || q.contains("burnout") || q.contains("pastoral") || q.contains("care") || q.contains("dominance") || q.contains("inactivity") || q.contains("smart bridge") || q.contains("physics"))) matches = true;
            if (i == 6 && (q.contains("kiosk") || q.contains("nursery") || q.contains("checkin") || q.contains("pin") || q.contains("timeout") || q.contains("security") || q.contains("child"))) matches = true;
            if (i == 7 && (q.contains("backup") || q.contains("compact") || q.contains("vacuum") || q.contains("audit") || q.contains("share") || q.contains("export") || q.contains("database") || q.contains("pco") || q.contains("retention") || q.contains("planning center"))) matches = true;
            if (i == 8 && (q.contains("danger") || q.contains("purge") || q.contains("delete") || q.contains("demo") || q.contains("reset"))) matches = true;
            item->setHidden(!matches);
        }
    }
}

void SettingsWidget::onMarkDirty() {
    m_isDirty = true;
    m_btnSaveSettings->setText("💾 Save Changes (Unsaved)");
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    const QString txt = tok.accentWarning.lightness() > 140 ? "#11111b" : "#ffffff";
    m_btnSaveSettings->setStyleSheet(QString("padding: 8px 18px; font-weight: bold; font-size: 13px; background-color: %1; color: %2;")
                                         .arg(tok.accentWarning.name(), txt));
}

void SettingsWidget::selectThemeCard(const QString& themeId) {
    if (!m_themeGallery) return;
    for (int i = 0; i < m_themeGallery->count(); ++i) {
        QListWidgetItem* item = m_themeGallery->item(i);
        if (item->data(Qt::UserRole).toString() == themeId) {
            m_themeGallery->setCurrentItem(item);
            return;
        }
    }
    if (m_themeGallery->count() > 0) m_themeGallery->setCurrentRow(0);
}

void SettingsWidget::populateThemePresets(int filterIndex) {
    if (!m_themeGallery) return;
    m_themeGallery->blockSignals(true);
    m_themeGallery->clear();

    QString currentId = ThemeManager::instance().activeThemeId();

    for (const QString& tId : ThemeManager::instance().availableThemes()) {
        ThemeTokens tok = ThemeManager::instance().getThemeTokens(tId);
        // filterIndex 0: All Themes, 1: Dark Only, 2: Light Only
        if (filterIndex == 1 && !tok.isDark && tId != "system_auto") continue;
        if (filterIndex == 2 && tok.isDark && tId != "system_auto") continue;

        QPixmap preview(120, 68);
        preview.fill(Qt::transparent);
        QPainter p(&preview);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect r = preview.rect().adjusted(1, 1, -1, -1);

        // window base
        p.setBrush(tok.bgBase);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, 6, 6);
        // surface panel
        p.setBrush(tok.bgSurface);
        p.drawRoundedRect(r.adjusted(8, 24, -8, -8), 4, 4);
        // accent "button"
        p.setBrush(tok.accentPrimary);
        p.drawRoundedRect(QRect(r.right() - 38, r.bottom() - 14, 30, 8), 2, 2);
        // text lines
        p.setBrush(tok.textPrimary);
        p.drawRoundedRect(QRect(r.left() + 14, r.top() + 9, 46, 4), 2, 2);
        p.setBrush(tok.textSecondary);
        p.drawRoundedRect(QRect(r.left() + 16, r.top() + 30, 58, 3), 1, 1);
        p.drawRoundedRect(QRect(r.left() + 16, r.top() + 37, 44, 3), 1, 1);
        p.drawRoundedRect(QRect(r.left() + 16, r.top() + 44, 52, 3), 1, 1);

        QListWidgetItem* item = new QListWidgetItem(QIcon(preview), tok.name, m_themeGallery);
        item->setData(Qt::UserRole, tId);
        item->setToolTip(tok.name);
        item->setSizeHint(QSize(128, 92));
        if (tId == currentId) m_themeGallery->setCurrentItem(item);
    }
    if (m_themeGallery->currentRow() < 0 && m_themeGallery->count() > 0) {
        m_themeGallery->setCurrentRow(0);
    }
    m_themeGallery->blockSignals(false);
}

void SettingsWidget::updateLivePreview() {
    ThemeTokens t = ThemeManager::instance().currentTokens();
    const QString onAccent = (t.accentPrimary.lightness() > 128) ? "#11111b" : "#ffffff";

    m_previewCard->setStyleSheet(QString(
        "#liveThemePreviewCard { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: %3px; "
        "}"
    ).arg(t.bgSurface.name(), t.borderColor.name(), QString::number(t.borderRadiusMd)));

    // Sidebar strip: base background with an accent nav "item" and muted items.
    m_prevSidebar->setStyleSheet(QString(
        "QWidget { background-color: %1; border-top-left-radius: %2px; border-bottom-left-radius: %2px; }"
    ).arg(t.bgBase.name(), QString::number(t.borderRadiusMd)));

    m_lblPreviewHeader->setStyleSheet(QString("color: %1; font-size: 15px; font-weight: bold;").arg(t.textPrimary.name()));
    m_lblPreviewSub->setStyleSheet(QString("color: %1; font-size: 12px;").arg(t.textSecondary.name()));
    m_lblPreviewBadge->setStyleSheet(QString("background-color: %1; color: %2; padding: 3px 8px; border-radius: 8px; font-size: 10px; font-weight: bold;")
                                         .arg(t.accentPrimary.name(), onAccent));

    m_prevInput->setStyleSheet(QString(
        "QLineEdit { background-color: %1; border: 1px solid %2; border-radius: %3px; "
        "color: %4; padding: 5px 10px; font-size: 12px; }"
        "QLineEdit::placeholder { color: %5; }"
    ).arg(t.bgInput.name(), t.borderColor.name(), QString::number(t.borderRadiusSm),
          t.textPrimary.name(), t.textMuted.name()));

    m_prevRow1->setStyleSheet(QString("background-color: %1; color: %2; font-size: 11px; padding: 3px 8px; border-radius: 4px;")
                                  .arg(t.bgBase.name(), t.textSecondary.name()));
    m_prevRow2->setStyleSheet(QString("background-color: %1; color: %2; font-size: 11px; padding: 3px 8px; border-radius: 4px;")
                                  .arg(t.bgBase.name(), t.textPrimary.name()));

    m_btnPreviewPrimary->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; border-radius: %3px; padding: 6px 14px;")
                                           .arg(t.accentPrimary.name(), onAccent, QString::number(t.borderRadiusSm)));

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

    // Restore the church logo preview if one was uploaded.
    const QString logoPath = s.value("churchLogo").toString();
    if (!logoPath.isEmpty() && QFile::exists(logoPath)) {
        QPixmap pix(logoPath);
        if (!pix.isNull()) {
            m_logoPreview->setPixmap(pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_logoPreview->setText("");
        }
    }

    m_chkAutoCollapseSidebar->setChecked(s.value("autoCollapseSidebar", true).toBool());

    // Load Modules & Features (default all to true)
    m_chkModPeople->setChecked(s.value("modules/people", true).toBool());
    m_chkModGroups->setChecked(s.value("modules/groups", true).toBool());
    m_chkModGraph->setChecked(s.value("modules/graph", true).toBool());
    m_chkModAnalytics->setChecked(s.value("modules/analytics", true).toBool());
    m_chkModShepherd->setChecked(s.value("modules/shepherd", true).toBool());
    m_chkModService->setChecked(s.value("modules/service", true).toBool());
    m_chkModPlanning->setChecked(s.value("modules/planning", true).toBool());
    m_chkModFacilities->setChecked(s.value("modules/facilities", true).toBool());
    m_chkModGiving->setChecked(s.value("modules/giving", s.value("modules/finance", true).toBool()).toBool());
    m_chkModInventory->setChecked(s.value("modules/inventory", true).toBool());
    m_chkModVault->setChecked(s.value("modules/vault", true).toBool());
    m_chkModKiosk->setChecked(s.value("modules/kiosk", true).toBool());

    int curIdx = m_cmbCurrency->findData(s.value("currencySymbol", "$").toString());
    if (curIdx >= 0) m_cmbCurrency->setCurrentIndex(curIdx);

    int dateIdx = m_cmbDateFormat->findData(s.value("dateFormat", "yyyy-MM-dd").toString());
    if (dateIdx >= 0) m_cmbDateFormat->setCurrentIndex(dateIdx);

    int weekIdx = m_cmbStartOfWeek->findData(s.value("startOfWeek", 0).toInt());
    if (weekIdx >= 0) m_cmbStartOfWeek->setCurrentIndex(weekIdx);

    int tzIdx = m_cmbTimezone->findData(s.value("timezone", "local").toString());
    if (tzIdx >= 0) m_cmbTimezone->setCurrentIndex(tzIdx);

    m_timeDefaultService->setTime(QTime::fromString(s.value("defaultServiceTime", "10:00").toString(), "HH:mm"));
    if (!m_timeDefaultService->time().isValid()) m_timeDefaultService->setTime(QTime(10, 0));

    m_spnTargetDuration->setValue(s.value("targetServiceDuration", 75).toInt());
    m_spnIcalAlarmMins->setValue(s.value("volunteerAlarmMinutes", 60).toInt());

    m_spnInactivityDays->setValue(s.value("inactivityDays", 90).toInt());
    m_spnBurnoutThreshold->setValue(s.value("burnoutThreshold", 3).toInt());
    m_spnFamilyDominancePercent->setValue(s.value("familyDominancePercent", 50).toInt());
    m_spnFollowUpDays->setValue(s.value("followUpDays", 14).toInt());
    m_spnSmartBridgeThreshold->setValue(s.value("smartBridgeMinScore", 40).toInt());
    m_chkGraphPhysicsDamping->setChecked(s.value("graphPhysicsEnabled", true).toBool());

    m_spnKioskTimeout->setValue(s.value("kioskTimeoutSeconds", 45).toInt());
    m_chkKioskRequirePin->setChecked(s.value("kioskRequirePin", true).toBool());

    int schedIdx = m_cmbAutoBackupSchedule->findData(s.value("autoBackupSchedule", "weekly").toString());
    if (schedIdx >= 0) m_cmbAutoBackupSchedule->setCurrentIndex(schedIdx);
    m_spnBackupRetention->setValue(s.value("backupRetentionCount", 10).toInt());

    // Update live PCO badge
    const QString pcoId = s.value("pcoAppId").toString();
    if (!pcoId.isEmpty()) {
        m_lblPcoStatusBadge->setText(QString("🟢 Planning Center Online: Connected (App ID: %1)").arg(pcoId));
        const bool dark = ThemeManager::instance().currentTokens().isDark;
        m_lblPcoStatusBadge->setStyleSheet(QString("padding: 4px 10px; border-radius: 6px; font-weight: bold; "
            "background-color: %1; color: %2;").arg(dark ? "rgba(166, 227, 161, 0.15)" : "rgba(21, 128, 61, 0.08)",
            dark ? "#a6e3a1" : "#15803d"));
    } else {
        m_lblPcoStatusBadge->setText("⚪ Planning Center Online: Not Configured");
        m_lblPcoStatusBadge->setStyleSheet("padding: 4px 10px; border-radius: 6px; font-weight: bold; background-color: rgba(255, 255, 255, 0.06);");
    }

    populateThemePresets(m_cmbThemeModeFilter->currentIndex());

    bool isSystemAuto = (ThemeManager::instance().activeThemeId() == "system_auto");
    m_chkSyncSystemTheme->blockSignals(true);
    m_chkSyncSystemTheme->setChecked(isSystemAuto);
    m_chkSyncSystemTheme->blockSignals(false);
    m_themeGallery->setEnabled(!isSystemAuto);
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
    const bool wasDirty = m_isDirty;
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

    // Save Module states
    s.setValue("modules/people", m_chkModPeople->isChecked());
    s.setValue("modules/groups", m_chkModGroups->isChecked());
    s.setValue("modules/graph", m_chkModGraph->isChecked());
    s.setValue("modules/analytics", m_chkModAnalytics->isChecked());
    s.setValue("modules/shepherd", m_chkModShepherd->isChecked());
    s.setValue("modules/service", m_chkModService->isChecked());
    s.setValue("modules/planning", m_chkModPlanning->isChecked());
    s.setValue("modules/facilities", m_chkModFacilities->isChecked());
    s.setValue("modules/giving", m_chkModGiving->isChecked());
    s.setValue("modules/inventory", m_chkModInventory->isChecked());
    s.setValue("modules/vault", m_chkModVault->isChecked());
    s.setValue("modules/kiosk", m_chkModKiosk->isChecked());

    s.setValue("currencySymbol", m_cmbCurrency->currentData().toString());
    s.setValue("dateFormat", m_cmbDateFormat->currentData().toString());
    s.setValue("startOfWeek", m_cmbStartOfWeek->currentData().toInt());
    s.setValue("timezone", m_cmbTimezone->currentData().toString());

    s.setValue("defaultServiceTime", m_timeDefaultService->time().toString("HH:mm"));
    s.setValue("targetServiceDuration", m_spnTargetDuration->value());
    s.setValue("volunteerAlarmMinutes", m_spnIcalAlarmMins->value());

    s.setValue("inactivityDays", m_spnInactivityDays->value());
    s.setValue("burnoutThreshold", m_spnBurnoutThreshold->value());
    s.setValue("familyDominancePercent", m_spnFamilyDominancePercent->value());
    s.setValue("followUpDays", m_spnFollowUpDays->value());
    s.setValue("smartBridgeMinScore", m_spnSmartBridgeThreshold->value());
    s.setValue("graphPhysicsEnabled", m_chkGraphPhysicsDamping->isChecked());

    s.setValue("kioskTimeoutSeconds", m_spnKioskTimeout->value());
    s.setValue("kioskRequirePin", m_chkKioskRequirePin->isChecked());

    s.setValue("autoBackupSchedule", m_cmbAutoBackupSchedule->currentData().toString());
    s.setValue("backupRetentionCount", m_spnBackupRetention->value());

    if (m_chkSyncSystemTheme->isChecked()) {
        ThemeManager::instance().loadTheme("system_auto");
    } else {
        // The active theme is already applied live by the gallery;
        // re-apply so the setting round-trips cleanly.
        ThemeManager::instance().loadTheme(ThemeManager::instance().activeThemeId());
    }

    s.sync();

    m_isDirty = false;
    m_btnSaveSettings->setText("💾 Save All Changes");
    m_btnSaveSettings->setStyleSheet("padding: 8px 18px; font-weight: bold; font-size: 13px;");

    if (wasDirty) {
        QMessageBox::information(this, "Settings Saved", "Application settings have been updated successfully.");
    }
    emit settingsSaved();
}

void SettingsWidget::updateColorButtonStyles() {
    ThemeTokens t = ThemeManager::instance().currentTokens();

    auto applyBtnStyle = [](QPushButton* btn, const QColor& col, const QString& label) {
        const QString textColor = (col.lightness() > 128) ? "#11111b" : "#ffffff";
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid rgba(0,0,0,0.25); "
            "border-radius: 6px; padding: 6px 10px; text-align: left; font-weight: bold; }"
            "QPushButton:hover { border: 2px solid %2; }")
            .arg(col.name(), textColor));
        btn->setText(QString("%1  %2").arg(label, col.name().toUpper()));
    };

    applyBtnStyle(m_btnBgBaseColor, t.bgBase, "Window Background");
    applyBtnStyle(m_btnBgSurfaceColor, t.bgSurface, "Cards & Panels");
    applyBtnStyle(m_btnAccentPrimaryColor, t.accentPrimary, "Accent Color");
    applyBtnStyle(m_btnTextPrimaryColor, t.textPrimary, "Text Color");
    m_spnCornerRadius->setValue(t.borderRadiusMd);

    // Update WCAG Contrast Guardian meters
    if (m_lblContrastTextBg && m_lblContrastButtonAccent && m_lblContrastWarning) {
        double textRatio = ThemeManager::contrastRatio(t.textPrimary, t.bgBase);
        QColor onBtnColor = (t.accentPrimary.lightness() > 140) ? QColor("#11111b") : QColor("#ffffff");
        double btnRatio = ThemeManager::contrastRatio(onBtnColor, t.accentPrimary);

        m_lblContrastTextBg->setText(QString("%1 (Text vs Background)")
                                         .arg(ThemeManager::contrastRating(textRatio)));
        m_lblContrastButtonAccent->setText(QString("%1 (Button Text vs Accent)")
                                               .arg(ThemeManager::contrastRating(btnRatio)));

        if (textRatio < 4.5 || btnRatio < 4.5) {
            m_lblContrastWarning->setText("⚠️ Caution: Contrast ratio falls below WCAG 2.1 AA (4.5:1) minimum standard.");
            m_lblContrastWarning->setVisible(true);
        } else {
            m_lblContrastWarning->setVisible(false);
        }
    }
}

void SettingsWidget::onPickLogo() {
    const QString file = QFileDialog::getOpenFileName(this, "Select Church Logo", QString(),
                                                      "Images (*.png *.jpg *.jpeg *.bmp *.svg)");
    if (file.isEmpty()) return;

    // Copy into the app data dir so the logo is stable for document printing
    // and survives the original file being moved or deleted.
    const QString suffix = QFileInfo(file).suffix().toLower();
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logos";
    QDir().mkpath(destDir);
    const QString dest = destDir + "/church_logo." + suffix;
    if (QFile::exists(dest)) QFile::remove(dest);
    if (!QFile::copy(file, dest)) {
        QMessageBox::critical(this, "Error", "Could not copy the selected logo into the app data folder.");
        return;
    }
    QSettings("OpenEkklesia", "CHMS").setValue("churchLogo", dest);

    QPixmap pix(dest);
    if (!pix.isNull()) {
        m_logoPreview->setPixmap(pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_logoPreview->setText("");
    }
    onMarkDirty();
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
        selectThemeCard(ThemeManager::instance().activeThemeId());

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
            selectThemeCard(ThemeManager::instance().activeThemeId());

            updateLivePreview();
            QMessageBox::information(this, "Preset Saved", QString("Custom theme preset '%1' has been saved!").arg(presetName));
        }
    }
}

void SettingsWidget::onResetDefaultTheme() {
    ThemeManager::instance().clearCustomizationOverride();
    ThemeManager::instance().loadTheme("catppuccin_mocha");
    populateThemePresets(m_cmbThemeModeFilter->currentIndex());
    selectThemeCard("catppuccin_mocha");
    updateColorButtonStyles();
    updateLivePreview();
    onMarkDirty();
}
