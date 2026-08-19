#include "planningcenterdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QSettings>
#include <QDateTime>

PlanningCenterDialog::PlanningCenterDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("🔌 Planning Center Online Integration & Importer");
    resize(640, 560);
    setupUI();
    loadCredentials();
}

void PlanningCenterDialog::setupUI() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setSpacing(14);
    root->setContentsMargins(20, 20, 20, 20);

    // Banner
    QLabel* title = new QLabel("Planning Center Online Sync", this);
    title->setStyleSheet("font-size: 16px; font-weight: bold;");
    QLabel* subtitle = new QLabel("Connect your church's Planning Center account using a Personal Access Token to import or synchronize rosters, life groups, and service plans.", this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color: #a6adc8; font-size: 12px; margin-bottom: 4px;");
    root->addWidget(title);
    root->addWidget(subtitle);

    // Step 1: Credentials
    QGroupBox* grpCreds = new QGroupBox("1. API Credentials (Personal Access Token)", this);
    QFormLayout* formCreds = new QFormLayout(grpCreds);
    formCreds->setSpacing(10);

    m_txtAppId = new QLineEdit(this);
    m_txtAppId->setPlaceholderText("e.g. pc_app_...");
    m_txtSecret = new QLineEdit(this);
    m_txtSecret->setEchoMode(QLineEdit::Password);
    m_txtSecret->setPlaceholderText("e.g. pc_sec_...");

    formCreds->addRow("Application ID:", m_txtAppId);
    formCreds->addRow("Secret Key:", m_txtSecret);

    QHBoxLayout* credBtns = new QHBoxLayout();
    m_btnTest = new QPushButton("⚡ Test Connection", this);
    m_btnTest->setObjectName("primaryBtn");
    m_btnSaveCreds = new QPushButton("💾 Save Credentials", this);
    m_btnClearCreds = new QPushButton("🗑️ Clear", this);

    connect(m_btnTest, &QPushButton::clicked, this, &PlanningCenterDialog::onTestConnection);
    connect(m_btnSaveCreds, &QPushButton::clicked, this, &PlanningCenterDialog::onSaveCredentials);
    connect(m_btnClearCreds, &QPushButton::clicked, this, &PlanningCenterDialog::onClearCredentials);

    credBtns->addWidget(m_btnTest);
    credBtns->addWidget(m_btnSaveCreds);
    credBtns->addWidget(m_btnClearCreds);
    credBtns->addStretch();
    formCreds->addRow(credBtns);

    m_lblStatus = new QLabel("Status: Not verified", this);
    m_lblStatus->setStyleSheet("font-size: 12px; color: #a6adc8; font-style: italic;");
    formCreds->addRow(m_lblStatus);
    root->addWidget(grpCreds);

    // Step 2: Import Scopes
    QGroupBox* grpScope = new QGroupBox("2. Select Scopes to Import", this);
    QVBoxLayout* layScope = new QVBoxLayout(grpScope);
    m_chkImportPeople = new QCheckBox("👥 People & Membership Directory (Names, contact info, statuses)", this);
    m_chkImportPeople->setChecked(true);
    m_chkImportGroups = new QCheckBox("🏠 Small Groups & Serving Teams (Life groups, names, schedules)", this);
    m_chkImportGroups->setChecked(true);
    m_chkImportServices = new QCheckBox("🗓️ Service Types & Plan Liturgies", this);
    m_chkImportServices->setChecked(true);
    m_chkImportGiving = new QCheckBox("💳 Giving & Contributions (Funds, online donations, checks)", this);
    m_chkImportGiving->setChecked(true);

    layScope->addWidget(m_chkImportPeople);
    layScope->addWidget(m_chkImportGroups);
    layScope->addWidget(m_chkImportServices);
    layScope->addWidget(m_chkImportGiving);
    root->addWidget(grpScope);

    // Step 3: Execution & Logs
    QGroupBox* grpAction = new QGroupBox("3. Import Execution & Log", this);
    QVBoxLayout* layAction = new QVBoxLayout(grpAction);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layAction->addWidget(m_progressBar);

    m_txtLog = new QTextEdit(this);
    m_txtLog->setReadOnly(true);
    m_txtLog->setPlaceholderText("Log output will appear here during synchronization...");
    m_txtLog->setStyleSheet("font-family: monospace; font-size: 11px; background: rgba(0, 0, 0, 0.15);");
    m_txtLog->setMaximumHeight(120);
    layAction->addWidget(m_txtLog);

    m_btnImport = new QPushButton("🚀 Start Planning Center Import", this);
    m_btnImport->setObjectName("primaryBtn");
    m_btnImport->setMinimumHeight(38);
    connect(m_btnImport, &QPushButton::clicked, this, &PlanningCenterDialog::onStartImport);
    layAction->addWidget(m_btnImport);

    root->addWidget(grpAction);
}

