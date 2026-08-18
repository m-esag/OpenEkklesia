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
#include "../database/databasemanager.h"

PlanningWidget::PlanningWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshRoadmap();
    refreshGanttChart();
}

PlanningWidget::~PlanningWidget() {}

void PlanningWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(15);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* header = new QLabel("🗓️ Ministry Planning: Roadmap & Gantt Charts", this);
    header->setObjectName("pageHeader");
    headerLayout->addWidget(header);
    headerLayout->addStretch();

    m_btnAddTask = new QPushButton("➕ Add Initiative / Task", this);
    m_btnAddTask->setObjectName("primaryBtn");
    connect(m_btnAddTask, &QPushButton::clicked, this, &PlanningWidget::onAddTask);
    headerLayout->addWidget(m_btnAddTask);

    m_btnDeleteTask = new QPushButton("🗑️ Remove Selected", this);
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
    m_roadmapTable->setColumnCount(7);
    m_roadmapTable->setHorizontalHeaderLabels({"Ministry / Team", "Initiative Name", "JAN", "FEB", "MAR", "APR", "MAY/JUN"});
    m_roadmapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_roadmapTable->setAlternatingRowColors(true);
    roadLayout->addWidget(m_roadmapTable);
    tabs->addTab(roadmapTab, "🗺️ Ministry Product Roadmap");

    // 2. Gantt Chart View (20-Day Detailed Task Timeline Matrix)
    QWidget* ganttTab = new QWidget(this);
    QVBoxLayout* ganttLayout = new QVBoxLayout(ganttTab);
    m_ganttTable = new QTableWidget(this);
    m_ganttTable->setColumnCount(22);
    QStringList ganttHeaders = {"Activity / Task", "Owner / Leader"};
    for (int i = 1; i <= 20; ++i) {
        ganttHeaders.append(QString("D%1").arg(i));
    }
    m_ganttTable->setHorizontalHeaderLabels(ganttHeaders);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int i = 2; i < 22; ++i) {
        m_ganttTable->setColumnWidth(i, 35);
    }
    m_ganttTable->setAlternatingRowColors(true);
    ganttLayout->addWidget(m_ganttTable);
    tabs->addTab(ganttTab, "📈 Event Project Gantt Chart");

    connect(tabs, &QTabWidget::currentChanged, this, &PlanningWidget::onTabChanged);

    mainLayout->addWidget(tabs);
}

void PlanningWidget::onTabChanged(int index) {
    if (index == 0) refreshRoadmap();
    else refreshGanttChart();
}

void PlanningWidget::refreshRoadmap() {
    QSqlDatabase db = DatabaseManager::instance().database();
    m_roadmapTable->setRowCount(0);
    QSqlQuery qFetch("SELECT id, team, title, start_month, duration_months, COALESCE(status, 'In-Progress') FROM ministry_tasks ORDER BY team, start_month", db);
    int row = 0;
    while (qFetch.next()) {
        int id = qFetch.value(0).toInt();
        QString team = qFetch.value(1).toString();
        QString title = qFetch.value(2).toString();
        int startM = qFetch.value(3).toInt();
        int durM = qFetch.value(4).toInt();
        QString status = qFetch.value(5).toString();

        m_roadmapTable->insertRow(row);
        QTableWidgetItem* teamItem = new QTableWidgetItem(team);
        teamItem->setData(Qt::UserRole, id);
        m_roadmapTable->setItem(row, 0, teamItem);
        m_roadmapTable->setItem(row, 1, new QTableWidgetItem(title));

        for (int m = 1; m <= 5; ++m) {
            QTableWidgetItem* cell = new QTableWidgetItem();
            if (m >= startM && m < startM + durM) {
                QString badge = (status == "Completed") ? " ✅ " : ((status == "Planned") ? " 📌 " : " ⚡ ");
                cell->setText(badge + title);
                QColor bg = (status == "Completed") ? QColor(166, 227, 161) : ((status == "Planned") ? QColor(249, 226, 175) : QColor(137, 180, 250));
                cell->setBackground(bg);
                cell->setForeground(QColor(17, 17, 27));
            }
            m_roadmapTable->setItem(row, m + 1, cell);
        }
        row++;
    }
}

