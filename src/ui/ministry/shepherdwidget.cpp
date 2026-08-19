#include "shepherdwidget.h"
#include "../database/databasemanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QCheckBox>
#include <QDateEdit>
#include <QDate>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>

ShepherdWidget::ShepherdWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshAll();
}

void ShepherdWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(25, 25, 25, 25);
    mainLayout->setSpacing(16);

    // Header + stats
    QHBoxLayout* headerRow = new QHBoxLayout();
    QVBoxLayout* titleCol = new QVBoxLayout();
    QLabel* header = new QLabel("🛐 Shepherd's Workbench", this);
    header->setObjectName("pageHeader");
    QLabel* sub = new QLabel("Today's pastoral priorities — the people who need you, gathered in one place.", this);
    sub->setStyleSheet("font-size: 13px; opacity: 0.8;");
    titleCol->addWidget(header);
    titleCol->addWidget(sub);
    headerRow->addLayout(titleCol);
    headerRow->addStretch();

    QHBoxLayout* statsRow = new QHBoxLayout();
    statsRow->setSpacing(12);
    auto statCard = [this](const QString& label, QLabel** value) {
        QWidget* card = new QWidget(this);
        card->setObjectName("cardWidget");
        QVBoxLayout* v = new QVBoxLayout(card);
        v->setContentsMargins(14, 10, 14, 10);
        v->setSpacing(2);
        *value = new QLabel("0", card);
        (*value)->setStyleSheet("font-size: 22px; font-weight: bold;");
        QLabel* lbl = new QLabel(label, card);
        lbl->setStyleSheet("font-size: 11px; opacity: 0.7;");
        v->addWidget(*value);
        v->addWidget(lbl);
        return card;
    };
    statsRow->addWidget(statCard("Follow-ups Due", &m_lblDueCount));
    statsRow->addWidget(statCard("Members At-Risk", &m_lblAtRiskCount));
    statsRow->addWidget(statCard("Open Prayer Items", &m_lblPrayerCount));
    statsRow->addStretch();
    headerRow->addLayout(statsRow);
    mainLayout->addLayout(headerRow);

    m_tabs = new QTabWidget(this);
    mainLayout->addWidget(m_tabs);

    // ---- Tab 1: Today's Queue ----
    QWidget* tabQueue = new QWidget(this);
    QVBoxLayout* queueLay = new QVBoxLayout(tabQueue);
    queueLay->setContentsMargins(10, 12, 10, 10);

    QLabel* queueHint = new QLabel("Activities with a follow-up due today or earlier. Work the queue, then mark each one done.", tabQueue);
    queueHint->setStyleSheet("font-size: 12px; opacity: 0.8;");
    queueLay->addWidget(queueHint);

    m_queueTable = new QTableWidget(tabQueue);
    m_queueTable->setColumnCount(5);
    m_queueTable->setHorizontalHeaderLabels({"Due", "Member", "Type", "Notes / Outcome", "Overdue"});
    m_queueTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_queueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_queueTable->verticalHeader()->setVisible(false);
    connect(m_queueTable, &QTableWidget::cellDoubleClicked, this, &ShepherdWidget::onQueueActivated);
    queueLay->addWidget(m_queueTable);

    QHBoxLayout* queueBtns = new QHBoxLayout();
    m_btnMarkDone = new QPushButton("✅ Mark Selected Follow-up Done", tabQueue);
    m_btnMarkDone->setObjectName("primaryBtn");
    connect(m_btnMarkDone, &QPushButton::clicked, this, &ShepherdWidget::onMarkFollowUpDone);
    m_btnLogFollowUp = new QPushButton("➕ Log a Pastoral Activity", tabQueue);
    m_btnLogFollowUp->setObjectName("secondaryBtn");
    connect(m_btnLogFollowUp, &QPushButton::clicked, this, &ShepherdWidget::onLogFollowUp);
    queueBtns->addWidget(m_btnMarkDone);
    queueBtns->addWidget(m_btnLogFollowUp);
    queueBtns->addStretch();
    queueLay->addLayout(queueBtns);
    m_tabs->addTab(tabQueue, "📌 Today's Queue");

    // ---- Tab 2: Member Activities ----
    QWidget* tabMember = new QWidget(this);
    QVBoxLayout* memberLay = new QVBoxLayout(tabMember);
    memberLay->setContentsMargins(10, 12, 10, 10);

    QHBoxLayout* memberTop = new QHBoxLayout();
    m_personCombo = new QComboBox(tabMember);
    m_personCombo->setMinimumWidth(280);
    connect(m_personCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ShepherdWidget::onPersonChanged);
    m_btnLogActivity = new QPushButton("➕ Log Activity", tabMember);
    m_btnLogActivity->setObjectName("primaryBtn");
    connect(m_btnLogActivity, &QPushButton::clicked, this, &ShepherdWidget::onLogActivity);
    memberTop->addWidget(new QLabel("Member:", tabMember));
    memberTop->addWidget(m_personCombo);
    memberTop->addWidget(m_btnLogActivity);
    memberTop->addStretch();
    memberLay->addLayout(memberTop);

    m_timelineList = new QListWidget(tabMember);
    m_timelineList->setObjectName("historyList");
    m_timelineList->setAlternatingRowColors(true);
    memberLay->addWidget(m_timelineList);
    m_tabs->addTab(tabMember, "👤 Member Care Timeline");

    // ---- Tab 3: Prayer List ----
    QWidget* tabPrayer = new QWidget(this);
    QVBoxLayout* prayerLay = new QVBoxLayout(tabPrayer);
    prayerLay->setContentsMargins(10, 12, 10, 10);

    m_prayerList = new QListWidget(tabPrayer);
    m_prayerList->setAlternatingRowColors(true);
    prayerLay->addWidget(m_prayerList);

    QHBoxLayout* prayerBtns = new QHBoxLayout();
    m_btnAddPrayer = new QPushButton("➕ Add Prayer Request", tabPrayer);
    m_btnAddPrayer->setObjectName("primaryBtn");
    connect(m_btnAddPrayer, &QPushButton::clicked, this, [this]() {
        if (showActivityDialog(-1, "Prayer")) refreshAll();
    });
    m_btnPrayerDone = new QPushButton("✅ Mark Selected Answered", tabPrayer);
    m_btnPrayerDone->setObjectName("secondaryBtn");
    connect(m_btnPrayerDone, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* item = m_prayerList->currentItem();
        if (!item) {
            QMessageBox::information(this, "Notice", "Select a prayer item to mark as answered.");
            return;
        }
        int id = item->data(Qt::UserRole).toInt();
        QSqlQuery q;
        q.prepare("UPDATE pastoral_activities SET follow_up_done = 1 WHERE id = ?");
        q.addBindValue(id);
        q.exec();
        refreshAll();
    });
    prayerBtns->addWidget(m_btnAddPrayer);
    prayerBtns->addWidget(m_btnPrayerDone);
    prayerBtns->addStretch();
    prayerLay->addLayout(prayerBtns);
    m_tabs->addTab(tabPrayer, "🙏 Prayer List");
}

