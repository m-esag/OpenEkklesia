#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

class SettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWidget(QWidget *parent = nullptr);
    ~SettingsWidget() = default;

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

private:
    void setupUI();
    void updateColorButtonStyles();

    QLineEdit* m_txtChurchName;
    QLineEdit* m_txtChurchSubtitle;

    QComboBox* m_cmbThemeModeFilter;
    QComboBox* m_cmbThemePreset;
    void populateThemePresets(int filterIndex);
    QCheckBox* m_chkSyncSystemTheme;
    QCheckBox* m_chkAutoCollapseSidebar;
    QCheckBox* m_chkShowCongregation;
    QCheckBox* m_chkShowMinistry;
    QCheckBox* m_chkShowFinance;

    // Theme Editor Interactive Controls
    QPushButton* m_btnBgBaseColor;
    QPushButton* m_btnBgSurfaceColor;
    QPushButton* m_btnAccentPrimaryColor;
    QPushButton* m_btnTextPrimaryColor;
    QSpinBox* m_spnCornerRadius;
    QSpinBox* m_spnFontSize;
    
    // Church Brand Theme Generator
    QPushButton* m_btnPickBrandColor;
    QComboBox* m_cmbBrandBaseMode;

    QPushButton* m_btnSavePreset;
    QPushButton* m_btnResetTheme;

    QSpinBox* m_spnBurnoutThreshold;
    QSpinBox* m_spnFamilyDominancePercent;
    
    QLabel* m_lblDbPath;
    QPushButton* m_btnBackupDb;
    QPushButton* m_btnCompactDb;
    QPushButton* m_btnPurgeDemoData;
    QPushButton* m_btnViewAuditLogs;
    QPushButton* m_btnSaveSettings;
};

#endif // SETTINGSWIDGET_H
