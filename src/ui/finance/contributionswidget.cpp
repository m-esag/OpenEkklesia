#include "contributionswidget.h"
#include "../database/databasemanager.h"
#include <QSettings>
#include "../common/appcurrency.h"
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
#include <QCoreApplication>
#include <QDebug>
#include <QInputDialog>
#include <QMenu>
#include <QListWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QTextStream>
#include <QColor>
#include <QFileDialog>

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
    // Most-recent Sunday; on Sunday itself that is today (dayOfWeek 7 -> 0).
    int daysToSubtract = today.dayOfWeek() % 7;
    QDate prevSunday = today.addDays(-daysToSubtract);
    m_dateEdit = new QDateEdit(prevSunday, this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");

    m_personCombo = new QComboBox(this);
    m_personCombo->setEditable(true);

    m_amountSpin = new QDoubleSpinBox(this);
    m_amountSpin->setRange(0.00, 999999.00);
    m_amountSpin->setDecimals(2);
    m_amountSpin->setPrefix(appCurrencySymbol() + " ");

    m_fundCombo = new QComboBox(this);

    // The entry form is deliberately minimal for Sunday-morning speed:
    // Date / Person / Amount / Fund / optional Note. Payment method and
    // check number are no longer captured here — the Planning Center import
    // still records them for online/check gifts, and bank reconciliation
    // lives in GnuCash.
    m_noteEdit = new QLineEdit(this);
    m_noteEdit->setPlaceholderText("Optional note (e.g. 'in memory of...')");

    form->addRow("Date:", m_dateEdit);
    form->addRow("Person:", m_personCombo);
    form->addRow("Amount:", m_amountSpin);
    form->addRow("Fund:", m_fundCombo);
    form->addRow("Note:", m_noteEdit);

    formLayout->addLayout(form);

    m_btnSave = new QPushButton("Save Entry (Enter)", this);
    m_btnSave->setObjectName("saveBtn");
    connect(m_btnSave, &QPushButton::clicked, this, &ContributionsWidget::onSaveContribution);
    formLayout->addWidget(m_btnSave);

    formLayout->addSpacing(15);

    // Fund administration — funds are the organizing concept for giving, so
    // they can be added/renamed/deactivated without leaving this page.
    QLabel* fundsLabel = new QLabel("Funds & Accounting Export", this);
    fundsLabel->setObjectName("detailSubHeader");
    formLayout->addWidget(fundsLabel);

    m_btnManageFunds = new QPushButton("Manage Funds", this);
    m_btnManageFunds->setObjectName("secondaryBtn");
    connect(m_btnManageFunds, &QPushButton::clicked, this, &ContributionsWidget::onManageFunds);
    formLayout->addWidget(m_btnManageFunds);

    m_btnGnuCashExport = new QPushButton("Export for GnuCash (CSV)", this);
    m_btnGnuCashExport->setObjectName("accentBtnCyan");
    m_btnGnuCashExport->setToolTip("Export recorded giving for a date range as CSV, ready to import into GnuCash as income transactions.");
    connect(m_btnGnuCashExport, &QPushButton::clicked, this, &ContributionsWidget::onExportGnuCashCsv);
    formLayout->addWidget(m_btnGnuCashExport);

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

    // Anonymous plate offerings: item at kAnonymousIndex, bound to no person.
    m_personCombo->insertItem(kAnonymousIndex, "— Anonymous (plate offering) —");

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

void ContributionsWidget::refreshContributions() {
    m_personCombo->blockSignals(true);
    m_personCombo->clear();
    m_peopleList.clear();

    QSqlQuery q;
    if (q.exec("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name")) {
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            m_peopleList.append({id, name});
            m_personCombo->addItem(name, id);
        }
    }
    m_personCombo->blockSignals(false);
    m_personCombo->insertItem(kAnonymousIndex, "— Anonymous (plate offering) —");

    m_fundCombo->blockSignals(true);
    m_fundCombo->clear();
    m_fundsList.clear();
    if (q.exec("SELECT id, fund_name FROM funds WHERE is_active = 1 ORDER BY fund_name")) {
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            m_fundsList.append({id, name});
            m_fundCombo->addItem(name, id);
        }
    }
    m_fundCombo->blockSignals(false);

    refreshHistoryModel();
}

