#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QFrame>

class SettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWidget(QWidget *parent = nullptr);
    ~SettingsWidget() override = default;

    void loadSettings();
    void saveSettings();

signals:
    void settingsSaved();
    void themeToggleRequested();

private slots:
    void onPickColor();
    void onRadiusSliderChanged(int value);
    void onFontSizeSliderChanged(int value);
    void onGenerateChurchBrand();
    void onSaveCustomPreset();
    void onResetDefaultTheme();
    void onSearchTextChanged(const QString& text);
    void onCategorySelected(int row);
    void onMarkDirty();

private:
    void setupUI();
    void updateColorButtonStyles();
    void updateLivePreview();
    void populateThemePresets(int filterIndex);

    // Navigation & Search
    QLineEdit* m_searchBox;
    QListWidget* m_navList;
    QStackedWidget* m_stackPages;

    // Header & Actions
    QLabel* m_lblStatus;
    QPushButton* m_btnSaveSettings;
    bool m_isDirty;

    // --- Page 1: General & Profile ---
    QLineEdit* m_txtChurchName;
    QLineEdit* m_txtChurchSubtitle;
    QLineEdit* m_txtDenomination;
    QLineEdit* m_txtAddress;
    QLineEdit* m_txtPhone;
    QLineEdit* m_txtEmail;
    QLineEdit* m_txtWebsite;
    QLineEdit* m_txtTaxId;

    // --- Page 2: Appearance & Theme ---
    QComboBox* m_cmbThemeModeFilter;
    QListWidget* m_themeGallery;   // visual theme card gallery (IconMode)
    QCheckBox* m_chkSyncSystemTheme;
    QCheckBox* m_chkAutoCollapseSidebar;
    QPushButton* m_btnBgBaseColor;
    QPushButton* m_btnBgSurfaceColor;
    QPushButton* m_btnAccentPrimaryColor;
    QPushButton* m_btnTextPrimaryColor;
    QSpinBox* m_spnCornerRadius;
    QSpinBox* m_spnFontSize;
    QPushButton* m_btnPickBrandColor;
    QComboBox* m_cmbBrandBaseMode;
    QPushButton* m_btnSavePreset;
    QPushButton* m_btnResetTheme;
    void selectThemeCard(const QString& themeId);

    // Live Theme Preview Card
    QFrame* m_previewCard;
    QLabel* m_lblPreviewHeader;
    QLabel* m_lblPreviewSub;
    QPushButton* m_btnPreviewPrimary;
    QPushButton* m_btnPreviewSecondary;
    QLabel* m_lblPreviewBadge;
    QWidget* m_prevSidebar;
    QLineEdit* m_prevInput;
    QLabel* m_prevRow1;
    QLabel* m_prevRow2;

    // --- Page 3: Modules & Features ---
    QCheckBox* m_chkModPeople;
    QCheckBox* m_chkModGroups;
    QCheckBox* m_chkModFinance;
    QCheckBox* m_chkModService;
    QCheckBox* m_chkModGraph;
    QCheckBox* m_chkModInventory;
    QCheckBox* m_chkModVault;
    QCheckBox* m_chkModFacilities;
    QCheckBox* m_chkModPlanning;
    QCheckBox* m_chkModAnalytics;
    QCheckBox* m_chkModShepherd;
    QCheckBox* m_chkModKiosk;

    // --- Page 4: Localization & Region ---
    QComboBox* m_cmbCurrency;
    QComboBox* m_cmbDateFormat;
    QComboBox* m_cmbStartOfWeek;
    QComboBox* m_cmbTimezone;

    // --- Page 5: Pastoral & Graph Thresholds ---
    QSpinBox* m_spnBurnoutThreshold;
    QSpinBox* m_spnFamilyDominancePercent;
    QSpinBox* m_spnFollowUpDays;

    // --- Page 6: Data & Backup ---
    QLabel* m_lblDbPath;
    QPushButton* m_btnBackupDb;
    QPushButton* m_btnRestoreDb;
    QPushButton* m_btnCompactDb;
    QPushButton* m_btnViewAuditLogs;
    QPushButton* m_btnSelectiveShare;
    QPushButton* m_btnPlanningCenter;

    // --- Page 7: Danger Zone ---
    QPushButton* m_btnPurgeDemoData;
    QPushButton* m_btnResetAppDefaults;
};

#endif // SETTINGSWIDGET_H