void PlanningCenterDialog::loadCredentials() {
    QSettings s("OpenEkklesia", "CHMS");
    m_txtAppId->setText(s.value("pco/appId", "").toString());
    m_txtSecret->setText(s.value("pco/secret", "").toString());
}

void PlanningCenterDialog::onSaveCredentials() {
    QSettings s("OpenEkklesia", "CHMS");
    s.setValue("pco/appId", m_txtAppId->text().trimmed());
    s.setValue("pco/secret", m_txtSecret->text().trimmed());
    s.sync();
    QMessageBox::information(this, "Saved", "Planning Center credentials saved.");
}

void PlanningCenterDialog::onClearCredentials() {
    m_txtAppId->clear();
    m_txtSecret->clear();
    QSettings s("OpenEkklesia", "CHMS");
    s.remove("pco/appId");
    s.remove("pco/secret");
    s.sync();
    m_lblStatus->setText("Status: Credentials cleared.");
    m_lblStatus->setStyleSheet("color: #a6adc8;");
}

void PlanningCenterDialog::onTestConnection() {
    QString appId = m_txtAppId->text().trimmed();
    QString secret = m_txtSecret->text().trimmed();

    m_lblStatus->setText("Status: Authenticating with Planning Center API...");
    m_lblStatus->setStyleSheet("color: #89b4fa;");
    m_btnTest->setEnabled(false);

    m_client.testConnection(appId, secret, [this](bool success, const QString& message) {
        m_btnTest->setEnabled(true);
        if (success) {
            m_lblStatus->setText("Status: ✅ Connection verified!");
            m_lblStatus->setStyleSheet("color: #a6e3a1; font-weight: bold;");
            m_txtLog->append(QString("[%1] Authenticated successfully with Planning Center.").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        } else {
            m_lblStatus->setText("Status: ❌ Connection failed.");
            m_lblStatus->setStyleSheet("color: #f38ba8; font-weight: bold;");
            m_txtLog->append(QString("[%1] Error: %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), message));
        }
    });
}

void PlanningCenterDialog::onStartImport() {
    QString appId = m_txtAppId->text().trimmed();
    QString secret = m_txtSecret->text().trimmed();

    if (appId.isEmpty() || secret.isEmpty()) {
        QMessageBox::warning(this, "Credentials Required", "Please enter your Planning Center App ID and Secret before importing.");
        return;
    }

    m_btnImport->setEnabled(false);
    m_progressBar->setValue(5);
    m_txtLog->append(QString("[%1] Starting synchronization pipeline...").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

    // Define sequential tasks
    struct Task {
        QString name;
        std::function<void(std::function<void()> next)> run;
    };

    auto tasks = std::make_shared<QList<Task>>();

    // Task 1: People
    if (m_chkImportPeople->isChecked()) {
        tasks->append({"People", [this, appId, secret](std::function<void()> next) {
            m_txtLog->append("Fetching People from Planning Center (/people/v2/people)...");
            m_progressBar->setValue(15);
            m_client.fetchPeople(appId, secret, [this, next](bool ok, const QJsonArray& data, const QString& err) {
                if (ok) {
                    m_txtLog->append(QString("Received %1 member records from Planning Center API.").arg(data.size()));
                    PcoImportSummary sum = m_client.importPeopleData(data);
                    m_txtLog->append(QString("✅ Synced %1 member records into OpenEkklesia.").arg(sum.peopleCount));
                    for (const QString& e : sum.errors) m_txtLog->append(QString("⚠️ %1").arg(e));
                } else {
                    m_txtLog->append(QString("❌ Failed to fetch people: %1").arg(err));
                }
                m_progressBar->setValue(35);
                next();
            });
        }});
    }

    // Task 2: Groups
    if (m_chkImportGroups->isChecked()) {
        tasks->append({"Groups", [this, appId, secret](std::function<void()> next) {
            m_txtLog->append("Fetching Groups from Planning Center (/groups/v2/groups)...");
            m_client.fetchGroups(appId, secret, [this, next](bool ok, const QJsonArray& data, const QString& err) {
                if (ok) {
                    m_txtLog->append(QString("Received %1 group records from API.").arg(data.size()));
                    PcoImportSummary sum = m_client.importGroupsData(data);
                    m_txtLog->append(QString("✅ Synced %1 groups.").arg(sum.groupsCount));
                    for (const QString& e : sum.errors) m_txtLog->append(QString("⚠️ %1").arg(e));
                } else {
                    m_txtLog->append(QString("❌ Failed to fetch groups: %1").arg(err));
                }
                m_progressBar->setValue(55);
                next();
            });
        }});
    }

    // Task 3: Services
    if (m_chkImportServices->isChecked()) {
        tasks->append({"Services", [this, appId, secret](std::function<void()> next) {
            m_txtLog->append("Fetching Services from Planning Center (/services/v2/service_types)...");
            m_client.fetchServicePlans(appId, secret, [this, next](bool ok, const QJsonArray& data, const QString& err) {
                if (ok) {
                    m_txtLog->append(QString("Received %1 service plans from API.").arg(data.size()));
                    PcoImportSummary sum = m_client.importServicesData(data);
                    m_txtLog->append(QString("✅ Synced %1 service types & plans.").arg(sum.servicesCount));
                    for (const QString& e : sum.errors) m_txtLog->append(QString("⚠️ %1").arg(e));
                } else {
                    m_txtLog->append(QString("❌ Failed to fetch service plans: %1").arg(err));
                }
                m_progressBar->setValue(75);
                next();
            });
        }});
    }

    // Task 4: Giving (Funds & Donations)
    if (m_chkImportGiving->isChecked()) {
        tasks->append({"Giving", [this, appId, secret](std::function<void()> next) {
            m_txtLog->append("Fetching Giving Funds (/giving/v2/funds)...");
            m_client.fetchGivingFunds(appId, secret, [this, appId, secret, next](bool fOk, const QJsonArray& fData, const QString& fErr) {
                if (fOk) {
                    m_txtLog->append(QString("Received %1 giving funds from API.").arg(fData.size()));
                } else {
                    m_txtLog->append(QString("⚠️ Could not fetch funds: %1").arg(fErr));
                }

                m_txtLog->append("Fetching Giving Donations (/giving/v2/donations)...");
                m_client.fetchGivingDonations(appId, secret, [this, fData, next](bool dOk, const QJsonArray& dData, const QString& dErr) {
                    if (dOk) {
                        m_txtLog->append(QString("Received %1 donations from API.").arg(dData.size()));
                        PcoImportSummary sum = m_client.importGivingData(fData, dData);
                        m_txtLog->append(QString("✅ Synced %1 funds and %2 donations into Contributions.").arg(sum.fundsCount).arg(sum.donationsCount));
                        for (const QString& e : sum.errors) m_txtLog->append(QString("⚠️ %1").arg(e));
                    } else {
                        m_txtLog->append(QString("⚠️ Could not fetch donations: %1").arg(dErr));
                    }
                    m_progressBar->setValue(95);
                    next();
                });
            });
        }});
    }

    if (tasks->isEmpty()) {
        m_txtLog->append("⚠️ No scopes selected for import.");
        m_btnImport->setEnabled(true);
        m_progressBar->setValue(0);
        return;
    }

    // Run pipeline runner
    auto runNextTask = std::make_shared<std::function<void(int)>>();
    *runNextTask = [this, tasks, runNextTask](int index) {
        if (index >= tasks->size()) {
            m_progressBar->setValue(100);
            m_btnImport->setEnabled(true);
            m_txtLog->append("🎉 Planning Center Synchronization Finished.");
            QMessageBox::information(this, "Import Complete", "Planning Center Online data has been synchronized!");
            return;
        }

        const Task& task = tasks->at(index);
        task.run([runNextTask, index]() {
            (*runNextTask)(index + 1);
        });
    };

    (*runNextTask)(0);
}
