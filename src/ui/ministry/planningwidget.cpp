#include "planningwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QLocale>
#include <QMenu>
#include "../database/databasemanager.h"
#include "../core/thememanager.h"
#include "../core/iconmanager.h"

PlanningWidget::PlanningWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshRoadmap();
    refreshGanttChart();
    applyRole("admin"); // default: full access until MainWindow applies the real role
}

PlanningWidget::~PlanningWidget() {}

void PlanningWidget::applyRole(const QString& role) {
    m_readOnly = (role.compare("readonly", Qt::CaseInsensitive) == 0 ||
                  role.compare("guest", Qt::CaseInsensitive) == 0);
    const bool canEdit = !m_readOnly;
    m_btnAddTask->setEnabled(canEdit);
    m_btnEditTask->setEnabled(canEdit);
    m_btnDeleteTask->setEnabled(canEdit);

    const QString tip = m_readOnly
        ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
        : "";
    for (QPushButton* b : {m_btnAddTask, m_btnEditTask, m_btnDeleteTask}) {
        b->setToolTip(tip);
    }
}

void PlanningWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(15);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* header = new QLabel("Ministry Planning: Roadmap & Gantt Charts", this);
    header->setObjectName("pageHeader");
    headerLayout->addWidget(header);
    headerLayout->addStretch();

    m_btnAddTask = new QPushButton(" Add Initiative / Task", this);
    m_btnAddTask->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnAddTask->setObjectName("primaryBtn");
    connect(m_btnAddTask, &QPushButton::clicked, this, &PlanningWidget::onAddTask);
    headerLayout->addWidget(m_btnAddTask);

    m_btnEditTask = new QPushButton(" Edit Selected", this);
    m_btnEditTask->setIcon(IconManager::instance().getIcon("pencil", QColor(), 14));
    m_btnEditTask->setObjectName("secondaryBtn");
    m_btnEditTask->setToolTip("Edit the selected task — including its status (Planned → In-Progress → Completed)");
    connect(m_btnEditTask, &QPushButton::clicked, this, &PlanningWidget::onEditTask);
    headerLayout->addWidget(m_btnEditTask);

    m_btnDeleteTask = new QPushButton(" Remove Selected", this);
    m_btnDeleteTask->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDeleteTask->setObjectName("dangerBtn");
    connect(m_btnDeleteTask, &QPushButton::clicked, this, &PlanningWidget::onDeleteTask);
    headerLayout->addWidget(m_btnDeleteTask);

    mainLayout->addLayout(headerLayout);

    // Tabbed view for Roadmap vs. Gantt Chart
    QTabWidget* tabs = new QTabWidget(this);

    // 1. Roadmap View (Team / Department Quarterly Benchmarks)
    QWidget* roadmapTab = new QWidget(this);
    QVBoxLayout* roadLayout = new QVBoxLayout(roadmapTab);
    m_roadmapTable = new QTableWidget(this);
    m_roadmapTable->setObjectName("planningRoadmapTable");
    m_roadmapTable->setColumnCount(7);
    m_roadmapTable->setHorizontalHeaderLabels({"Ministry / Team", "Initiative Name", "JAN", "FEB", "MAR", "APR", "MAY/JUN"});
    m_roadmapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_roadmapTable->setAlternatingRowColors(true);
    roadLayout->addWidget(m_roadmapTable);
    tabs->addTab(roadmapTab, "Ministry Product Roadmap");
    tabs->setTabIcon(0, IconManager::instance().getIcon("map", ThemeManager::instance().currentTokens().accentPrimary, 14));

    // 2. Gantt Chart View (20-day rolling window anchored to a real date)
    QWidget* ganttTab = new QWidget(this);
    QVBoxLayout* ganttLayout = new QVBoxLayout(ganttTab);
    QHBoxLayout* ganttHeader = new QHBoxLayout();
    QLabel* ganttHint = new QLabel("20-day window starting:", ganttTab);
    m_anchorDate = new QDateEdit(QDate::currentDate(), ganttTab);
    m_anchorDate->setObjectName("planningAnchorDate");
    m_anchorDate->setCalendarPopup(true);
    m_anchorDate->setDisplayFormat("yyyy-MM-dd");
    connect(m_anchorDate, &QDateEdit::dateChanged, this, [this](const QDate&) { refreshGanttChart(); });
    ganttHeader->addWidget(ganttHint);
    ganttHeader->addWidget(m_anchorDate);
    ganttHeader->addStretch();
    ganttLayout->addLayout(ganttHeader);

    m_ganttTable = new QTableWidget(this);
    m_ganttTable->setObjectName("planningGanttTable");
    m_ganttTable->setColumnCount(22);
    m_ganttTable->setHorizontalHeaderLabels({"Activity / Task", "Owner / Leader"});
    m_ganttTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int i = 2; i < 22; ++i) {
        m_ganttTable->setColumnWidth(i, 42);
    }
    m_ganttTable->setAlternatingRowColors(true);
    ganttLayout->addWidget(m_ganttTable);
    tabs->addTab(ganttTab, "Event Project Gantt Chart");
    tabs->setTabIcon(1, IconManager::instance().getIcon("bar-chart-2", ThemeManager::instance().currentTokens().accentPrimary, 14));

    auto setupPlanningContextMenu = [this](QTableWidget* tbl) {
        tbl->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tbl, &QTableWidget::customContextMenuRequested, this, [this, tbl](const QPoint& pos) {
            int r = tbl->rowAt(pos.y());
            if (r < 0 || !tbl->item(r, 0)) return;
            tbl->selectRow(r);
            int taskId = tbl->item(r, 0)->data(Qt::UserRole).toInt();
            if (taskId <= 0) return;

            const ThemeTokens tokPlan = ThemeManager::instance().currentTokens();
            QMenu menu(this);
            QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Task Details...");
            QAction* actComplete = menu.addAction(IconManager::instance().getIcon("check-circle-2", tokPlan.accentSuccess, 14), "Mark Status as Completed");
            QAction* actPlanned = menu.addAction(IconManager::instance().getIcon("calendar", QColor(), 14), "Mark Status as Planned");
            QAction* actInProgress = menu.addAction(IconManager::instance().getIcon("clock", tokPlan.accentWarning, 14), "Mark Status as In-Progress");
            menu.addSeparator();
            QAction* actSnooze = menu.addAction(IconManager::instance().getIcon("calendar-plus", QColor(), 14), "Extend Due Date (+7 Days)");
            menu.addSeparator();
            QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Task");

            QAction* sel = menu.exec(tbl->viewport()->mapToGlobal(pos));
            if (!sel) return;

            if (sel == actEdit) {
                openTaskDialog(taskId);
            } else if (sel == actComplete || sel == actPlanned || sel == actInProgress) {
                QString newStat = (sel == actComplete) ? "Completed" : ((sel == actPlanned) ? "Planned" : "In-Progress");
                QSqlQuery uq;
                uq.prepare("UPDATE ministry_tasks SET status = ? WHERE id = ?");
                uq.addBindValue(newStat);
                uq.addBindValue(taskId);
                uq.exec();
                refreshRoadmap();
                refreshGanttChart();
            } else if (sel == actSnooze) {
                QSqlQuery q;
                q.prepare("SELECT end_date FROM ministry_tasks WHERE id = ?");
                q.addBindValue(taskId);
                if (q.exec() && q.next()) {
                    QDate curEnd = QDate::fromString(q.value(0).toString(), Qt::ISODate);
                    if (!curEnd.isValid()) curEnd = QDate::currentDate();
                    QDate newEnd = curEnd.addDays(7);
                    QSqlQuery uq;
                    uq.prepare("UPDATE ministry_tasks SET end_date = ? WHERE id = ?");
                    uq.addBindValue(newEnd.toString(Qt::ISODate));
                    uq.addBindValue(taskId);
                    uq.exec();
                    refreshRoadmap();
                    refreshGanttChart();
                }
            } else if (sel == actDelete) {
                onDeleteTask();
            }
        });
    };
    setupPlanningContextMenu(m_roadmapTable);
    setupPlanningContextMenu(m_ganttTable);

    connect(tabs, &QTabWidget::currentChanged, this, &PlanningWidget::onTabChanged);

    mainLayout->addWidget(tabs);
}

