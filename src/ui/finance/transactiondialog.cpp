#include "transactiondialog.h"
#include "../database/databasemanager.h"
#include "../common/appcurrency.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDate>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDebug>

TransactionDialog::TransactionDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    loadDropdownData();
    
    // Add two default split rows to get started
    onAddSplitRow();
    onAddSplitRow();
}

TransactionDialog::~TransactionDialog() {}

void TransactionDialog::setupUI() {
    resize(650, 450);
    setWindowTitle("New Double-Entry Transaction");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Header info (Date & Description)
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(15);

    QVBoxLayout* dateLayout = new QVBoxLayout();
    QLabel* lblDate = new QLabel("Date:", this);
    m_dateEdit = new QDateEdit(QDate::currentDate(), this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");
    dateLayout->addWidget(lblDate);
    dateLayout->addWidget(m_dateEdit);
    headerLayout->addLayout(dateLayout);

    QVBoxLayout* descLayout = new QVBoxLayout();
    QLabel* lblDesc = new QLabel("Description / Memo:", this);
    m_descEdit = new QLineEdit(this);
    m_descEdit->setPlaceholderText("e.g. Weekly Offering, Utility Bill, etc.");
    descLayout->addWidget(lblDesc);
    descLayout->addWidget(m_descEdit);
    headerLayout->addLayout(descLayout, 1);

    mainLayout->addLayout(headerLayout);

    // Dynamic Splits section (scrollable)
    QLabel* lblSplits = new QLabel("Transaction Splits (Balanced Debits & Credits):", this);
    lblSplits->setStyleSheet("font-weight: bold; margin-top: 10px;");
    mainLayout->addWidget(lblSplits);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: 1px solid #313244; border-radius: 6px; background-color: #1e1e2e; }");
    
    QWidget* scrollContent = new QWidget(scrollArea);
    m_splitsLayout = new QVBoxLayout(scrollContent);
    m_splitsLayout->setContentsMargins(10, 10, 10, 10);
    m_splitsLayout->setSpacing(8);
    m_splitsLayout->addStretch();
    
    scrollContent->setLayout(m_splitsLayout);
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // Buttons & Verification Bar
    QHBoxLayout* footerLayout = new QHBoxLayout();
    
    QPushButton* btnAddSplit = new QPushButton(" Add Split Row", this);
    connect(btnAddSplit, &QPushButton::clicked, this, &TransactionDialog::onAddSplitRow);
    footerLayout->addWidget(btnAddSplit);

    footerLayout->addStretch();

    QLabel* lblBalText = new QLabel("Total Balance:", this);
    footerLayout->addWidget(lblBalText);

    m_lblBalance = new QLabel(appCurrencySymbol() + "0.00", this);
    m_lblBalance->setStyleSheet("font-weight: bold; font-size: 14px;");
    footerLayout->addWidget(m_lblBalance);

    mainLayout->addLayout(footerLayout);

    // Dialog Actions
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    QPushButton* btnCancel = new QPushButton("Cancel", this);
    m_btnSave = new QPushButton("Save Transaction", this);
    m_btnSave->setDefault(true);
    
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnSave, &QPushButton::clicked, this, &TransactionDialog::onSaveClicked);

    actionsLayout->addStretch();
    actionsLayout->addWidget(btnCancel);
    actionsLayout->addWidget(m_btnSave);

    mainLayout->addLayout(actionsLayout);

    // Stylesheet adaptation for the dark theme
    QString style = 
        "QDialog { background-color: #181825; color: #cdd6f4; }"
        "QLabel { color: #cdd6f4; }"
        "QLineEdit, QDateEdit, QDoubleSpinBox, QComboBox { background-color: #1e1e2e; border: 1px solid #313244; border-radius: 6px; color: #cdd6f4; padding: 6px; }"
        "QPushButton { background-color: #313244; border: 1px solid #45475a; border-radius: 6px; color: #cdd6f4; padding: 8px 15px; }"
        "QPushButton:hover { background-color: #45475a; }"
        "QPushButton#m_btnSave { background-color: #89b4fa; color: #11111b; font-weight: bold; }"
        "QPushButton:disabled { background-color: #11111b; color: #585b70; border: 1px solid #11111b; }";
    setStyleSheet(style);
}

void TransactionDialog::loadDropdownData() {
    QSqlQuery query;
    
    // Load accounts
    if (query.exec("SELECT id, name, type FROM accounts ORDER BY type, name")) {
        while (query.next()) {
            int id = query.value(0).toInt();
            QString name = query.value(1).toString();
            QString type = query.value(2).toString();
            m_accountsList.append({id, QString("%1 (%2)").arg(name, type)});
        }
    }

    // Load members
    if (query.exec("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name")) {
        while (query.next()) {
            int id = query.value(0).toInt();
            QString name = query.value(1).toString();
            m_membersList.append({id, name});
        }
    }
}

