#include "contributionswidget.h"
#include "../database/databasemanager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QCompleter>
#include <QDir>
#include <QPdfWriter>
#include <QTextDocument>
#include <QCoreApplication>
#include <QDebug>
#include <QInputDialog>
#include <QMenu>

ContributionsWidget::ContributionsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupModels();
    refreshHistoryModel();
}

ContributionsWidget::~ContributionsWidget() {}

void ContributionsWidget::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(20);

    // Left Panel (Rapid Batch Entry Form)
    QWidget* formPanel = new QWidget(this);
    formPanel->setObjectName("formPanel");
    
    QVBoxLayout* formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(20, 20, 20, 20);
    formLayout->setSpacing(15);

    QLabel* titleLabel = new QLabel("Rapid Contributions Batch Entry", this);
    titleLabel->setObjectName("detailHeader");
    formLayout->addWidget(titleLabel);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(12);

    // Default to previous Sunday
    QDate today = QDate::currentDate();
    int daysToSubtract = today.dayOfWeek(); // Mon=1, Sun=7
    QDate prevSunday = today.addDays(-daysToSubtract);
    m_dateEdit = new QDateEdit(prevSunday, this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");

    m_personCombo = new QComboBox(this);
    m_personCombo->setEditable(true);

    m_amountSpin = new QDoubleSpinBox(this);
    m_amountSpin->setRange(0.00, 999999.00);
    m_amountSpin->setDecimals(2);
    m_amountSpin->setPrefix("$ ");

    m_fundCombo = new QComboBox(this);

    m_paymentCombo = new QComboBox(this);
    m_paymentCombo->addItems({"Cash", "Check", "Electronic"});
    connect(m_paymentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ContributionsWidget::onPaymentMethodChanged);

    m_checkNumberEdit = new QLineEdit(this);
    m_checkNumberEdit->setPlaceholderText("Check number (only if check)");
    m_checkNumberEdit->setEnabled(false);

    form->addRow("Date:", m_dateEdit);
    form->addRow("Person:", m_personCombo);
    form->addRow("Amount:", m_amountSpin);
    form->addRow("Fund:", m_fundCombo);
    form->addRow("Payment Method:", m_paymentCombo);
    form->addRow("Check Number:", m_checkNumberEdit);

    formLayout->addLayout(form);

    m_btnSave = new QPushButton("Save Entry (Enter)", this);
    m_btnSave->setObjectName("saveBtn");
    connect(m_btnSave, &QPushButton::clicked, this, &ContributionsWidget::onSaveContribution);
    formLayout->addWidget(m_btnSave);

    formLayout->addSpacing(15);
    
    // Tax reporting features
    QLabel* exportLabel = new QLabel("Year-End Tax Receipts", this);
    exportLabel->setObjectName("detailSubHeader");
    formLayout->addWidget(exportLabel);

    m_btnExportPDF = new QPushButton("Export Tax Statements (PDF)", this);
    m_btnExportPDF->setObjectName("accentBtnPink");
    connect(m_btnExportPDF, &QPushButton::clicked, this, &ContributionsWidget::onExportTaxStatements);
    formLayout->addWidget(m_btnExportPDF);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    formLayout->addWidget(m_progressBar);

    formLayout->addStretch();
    mainLayout->addWidget(formPanel, 2);

    // Right Panel (Running Log Table)
    QWidget* logPanel = new QWidget(this);
    QVBoxLayout* logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(12);

    QLabel* logLabel = new QLabel("Recent Contributions Log", this);
    logLabel->setObjectName("detailHeader");
    logLayout->addWidget(logLabel);

    m_historyTable = new QTableView(this);
    m_historyTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->verticalHeader()->setVisible(false);
    connect(m_historyTable, &QTableView::customContextMenuRequested, this, &ContributionsWidget::onHistoryContextMenu);
    logLayout->addWidget(m_historyTable);

    mainLayout->addWidget(logPanel, 3);
}

void ContributionsWidget::setupModels() {
    QSqlQuery query;

    // Load active people
    if (query.exec("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name")) {
        while (query.next()) {
            int id = query.value(0).toInt();
            QString name = query.value(1).toString();
            m_peopleList.append({id, name});
            m_personCombo->addItem(name, id);
        }
    }
    
    // Add autocomplete to Person combo
    m_personCombo->setEditable(true);
    QCompleter* completer = new QCompleter(this);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setModel(m_personCombo->model());
    m_personCombo->setCompleter(completer);

    // Load active funds
    if (query.exec("SELECT id, fund_name FROM funds WHERE is_active = 1 ORDER BY fund_name")) {
        while (query.next()) {
            int id = query.value(0).toInt();
            QString name = query.value(1).toString();
            m_fundsList.append({id, name});
            m_fundCombo->addItem(name, id);
        }
    }

    m_historyModel = new QSqlQueryModel(this);
    m_historyTable->setModel(m_historyModel);
}

