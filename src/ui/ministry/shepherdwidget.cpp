#include "shepherdwidget.h"
#include "../database/databasemanager.h"
#include "../core/thememanager.h"
#include "../core/iconmanager.h"
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
#include <QDateTime>
#include <QMessageBox>
#include <QInputDialog>
#include <QSettings>
#include <QMenu>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QSqlQuery>
#include <QSqlError>
#include <QClipboard>
#include <QGuiApplication>

ShepherdWidget::ShepherdWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshAll();
}

void ShepherdWidget::applyRole(const QString& role) {
    m_userRole = role;
    const bool isReadOnly = (role == "readonly" || role == "guest");
    const bool canEdit = !isReadOnly;
    const QString roTip = isReadOnly ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify." : "";
    if (m_btnMarkDone) { m_btnMarkDone->setEnabled(canEdit); m_btnMarkDone->setToolTip(roTip); }
    if (m_btnReschedule) { m_btnReschedule->setEnabled(canEdit); m_btnReschedule->setToolTip(roTip); }
    if (m_btnLogFollowUp) { m_btnLogFollowUp->setEnabled(canEdit); m_btnLogFollowUp->setToolTip(roTip); }
    if (m_btnLogActivity) { m_btnLogActivity->setEnabled(canEdit); m_btnLogActivity->setToolTip(roTip); }
    if (m_btnAddPrayer) { m_btnAddPrayer->setEnabled(canEdit); m_btnAddPrayer->setToolTip(roTip); }
    if (m_btnPrayerDone) { m_btnPrayerDone->setEnabled(canEdit); m_btnPrayerDone->setToolTip(roTip); }
}

void ShepherdWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(25, 25, 25, 25);
    mainLayout->setSpacing(16);

    // Header + stats
    QHBoxLayout* headerRow = new QHBoxLayout();
    QVBoxLayout* titleCol = new QVBoxLayout();
    QLabel* header = new QLabel("Shepherd's Workbench", this);
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
    m_queueTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_queueTable, &QTableWidget::customContextMenuRequested, this, &ShepherdWidget::onQueueContextMenu);
    connect(m_queueTable, &QTableWidget::cellDoubleClicked, this, &ShepherdWidget::onQueueActivated);
    queueLay->addWidget(m_queueTable);

    QHBoxLayout* queueBtns = new QHBoxLayout();
    queueBtns->setSpacing(8);

    m_btnMarkDone = new QPushButton(" Mark Done", tabQueue);
    m_btnMarkDone->setIcon(IconManager::instance().getPrimaryIcon("check", 14));
    m_btnMarkDone->setObjectName("primaryBtn");
    connect(m_btnMarkDone, &QPushButton::clicked, this, &ShepherdWidget::onMarkFollowUpDone);

    m_btnReschedule = new QPushButton(" Reschedule / Snooze", tabQueue);
    m_btnReschedule->setIcon(IconManager::instance().getIcon("calendar", QColor(), 14));
    m_btnReschedule->setObjectName("secondaryBtn");
    connect(m_btnReschedule, &QPushButton::clicked, this, &ShepherdWidget::onRescheduleFollowUp);

    m_btnCalendar = new QPushButton(" Export Calendar (.ics)", tabQueue);
    m_btnCalendar->setIcon(IconManager::instance().getIcon("download", QColor(), 14));
    m_btnCalendar->setObjectName("secondaryBtn");
    connect(m_btnCalendar, &QPushButton::clicked, this, &ShepherdWidget::onExportCalendarEvent);

    m_btnLogFollowUp = new QPushButton(" Log Activity", tabQueue);
    m_btnLogFollowUp->setIcon(IconManager::instance().getIcon("plus", QColor(), 14));
    m_btnLogFollowUp->setObjectName("secondaryBtn");
    connect(m_btnLogFollowUp, &QPushButton::clicked, this, &ShepherdWidget::onLogFollowUp);

    queueBtns->addWidget(m_btnMarkDone);
    queueBtns->addWidget(m_btnReschedule);
    queueBtns->addWidget(m_btnCalendar);
    queueBtns->addWidget(m_btnLogFollowUp);
    queueBtns->addStretch();
    queueLay->addLayout(queueBtns);
    m_tabs->addTab(tabQueue, "Today's Queue");
    m_tabs->setTabIcon(0, IconManager::instance().getIcon("check-square", ThemeManager::instance().currentTokens().accentPrimary, 14));

    // ---- Tab 2: Member Activities ----
    QWidget* tabMember = new QWidget(this);
    QVBoxLayout* memberLay = new QVBoxLayout(tabMember);
    memberLay->setContentsMargins(10, 12, 10, 10);

    QHBoxLayout* memberTop = new QHBoxLayout();
    m_personCombo = new QComboBox(tabMember);
    m_personCombo->setMinimumWidth(280);
    connect(m_personCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ShepherdWidget::onPersonChanged);
    m_btnLogActivity = new QPushButton(" Log Activity", tabMember);
    m_btnLogActivity->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
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
    m_timelineList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_timelineList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_timelineList->itemAt(pos);
        if (!item) return;
        int actId = item->data(Qt::UserRole).toInt();
        if (actId <= 0) return;

        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Notes Text");
        menu.addSeparator();
        QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Activity Notes...");
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Delete Activity Log");

        if (isReadOnly) {
            actEdit->setEnabled(false);
            actEdit->setToolTip(roTooltip);
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_timelineList->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QGuiApplication::clipboard()->setText(item->toolTip());
        } else if (sel == actEdit && !isReadOnly) {
            bool ok = false;
            QString curNotes = item->toolTip();
            QString newNotes = QInputDialog::getMultiLineText(this, "Edit Activity Notes", "Notes:", curNotes, &ok);
            if (ok && !newNotes.trimmed().isEmpty()) {
                QSqlQuery uq;
                uq.prepare("UPDATE pastoral_activities SET notes = ? WHERE id = ?");
                uq.addBindValue(newNotes.trimmed());
                uq.addBindValue(actId);
                uq.exec();
                refreshTimeline();
            }
        } else if (sel == actDelete && !isReadOnly) {
            if (QMessageBox::question(this, "Confirm Delete", "Delete this pastoral activity record?") == QMessageBox::Yes) {
                QSqlQuery dq;
                dq.prepare("DELETE FROM pastoral_activities WHERE id = ?");
                dq.addBindValue(actId);
                dq.exec();
                refreshAll();
            }
        }
    });
    memberLay->addWidget(m_timelineList);
    m_tabs->addTab(tabMember, "Member Care Timeline");
    m_tabs->setTabIcon(1, IconManager::instance().getIcon("user", ThemeManager::instance().currentTokens().accentPrimary, 14));

    // ---- Tab 3: Prayer List ----
    QWidget* tabPrayer = new QWidget(this);
    QVBoxLayout* prayerLay = new QVBoxLayout(tabPrayer);
    prayerLay->setContentsMargins(10, 12, 10, 10);

    m_prayerList = new QListWidget(tabPrayer);
    m_prayerList->setAlternatingRowColors(true);
    m_prayerList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_prayerList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_prayerList->itemAt(pos);
        if (!item) return;
        int pId = item->data(Qt::UserRole).toInt();
        if (pId <= 0) return;

        const ThemeTokens tokPray = ThemeManager::instance().currentTokens();
        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Prayer Request");
        menu.addSeparator();
        QAction* actAnswer = menu.addAction(IconManager::instance().getIcon("check-circle-2", tokPray.accentSuccess, 14), "Mark Prayer Answered");
        QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Prayer Request...");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Delete Prayer Request");

        if (isReadOnly) {
            actAnswer->setEnabled(false);
            actAnswer->setToolTip(roTooltip);
            actEdit->setEnabled(false);
            actEdit->setToolTip(roTooltip);
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_prayerList->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QGuiApplication::clipboard()->setText(item->toolTip());
        } else if (sel == actAnswer && !isReadOnly) {
            QSqlQuery q;
            q.prepare("UPDATE pastoral_activities SET follow_up_done = 1 WHERE id = ?");
            q.addBindValue(pId);
            q.exec();
            refreshPrayerList();
        } else if (sel == actEdit && !isReadOnly) {
            bool ok = false;
            QString newNotes = QInputDialog::getMultiLineText(this, "Edit Prayer Request", "Prayer details / updates:", item->toolTip(), &ok);
            if (ok && !newNotes.trimmed().isEmpty()) {
                QSqlQuery uq;
                uq.prepare("UPDATE pastoral_activities SET notes = ? WHERE id = ?");
                uq.addBindValue(newNotes.trimmed());
                uq.addBindValue(pId);
                uq.exec();
                refreshPrayerList();
            }
        } else if (sel == actDelete && !isReadOnly) {
            if (QMessageBox::question(this, "Confirm Delete", "Delete this prayer request?") == QMessageBox::Yes) {
                QSqlQuery dq;
                dq.prepare("DELETE FROM pastoral_activities WHERE id = ?");
                dq.addBindValue(pId);
                dq.exec();
                refreshPrayerList();
            }
        }
    });
    prayerLay->addWidget(m_prayerList);

    QHBoxLayout* prayerBtns = new QHBoxLayout();
    m_btnAddPrayer = new QPushButton(" Add Prayer Request", tabPrayer);
    m_btnAddPrayer->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnAddPrayer->setObjectName("primaryBtn");
    connect(m_btnAddPrayer, &QPushButton::clicked, this, [this]() {
        if (showActivityDialog(-1, "Prayer")) refreshAll();
    });
    m_btnPrayerDone = new QPushButton(" Mark Selected Answered", tabPrayer);
    m_btnPrayerDone->setIcon(IconManager::instance().getIcon("check", QColor(), 14));
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
    m_tabs->addTab(tabPrayer, "Prayer List");
    m_tabs->setTabIcon(2, IconManager::instance().getIcon("heart", ThemeManager::instance().currentTokens().accentPrimary, 14));

    // ---- Tab 4: Giving Care (lapsed-giver watchlist) ----
    // Giving drop-off is one of the earliest engagement signals, often
    // preceding an attendance drop. Double-click a row to open the profile.
    QWidget* tabGiving = new QWidget(this);
    QVBoxLayout* givingLay = new QVBoxLayout(tabGiving);
    givingLay->setContentsMargins(10, 12, 10, 10);

    QLabel* givingHint = new QLabel(
        "Members and regular attenders who gave in at least 3 of the 6 months before the "
        "last 60 days, but have not given since. A quiet call often matters here.", tabGiving);
    givingHint->setWordWrap(true);
    givingHint->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
    givingLay->addWidget(givingHint);

    m_lapsedGiverList = new QListWidget(tabGiving);
    m_lapsedGiverList->setAlternatingRowColors(true);
    connect(m_lapsedGiverList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int personId = item->data(Qt::UserRole).toInt();
        if (personId > 0) emit navigateToMember(personId);
    });
    givingLay->addWidget(m_lapsedGiverList);
    m_tabs->addTab(tabGiving, "Giving Care");
    m_tabs->setTabIcon(3, IconManager::instance().getIcon("dollar-sign", ThemeManager::instance().currentTokens().accentPrimary, 14));
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
    refreshLapsedGivers();
}