void PlanningWidget::refreshGanttChart() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery qFetch("SELECT id, title, owner, start_day, duration_days, COALESCE(status, 'In-Progress') FROM ministry_tasks ORDER BY start_day", db);
    m_ganttTable->setRowCount(0);
    int row = 0;
    while (qFetch.next()) {
        int id = qFetch.value(0).toInt();
        QString title = qFetch.value(1).toString();
        QString owner = qFetch.value(2).toString();
        int startD = qFetch.value(3).toInt();
        int durD = qFetch.value(4).toInt();
        QString status = qFetch.value(5).toString();

        m_ganttTable->insertRow(row);
        QTableWidgetItem* titleItem = new QTableWidgetItem(title);
        titleItem->setData(Qt::UserRole, id);
        m_ganttTable->setItem(row, 0, titleItem);
        m_ganttTable->setItem(row, 1, new QTableWidgetItem(QString("%1 [%2]").arg(owner, status)));

        for (int d = 1; d <= 20; ++d) {
            QTableWidgetItem* cell = new QTableWidgetItem();
            if (d >= startD && d < startD + durD) {
                QString mark = (status == "Completed") ? " ✔ " : ((status == "Planned") ? " • " : " ⚡ ");
                cell->setText(mark);
                cell->setTextAlignment(Qt::AlignCenter);
                QColor bg = (status == "Completed") ? QColor(166, 227, 161) : ((status == "Planned") ? QColor(249, 226, 175) : QColor(137, 180, 250));
                cell->setBackground(bg);
                cell->setForeground(QColor(17, 17, 27));
            }
            m_ganttTable->setItem(row, d + 1, cell);
        }
        row++;
    }
}

void PlanningWidget::onAddTask() {
    QDialog dlg(this);
    dlg.setWindowTitle("Add Ministry Task / Initiative");
    dlg.resize(360, 260);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QComboBox* teamCombo = new QComboBox(&dlg);
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery qGroups("SELECT id, name FROM groups ORDER BY name", db);
    while (qGroups.next()) {
        teamCombo->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    if (teamCombo->count() == 0) {
        teamCombo->addItems({"Worship & Media Team", "Small Groups & Care", "Nursery & Youth", "Facilities & Operations", "Outreach & Evangelism"});
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

    QLineEdit* titleEdit = new QLineEdit(&dlg);
    titleEdit->setPlaceholderText("e.g. Easter Production, AV Upgrade");

    QSpinBox* startDaySpin = new QSpinBox(&dlg);
    startDaySpin->setRange(1, 20);
    startDaySpin->setValue(1);

    QSpinBox* durDaysSpin = new QSpinBox(&dlg);
    durDaysSpin->setRange(1, 10);
    durDaysSpin->setValue(3);

    QSpinBox* startMonthSpin = new QSpinBox(&dlg);
    startMonthSpin->setRange(1, 5);
    startMonthSpin->setValue(1);

    QSpinBox* durMonthsSpin = new QSpinBox(&dlg);
    durMonthsSpin->setRange(1, 3);
    durMonthsSpin->setValue(1);

    QComboBox* statusCombo = new QComboBox(&dlg);
    statusCombo->addItems({"Planned", "In-Progress", "Completed"});
    statusCombo->setCurrentText("In-Progress");

    form->addRow("Select Ministry / Group:", teamCombo);
    form->addRow("Select Leader / Owner:", ownerCombo);
    form->addRow("Initiative / Task Title:", titleEdit);
    form->addRow("Initiative Status:", statusCombo);
    form->addRow("Gantt Start Day (1-20):", startDaySpin);
    form->addRow("Gantt Duration (Days):", durDaysSpin);
    form->addRow("Roadmap Start Month (1-5):", startMonthSpin);
    form->addRow("Roadmap Duration (Months):", durMonthsSpin);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString team = teamCombo->currentText();
        QString title = titleEdit->text().trimmed();
        QString owner = ownerCombo->currentText();
        QString status = statusCombo->currentText();
        int sDay = startDaySpin->value();
        int dDays = durDaysSpin->value();
        int sMonth = startMonthSpin->value();
        int dMonths = durMonthsSpin->value();

        if (title.isEmpty() || owner.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Initiative title and owner name are required.");
            return;
        }

        QSqlQuery q(db);
        q.prepare("INSERT INTO ministry_tasks (team, title, owner, start_day, duration_days, start_month, duration_months, status) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        q.addBindValue(team);
        q.addBindValue(title);
        q.addBindValue(owner);
        q.addBindValue(sDay);
        q.addBindValue(dDays);
        q.addBindValue(sMonth);
        q.addBindValue(dMonths);
        q.addBindValue(status);

        if (q.exec()) {
            refreshRoadmap();
            refreshGanttChart();
            QMessageBox::information(this, "Task Added", "Ministry task added to Roadmap & Gantt Timeline!");
        } else {
            QMessageBox::critical(this, "Error", "Failed to add task: " + q.lastError().text());
        }
    }
}

void PlanningWidget::onDeleteTask() {
    int row = m_ganttTable->currentRow();
    int taskId = 0;
    if (row >= 0 && m_ganttTable->item(row, 0)) {
        taskId = m_ganttTable->item(row, 0)->data(Qt::UserRole).toInt();
    } else {
        int rRow = m_roadmapTable->currentRow();
        if (rRow >= 0 && m_roadmapTable->item(rRow, 0)) {
            taskId = m_roadmapTable->item(rRow, 0)->data(Qt::UserRole).toInt();
        }
    }

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