void ContributionsWidget::refreshHistoryModel() {
    QString queryStr = 
        "SELECT c.id AS \"ID\", "
        "       c.contribution_date AS \"Date\", "
        "       (p.first_name || ' ' || p.last_name) AS \"Contributor\", "
        "       f.fund_name AS \"Fund\", "
        "       printf('$%.2f', c.amount / 100.0) AS \"Amount\", "
        "       c.payment_method AS \"Method\" "
        "FROM contributions c "
        "JOIN people p ON c.person_id = p.id "
        "JOIN funds f ON c.fund_id = f.id "
        "ORDER BY c.id DESC LIMIT 50";
    m_historyModel->setQuery(queryStr);
    m_historyTable->hideColumn(0);
}

void ContributionsWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        onSaveContribution();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ContributionsWidget::onPaymentMethodChanged(int index) {
    bool isCheck = (m_paymentCombo->itemText(index) == "Check");
    m_checkNumberEdit->setEnabled(isCheck);
    if (!isCheck) {
        m_checkNumberEdit->clear();
    }
}

void ContributionsWidget::onSaveContribution() {
    if (m_amountSpin->value() <= 0.00) {
        QMessageBox::warning(this, "Validation Error", "Please enter a positive contribution amount.");
        return;
    }

    int personId = m_personCombo->currentData().toInt();
    if (personId == 0) {
        // Find text match if typed
        QString typedText = m_personCombo->currentText();
        for (const auto& person : m_peopleList) {
            if (person.second.compare(typedText, Qt::CaseInsensitive) == 0) {
                personId = person.first;
                break;
            }
        }
    }

    if (personId == 0) {
        QMessageBox::warning(this, "Validation Error", "Invalid person selected. Please select a name from the list.");
        return;
    }

    int fundId = m_fundCombo->currentData().toInt();
    int amountCents = static_cast<int>(m_amountSpin->value() * 100.0 + 0.5);

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    QSqlQuery query;
    query.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) "
                  "VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(personId);
    query.addBindValue(fundId);
    query.addBindValue(amountCents);
    query.addBindValue(m_dateEdit->date().toString("yyyy-MM-dd"));
    query.addBindValue(m_paymentCombo->currentText());
    query.addBindValue(m_checkNumberEdit->text().trimmed().isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : m_checkNumberEdit->text().trimmed());

    if (query.exec()) {
        db.commit();
        DatabaseManager::logMemberHistory(personId, "Contribution", QString("Contributed $%1 to Fund ID %2").arg(QString::number(amountCents / 100.0, 'f', 2), QString::number(fundId)));
        DatabaseManager::logAudit("contributions", query.lastInsertId().toInt(), "amount", "", QString::number(amountCents), "finance");
        refreshHistoryModel();
        
        // Rapid batch UX: clear amount & person, refocus person
        m_amountSpin->setValue(0.00);
        m_personCombo->setCurrentIndex(-1);
        m_personCombo->lineEdit()->clear();
        m_checkNumberEdit->clear();
        m_personCombo->setFocus();
    } else {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to save contribution: " + query.lastError().text());
    }
}