void ShepherdWidget::refreshLapsedGivers() {
    if (!m_lapsedGiverList) return;
    m_lapsedGiverList->clear();

    // "Habitual then silent": gave in >= 3 distinct months within the window
    // from 6 months ago to 60 days ago, and nothing in the last 60 days.
    // Anonymous gifts have no person link and are excluded by the join.
    QSqlQuery q;
    q.prepare(
        "SELECT p.id, p.first_name || ' ' || p.last_name, "
        "       (SELECT COUNT(DISTINCT strftime('%Y-%m', c.contribution_date)) "
        "          FROM contributions c "
        "         WHERE c.person_id = p.id "
        "           AND c.contribution_date >= date('now', '-6 months') "
        "           AND c.contribution_date <  date('now', '-60 days')) AS active_months, "
        "       (SELECT MAX(c.contribution_date) FROM contributions c WHERE c.person_id = p.id) AS last_gift "
        "FROM people p "
        "WHERE p.membership_status IN ('Member', 'Regular Attender') "
        "  AND active_months >= 3 "
        "  AND last_gift < date('now', '-60 days') "
        "ORDER BY last_gift ASC");
    if (!q.exec()) {
        qWarning() << "refreshLapsedGivers failed:" << q.lastError().text();
        return;
    }
    while (q.next()) {
        int id = q.value(0).toInt();
        QString name = q.value(1).toString();
        int months = q.value(2).toInt();
        QString last = q.value(3).toString();
        QListWidgetItem* item = new QListWidgetItem(
            QString("%1 — gave in %2 of the last 6 months, silent since %3")
                .arg(name).arg(months).arg(last.isEmpty() ? "—" : last),
            m_lapsedGiverList);
        item->setIcon(IconManager::instance().getDangerIcon("trending-down", 14));
        item->setData(Qt::UserRole, id);
    }
    if (m_lapsedGiverList->count() == 0) {
        m_lapsedGiverList->addItem("No lapsed givers — regular giving patterns are all current.");
    }
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
        const ThemeTokens tok = ThemeManager::instance().currentTokens();
        od->setForeground(overdue ? tok.accentDanger : tok.accentWarning);
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
        "SELECT id, activity_date, activity_type, notes, outcome, follow_up_date, follow_up_done "
        "FROM pastoral_activities WHERE person_id = ? ORDER BY activity_date DESC, id DESC");
    q.addBindValue(personId);
    if (!q.exec()) return;

    while (q.next()) {
        int actId = q.value(0).toInt();
        QString line = QString("%1 • %2").arg(q.value(1).toString(), q.value(2).toString());
        if (!q.value(5).toString().isEmpty()) {
            line += QString(" (follow-up %1%2)")
                        .arg(q.value(5).toString(),
                             q.value(6).toInt() == 0 ? " ⏳" : " ✅");
        }
        QString detail = q.value(3).toString();
        if (!q.value(4).toString().isEmpty()) {
            detail = detail.isEmpty() ? q.value(4).toString() : detail + " — " + q.value(4).toString();
        }
        if (!detail.isEmpty()) line += "\n      " + detail;
        QListWidgetItem* item = new QListWidgetItem(line, m_timelineList);
        item->setData(Qt::UserRole, actId);
        item->setToolTip(detail.isEmpty() ? line : detail);
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

void ShepherdWidget::onRescheduleFollowUp() {
    int row = m_queueTable->currentRow();
    if (row < 0 || !m_queueTable->item(row, 0) || !m_queueTable->item(row, 1)) {
        QMessageBox::information(this, "Notice", "Select a follow-up item from the queue first.");
        return;
    }
    int id = m_queueTable->item(row, 0)->data(Qt::UserRole).toInt();
    QString memberName = m_queueTable->item(row, 1)->text();
    QString currentDue = m_queueTable->item(row, 0)->text();

    QDialog dlg(this);
    dlg.setWindowTitle("Reschedule Follow-up — " + memberName);
    dlg.resize(380, 220);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(12);

    QLabel* lbl = new QLabel(QString("Select a new follow-up date for <b>%1</b> (currently due %2):").arg(memberName, currentDue), &dlg);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);

    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate().addDays(7), &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    lay->addWidget(dateEdit);

    QHBoxLayout* presets = new QHBoxLayout();
    presets->setSpacing(6);
    auto makePreset = [&dateEdit](const QString& text, int days) {
        QPushButton* b = new QPushButton(text);
        b->setObjectName("secondaryBtn");
        b->setStyleSheet("font-size: 11px; padding: 4px 8px;");
        QObject::connect(b, &QPushButton::clicked, [dateEdit, days]() {
            dateEdit->setDate(QDate::currentDate().addDays(days));
        });
        return b;
    };
    presets->addWidget(makePreset("+3 Days", 3));
    presets->addWidget(makePreset("+1 Week", 7));
    presets->addWidget(makePreset("+2 Weeks", 14));
    presets->addWidget(makePreset("+1 Month", 30));
    lay->addLayout(presets);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString newDate = dateEdit->date().toString("yyyy-MM-dd");
        QSqlQuery q;
        q.prepare("UPDATE pastoral_activities SET follow_up_date = ?, follow_up_done = 0 WHERE id = ?");
        q.addBindValue(newDate);
        q.addBindValue(id);
        if (q.exec()) {
            refreshAll();
            QMessageBox::information(this, "Success", QString("Follow-up rescheduled to %1.").arg(newDate));
        } else {
            QMessageBox::critical(this, "Database Error", "Failed to reschedule: " + q.lastError().text());
        }
    }
}