void ContributionsWidget::refreshHistoryModel() {
    // person_id is nullable (anonymous plate offerings); payment_method is
    // only written by the Planning Center import these days — coalesce both
    // so rows never render blank.
    QString queryStr = QString(
        "SELECT c.id AS \"ID\", "
        "       c.contribution_date AS \"Date\", "
        "       COALESCE(p.first_name || ' ' || p.last_name, '— Anonymous —') AS \"Contributor\", "
        "       f.fund_name AS \"Fund\", "
        "       printf('%1%.2f', c.amount / 100.0) AS \"Amount\", "
        "       COALESCE(NULLIF(c.payment_method, ''), '—') AS \"Method\", "
        "       COALESCE(c.note, '') AS \"Note\" "
        "FROM contributions c "
        "LEFT JOIN people p ON c.person_id = p.id "
        "JOIN funds f ON c.fund_id = f.id "
        "ORDER BY c.id DESC LIMIT 50").arg(appCurrencySymbol());
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

void ContributionsWidget::onSaveContribution() {
    if (m_amountSpin->value() <= 0.00) {
        QMessageBox::warning(this, "Validation Error", "Please enter a positive contribution amount.");
        return;
    }

    // Anonymous selection is a deliberate choice (plate offering with no
    // envelope/name); anything else that fails to resolve is a typo.
    const bool anonymous = (m_personCombo->currentIndex() == kAnonymousIndex);
    int personId = 0;
    if (!anonymous) {
        personId = m_personCombo->currentData().toInt();
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
            QMessageBox::warning(this, "Validation Error",
                                 "Invalid person selected. Pick a name from the list, or choose '— Anonymous —'.");
            return;
        }
    }

    int fundId = m_fundCombo->currentData().toInt();
    int amountCents = static_cast<int>(m_amountSpin->value() * 100.0 + 0.5);
    QString note = m_noteEdit->text().trimmed();

    // First-gift milestone: detect before inserting so a person's earliest
    // recorded gift is celebrated in their pastoral timeline.
    bool firstGift = false;
    if (!anonymous) {
        QSqlQuery cnt;
        cnt.prepare("SELECT COUNT(*) FROM contributions WHERE person_id = ?");
        cnt.addBindValue(personId);
        if (cnt.exec() && cnt.next()) firstGift = (cnt.value(0).toInt() == 0);
    }

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    QSqlQuery query;
    query.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, note) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(anonymous ? QVariant(QMetaType(QMetaType::Int)) : QVariant(personId));
    query.addBindValue(fundId);
    query.addBindValue(amountCents);
    query.addBindValue(m_dateEdit->date().toString("yyyy-MM-dd"));
    query.addBindValue(note.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(note));

    if (query.exec()) {
        db.commit();
        QString fundName;
        for (const auto& f : m_fundsList) {
            if (f.first == fundId) { fundName = f.second; break; }
        }
        if (fundName.isEmpty()) fundName = QString("Fund #%1").arg(fundId);
        const QString amountStr = QString("%1%2").arg(appCurrencySymbol(),
                                                      QString::number(amountCents / 100.0, 'f', 2));
        if (!anonymous) {
            if (firstGift) {
                DatabaseManager::logMemberHistory(personId, "First Gift",
                    QString("First recorded contribution: %1 to %2.").arg(amountStr, fundName));
            }
            DatabaseManager::logMemberHistory(personId, "Contribution",
                QString("Contributed %1 to %2").arg(amountStr, fundName));
        }
        DatabaseManager::logAudit("contributions", query.lastInsertId().toInt(), "amount", "", QString::number(amountCents), "finance");
        refreshHistoryModel();

        // Rapid batch UX: clear amount/note & person, refocus person
        m_amountSpin->setValue(0.00);
        m_noteEdit->clear();
        m_personCombo->setCurrentIndex(-1);
        m_personCombo->lineEdit()->clear();
        m_personCombo->setFocus();
    } else {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to save contribution: " + query.lastError().text());
    }
}

void ContributionsWidget::onHistoryContextMenu(const QPoint& pos) {
    if (m_userRole == "readonly") return;

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
        double currentAmount = amountStr.replace(appCurrencySymbol(), "").toDouble();
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

void ContributionsWidget::refreshFundCombo() {
    m_fundCombo->blockSignals(true);
    m_fundCombo->clear();
    m_fundsList.clear();
    QSqlQuery q;
    if (q.exec("SELECT id, fund_name FROM funds WHERE is_active = 1 ORDER BY fund_name")) {
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            m_fundsList.append({id, name});
            m_fundCombo->addItem(name, id);
        }
    }
    m_fundCombo->blockSignals(false);
}