void ContributionsWidget::onExportTaxStatements() {
    QSqlQuery query;
    QList<int> contributors;

    // Get unique contributors with giving in the current year
    QString yearStr = QDate::currentDate().toString("yyyy");
    query.prepare("SELECT DISTINCT person_id FROM contributions WHERE contribution_date LIKE ?");
    query.addBindValue(yearStr + "%");
    if (query.exec()) {
        while (query.next()) {
            contributors.append(query.value(0).toInt());
        }
    }

    if (contributors.isEmpty()) {
        QMessageBox::information(this, "No Data", "No contributions found for the tax year " + yearStr);
        return;
    }

    // Set up export directory
    QString exportDir = QDir::homePath() + "/Documents/OpenEkklesia_Tax_Statements/";
    QDir().mkpath(exportDir);

    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, contributors.size());
    m_progressBar->setValue(0);

    int count = 0;
    for (int i = 0; i < contributors.size(); ++i) {
        int personId = contributors[i];
        
        // Fetch person details
        QSqlQuery pQuery;
        pQuery.prepare("SELECT first_name, last_name, email FROM people WHERE id = ?");
        pQuery.addBindValue(personId);
        if (!pQuery.exec() || !pQuery.next()) continue;

        QString firstName = pQuery.value(0).toString();
        QString lastName = pQuery.value(1).toString();
        QString fullName = firstName + " " + lastName;

        // Fetch household details
        QSqlQuery hQuery;
        hQuery.prepare("SELECT h.family_name, h.address_line1, h.city, h.zip_code "
                       "FROM households h JOIN people p ON p.household_id = h.id "
                       "WHERE p.id = ?");
        hQuery.addBindValue(personId);
        QString familyName = "The " + lastName + " Household";
        QString address = "No Address Recorded";
        if (hQuery.exec() && hQuery.next()) {
            familyName = hQuery.value(0).toString();
            address = QString("%1<br>%2, %3").arg(hQuery.value(1).toString(), hQuery.value(2).toString(), hQuery.value(3).toString());
        }

        // Fetch history of giving
        QSqlQuery gQuery;
        gQuery.prepare("SELECT c.contribution_date, f.fund_name, c.payment_method, c.check_number, c.amount "
                       "FROM contributions c JOIN funds f ON c.fund_id = f.id "
                       "WHERE c.person_id = ? AND c.contribution_date LIKE ? "
                       "ORDER BY c.contribution_date ASC");
        gQuery.addBindValue(personId);
        gQuery.addBindValue(yearStr + "%");
        
        if (!gQuery.exec()) continue;

        QString itemsHtml = "";
        double totalSum = 0.0;
        while (gQuery.next()) {
            QString date = gQuery.value(0).toString();
            QString fund = gQuery.value(1).toString();
            QString method = gQuery.value(2).toString();
            QString checkNo = gQuery.value(3).toString();
            if (checkNo.isEmpty()) checkNo = "-";
            double amount = gQuery.value(4).toInt() / 100.0;
            totalSum += amount;

            itemsHtml += QString(
                "<tr>"
                "  <td style='padding: 6px; border-bottom: 1px solid #ddd;'>%1</td>"
                "  <td style='padding: 6px; border-bottom: 1px solid #ddd;'>%2</td>"
                "  <td style='padding: 6px; border-bottom: 1px solid #ddd;'>%3</td>"
                "  <td style='padding: 6px; border-bottom: 1px solid #ddd;'>%4</td>"
                "  <td style='padding: 6px; border-bottom: 1px solid #ddd; text-align: right;'>$%5</td>"
                "</tr>"
            ).arg(date, fund, method, checkNo, QString::number(amount, 'f', 2));
        }

        // Create HTML content
        QString htmlContent = QString(
            "<html>"
            "<body>"
            "  <div style='font-family: sans-serif; padding: 20px;'>"
            "    <h1 style='color: #444;'>OpenEkklesia Church</h1>"
            "    <p style='color: #666;'>123 Church Way, Suite 100 | Metropolis, NY 12345</p>"
            "    <hr style='border: 0; border-top: 2px solid #555;'>"
            "    <br><br>"
            "    <table style='width: 100%;'>"
            "      <tr>"
            "        <td style='width: 50%%; vertical-align: top;'>"
            "          <strong>Donor:</strong><br>"
            "          %1<br>"
            "          %2"
            "        </td>"
            "        <td style='width: 50%%; vertical-align: top; text-align: right;'>"
            "          <strong>Statement Year:</strong> %3<br>"
            "          <strong>Date Printed:</strong> %4"
            "        </td>"
            "      </tr>"
            "    </table>"
            "    <br><br>"
            "    <h3 style='color: #444;'>Giving Summary Statement</h3>"
            "    <table style='width: 100%%; border-collapse: collapse;'>"
            "      <thead>"
            "        <tr style='background-color: #eee; text-align: left;'>"
            "          <th style='padding: 8px; border-bottom: 2px solid #555;'>Date</th>"
            "          <th style='padding: 8px; border-bottom: 2px solid #555;'>Fund</th>"
            "          <th style='padding: 8px; border-bottom: 2px solid #555;'>Method</th>"
            "          <th style='padding: 8px; border-bottom: 2px solid #555;'>Ref #</th>"
            "          <th style='padding: 8px; border-bottom: 2px solid #555; text-align: right;'>Amount</th>"
            "        </tr>"
            "      </thead>"
            "      <tbody>"
            "        %5"
            "      </tbody>"
            "    </table>"
            "    <br>"
            "    <div style='text-align: right; font-size: 16px; font-weight: bold;'>"
            "      Total Tax-Deductible Contribution: $%6"
            "    </div>"
            "    <br><br><br>"
            "    <p style='font-size: 12px; color: #555; text-align: center;'>"
            "      Thank you for your generous support of OpenEkklesia. No goods or services were provided in exchange for this contribution."
            "    </p>"
            "  </div>"
            "</body>"
            "</html>"
        ).arg(fullName, address, yearStr, QDate::currentDate().toString("yyyy-MM-dd"), itemsHtml, QString::number(totalSum, 'f', 2));

        // Output to PDF
        QString pdfPath = QString("%1Tax_Statement_%2_%3.pdf").arg(exportDir, yearStr, lastName + "_" + firstName);
        QPdfWriter pdfWriter(pdfPath);
        pdfWriter.setPageSize(QPageSize(QPageSize::Letter));
        
        QTextDocument doc;
        doc.setHtml(htmlContent);
        doc.print(&pdfWriter);

        count++;
        m_progressBar->setValue(count);
        QCoreApplication::processEvents(); // Keep UI updated
    }

    m_progressBar->setVisible(false);
    QMessageBox::information(this, "Export Success", 
                            QString("Successfully exported %1 tax statement PDFs to: %2").arg(QString::number(count), exportDir));
}