void TransactionDialog::onAddSplitRow() {
    // Remove the stretch at the bottom temporarily
    m_splitsLayout->takeAt(m_splitsLayout->count() - 1);

    SplitRow row;
    row.container = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Account selector
    row.accountCombo = new QComboBox(row.container);
    for (const auto& acc : m_accountsList) {
        row.accountCombo->addItem(acc.second, acc.first);
    }
    layout->addWidget(row.accountCombo, 2);

    // Amount box
    row.amountSpin = new QDoubleSpinBox(row.container);
    row.amountSpin->setRange(-999999.00, 999999.00);
    row.amountSpin->setDecimals(2);
    row.amountSpin->setSingleStep(10.00);
    connect(row.amountSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TransactionDialog::onValuesChanged);
    layout->addWidget(row.amountSpin, 1);

    // Optional Member / Donor selector
    row.memberCombo = new QComboBox(row.container);
    row.memberCombo->addItem("- Optional Donor -", QVariant());
    for (const auto& mem : m_membersList) {
        row.memberCombo->addItem(mem.second, mem.first);
    }
    layout->addWidget(row.memberCombo, 2);

    // Remove row button
    row.btnRemove = new QPushButton("✕", row.container);
    row.btnRemove->setObjectName("ghostDangerBtn");
    row.btnRemove->setFixedWidth(30);
    row.btnRemove->setToolTip("Remove this split row");
    connect(row.btnRemove, &QPushButton::clicked, this, &TransactionDialog::onRemoveSplitRow);
    layout->addWidget(row.btnRemove);

    m_splitsLayout->addWidget(row.container);
    m_splitRows.append(row);

    // Re-add stretch at bottom
    m_splitsLayout->addStretch();

    onValuesChanged();
}

void TransactionDialog::onRemoveSplitRow() {
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    for (int i = 0; i < m_splitRows.size(); ++i) {
        if (m_splitRows[i].btnRemove == button) {
            QWidget* container = m_splitRows[i].container;
            m_splitsLayout->removeWidget(container);
            container->deleteLater();
            m_splitRows.removeAt(i);
            break;
        }
    }

    onValuesChanged();
}

void TransactionDialog::onValuesChanged() {
    updateBalanceVerification();
}

void TransactionDialog::updateBalanceVerification() {
    double total = 0.0;
    for (const auto& row : m_splitRows) {
        total += row.amountSpin->value();
    }

    m_lblBalance->setText(QString("%1%2").arg(appCurrencySymbol(), QString::number(total, 'f', 2)));

    // Double-entry matches to zero
    bool balanced = qAbs(total) < 0.001 && m_splitRows.size() >= 2;
    if (balanced) {
        m_lblBalance->setStyleSheet("color: #a6e3a1; font-weight: bold; font-size: 14px;"); // Green
        m_btnSave->setEnabled(true);
    } else {
        m_lblBalance->setStyleSheet("color: #f38ba8; font-weight: bold; font-size: 14px;"); // Red
        m_btnSave->setEnabled(false);
    }
}

void TransactionDialog::onSaveClicked() {
    if (m_descEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Please enter a transaction description.");
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    db.transaction();

    QSqlQuery query;
    query.prepare("INSERT INTO transactions (date, description) VALUES (?, ?)");
    query.addBindValue(m_dateEdit->date().toString("yyyy-MM-dd"));
    query.addBindValue(m_descEdit->text().trimmed());

    if (!query.exec()) {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to save transaction: " + query.lastError().text());
        return;
    }

    int transactionId = query.lastInsertId().toInt();

    query.prepare("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (?, ?, ?, ?)");
    for (const auto& row : m_splitRows) {
        int accountId = row.accountCombo->currentData().toInt();
        double amount = row.amountSpin->value();
        QVariant memberId = row.memberCombo->currentData();

        query.addBindValue(transactionId);
        query.addBindValue(accountId);
        query.addBindValue(amount);
        query.addBindValue(memberId);

        if (!query.exec()) {
            db.rollback();
            QMessageBox::critical(this, "Database Error", "Failed to save transaction splits: " + query.lastError().text());
            return;
        }
    }

    db.commit();
    accept();
}

void TransactionDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        if (m_btnSave->isEnabled()) {
            onSaveClicked();
            event->accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}
