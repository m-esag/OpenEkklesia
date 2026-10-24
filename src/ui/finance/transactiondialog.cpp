#include "transactiondialog.h"
#include "../database/databasemanager.h"
#include "../common/appcurrency.h"
#include "../core/iconmanager.h"
#include "../core/thememanager.h"
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
    scrollArea->setStyleSheet("QScrollArea { border: 1px solid palette(mid); border-radius: 6px; background-color: palette(base); }");
    
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
    btnCancel->setObjectName("secondaryBtn");
    m_btnSave = new QPushButton("Save Transaction", this);
    m_btnSave->setObjectName("primaryBtn");
    m_btnSave->setDefault(true);
    
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnSave, &QPushButton::clicked, this, &TransactionDialog::onSaveClicked);

    actionsLayout->addStretch();
    actionsLayout->addWidget(btnCancel);
    actionsLayout->addWidget(m_btnSave);

    mainLayout->addLayout(actionsLayout);
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
    row.btnRemove = new QPushButton(row.container);
    row.btnRemove->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
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
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    bool balanced = qAbs(total) < 0.001 && m_splitRows.size() >= 2;
    const QColor color = balanced ? tok.accentSuccess : tok.accentDanger;
    m_lblBalance->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 14px;").arg(color.name()));
    m_btnSave->setEnabled(balanced);
}

bool TransactionDialog::saveTransaction(QSqlDatabase& db,
                                        const QDate& date,
                                        const QString& description,
                                        const QList<LedgerSplit>& splits,
                                        QString* error) {
    const QString desc = description.trimmed();
    if (desc.isEmpty()) {
        if (error) *error = "Please enter a transaction description.";
        return false;
    }
    if (splits.size() < 2) {
        if (error) *error = "A double-entry transaction needs at least two splits.";
        return false;
    }

    double total = 0.0;
    for (const LedgerSplit& split : splits) {
        if (split.accountId <= 0) {
            if (error) *error = "Every split must reference an account.";
            return false;
        }
        total += split.amount;
    }
    if (qAbs(total) > 0.005) {
        if (error) *error = QString("Transaction is not balanced: the splits total %1 instead of 0.")
                                .arg(QString::number(total, 'f', 2));
        return false;
    }

    // Referential pre-check: fail with a clear message instead of relying on
    // the (optional) SQLite foreign-key pragma to reject the INSERTs.
    for (const LedgerSplit& split : splits) {
        QSqlQuery accountCheck(db);
        accountCheck.prepare("SELECT 1 FROM accounts WHERE id = ?");
        accountCheck.addBindValue(split.accountId);
        if (!accountCheck.exec() || !accountCheck.next()) {
            if (error) *error = QString("Account id %1 does not exist.").arg(split.accountId);
            return false;
        }
    }

    if (!db.transaction()) {
        if (error) *error = QString("Could not start the database transaction: %1").arg(db.lastError().text());
        return false;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO transactions (date, description) VALUES (?, ?)");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.addBindValue(desc);

    if (!query.exec()) {
        const QString message = QString("Failed to save transaction: %1").arg(query.lastError().text());
        db.rollback();
        if (error) *error = message;
        return false;
    }

    const int transactionId = query.lastInsertId().toInt();

    // A fresh QSqlQuery per split keeps positional binding unambiguous (the
    // bind count is not carried over from the transactions INSERT).
    for (const LedgerSplit& split : splits) {
        QSqlQuery splitQuery(db);
        splitQuery.prepare("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (?, ?, ?, ?)");
        splitQuery.addBindValue(transactionId);
        splitQuery.addBindValue(split.accountId);
        splitQuery.addBindValue(split.amount);
        splitQuery.addBindValue(split.personId > 0 ? QVariant(split.personId) : QVariant());

        if (!splitQuery.exec()) {
            const QString message = QString("Failed to save transaction splits: %1").arg(splitQuery.lastError().text());
            db.rollback();
            if (error) *error = message;
            return false;
        }
    }

    if (!db.commit()) {
        const QString message = QString("Failed to commit the transaction: %1").arg(db.lastError().text());
        db.rollback();
        if (error) *error = message;
        return false;
    }

    if (error) error->clear();
    return true;
}

void TransactionDialog::onSaveClicked() {
    // Enter is bound to save, and a default button can also fire while the
    // first insert is still running; ignore re-entrant saves. The flag is also
    // left set after a successful commit (see below), so even a queued second
    // Enter cannot insert the same entry twice.
    if (m_saving) return;
    m_saving = true;

    QList<LedgerSplit> splits;
    splits.reserve(m_splitRows.size());
    for (const auto& row : m_splitRows) {
        LedgerSplit split;
        split.accountId = row.accountCombo->currentData().toInt();
        split.amount = row.amountSpin->value();
        const QVariant memberId = row.memberCombo->currentData();
        split.personId = memberId.isValid() ? memberId.toInt() : 0;
        splits.append(split);
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QString error;
    const bool saved = saveTransaction(db, m_dateEdit->date(), m_descEdit->text(), splits, &error);

    if (!saved) {
        // Re-arm so the user can correct the entry and retry.
        m_saving = false;
        QMessageBox::warning(this, "Transaction Not Saved", error);
        return;
    }

    // m_saving deliberately stays true: the entry is committed and the dialog
    // is about to close, so no further save may run.
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