void PlanningWidget::onTabChanged(int index) {
    m_activeTab = index;
    if (index == 0) refreshRoadmap();
    else refreshGanttChart();
}

// Task id from the row selected in the ACTIVE tab — never from a stale
// selection in the hidden table (which previously caused wrong deletions).
int PlanningWidget::selectedTaskId() const {
    const QTableWidget* table = (m_activeTab == 0) ? m_roadmapTable : m_ganttTable;
    const int row = table->currentRow();
    if (row >= 0 && table->item(row, 0)) {
        return table->item(row, 0)->data(Qt::UserRole).toInt();
    }
    return 0;
}

void PlanningWidget::refreshRoadmap() {
    QSqlDatabase db = DatabaseManager::instance().database();
    m_roadmapTable->setRowCount(0);

    // Real calendar months of the current year (JAN..DEC). A task fills every
    // month its [start_date, end_date] span intersects.
    const int year = QDate::currentDate().year();
    QStringList headers = {"Ministry / Team", "Initiative Name"};
    for (int m = 1; m <= 12; ++m) headers << QLocale::c().monthName(m).left(3).toUpper();
    m_roadmapTable->setColumnCount(14);
    m_roadmapTable->setHorizontalHeaderLabels(headers);

    QSqlQuery qFetch(
        "SELECT t.id, COALESCE(g.name, t.team), t.title, t.start_date, t.end_date, "
        "       COALESCE(t.status, 'In-Progress') "
        "FROM ministry_tasks t LEFT JOIN groups g ON g.id = t.team_id "
        "ORDER BY COALESCE(g.name, t.team), t.start_date", db);
    int row = 0;
    while (qFetch.next()) {
        const int id = qFetch.value(0).toInt();
        const QString team = qFetch.value(1).toString();
        const QString title = qFetch.value(2).toString();
        const QDate start = QDate::fromString(qFetch.value(3).toString(), Qt::ISODate);
        const QDate end = QDate::fromString(qFetch.value(4).toString(), Qt::ISODate);
        const QString status = qFetch.value(5).toString();

        m_roadmapTable->insertRow(row);
        QTableWidgetItem* teamItem = new QTableWidgetItem(team);
        teamItem->setData(Qt::UserRole, id);
        m_roadmapTable->setItem(row, 0, teamItem);
        m_roadmapTable->setItem(row, 1, new QTableWidgetItem(title));

        for (int m = 1; m <= 12; ++m) {
            QTableWidgetItem* cell = new QTableWidgetItem();
            if (start.isValid() && end.isValid()) {
                const QDate monthStart(year, m, 1);
                const QDate monthEnd = monthStart.addMonths(1).addDays(-1);
                if (start <= monthEnd && end >= monthStart) {
                    QString badge = (status == "Completed") ? " ✅ " : ((status == "Planned") ? " 📌 " : " ⚡ ");
                    cell->setText(badge + title);
                    QColor bg = (status == "Completed") ? QColor(166, 227, 161)
                              : ((status == "Planned") ? QColor(249, 226, 175) : QColor(137, 180, 250));
                    cell->setBackground(bg);
                    cell->setForeground(QColor(17, 17, 27));
                }
            }
            m_roadmapTable->setItem(row, m + 1, cell);
        }
        row++;
    }
}