void ContributionsWidget::onHistoryContextMenu(const QPoint& pos) {
    if (m_userRole == "readonly" || m_userRole == "pastor") return;

    QModelIndex index = m_historyTable->indexAt(pos);
    if (!index.isValid()) return;

    int row = index.row();
    int contributionId = m_historyModel->index(row, 0).data().toInt();
    QString date = m_historyModel->index(row, 1).data().toString();
    QString name = m_historyModel->index(row, 2).data().toString();
    QString fundName = m_historyModel->index(row, 3).data().toString();
    QString amountStr = m_historyModel->index(row, 4).data().toString();
    QString method = m_historyModel->index(row, 5).data().toString();

    QMenu menu(this);
    QAction* actEdit = menu.addAction("✏️ Edit Amount");
    QAction* actDelete = menu.addAction("❌ Delete Giving Record");

    QAction* selected = menu.exec(m_historyTable->viewport()->mapToGlobal(pos));
    if (!selected) return;

    QSqlDatabase db = DatabaseManager::instance().database();

    if (selected == actEdit) {
        bool ok;
        double currentAmount = amountStr.replace("$", "").toDouble();
        double newAmount = QInputDialog::getDouble(this, "Edit Contribution", 
                                                    QString("Enter new amount for %1's contribution to %2:").arg(name, fundName), 
                                                    currentAmount, 0.01, 999999.99, 2, &ok);
        if (ok) {
            int newAmountCents = qRound(newAmount * 100);
            int currentAmountCents = qRound(currentAmount * 100);
            QSqlQuery query(db);
            query.prepare("UPDATE contributions SET amount = ? WHERE id = ?");
            query.addBindValue(newAmountCents);
            query.addBindValue(contributionId);
            if (query.exec()) {
                DatabaseManager::logAudit("contributions", contributionId, "amount", QString::number(currentAmountCents), QString::number(newAmountCents), "finance");
                refreshHistoryModel();
                QMessageBox::information(this, "Saved", "Contribution updated successfully.");
            } else {
                QMessageBox::warning(this, "Error", "Failed to update record: " + query.lastError().text());
            }
        }
    } else if (selected == actDelete) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Delete", 
                                                                  QString("Are you sure you want to delete this contribution of %1 from %2?").arg(amountStr, name),
                                                                  QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QSqlQuery query(db);
            query.prepare("DELETE FROM contributions WHERE id = ?");
            query.addBindValue(contributionId);
            if (query.exec()) {
                DatabaseManager::logAudit("contributions", contributionId, "amount", amountStr, "DELETED", "finance");
                refreshHistoryModel();
                QMessageBox::information(this, "Deleted", "Contribution deleted successfully.");
            } else {
                QMessageBox::warning(this, "Error", "Failed to delete record: " + query.lastError().text());
            }
        }
    }
}

void ContributionsWidget::applyRole(const QString& role) {
    m_userRole = role;
    if (role == "readonly" || role == "pastor") {
        m_btnSave->setEnabled(false);
        m_btnExportPDF->setEnabled(false);
        m_dateEdit->setEnabled(false);
        m_personCombo->setEnabled(false);
        m_amountSpin->setEnabled(false);
        m_fundCombo->setEnabled(false);
        m_paymentCombo->setEnabled(false);
        m_checkNumberEdit->setEnabled(false);
    }
}
