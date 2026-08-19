#ifndef PLANNINGCENTERDIALOG_H
#define PLANNINGCENTERDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include "../integrations/planningcenterclient.h"

class PlanningCenterDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlanningCenterDialog(QWidget *parent = nullptr);
    ~PlanningCenterDialog() override = default;

private slots:
    void onTestConnection();
    void onStartImport();
    void onSaveCredentials();
    void onClearCredentials();

private:
    void setupUI();
    void loadCredentials();

    PlanningCenterClient m_client;

    QLineEdit* m_txtAppId;
    QLineEdit* m_txtSecret;
    QPushButton* m_btnTest;
    QPushButton* m_btnSaveCreds;
    QPushButton* m_btnClearCreds;
    QLabel* m_lblStatus;

    QCheckBox* m_chkImportPeople;
    QCheckBox* m_chkImportGroups;
    QCheckBox* m_chkImportServices;
    QCheckBox* m_chkImportGiving;

    QPushButton* m_btnImport;
    QProgressBar* m_progressBar;
    QTextEdit* m_txtLog;
};

#endif // PLANNINGCENTERDIALOG_H