void PlanningWidget::refreshGanttChart() {
    QSqlDatabase db = DatabaseManager::instance().database();

    // Column headers are real dates in the rolling window: anchor..anchor+19.
    const QDate anchor = m_anchorDate->date();
    QStringList ganttHeaders = {"Activity / Task", "Owner / Leader"};
    for (int i = 0; i < 20; ++i) ganttHeaders << anchor.addDays(i).toString("M/d");
    m_ganttTable->setHorizontalHeaderLabels(ganttHeaders);

    QSqlQuery qFetch(
        "SELECT t.id, t.title, COALESCE(p.first_name || ' ' || p.last_name, t.owner), "
        "       t.start_date, t.end_date, COALESCE(t.status, 'In-Progress') "
        "FROM ministry_tasks t LEFT JOIN people p ON p.id = t.owner_id "
        "ORDER BY t.start_date", db);
    m_ganttTable->setRowCount(0);
    int row = 0;
    while (qFetch.next()) {
        const int id = qFetch.value(0).toInt();
        const QString title = qFetch.value(1).toString();
        const QString owner = qFetch.value(2).toString();
        const QDate start = QDate::fromString(qFetch.value(3).toString(), Qt::ISODate);
        const QDate end = QDate::fromString(qFetch.value(4).toString(), Qt::ISODate);
        const QString status = qFetch.value(5).toString();

        m_ganttTable->insertRow(row);
        QTableWidgetItem* titleItem = new QTableWidgetItem(title);
        titleItem->setData(Qt::UserRole, id);
        m_ganttTable->setItem(row, 0, titleItem);
        m_ganttTable->setItem(row, 1, new QTableWidgetItem(QString("%1 [%2]").arg(owner, status)));

        if (start.isValid() && end.isValid() && end >= anchor) {
            int firstDay = qMax(0, anchor.daysTo(start));       // clamp to window
            int lastDay = qMin(19, anchor.daysTo(end));          // clamp to window
            for (int d = firstDay; d <= lastDay; ++d) {
                QTableWidgetItem* cell = new QTableWidgetItem();
                QString mark = (status == "Completed") ? " ✔ " : ((status == "Planned") ? " • " : " ⚡ ");
                cell->setText(mark);
                cell->setTextAlignment(Qt::AlignCenter);
                QColor bg = (status == "Completed") ? QColor(166, 227, 161)
                          : ((status == "Planned") ? QColor(249, 226, 175) : QColor(137, 180, 250));
                cell->setBackground(bg);
                cell->setForeground(QColor(17, 17, 27));
                m_ganttTable->setItem(row, d + 2, cell);
            }
        }
        row++;
    }
}