void ShepherdWidget::onExportCalendarEvent() {
    int row = m_queueTable->currentRow();
    if (row < 0 || !m_queueTable->item(row, 0) || !m_queueTable->item(row, 1)
        || !m_queueTable->item(row, 2) || !m_queueTable->item(row, 3)) {
        QMessageBox::information(this, "Notice", "Select a follow-up item from the queue first.");
        return;
    }
    int id = m_queueTable->item(row, 0)->data(Qt::UserRole).toInt();
    QString memberName = m_queueTable->item(row, 1)->text();
    QString type = m_queueTable->item(row, 2)->text();
    QString notes = m_queueTable->item(row, 3)->text();
    QString dueDate = m_queueTable->item(row, 0)->text();

    QString cleanName = memberName;
    cleanName.replace(" ", "_");
    QString cleanDate = dueDate;
    cleanDate.replace("-", "");
    QString defaultFile = QString("Pastoral_FollowUp_%1_%2.ics").arg(cleanName, cleanDate);

    QString fileName = QFileDialog::getSaveFileName(this, "Save Calendar Reminder (.ics)", defaultFile, "iCalendar Files (*.ics)");
    if (fileName.isEmpty()) return;

    auto warn = QMessageBox::warning(this, "Data Security Notice",
        "This calendar file contains pastoral care details in plain text.\nOnly store it on a trusted device.",
        QMessageBox::Ok | QMessageBox::Cancel);
    if (warn != QMessageBox::Ok) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "File Error", "Could not create calendar file: " + file.errorString());
        return;
    }

    QString dateStamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddThhmmssZ");
    QString startDate = cleanDate;

    QTextStream out(&file);
    out << "BEGIN:VCALENDAR\n";
    out << "VERSION:2.0\n";
    out << "PRODID:-//OpenEkklesia//Shepherd Workbench//EN\n";
    out << "CALSCALE:GREGORIAN\n";
    out << "BEGIN:VEVENT\n";
    out << QString("UID:oe-pastoral-%1@openekklesia.local\n").arg(id);
    out << QString("DTSTAMP:%1\n").arg(dateStamp);
    out << QString("DTSTART;VALUE=DATE:%1\n").arg(startDate);
    out << QString("SUMMARY:Pastoral Care: %1 (%2)\n").arg(memberName, type);
    out << QString("DESCRIPTION:Pastoral Follow-up with %1.\\nType: %2\\nNotes: %3\n").arg(memberName, type, notes);
    out << "STATUS:CONFIRMED\n";
    out << "BEGIN:VALARM\n";
    out << "TRIGGER:-PT15M\n";
    out << "ACTION:DISPLAY\n";
    out << QString("DESCRIPTION:Reminder: Pastoral follow-up with %1\n").arg(memberName);
    out << "END:VALARM\n";
    out << "END:VEVENT\n";
    out << "END:VCALENDAR\n";
    file.close();

    DatabaseManager::logAudit("exports", id, "pastoral_ics", memberName, fileName, "shepherd");
    QMessageBox::information(this, "Calendar Event Exported",
        QString("Calendar reminder exported successfully!\n\nYou can open this file in Outlook, Apple Calendar, Google Calendar, or Thunderbird:\n%1").arg(fileName));
}