void ShepherdWidget::refreshAll() {
    // Rebuild the person combo (keep selection if possible).
    QSqlQuery pq;
    pq.exec("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name");
    QString selected;
    if (m_personCombo->currentIndex() >= 0) selected = m_personCombo->currentData().toString();
    m_personCombo->blockSignals(true);
    m_personCombo->clear();
    while (pq.next()) {
        m_personCombo->addItem(pq.value(1).toString(), pq.value(0).toInt());
    }
    if (!selected.isEmpty()) {
        int idx = m_personCombo->findData(selected);
        if (idx >= 0) m_personCombo->setCurrentIndex(idx);
    }
    m_personCombo->blockSignals(false);

    refreshStats();
    refreshQueue();
    refreshTimeline();
    refreshPrayerList();
}

void ShepherdWidget::refreshStats() {
    QSqlQuery q;

    if (q.exec("SELECT COUNT(*) FROM pastoral_activities WHERE follow_up_date IS NOT NULL AND follow_up_date <= date('now', 'localtime') AND follow_up_done = 0") && q.next()) {
        m_lblDueCount->setText(QString::number(q.value(0).toInt()));
    }
    if (q.exec("SELECT COUNT(*) FROM people WHERE care_flag IN ('At-Risk', 'Follow-up')") && q.next()) {
        m_lblAtRiskCount->setText(QString::number(q.value(0).toInt()));
    }
    if (q.exec("SELECT COUNT(*) FROM pastoral_activities WHERE activity_type = 'Prayer' AND follow_up_done = 0") && q.next()) {
        m_lblPrayerCount->setText(QString::number(q.value(0).toInt()));
    }
}