void ContributionsWidget::onManageFunds() {
    QDialog dlg(this);
    dlg.setWindowTitle("Manage Giving Funds");
    dlg.resize(420, 360);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);

    QListWidget* list = new QListWidget(&dlg);
    lay->addWidget(list, 1);

    auto refreshList = [list]() {
        list->clear();
        QSqlQuery q;
        q.exec("SELECT id, fund_name, is_active FROM funds ORDER BY fund_name");
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            bool active = q.value(2).toBool();
            QListWidgetItem* item = new QListWidgetItem(
                active ? name : QString("%1 (inactive)").arg(name), list);
            item->setData(Qt::UserRole, id);
            if (!active) item->setForeground(QColor("#a6adc8"));
        }
    };
    refreshList();

    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QPushButton* btnAdd = btns->addButton("➕ Add Fund", QDialogButtonBox::ActionRole);
    QPushButton* btnRename = btns->addButton("✏️ Rename", QDialogButtonBox::ActionRole);
    QPushButton* btnToggle = btns->addButton("Deactivate / Reactivate", QDialogButtonBox::ActionRole);
    lay->addWidget(btns);

    // Funds are referenced by existing contributions, so they are never
    // hard-deleted — deactivation removes them from the entry form while
    // preserving historical giving and tax statements.
    connect(btnAdd, &QPushButton::clicked, this, [this, &refreshList]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Fund", "Fund name:", QLineEdit::Normal, "", &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QSqlQuery q;
        q.prepare("INSERT INTO funds (fund_name, is_active) VALUES (?, 1)");
        q.addBindValue(name);
        if (q.exec()) {
            refreshList();
            refreshFundCombo();
        } else {
            QMessageBox::warning(this, "Error", "Could not add fund (duplicate name?): " + q.lastError().text());
        }
    });

    connect(btnRename, &QPushButton::clicked, this, [this, list, &refreshList]() {
        QListWidgetItem* sel = list->currentItem();
        if (!sel) return;
        int id = sel->data(Qt::UserRole).toInt();
        bool ok;
        QString name = QInputDialog::getText(this, "Rename Fund", "New name:", QLineEdit::Normal,
                                             sel->text().replace(" (inactive)", ""), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QSqlQuery q;
        q.prepare("UPDATE funds SET fund_name = ? WHERE id = ?");
        q.addBindValue(name);
        q.addBindValue(id);
        if (q.exec()) {
            refreshList();
            refreshFundCombo();
            refreshHistoryModel();
        }
    });

    connect(btnToggle, &QPushButton::clicked, this, [this, list, &refreshList]() {
        QListWidgetItem* sel = list->currentItem();
        if (!sel) return;
        int id = sel->data(Qt::UserRole).toInt();
        QSqlQuery q;
        q.prepare("UPDATE funds SET is_active = 1 - is_active WHERE id = ?");
        q.addBindValue(id);
        if (q.exec()) {
            refreshList();
            refreshFundCombo();
        }
    });

    dlg.exec();
}