void ShepherdWidget::onQueueContextMenu(const QPoint& pos) {
    int row = m_queueTable->rowAt(pos.y());
    if (row < 0 || !m_queueTable->item(row, 0) || !m_queueTable->item(row, 1)
        || !m_queueTable->item(row, 2)) return;

    m_queueTable->selectRow(row);
    int personId = m_queueTable->item(row, 1)->data(Qt::UserRole + 1).toInt();
    QString memberName = m_queueTable->item(row, 1)->text();
    QString dueDate = m_queueTable->item(row, 0)->text();
    QString actionType = m_queueTable->item(row, 2)->text();

    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
    const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

    QMenu menu(this);
    QAction* actDone = menu.addAction(IconManager::instance().getIcon("check-circle-2", tok.accentSuccess, 14), "Mark Follow-up Done");
    QAction* actReschedule = menu.addAction(IconManager::instance().getIcon("clock", tok.accentWarning, 14), "Reschedule / Snooze...");
    QAction* actCalendar = menu.addAction(IconManager::instance().getIcon("calendar", QColor(), 14), "Export Calendar Reminder (.ics)...");
    menu.addSeparator();
    QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Task Summary");
    QAction* actProfile = menu.addAction(IconManager::instance().getIcon("user", tok.accentPrimary, 14), QString("View %1 in Directory").arg(memberName));

    if (isReadOnly) {
        actDone->setEnabled(false);
        actDone->setToolTip(roTooltip);
        actReschedule->setEnabled(false);
        actReschedule->setToolTip(roTooltip);
    }

    QAction* selected = menu.exec(m_queueTable->viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == actDone && !isReadOnly) {
        onMarkFollowUpDone();
    } else if (selected == actReschedule && !isReadOnly) {
        onRescheduleFollowUp();
    } else if (selected == actCalendar) {
        onExportCalendarEvent();
    } else if (selected == actCopy) {
        QString summary = QString("%1: Follow-up with %2 (Due: %3)").arg(actionType, memberName, dueDate);
        QGuiApplication::clipboard()->setText(summary);
    } else if (selected == actProfile) {
        if (personId > 0) emit navigateToMember(personId);
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
    // Default follow-up window comes from Settings (Pastoral & Graph → followUpDays).
    QSettings defS("OpenEkklesia", "CHMS");
    const int followUpDays = qBound(0, defS.value("followUpDays", 14).toInt(), 365);
    QDateEdit* followUpEdit = new QDateEdit(QDate::currentDate().addDays(followUpDays), &dlg);
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