void ShepherdWidget::refreshQueue() {
    m_queueTable->setRowCount(0);
    QSqlQuery q;
    q.prepare(
        "SELECT pa.id, pa.follow_up_date, p.first_name || ' ' || p.last_name, pa.activity_type, "
        "       pa.notes, pa.outcome, p.id "
        "FROM pastoral_activities pa JOIN people p ON pa.person_id = p.id "
        "WHERE pa.follow_up_date IS NOT NULL AND pa.follow_up_date <= date('now', 'localtime') AND pa.follow_up_done = 0 "
        "ORDER BY pa.follow_up_date ASC");
    if (!q.exec()) return;

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    int row = 0;
    while (q.next()) {
        m_queueTable->insertRow(row);
        const QString due = q.value(1).toString();
        m_queueTable->setItem(row, 0, new QTableWidgetItem(due));
        m_queueTable->setItem(row, 1, new QTableWidgetItem(q.value(2).toString()));
        m_queueTable->setItem(row, 2, new QTableWidgetItem(q.value(3).toString()));
        QString notes = q.value(4).toString();
        if (!q.value(5).toString().isEmpty()) {
            notes = notes.isEmpty() ? q.value(5).toString() : notes + " — " + q.value(5).toString();
        }
        m_queueTable->setItem(row, 3, new QTableWidgetItem(notes));
        const bool overdue = due < today;
        QTableWidgetItem* od = new QTableWidgetItem(overdue ? "⚠️ Overdue" : "Due today");
        od->setForeground(QColor(overdue ? "#f38ba8" : "#f9e2af"));
        m_queueTable->setItem(row, 4, od);
        m_queueTable->item(row, 0)->setData(Qt::UserRole, q.value(0).toInt());
        m_queueTable->item(row, 1)->setData(Qt::UserRole + 1, q.value(6).toInt());
        ++row;
    }
}

void ShepherdWidget::refreshTimeline() {
    m_timelineList->clear();
    int personId = m_personCombo->currentData().toInt();
    if (personId <= 0) return;

    QSqlQuery q;
    q.prepare(
        "SELECT activity_date, activity_type, notes, outcome, follow_up_date, follow_up_done "
        "FROM pastoral_activities WHERE person_id = ? ORDER BY activity_date DESC, id DESC");
    q.addBindValue(personId);
    if (!q.exec()) return;

    while (q.next()) {
        QString line = QString("%1 • %2").arg(q.value(0).toString(), q.value(1).toString());
        if (!q.value(4).toString().isEmpty()) {
            line += QString(" (follow-up %1%2)")
                        .arg(q.value(4).toString(),
                             q.value(5).toInt() == 0 ? " ⏳" : " ✅");
        }
        QString detail = q.value(2).toString();
        if (!q.value(3).toString().isEmpty()) {
            detail = detail.isEmpty() ? q.value(3).toString() : detail + " — " + q.value(3).toString();
        }
        if (!detail.isEmpty()) line += "\n      " + detail;
        QListWidgetItem* item = new QListWidgetItem(line, m_timelineList);
        item->setToolTip(line);
    }
    if (m_timelineList->count() == 0) {
        m_timelineList->addItem("No pastoral activities logged for this member yet.");
    }
}

void ShepherdWidget::refreshPrayerList() {
    m_prayerList->clear();
    QSqlQuery q;
    q.prepare(
        "SELECT pa.id, p.first_name || ' ' || p.last_name, pa.notes, pa.activity_date, pa.follow_up_done "
        "FROM pastoral_activities pa JOIN people p ON pa.person_id = p.id "
        "WHERE pa.activity_type = 'Prayer' ORDER BY pa.follow_up_done ASC, pa.activity_date DESC");
    if (!q.exec()) return;

    while (q.next()) {
        const bool done = q.value(4).toInt() != 0;
        QString line = QString("%1 — %2%3: %4")
                           .arg(q.value(3).toString(),
                                q.value(1).toString(),
                                done ? " ✅" : "",
                                q.value(2).toString());
        QListWidgetItem* item = new QListWidgetItem(line, m_prayerList);
        item->setData(Qt::UserRole, q.value(0).toInt());
        item->setToolTip(line);
    }
    if (m_prayerList->count() == 0) {
        m_prayerList->addItem("No prayer requests recorded. Add one from the queue or member tab.");
    }
}

void ShepherdWidget::onPersonChanged(int) {
    refreshTimeline();
}