void ContributionsWidget::onExportGnuCashCsv() {
    QDialog dlg(this);
    dlg.setWindowTitle("Export Giving for GnuCash");
    dlg.resize(360, 190);

    QFormLayout* form = new QFormLayout(&dlg);
    QDateEdit* from = new QDateEdit(QDate::currentDate().addMonths(-1), &dlg);
    QDateEdit* to = new QDateEdit(QDate::currentDate(), &dlg);
    for (auto* d : {from, to}) {
        d->setCalendarPopup(true);
        d->setDisplayFormat("yyyy-MM-dd");
    }
    QCheckBox* chkSummary = new QCheckBox("One row per fund (summary) instead of per-gift", &dlg);
    chkSummary->setChecked(false);
    form->addRow("From:", from);
    form->addRow("To:", to);
    form->addRow(chkSummary);

    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);
    if (dlg.exec() != QDialog::Accepted) return;

    QString suggested = QString("OpenEkklesia_Giving_%1_%2.csv")
                            .arg(from->date().toString("yyyyMMdd"), to->date().toString("yyyyMMdd"));
    QString fileName = QFileDialog::getSaveFileName(this, "Export GnuCash CSV",
                       QDir::homePath() + "/" + suggested, "CSV Files (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not open file for writing: " + file.errorString());
        return;
    }

    // Proper CSV quoting: wrap fields containing commas/quotes/newlines and
    // double embedded quotes (RFC 4180), since fund and donor names are free
    // text. GnuCash's CSV import maps these columns to a transaction.
    auto csv = [](const QString& s) {
        QString v = s;
        if (v.contains(',') || v.contains('"') || v.contains('\n'))
            v = '"' + v.replace('"', "\"\"") + '"';
        return v;
    };

    QTextStream out(&file);
    int rowCount = 0;
    double totalCents = 0.0;

    if (chkSummary->isChecked()) {
        out << "Date,Description,Deposit\n";
        QSqlQuery q;
        q.prepare("SELECT f.fund_name, SUM(c.amount) FROM contributions c "
                  "JOIN funds f ON c.fund_id = f.id "
                  "WHERE c.contribution_date >= ? AND c.contribution_date <= ? "
                  "GROUP BY f.id ORDER BY f.fund_name");
        q.addBindValue(from->date().toString("yyyy-MM-dd"));
        q.addBindValue(to->date().toString("yyyy-MM-dd"));
        if (q.exec()) {
            while (q.next()) {
                QString fund = q.value(0).toString();
                double cents = q.value(1).toDouble();
                out << to->date().toString("yyyy-MM-dd") << ','
                    << csv(QString("Giving — %1 (%2 to %3)")
                               .arg(fund, from->date().toString("yyyy-MM-dd"), to->date().toString("yyyy-MM-dd")))
                    << ',' << QString::number(cents / 100.0, 'f', 2) << '\n';
                totalCents += cents;
                rowCount++;
            }
        }
    } else {
        out << "Date,Description,Deposit\n";
        QSqlQuery q;
        q.prepare("SELECT c.contribution_date, f.fund_name, "
                  "(p.first_name || ' ' || p.last_name), c.payment_method, c.amount "
                  "FROM contributions c "
                  "JOIN funds f ON c.fund_id = f.id "
                  "LEFT JOIN people p ON c.person_id = p.id "
                  "WHERE c.contribution_date >= ? AND c.contribution_date <= ? "
                  "ORDER BY c.contribution_date, c.id");
        q.addBindValue(from->date().toString("yyyy-MM-dd"));
        q.addBindValue(to->date().toString("yyyy-MM-dd"));
        if (q.exec()) {
            while (q.next()) {
                QString date = q.value(0).toString();
                QString fund = q.value(1).toString();
                QString donor = q.value(2).toString();
                QString method = q.value(3).toString();
                double cents = q.value(4).toDouble();
                QString donorLabel = donor.isEmpty() ? "Anonymous" : donor;
                if (method.isEmpty()) method = "—";
                out << date << ','
                    << csv(QString("Giving — %1 — %2 (%3)").arg(fund, donorLabel, method))
                    << ',' << QString::number(cents / 100.0, 'f', 2) << '\n';
                totalCents += cents;
                rowCount++;
            }
        }
    }
    file.close();

    QMessageBox::information(this, "Export Complete",
        QString("Exported %1 rows totaling %2%3.\n\nIn GnuCash: File → Import → Import CSV... and map "
                "the Deposit column to your income account splits.")
            .arg(QString::number(rowCount), appCurrencySymbol(), QString::number(totalCents / 100.0, 'f', 2)));
}

void ContributionsWidget::applyRole(const QString& role) {
    m_userRole = role;
    // Feature visibility is controlled by the Modules system; roles only
    // distinguish full access from read-only (Guest). A pastor/admin/finance
    // user gets the full finance form.
    const bool readOnly = (role == "readonly");
    m_btnSave->setEnabled(!readOnly);
    m_btnManageFunds->setEnabled(!readOnly);
    m_btnGnuCashExport->setEnabled(!readOnly);
    m_dateEdit->setEnabled(!readOnly);
    m_personCombo->setEnabled(!readOnly);
    m_amountSpin->setEnabled(!readOnly);
    m_fundCombo->setEnabled(!readOnly);
    m_noteEdit->setReadOnly(readOnly);
}