// Owner combos render "Name (Role)"; the stored text keeps only the clean name.
static QString cleanOwnerName(const QString& text) {
    const int paren = text.lastIndexOf(" (");
    if (paren > 0) return text.left(paren);
    return text;
}

void PlanningWidget::onAddTask() {
    openTaskDialog(0);
}

void PlanningWidget::onEditTask() {
    const int id = selectedTaskId();
    if (id <= 0) {
        QMessageBox::warning(this, "Selection Required", "Please select a task row to edit.");
        return;
    }
    openTaskDialog(id);
}

// Shared add/edit dialog. editId > 0 loads the existing row and updates it
// on accept (the only way to move a task's status forward).
void PlanningWidget::openTaskDialog(int editId) {
    QSqlDatabase db = DatabaseManager::instance().database();

    // Existing values for edit mode.
    QString curTeam, curTitle, curOwner, curStatus;
    QString curStartDate, curEndDate;
    int curTeamId = 0, curOwnerId = 0, curAnchorId = 0;
    if (editId > 0) {
        QSqlQuery qLoad(db);
        qLoad.prepare("SELECT team, title, owner, status, team_id, owner_id, "
                      "start_date, end_date, anchor_event_id FROM ministry_tasks WHERE id = ?");
        qLoad.addBindValue(editId);
        if (qLoad.exec() && qLoad.next()) {
            curTeam = qLoad.value(0).toString();
            curTitle = qLoad.value(1).toString();
            curOwner = qLoad.value(2).toString();
            curStatus = qLoad.value(3).toString();
            curTeamId = qLoad.value(4).toInt();
            curOwnerId = qLoad.value(5).toInt();
            curStartDate = qLoad.value(6).toString();
            curEndDate = qLoad.value(7).toString();
            curAnchorId = qLoad.value(8).toInt();
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(editId > 0 ? "Edit Ministry Task / Initiative" : "Add Ministry Task / Initiative");
    dlg.resize(360, 260);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QComboBox* teamCombo = new QComboBox(&dlg);
    QSqlQuery qGroups("SELECT id, name FROM groups ORDER BY name", db);
    while (qGroups.next()) {
        teamCombo->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    if (teamCombo->count() == 0) {
        teamCombo->addItems({"Worship & Media Team", "Small Groups & Care", "Nursery & Youth", "Facilities & Operations", "Outreach & Evangelism"});
    }
    if (editId > 0) {
        // Prefer the group link; fall back to the stored text (renamed/removed group).
        int idx = curTeamId > 0 ? teamCombo->findData(curTeamId) : -1;
        if (idx < 0) idx = teamCombo->findText(curTeam);
        if (idx >= 0) teamCombo->setCurrentIndex(idx);
        else if (!curTeam.isEmpty()) teamCombo->insertItem(0, curTeam, 0);
    }

    QComboBox* ownerCombo = new QComboBox(&dlg);
    auto populateLeadersForGroup = [ownerCombo, teamCombo, db]() {
        ownerCombo->clear();
        int groupId = teamCombo->currentData().toInt();
        QSqlQuery qLeaders(db);
        if (groupId > 0) {
            qLeaders.prepare(
                "SELECT p.id, p.first_name || ' ' || p.last_name || ' (' || er.relationship_type || ')' "
                "FROM entity_relationships er "
                "JOIN people p ON er.source_id = p.id "
                "WHERE er.target_type = 'Group' AND er.target_id = ? "
                "ORDER BY (er.relationship_type LIKE '%Leader%') DESC, p.last_name");
            qLeaders.addBindValue(groupId);
        } else {
            qLeaders.prepare("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name");
        }
        if (qLeaders.exec()) {
            while (qLeaders.next()) {
                ownerCombo->addItem(qLeaders.value(1).toString(), qLeaders.value(0).toInt());
            }
        }
        if (ownerCombo->count() == 0) {
            QSqlQuery qAll("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name", db);
            while (qAll.next()) {
                ownerCombo->addItem(qAll.value(1).toString(), qAll.value(0).toInt());
            }
        }
    };
    QObject::connect(teamCombo, &QComboBox::currentIndexChanged, &dlg, populateLeadersForGroup);
    populateLeadersForGroup();
    if (editId > 0 && !curOwner.isEmpty()) {
        // Prefer the person link; fall back to text (with legacy "(Role)" suffix handled).
        int idx = curOwnerId > 0 ? ownerCombo->findData(curOwnerId) : -1;
        if (idx < 0) idx = ownerCombo->findText(curOwner);
        if (idx < 0) idx = ownerCombo->findText(cleanOwnerName(curOwner));
        if (idx >= 0) ownerCombo->setCurrentIndex(idx);
        else ownerCombo->insertItem(0, curOwner, 0);
    }

    QLineEdit* titleEdit = new QLineEdit(curTitle, &dlg);
    titleEdit->setPlaceholderText("e.g. Easter Production, AV Upgrade");

    // Real calendar dates (the source of truth for both views).
    QDate startDate = QDate::fromString(curStartDate, Qt::ISODate);
    if (!startDate.isValid()) startDate = QDate::currentDate();
    QDate endDate = QDate::fromString(curEndDate, Qt::ISODate);
    if (!endDate.isValid() || endDate < startDate) endDate = startDate.addDays(7);

    QDateEdit* startDateEdit = new QDateEdit(startDate, &dlg);
    startDateEdit->setCalendarPopup(true);
    startDateEdit->setDisplayFormat("yyyy-MM-dd");
    QDateEdit* endDateEdit = new QDateEdit(endDate, &dlg);
    endDateEdit->setCalendarPopup(true);
    endDateEdit->setDisplayFormat("yyyy-MM-dd");

    // Optional anchor: link the task to a dated service (Service Builder
    // integration: prep tasks surface next to the service they prepare for).
    QComboBox* anchorCombo = new QComboBox(&dlg);
    anchorCombo->addItem("-- None --", 0);
    QSqlQuery qServices(
        "SELECT id, date || ' — ' || COALESCE(NULLIF(title, ''), 'Untitled Service') "
        "FROM services ORDER BY date", db);
    while (qServices.next()) {
        anchorCombo->addItem(qServices.value(1).toString(), qServices.value(0).toInt());
    }
    int anchorIdx = anchorCombo->findData(curAnchorId);
    if (anchorIdx >= 0) anchorCombo->setCurrentIndex(anchorIdx);

    QComboBox* statusCombo = new QComboBox(&dlg);
    statusCombo->addItems({"Planned", "In-Progress", "Completed"});
    statusCombo->setCurrentText(curStatus.isEmpty() ? "In-Progress" : curStatus);

    form->addRow("Select Ministry / Group:", teamCombo);
    form->addRow("Select Leader / Owner:", ownerCombo);
    form->addRow("Initiative / Task Title:", titleEdit);
    form->addRow("Initiative Status:", statusCombo);
    form->addRow("Start Date:", startDateEdit);
    form->addRow("End Date:", endDateEdit);
    form->addRow("Anchor Event (optional):", anchorCombo);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString team = teamCombo->currentText();
        QString title = titleEdit->text().trimmed();
        QString owner = cleanOwnerName(ownerCombo->currentText()); // drop "(Role)" suffix
        QString status = statusCombo->currentText();
        int teamId = teamCombo->currentData().toInt();
        int ownerId = ownerCombo->currentData().toInt();
        int anchorId = anchorCombo->currentData().toInt();

        QDate startDate = startDateEdit->date();
        QDate endDate = endDateEdit->date();
        if (endDate < startDate) std::swap(startDate, endDate);

        if (title.isEmpty() || owner.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Initiative title and owner name are required.");
            return;
        }

        // Legacy grid columns are NOT NULL; write derived values (the views
        // are driven by the real dates now).
        const int sDay = startDate.day();
        const int sMonth = startDate.month();
        const int dDays = qMax(1, startDate.daysTo(endDate));
        const int dMonths = 1;

        QSqlQuery q(db);
        if (editId > 0) {
            q.prepare("UPDATE ministry_tasks SET team = ?, title = ?, owner = ?, status = ?, "
                      "start_day = ?, duration_days = ?, start_month = ?, duration_months = ?, "
                      "team_id = ?, owner_id = ?, start_date = ?, end_date = ?, anchor_event_id = ? "
                      "WHERE id = ?");
        } else {
            q.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                      "start_month, duration_months, team_id, owner_id, start_date, end_date, anchor_event_id) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        }
        q.addBindValue(team);
        q.addBindValue(title);
        q.addBindValue(owner);
        q.addBindValue(status);
        q.addBindValue(sDay);
        q.addBindValue(dDays);
        q.addBindValue(sMonth);
        q.addBindValue(dMonths);
        q.addBindValue(teamId > 0 ? QVariant(teamId) : QVariant(QMetaType(QMetaType::Int)));
        q.addBindValue(ownerId > 0 ? QVariant(ownerId) : QVariant(QMetaType(QMetaType::Int)));
        q.addBindValue(startDate.toString(Qt::ISODate));
        q.addBindValue(endDate.toString(Qt::ISODate));
        q.addBindValue(anchorId > 0 ? QVariant(anchorId) : QVariant(QMetaType(QMetaType::Int)));
        if (editId > 0) q.addBindValue(editId);

        if (q.exec()) {
            refreshRoadmap();
            refreshGanttChart();
            QMessageBox::information(this, editId > 0 ? "Task Updated" : "Task Added",
                                     editId > 0 ? "Ministry task updated (status and timeline changes saved)."
                                                : "Ministry task added to Roadmap & Gantt Timeline!");
        } else {
            QMessageBox::critical(this, "Error", "Failed to save task: " + q.lastError().text());
        }
    }
}

void PlanningWidget::onDeleteTask() {
    int taskId = selectedTaskId();
    if (taskId <= 0) {
        QMessageBox::warning(this, "Selection Required", "Please select a task row to delete.");
        return;
    }

    if (QMessageBox::question(this, "Confirm Delete", "Remove this task from the Ministry Roadmap & Gantt Chart?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("DELETE FROM ministry_tasks WHERE id = ?");
        q.addBindValue(taskId);
        if (q.exec()) {
            refreshRoadmap();
            refreshGanttChart();
        }
    }
}