void ShepherdWidget::onLogActivity() {
    if (showActivityDialog(m_personCombo->currentData().toInt())) {
        refreshAll();
    }
}

void ShepherdWidget::onLogFollowUp() {
    int personId = -1;
    int row = m_queueTable->currentRow();
    if (row >= 0) {
        personId = m_queueTable->item(row, 1) ? m_queueTable->item(row, 1)->data(Qt::UserRole + 1).toInt() : -1;
    }
    if (showActivityDialog(personId)) {
        refreshAll();
    }
}

void ShepherdWidget::onMarkFollowUpDone() {
    int row = m_queueTable->currentRow();
    if (row < 0 || !m_queueTable->item(row, 0)) {
        QMessageBox::information(this, "Notice", "Select a follow-up item from the queue first.");
        return;
    }
    int id = m_queueTable->item(row, 0)->data(Qt::UserRole).toInt();
    QSqlQuery q;
    q.prepare("UPDATE pastoral_activities SET follow_up_done = 1 WHERE id = ?");
    q.addBindValue(id);
    if (q.exec()) {
        refreshAll();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to update follow-up: " + q.lastError().text());
    }
}

void ShepherdWidget::onQueueActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || !m_queueTable->item(row, 1)) return;
    int personId = m_queueTable->item(row, 1)->data(Qt::UserRole + 1).toInt();
    if (personId > 0) emit navigateToMember(personId);
}

bool ShepherdWidget::showActivityDialog(int prefillPersonId, const QString& prefillType) {
    QDialog dlg(this);
    dlg.setWindowTitle("Log Pastoral Activity");
    dlg.resize(440, 400);
    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(10);

    QComboBox* personCombo = new QComboBox(&dlg);
    QSqlQuery pq;
    pq.exec("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name");
    while (pq.next()) personCombo->addItem(pq.value(1).toString(), pq.value(0).toInt());
    if (prefillPersonId > 0) {
        int idx = personCombo->findData(prefillPersonId);
        if (idx >= 0) personCombo->setCurrentIndex(idx);
    }
    form->addRow("Member:", personCombo);

    QComboBox* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({"Visit", "Phone Call", "Prayer", "Pastoral Care", "Discipleship", "Sacrament", "Outreach"});
    if (!prefillType.isEmpty()) {
        int idx = typeCombo->findText(prefillType, Qt::MatchContains);
        if (idx >= 0) typeCombo->setCurrentIndex(idx);
    }
    form->addRow("Activity Type:", typeCombo);

    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate(), &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    form->addRow("Date:", dateEdit);

    QTextEdit* notesEdit = new QTextEdit(&dlg);
    notesEdit->setPlaceholderText("What happened, what was discussed...");
    form->addRow("Notes:", notesEdit);

    QLineEdit* outcomeEdit = new QLineEdit(&dlg);
    outcomeEdit->setPlaceholderText("e.g. Hospital visit, prayed together");
    form->addRow("Outcome:", outcomeEdit);

    QCheckBox* followUpChk = new QCheckBox("Schedule a follow-up", &dlg);
    QDateEdit* followUpEdit = new QDateEdit(QDate::currentDate().addDays(14), &dlg);
    followUpEdit->setCalendarPopup(true);
    followUpEdit->setDisplayFormat("yyyy-MM-dd");
    followUpEdit->setEnabled(false);
    connect(followUpChk, &QCheckBox::toggled, followUpEdit, &QDateEdit::setEnabled);
    form->addRow(followUpChk);
    form->addRow("Follow-up by:", followUpEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    int personId = personCombo->currentData().toInt();
    if (personId <= 0) return false;
    QString notes = notesEdit->toPlainText().trimmed();
    if (notes.isEmpty() && outcomeEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Details", "Add a short note or outcome before saving.");
        return false;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, outcome, follow_up_date, follow_up_done) "
              "VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(personId);
    q.addBindValue(typeCombo->currentText());
    q.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
    q.addBindValue(notes);
    q.addBindValue(outcomeEdit->text().trimmed());
    q.addBindValue(followUpChk->isChecked() ? followUpEdit->date().toString("yyyy-MM-dd") : QVariant());
    q.addBindValue(0);
    if (!q.exec()) {
        QMessageBox::critical(this, "Database Error", "Failed to save activity: " + q.lastError().text());
        return false;
    }
    return true;
}
