#include "planningwidget.h"
#include "../../common/tableutil.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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
#include <QSlider>
#include <QTextEdit>
#include <QCheckBox>
#include <QScrollArea>
#include <QFrame>
#include <QLocale>
#include <QMenu>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTimeEdit>
#include <QProgressBar>
#include <QToolButton>
#include <QClipboard>
#include <QGuiApplication>
#include <QCursor>
#include <QDrag>
#include <QMimeData>
#include <QDropEvent>
#include "../database/databasemanager.h"
#include "../core/thememanager.h"
#include "../core/iconmanager.h"

namespace {
// Status chip colors follow the active theme instead of hardcoded Catppuccin
// Mocha pastels, which clashed on light brands and drifted from the design
// language of the other module tables. Pair with ThemeManager::textOn() for
// the foreground so contrast holds on every theme.
QColor statusChipColor(const QString& status) {
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    if (status == "Completed") return tok.accentSuccess;
    if (status == "Planned") return tok.accentWarning;
    return tok.accentPrimary;
}
} // namespace

// Owner combos render "Name (Role)"; the stored text keeps only the clean name.
static QString cleanOwnerName(const QString& text) {
    const int paren = text.lastIndexOf(" (");
    if (paren > 0) return text.left(paren);
    return text;
}

PlanningWidget::PlanningWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshAll();
    applyRole("admin"); // default: full access until MainWindow applies the real role
}

PlanningWidget::~PlanningWidget() {}

void PlanningWidget::applyRole(const QString& role) {
    m_userRole = role;
    m_readOnly = (role.compare("readonly", Qt::CaseInsensitive) == 0 ||
                  role.compare("guest", Qt::CaseInsensitive) == 0);
    const bool canEdit = !m_readOnly;
    if (m_btnAddTask) m_btnAddTask->setEnabled(canEdit);
    if (m_btnEditTask) m_btnEditTask->setEnabled(canEdit);
    if (m_btnDeleteTask) m_btnDeleteTask->setEnabled(canEdit);
    if (m_btnAddEvent) m_btnAddEvent->setEnabled(canEdit);
    if (m_btnEditEvent) m_btnEditEvent->setEnabled(canEdit);
    if (m_btnDeleteEvent) m_btnDeleteEvent->setEnabled(canEdit);

    const QString tip = m_readOnly
        ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
        : "";
    for (QPushButton* b : {m_btnAddTask, m_btnEditTask, m_btnDeleteTask, m_btnAddEvent, m_btnEditEvent, m_btnDeleteEvent}) {
        if (b) b->setToolTip(tip);
    }
    // Kanban quick-add buttons create tasks directly; they are not part of the
    // header button set above, so gate them explicitly.
    for (QToolButton* b : findChildren<QToolButton*>(QStringLiteral("kanbanQuickAdd"))) {
        b->setEnabled(canEdit);
        if (m_readOnly) {
            b->setToolTip(tip);
        } else if (QWidget* frame = b->parentWidget()) {
            QString st = frame->objectName();
            if (st.startsWith("kanbanCol_")) st = st.mid(10);
            b->setToolTip("Add task directly into " + st);
        }
    }
    // Kanban cards are rebuilt from the database, so rebuild them now to give
    // their stage-move arrows the new role's enabled state.
    if (!m_kanbanColumns.isEmpty()) refreshKanban();
}

void PlanningWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 24);
    mainLayout->setSpacing(14);

    // 1. Header & Action Ribbon
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);

    QLabel* header = new QLabel("Ministry Planning & Strategic Execution", this);
    header->setObjectName("pageHeader");
    headerLayout->addWidget(header);
    headerLayout->addStretch();

    m_btnAddTask = new QPushButton(" Add Initiative / Task", this);
    m_btnAddTask->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnAddTask->setObjectName("primaryBtn");
    connect(m_btnAddTask, &QPushButton::clicked, this, &PlanningWidget::onAddTask);
    headerLayout->addWidget(m_btnAddTask);

    m_btnAddEvent = new QPushButton(" Add Church Event", this);
    m_btnAddEvent->setIcon(IconManager::instance().getIcon("calendar", ThemeManager::instance().currentTokens().accentPrimary, 14));
    m_btnAddEvent->setObjectName("secondaryBtn");
    connect(m_btnAddEvent, &QPushButton::clicked, this, &PlanningWidget::onAddEvent);
    headerLayout->addWidget(m_btnAddEvent);

    m_btnEditTask = new QPushButton(" Edit Task", this);
    m_btnEditTask->setIcon(IconManager::instance().getIcon("edit-3", QColor(), 14));
    m_btnEditTask->setObjectName("secondaryBtn");
    m_btnEditTask->setToolTip("Edit selected initiative or task");
    connect(m_btnEditTask, &QPushButton::clicked, this, &PlanningWidget::onEditTask);
    headerLayout->addWidget(m_btnEditTask);

    m_btnDeleteTask = new QPushButton(" Remove", this);
    m_btnDeleteTask->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDeleteTask->setObjectName("dangerBtn");
    connect(m_btnDeleteTask, &QPushButton::clicked, this, &PlanningWidget::onDeleteTask);
    headerLayout->addWidget(m_btnDeleteTask);

    m_btnExport = new QPushButton(" Export Schedule", this);
    m_btnExport->setIcon(IconManager::instance().getIcon("download", QColor(), 14));
    m_btnExport->setObjectName("secondaryBtn");
    connect(m_btnExport, &QPushButton::clicked, this, &PlanningWidget::onExportSchedule);
    headerLayout->addWidget(m_btnExport);

    mainLayout->addLayout(headerLayout);

    // 2. KPI Summary Ribbon
    setupKPIBar(mainLayout);

    // 3. Tabbed Multi-Framework Planning Container
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("planningTabWidget");

    setupRoadmapTab(m_tabWidget);
    setupEventsTab(m_tabWidget);
    setupKanbanTab(m_tabWidget);
    setupGanttTab(m_tabWidget);
    setupGoalsTab(m_tabWidget);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &PlanningWidget::onTabChanged);
    mainLayout->addWidget(m_tabWidget);
}

void PlanningWidget::setupKPIBar(QVBoxLayout* parentLayout) {
    QFrame* kpiFrame = new QFrame(this);
    kpiFrame->setObjectName("planningKPIFrame");
    kpiFrame->setStyleSheet(
        "QFrame#planningKPIFrame { background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; padding: 6px; }");
    QHBoxLayout* kpiLayout = new QHBoxLayout(kpiFrame);
    kpiLayout->setContentsMargins(12, 6, 12, 6);
    kpiLayout->setSpacing(20);

    auto createKPICard = [this, kpiLayout](const QString& title, QLabel*& valLabel, const QString& iconName, const QColor& iconColor) {
        QHBoxLayout* card = new QHBoxLayout();
        card->setSpacing(8);
        QLabel* iconLbl = new QLabel(this);
        iconLbl->setPixmap(IconManager::instance().getPixmap(iconName, iconColor, 20));
        card->addWidget(iconLbl);

        QVBoxLayout* textCol = new QVBoxLayout();
        textCol->setSpacing(0);
        valLabel = new QLabel("0", this);
        valLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: palette(text);");
        QLabel* titleLbl = new QLabel(title, this);
        titleLbl->setStyleSheet("font-size: 11px; color: palette(placeholder-text);");
        textCol->addWidget(valLabel);
        textCol->addWidget(titleLbl);
        card->addLayout(textCol);

        kpiLayout->addLayout(card);
    };

    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    createKPICard("Total Initiatives", m_lblTotalTasks, "layers", tok.accentPrimary);
    createKPICard("In-Progress Tasks", m_lblActiveTasks, "clock", tok.accentWarning);
    createKPICard("Upcoming Events (30d)", m_lblUpcomingEvents, "calendar", tok.accentPrimary);
    createKPICard("High Priority", m_lblUrgentTasks, "alert-triangle", tok.accentDanger);
    createKPICard("Completion Rate", m_lblCompletionRate, "check-circle", tok.accentSuccess);
    kpiLayout->addStretch();

    parentLayout->addWidget(kpiFrame);
}

// -----------------------------------------------------------------------------
// Tab 0: Strategic Roadmap
// -----------------------------------------------------------------------------
void PlanningWidget::setupRoadmapTab(QTabWidget* tabs) {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(10);

    // Filter toolbar
    QHBoxLayout* bar = new QHBoxLayout();
    bar->setSpacing(8);

    QLabel* lblH = new QLabel("Horizon:", tab);
    m_horizonCombo = new QComboBox(tab);
    m_horizonCombo->addItems({"Annual (Jan - Dec)", "Quarterly (Q1 - Q4)", "Milestones & Critical Path"});
    connect(m_horizonCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshRoadmap(); });
    bar->addWidget(lblH);
    bar->addWidget(m_horizonCombo);

    QLabel* lblT = new QLabel("Ministry:", tab);
    m_roadmapTeamFilter = new QComboBox(tab);
    m_roadmapTeamFilter->addItem("All Ministries", 0);
    QSqlQuery qGroups("SELECT id, name FROM groups WHERE is_archived = 0 ORDER BY name", DatabaseManager::instance().database());
    while (qGroups.next()) {
        m_roadmapTeamFilter->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    connect(m_roadmapTeamFilter, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshRoadmap(); });
    bar->addWidget(lblT);
    bar->addWidget(m_roadmapTeamFilter);

    QLabel* lblP = new QLabel("Priority:", tab);
    m_roadmapPriorityFilter = new QComboBox(tab);
    m_roadmapPriorityFilter->addItems({"All Priorities", "Urgent", "High", "Medium", "Low"});
    connect(m_roadmapPriorityFilter, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshRoadmap(); });
    bar->addWidget(lblP);
    bar->addWidget(m_roadmapPriorityFilter);

    QLabel* lblS = new QLabel("Status:", tab);
    m_roadmapStatusFilter = new QComboBox(tab);
    m_roadmapStatusFilter->addItems({"All Statuses", "Planned", "In-Progress", "Under Review", "Completed"});
    connect(m_roadmapStatusFilter, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshRoadmap(); });
    bar->addWidget(lblS);
    bar->addWidget(m_roadmapStatusFilter);

    m_roadmapSearch = new QLineEdit(tab);
    m_roadmapSearch->setPlaceholderText("Search initiative, team, owner...");
    connect(m_roadmapSearch, &QLineEdit::textChanged, this, [this](const QString&) { refreshRoadmap(); });
    bar->addWidget(m_roadmapSearch, 1);

    lay->addLayout(bar);

    // Roadmap Table (preserves planningRoadmapTable object name for tests)
    m_roadmapTable = new QTableWidget(tab);
    m_roadmapTable->setObjectName("planningRoadmapTable");
    m_roadmapTable->setAlternatingRowColors(true);
    m_roadmapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_roadmapTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roadmapTable->setContextMenuPolicy(Qt::CustomContextMenu);
    // App-wide table conventions (column sizing honoring the user preference).
    // Sorting stays off: the quarter columns are positional, not sortable data.
    TableUtil::applyStandardTableSetup(m_roadmapTable, false);

    connect(m_roadmapTable, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        if (m_readOnly) return;
        if (r < 0 || !m_roadmapTable->item(r, 0)) return;
        int taskId = m_roadmapTable->item(r, 0)->data(Qt::UserRole).toInt();
        if (taskId > 0) openTaskDialog(taskId);
    });

    connect(m_roadmapTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int r = m_roadmapTable->rowAt(pos.y());
        if (r < 0 || !m_roadmapTable->item(r, 0)) return;
        m_roadmapTable->selectRow(r);
        int taskId = m_roadmapTable->item(r, 0)->data(Qt::UserRole).toInt();
        if (taskId <= 0) return;

        const ThemeTokens tok = ThemeManager::instance().currentTokens();
        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Initiative Details...");
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Initiative Details");
        menu.addSeparator();
        QAction* actCompleted = menu.addAction(IconManager::instance().getIcon("check-circle-2", tok.accentSuccess, 14), "Mark Status as Completed");
        QAction* actInProgress = menu.addAction(IconManager::instance().getIcon("clock", tok.accentWarning, 14), "Mark Status as In-Progress");
        QAction* actUnderReview = menu.addAction(IconManager::instance().getIcon("eye", tok.accentPrimary, 14), "Mark Status as Under Review");
        QAction* actPlanned = menu.addAction(IconManager::instance().getIcon("calendar", QColor(), 14), "Mark Status as Planned");
        menu.addSeparator();
        QAction* actSnooze = menu.addAction(IconManager::instance().getIcon("calendar-plus", QColor(), 14), "Extend Due Date (+7 Days)");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Initiative");

        if (isReadOnly) {
            actEdit->setEnabled(false);
            actEdit->setToolTip(roTooltip);
            actCompleted->setEnabled(false);
            actCompleted->setToolTip(roTooltip);
            actInProgress->setEnabled(false);
            actInProgress->setToolTip(roTooltip);
            actUnderReview->setEnabled(false);
            actUnderReview->setToolTip(roTooltip);
            actPlanned->setEnabled(false);
            actPlanned->setToolTip(roTooltip);
            actSnooze->setEnabled(false);
            actSnooze->setToolTip(roTooltip);
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_roadmapTable->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QString rowText;
            for (int c = 0; c < m_roadmapTable->columnCount(); ++c) {
                if (m_roadmapTable->item(r, c)) {
                    if (!rowText.isEmpty()) rowText += " | ";
                    rowText += m_roadmapTable->item(r, c)->text();
                }
            }
            QGuiApplication::clipboard()->setText(rowText);
        } else if (sel == actEdit && !isReadOnly) openTaskDialog(taskId);
        else if (sel == actCompleted && !isReadOnly) moveTaskStage(taskId, "Completed");
        else if (sel == actInProgress && !isReadOnly) moveTaskStage(taskId, "In-Progress");
        else if (sel == actUnderReview && !isReadOnly) moveTaskStage(taskId, "Under Review");
        else if (sel == actPlanned && !isReadOnly) moveTaskStage(taskId, "Planned");
        else if (sel == actSnooze && !isReadOnly) {
            QSqlQuery q(DatabaseManager::instance().database());
            q.prepare("SELECT end_date FROM ministry_tasks WHERE id = ?");
            q.addBindValue(taskId);
            if (q.exec() && q.next()) {
                QDate curEnd = QDate::fromString(q.value(0).toString(), Qt::ISODate);
                if (!curEnd.isValid()) curEnd = QDate::currentDate();
                QSqlQuery uq(DatabaseManager::instance().database());
                uq.prepare("UPDATE ministry_tasks SET end_date = ? WHERE id = ?");
                uq.addBindValue(curEnd.addDays(7).toString(Qt::ISODate));
                uq.addBindValue(taskId);
                uq.exec();
                refreshAll();
            }
        } else if (sel == actDelete && !isReadOnly) {
            onDeleteTask();
        }
    });

    lay->addWidget(m_roadmapTable);
    tabs->addTab(tab, "Strategic Roadmap");
    tabs->setTabIcon(0, IconManager::instance().getIcon("map", ThemeManager::instance().currentTokens().accentPrimary, 14));
}

// -----------------------------------------------------------------------------
// Tab 1: Church Events Master Hub
// -----------------------------------------------------------------------------
void PlanningWidget::setupEventsTab(QTabWidget* tabs) {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(10);

    QHBoxLayout* bar = new QHBoxLayout();
    bar->setSpacing(8);

    QLabel* lblT = new QLabel("Event Type:", tab);
    m_eventTypeFilter = new QComboBox(tab);
    m_eventTypeFilter->addItems({"All Event Types", "Worship / Service", "Outreach", "Youth / Children", "Conference / Retreat", "Training", "Fellowship"});
    connect(m_eventTypeFilter, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshEvents(); });
    bar->addWidget(lblT);
    bar->addWidget(m_eventTypeFilter);

    m_eventSearch = new QLineEdit(tab);
    m_eventSearch->setPlaceholderText("Search events by name, location, coordinator...");
    connect(m_eventSearch, &QLineEdit::textChanged, this, [this](const QString&) { refreshEvents(); });
    bar->addWidget(m_eventSearch, 1);

    m_btnEditEvent = new QPushButton(" Edit Event", tab);
    m_btnEditEvent->setIcon(IconManager::instance().getIcon("edit-3", QColor(), 14));
    m_btnEditEvent->setObjectName("secondaryBtn");
    connect(m_btnEditEvent, &QPushButton::clicked, this, &PlanningWidget::onEditEvent);
    bar->addWidget(m_btnEditEvent);

    m_btnDeleteEvent = new QPushButton(" Remove Event", tab);
    m_btnDeleteEvent->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDeleteEvent->setObjectName("dangerBtn");
    connect(m_btnDeleteEvent, &QPushButton::clicked, this, &PlanningWidget::onDeleteEvent);
    bar->addWidget(m_btnDeleteEvent);

    lay->addLayout(bar);

    m_eventsTable = new QTableWidget(tab);
    m_eventsTable->setObjectName("planningEventsTable");
    m_eventsTable->setColumnCount(9);
    m_eventsTable->setHorizontalHeaderLabels({"Date & Time", "Event Title", "Type", "Host Ministry", "Coordinator", "Facility / Room", "Attendance", "Budget", "Status"});
    m_eventsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_eventsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    TableUtil::applyStandardTableSetup(m_eventsTable, false); // respects the auto-size preference; overrides Interactive/Stretch above
    m_eventsTable->setAlternatingRowColors(true);
    m_eventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_eventsTable->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_eventsTable, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        if (m_readOnly) return;
        if (r < 0 || !m_eventsTable->item(r, 0)) return;
        int evId = m_eventsTable->item(r, 0)->data(Qt::UserRole).toInt();
        if (evId > 0) openEventDialog(evId);
    });

    connect(m_eventsTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int r = m_eventsTable->rowAt(pos.y());
        if (r < 0 || !m_eventsTable->item(r, 0)) return;
        m_eventsTable->selectRow(r);
        int evId = m_eventsTable->item(r, 0)->data(Qt::UserRole).toInt();
        if (evId <= 0) return;

        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Event...");
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Event Info");
        menu.addSeparator();
        QAction* actConfirm = menu.addAction(IconManager::instance().getIcon("check-circle-2", QColor(), 14), "Mark Status as Confirmed");
        QAction* actComplete = menu.addAction(IconManager::instance().getIcon("check", QColor(), 14), "Mark Status as Completed");
        QAction* actCancel = menu.addAction(IconManager::instance().getIcon("x-circle", QColor(), 14), "Mark Status as Cancelled");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Event");

        if (isReadOnly) {
            actEdit->setEnabled(false);
            actEdit->setToolTip(roTooltip);
            actConfirm->setEnabled(false);
            actConfirm->setToolTip(roTooltip);
            actComplete->setEnabled(false);
            actComplete->setToolTip(roTooltip);
            actCancel->setEnabled(false);
            actCancel->setToolTip(roTooltip);
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_eventsTable->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QString summary;
            for (int c = 0; c < m_eventsTable->columnCount(); ++c) {
                if (m_eventsTable->item(r, c)) {
                    if (!summary.isEmpty()) summary += " | ";
                    summary += m_eventsTable->item(r, c)->text();
                }
            }
            QGuiApplication::clipboard()->setText(summary);
        } else if (sel == actEdit && !isReadOnly) openEventDialog(evId);
        else if ((sel == actConfirm || sel == actComplete || sel == actCancel) && !isReadOnly) {
            QString st = (sel == actConfirm) ? "Confirmed" : ((sel == actComplete) ? "Completed" : "Cancelled");
            QSqlQuery uq(DatabaseManager::instance().database());
            uq.prepare("UPDATE ministry_events SET status = ? WHERE id = ?");
            uq.addBindValue(st);
            uq.addBindValue(evId);
            uq.exec();
            refreshEvents();
            refreshKPIs();
        } else if (sel == actDelete && !isReadOnly) {
            onDeleteEvent();
        }
    });

    lay->addWidget(m_eventsTable);
    tabs->addTab(tab, "Church Events Hub");
    tabs->setTabIcon(1, IconManager::instance().getIcon("calendar", ThemeManager::instance().currentTokens().accentPrimary, 14));
}

// -----------------------------------------------------------------------------
// Tab 2: Agile Ministry Kanban Board
// -----------------------------------------------------------------------------
void PlanningWidget::setupKanbanTab(QTabWidget* tabs) {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(10);

    QScrollArea* scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_kanbanContainer = new QWidget(scroll);
    m_kanbanContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); // scroll instead of inflating the tab/window minimum
    QHBoxLayout* boardLayout = new QHBoxLayout(m_kanbanContainer);
    boardLayout->setContentsMargins(0, 0, 0, 0);
    boardLayout->setSpacing(12);

    const QStringList stages = {"Backlog", "Planned", "In-Progress", "Under Review", "Completed"};
    const ThemeTokens tok = ThemeManager::instance().currentTokens();

    for (const QString& st : stages) {
        QFrame* colFrame = new QFrame(m_kanbanContainer);
        colFrame->setObjectName("kanbanCol_" + st);
        colFrame->setStyleSheet(
            "QFrame { background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; }");
        QVBoxLayout* colLay = new QVBoxLayout(colFrame);
        colLay->setContentsMargins(8, 8, 8, 8);
        colLay->setSpacing(8);

        // Header with stage name, counter badge, and quick add
        QHBoxLayout* colHeader = new QHBoxLayout();
        colHeader->setSpacing(6);
        QLabel* titleLbl = new QLabel(st, colFrame);
        titleLbl->setStyleSheet("font-size: 13px; font-weight: bold; color: palette(text);");
        colHeader->addWidget(titleLbl);

        QLabel* cntBadge = new QLabel("0", colFrame);
        cntBadge->setStyleSheet("background-color: palette(mid); color: palette(text); border-radius: 9px; padding: 2px 7px; font-size: 11px; font-weight: bold;");
        m_kanbanCounters[st] = cntBadge;
        colHeader->addWidget(cntBadge);
        colHeader->addStretch();

        QToolButton* btnQuickAdd = new QToolButton(colFrame);
        btnQuickAdd->setObjectName("kanbanQuickAdd");
        btnQuickAdd->setIcon(IconManager::instance().getIcon("plus", tok.accentPrimary, 12));
        btnQuickAdd->setToolTip("Add task directly into " + st);
        connect(btnQuickAdd, &QToolButton::clicked, this, [this, st]() {
            openTaskDialog(0, st);
        });
        colHeader->addWidget(btnQuickAdd);

        colLay->addLayout(colHeader);

        // Scrollable card list
        QScrollArea* cardScroll = new QScrollArea(colFrame);
        cardScroll->setWidgetResizable(true);
        cardScroll->setFrameShape(QFrame::NoFrame);
        QWidget* cardContainer = new QWidget(cardScroll);
        QVBoxLayout* cardLay = new QVBoxLayout(cardContainer);
        cardLay->setContentsMargins(2, 2, 2, 2);
        cardLay->setSpacing(8);
        cardLay->addStretch();
        // Drop target for kanban drag & drop. Cards themselves do not accept
        // drops, so a card dragged over another card resolves to this
        // container (Qt walks up to the nearest accepting ancestor).
        cardContainer->setProperty("kanbanStage", st);
        cardContainer->setAcceptDrops(true);
        cardContainer->installEventFilter(this);
        cardScroll->setWidget(cardContainer);

        m_kanbanColumns[st] = cardLay;
        colLay->addWidget(cardScroll, 1);
        boardLayout->addWidget(colFrame, 1);
    }

    scroll->setWidget(m_kanbanContainer);
    lay->addWidget(scroll);

    tabs->addTab(tab, "Agile Kanban Board");
    tabs->setTabIcon(2, IconManager::instance().getIcon("columns", ThemeManager::instance().currentTokens().accentPrimary, 14));
}

// -----------------------------------------------------------------------------
// Tab 3: Project Gantt Timeline
// -----------------------------------------------------------------------------
void PlanningWidget::setupGanttTab(QTabWidget* tabs) {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(10);

    QHBoxLayout* bar = new QHBoxLayout();
    bar->setSpacing(8);

    QLabel* lblAnchor = new QLabel("Timeline Window Starting:", tab);
    m_anchorDate = new QDateEdit(QDate::currentDate(), tab);
    m_anchorDate->setObjectName("planningAnchorDate");
    m_anchorDate->setCalendarPopup(true);
    m_anchorDate->setDisplayFormat("yyyy-MM-dd");
    connect(m_anchorDate, &QDateEdit::dateChanged, this, [this](const QDate&) { refreshGanttChart(); });
    bar->addWidget(lblAnchor);
    bar->addWidget(m_anchorDate);

    QPushButton* btnToday = new QPushButton("Today", tab);
    btnToday->setObjectName("secondaryBtn");
    connect(btnToday, &QPushButton::clicked, this, [this]() {
        m_anchorDate->setDate(QDate::currentDate());
    });
    bar->addWidget(btnToday);

    QLabel* lblSpan = new QLabel("Span:", tab);
    m_ganttSpanCombo = new QComboBox(tab);
    m_ganttSpanCombo->addItem("20-Day Window", 20);
    m_ganttSpanCombo->addItem("14-Day Sprint", 14);
    m_ganttSpanCombo->addItem("30-Day Month", 30);
    m_ganttSpanCombo->addItem("60-Day Horizon", 60);
    connect(m_ganttSpanCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshGanttChart(); });
    bar->addWidget(lblSpan);
    bar->addWidget(m_ganttSpanCombo);

    QLabel* lblTeam = new QLabel("Ministry:", tab);
    m_ganttTeamFilter = new QComboBox(tab);
    m_ganttTeamFilter->addItem("All Ministries", 0);
    QSqlQuery qGroups("SELECT id, name FROM groups WHERE is_archived = 0 ORDER BY name", DatabaseManager::instance().database());
    while (qGroups.next()) {
        m_ganttTeamFilter->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    connect(m_ganttTeamFilter, QOverload<int>::of(&QComboBox::activated), this, [this](int) { refreshGanttChart(); });
    bar->addWidget(lblTeam);
    bar->addWidget(m_ganttTeamFilter);
    bar->addStretch();

    lay->addLayout(bar);

    m_ganttTable = new QTableWidget(tab);
    m_ganttTable->setObjectName("planningGanttTable");
    m_ganttTable->setAlternatingRowColors(true);
    m_ganttTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ganttTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ganttTable->setContextMenuPolicy(Qt::CustomContextMenu);
    TableUtil::applyStandardTableSetup(m_ganttTable, false); // positional columns, not sortable

    connect(m_ganttTable, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        if (m_readOnly) return;
        if (r < 0 || !m_ganttTable->item(r, 0)) return;
        int taskId = m_ganttTable->item(r, 0)->data(Qt::UserRole).toInt();
        if (taskId > 0) openTaskDialog(taskId);
    });

    connect(m_ganttTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int r = m_ganttTable->rowAt(pos.y());
        if (r < 0 || !m_ganttTable->item(r, 0)) return;
        m_ganttTable->selectRow(r);
        int taskId = m_ganttTable->item(r, 0)->data(Qt::UserRole).toInt();
        if (taskId <= 0) return;

        const ThemeTokens tok = ThemeManager::instance().currentTokens();
        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Initiative Details...");
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Initiative Title");

        QMenu* mStatus = menu.addMenu(IconManager::instance().getIcon("check-circle-2", tok.accentSuccess, 14), "Set Status");
        QAction* sDone = mStatus->addAction("Mark Completed");
        QAction* sProg = mStatus->addAction("Mark In-Progress");
        QAction* sRev = mStatus->addAction("Mark Under Review");
        QAction* sPlan = mStatus->addAction("Mark Planned");

        QAction* actExtend = menu.addAction(IconManager::instance().getIcon("calendar-plus", QColor(), 14), "Extend Due Date (+7 Days)");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Initiative");

        if (isReadOnly) {
            actEdit->setEnabled(false);
            actEdit->setToolTip(roTooltip);
            mStatus->setEnabled(false);
            mStatus->setToolTip(roTooltip);
            actExtend->setEnabled(false);
            actExtend->setToolTip(roTooltip);
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_ganttTable->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QGuiApplication::clipboard()->setText(m_ganttTable->item(r, 0)->text());
        } else if (sel == actEdit && !isReadOnly) {
            openTaskDialog(taskId);
        } else if (sel == sDone && !isReadOnly) {
            moveTaskStage(taskId, "Completed");
        } else if (sel == sProg && !isReadOnly) {
            moveTaskStage(taskId, "In-Progress");
        } else if (sel == sRev && !isReadOnly) {
            moveTaskStage(taskId, "Under Review");
        } else if (sel == sPlan && !isReadOnly) {
            moveTaskStage(taskId, "Planned");
        } else if (sel == actExtend && !isReadOnly) {
            QSqlQuery q(DatabaseManager::instance().database());
            q.prepare("SELECT end_date FROM ministry_tasks WHERE id = ?");
            q.addBindValue(taskId);
            if (q.exec() && q.next()) {
                QDate curEnd = QDate::fromString(q.value(0).toString(), Qt::ISODate);
                if (!curEnd.isValid()) curEnd = QDate::currentDate();
                QSqlQuery uq(DatabaseManager::instance().database());
                uq.prepare("UPDATE ministry_tasks SET end_date = ? WHERE id = ?");
                uq.addBindValue(curEnd.addDays(7).toString(Qt::ISODate));
                uq.addBindValue(taskId);
                uq.exec();
                refreshAll();
            }
        } else if (sel == actDelete && !isReadOnly) {
            if (QMessageBox::question(this, "Confirm Delete", "Remove this initiative from the plan?") == QMessageBox::Yes) {
                QSqlQuery d(DatabaseManager::instance().database());
                d.prepare("DELETE FROM ministry_tasks WHERE id = ?");
                d.addBindValue(taskId);
                d.exec();
                refreshAll();
            }
        }
    });

    lay->addWidget(m_ganttTable);
    tabs->addTab(tab, "Event Project Gantt");
    tabs->setTabIcon(3, IconManager::instance().getIcon("bar-chart-2", ThemeManager::instance().currentTokens().accentPrimary, 14));
}

// -----------------------------------------------------------------------------
// Tab 4: Strategic Goals & OKRs Framework
// -----------------------------------------------------------------------------
void PlanningWidget::setupGoalsTab(QTabWidget* tabs) {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(tab);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(12);

    QLabel* intro = new QLabel("Church Strategic Vision Pillars & Objectives (OKRs)", tab);
    intro->setStyleSheet("font-size: 14px; font-weight: bold; color: palette(text);");
    lay->addWidget(intro);

    QScrollArea* scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_goalsContainer = new QWidget(scroll);
    m_goalsContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); // scroll instead of inflating the tab/window minimum
    m_goalsLayout = new QVBoxLayout(m_goalsContainer);
    m_goalsLayout->setContentsMargins(0, 0, 0, 0);
    m_goalsLayout->setSpacing(14);
    m_goalsLayout->addStretch();

    scroll->setWidget(m_goalsContainer);
    lay->addWidget(scroll);

    tabs->addTab(tab, "Ministry OKRs & Goals");
    tabs->setTabIcon(4, IconManager::instance().getIcon("target", ThemeManager::instance().currentTokens().accentPrimary, 14));
}

void PlanningWidget::onTabChanged(int index) {
    m_activeTab = index;
    if (index == 0) refreshRoadmap();
    else if (index == 1) refreshEvents();
    else if (index == 2) refreshKanban();
    else if (index == 3) refreshGanttChart();
    else if (index == 4) refreshGoals();
}

void PlanningWidget::refreshAll() {
    refreshKPIs();
    refreshRoadmap();
    refreshEvents();
    refreshKanban();
    refreshGanttChart();
    refreshGoals();
}

void PlanningWidget::refreshKPIs() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);

    int totalTasks = 0;
    int activeTasks = 0;
    int urgentTasks = 0;
    int completedTasks = 0;
    if (q.exec("SELECT COUNT(*), "
               "       SUM(CASE WHEN status = 'In-Progress' THEN 1 ELSE 0 END), "
               "       SUM(CASE WHEN priority = 'Urgent' OR priority = 'High' THEN 1 ELSE 0 END), "
               "       SUM(CASE WHEN status = 'Completed' THEN 1 ELSE 0 END) "
               "FROM ministry_tasks") && q.next()) {
        totalTasks = q.value(0).toInt();
        activeTasks = q.value(1).toInt();
        urgentTasks = q.value(2).toInt();
        completedTasks = q.value(3).toInt();
    }

    int upcomingEvents = 0;
    if (q.exec("SELECT COUNT(*) FROM ministry_events "
               "WHERE start_datetime >= datetime('now') AND start_datetime <= datetime('now', '+30 days')") && q.next()) {
        upcomingEvents = q.value(0).toInt();
    }

    if (m_lblTotalTasks) m_lblTotalTasks->setText(QString::number(totalTasks));
    if (m_lblActiveTasks) m_lblActiveTasks->setText(QString::number(activeTasks));
    if (m_lblUpcomingEvents) m_lblUpcomingEvents->setText(QString::number(upcomingEvents));
    if (m_lblUrgentTasks) m_lblUrgentTasks->setText(QString::number(urgentTasks));

    int rate = (totalTasks > 0) ? qRound(100.0 * completedTasks / totalTasks) : 100;
    if (m_lblCompletionRate) m_lblCompletionRate->setText(QString("%1%").arg(rate));
}

// -----------------------------------------------------------------------------
// Refresh Implementation: Strategic Roadmap
// -----------------------------------------------------------------------------
void PlanningWidget::refreshRoadmap() {
    if (!m_roadmapTable) return;
    QSqlDatabase db = DatabaseManager::instance().database();
    m_roadmapTable->setRowCount(0);

    const int horizon = m_horizonCombo ? m_horizonCombo->currentIndex() : 0;
    const int filterTeamId = m_roadmapTeamFilter ? m_roadmapTeamFilter->currentData().toInt() : 0;
    const QString filterPriority = m_roadmapPriorityFilter ? m_roadmapPriorityFilter->currentText() : "All Priorities";
    const QString filterStatus = m_roadmapStatusFilter ? m_roadmapStatusFilter->currentText() : "All Statuses";
    const QString searchText = m_roadmapSearch ? m_roadmapSearch->text().trimmed() : "";

    // Bind values (not string interpolation) so user-controlled filters can never
    // become SQL. Priority/status/search text are all externally influenced.
    QStringList conditions;
    QList<QVariant> bindValues;
    if (filterTeamId > 0) {
        conditions << "t.team_id = ?";
        bindValues << filterTeamId;
    }
    if (filterPriority != "All Priorities") {
        conditions << "t.priority = ?";
        bindValues << filterPriority;
    }
    if (filterStatus != "All Statuses") {
        conditions << "t.status = ?";
        bindValues << filterStatus;
    }
    if (!searchText.isEmpty()) {
        conditions << "(t.title LIKE ? OR t.team LIKE ? OR t.owner LIKE ? OR g.name LIKE ?)";
        const QString pat = "%" + searchText + "%";
        for (int i = 0; i < 4; ++i) bindValues << pat;
    }
    if (horizon == 2) { // Milestones only
        conditions << "(t.category = 'Milestone' OR t.priority = 'Urgent')";
    }

    QString whereSql = conditions.isEmpty() ? "" : ("WHERE " + conditions.join(" AND "));

    const int year = QDate::currentDate().year();

    if (horizon == 0) { // Annual View: 12 months JAN..DEC (preserves exact backward-compatible columns for tests)
        QStringList headers = {"Ministry / Team", "Initiative Name"};
        for (int m = 1; m <= 12; ++m) headers << QLocale::c().monthName(m).left(3).toUpper();
        m_roadmapTable->setColumnCount(14);
        m_roadmapTable->setHorizontalHeaderLabels(headers);
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

        QSqlQuery qFetch(db);
        qFetch.prepare(QString("SELECT t.id, COALESCE(g.name, t.team), t.title, t.start_date, t.end_date, "
                              "       COALESCE(t.status, 'In-Progress'), COALESCE(t.priority, 'Medium') "
                              "FROM ministry_tasks t LEFT JOIN groups g ON g.id = t.team_id "
                              "%1 "
                              "ORDER BY COALESCE(g.name, t.team), t.start_date").arg(whereSql));
        for (const QVariant &bv : std::as_const(bindValues)) qFetch.addBindValue(bv);
        qFetch.exec();

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
                        cell->setText(title);
                        const QColor bg = statusChipColor(status);
                        cell->setBackground(bg);
                        cell->setForeground(ThemeManager::textOn(bg));
                    }
                }
                m_roadmapTable->setItem(row, m + 1, cell);
            }
            row++;
        }
    } else if (horizon == 1) { // Quarterly Horizons: Q1, Q2, Q3, Q4
        m_roadmapTable->setColumnCount(6);
        m_roadmapTable->setHorizontalHeaderLabels({"Ministry / Team", "Initiative Name", "Q1 (Jan-Mar)", "Q2 (Apr-Jun)", "Q3 (Jul-Sep)", "Q4 (Oct-Dec)"});
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

        QSqlQuery qFetch(db);
        qFetch.prepare(QString("SELECT t.id, COALESCE(g.name, t.team), t.title, t.start_date, t.end_date, "
                              "       COALESCE(t.status, 'In-Progress') "
                              "FROM ministry_tasks t LEFT JOIN groups g ON g.id = t.team_id "
                              "%1 "
                              "ORDER BY COALESCE(g.name, t.team), t.start_date").arg(whereSql));
        for (const QVariant &bv : std::as_const(bindValues)) qFetch.addBindValue(bv);
        qFetch.exec();

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

            for (int q = 1; q <= 4; ++q) {
                QTableWidgetItem* cell = new QTableWidgetItem();
                if (start.isValid() && end.isValid()) {
                    const QDate qStart(year, (q - 1) * 3 + 1, 1);
                    const QDate qEnd = qStart.addMonths(3).addDays(-1);
                    if (start <= qEnd && end >= qStart) {
                        cell->setText(title + " [" + status + "]");
                        const QColor bg = statusChipColor(status);
                        cell->setBackground(bg);
                        cell->setForeground(ThemeManager::textOn(bg));
                    }
                }
                m_roadmapTable->setItem(row, q + 1, cell);
            }
            row++;
        }
    } else { // Milestones & Critical Path
        m_roadmapTable->setColumnCount(7);
        m_roadmapTable->setHorizontalHeaderLabels({"Initiative / Milestone", "Ministry / Team", "Target Due Date", "Lead Owner", "Priority", "Progress", "Status"});
        m_roadmapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        QSqlQuery qFetch(db);
        qFetch.prepare(QString("SELECT t.id, t.title, COALESCE(g.name, t.team), t.end_date, "
                              "       COALESCE(p.first_name || ' ' || p.last_name, t.owner), "
                              "       COALESCE(t.priority, 'Medium'), COALESCE(t.progress_pct, 0), "
                              "       COALESCE(t.status, 'In-Progress') "
                              "FROM ministry_tasks t "
                              "LEFT JOIN groups g ON g.id = t.team_id "
                              "LEFT JOIN people p ON p.id = t.owner_id "
                              "%1 ORDER BY t.end_date ASC").arg(whereSql));
        for (const QVariant &bv : std::as_const(bindValues)) qFetch.addBindValue(bv);
        qFetch.exec();

        int row = 0;
        while (qFetch.next()) {
            const int id = qFetch.value(0).toInt();
            const QString title = qFetch.value(1).toString();
            const QString team = qFetch.value(2).toString();
            const QString dueDate = qFetch.value(3).toString();
            const QString owner = qFetch.value(4).toString();
            const QString priority = qFetch.value(5).toString();
            const int pct = qFetch.value(6).toInt();
            const QString status = qFetch.value(7).toString();

            m_roadmapTable->insertRow(row);
            QTableWidgetItem* titleItem = new QTableWidgetItem(title);
            titleItem->setData(Qt::UserRole, id);
            m_roadmapTable->setItem(row, 0, titleItem);
            m_roadmapTable->setItem(row, 1, new QTableWidgetItem(team));
            m_roadmapTable->setItem(row, 2, new QTableWidgetItem(dueDate.isEmpty() ? "TBD" : dueDate));
            m_roadmapTable->setItem(row, 3, new QTableWidgetItem(owner));
            m_roadmapTable->setItem(row, 4, new QTableWidgetItem(priority));
            m_roadmapTable->setItem(row, 5, new QTableWidgetItem(QString("%1%").arg(pct)));
            m_roadmapTable->setItem(row, 6, new QTableWidgetItem(status));
            row++;
        }
    }
}

// -----------------------------------------------------------------------------
// Refresh Implementation: Church Events Master Hub
// -----------------------------------------------------------------------------
void PlanningWidget::refreshEvents() {
    if (!m_eventsTable) return;
    QSqlDatabase db = DatabaseManager::instance().database();
    m_eventsTable->setRowCount(0);

    const QString typeFilter = m_eventTypeFilter ? m_eventTypeFilter->currentText() : "All Event Types";
    const QString search = m_eventSearch ? m_eventSearch->text().trimmed() : "";

    QStringList conditions;
    if (typeFilter != "All Event Types") conditions << QString("e.event_type = '%1'").arg(typeFilter);
    if (!search.isEmpty()) {
        QString esc = search;
        esc.replace("'", "''");
        conditions << QString("(e.title LIKE '%%1%' OR e.facility_name LIKE '%%1%' OR g.name LIKE '%%1%' OR p.first_name LIKE '%%1%' OR p.last_name LIKE '%%1%')").arg(esc);
    }
    QString whereSql = conditions.isEmpty() ? "" : ("WHERE " + conditions.join(" AND "));

    QSqlQuery q(
        QString("SELECT e.id, e.start_datetime, e.title, e.event_type, "
                "       COALESCE(g.name, 'All Church'), "
                "       COALESCE(p.first_name || ' ' || p.last_name, 'Unassigned'), "
                "       e.facility_name, e.expected_attendance, e.budget_cents, e.status "
                "FROM ministry_events e "
                "LEFT JOIN groups g ON g.id = e.team_id "
                "LEFT JOIN people p ON p.id = e.coordinator_id "
                "%1 ORDER BY e.start_datetime ASC").arg(whereSql), db);

    int row = 0;
    while (q.next()) {
        const int id = q.value(0).toInt();
        const QString dt = q.value(1).toString();
        const QString title = q.value(2).toString();
        const QString type = q.value(3).toString();
        const QString team = q.value(4).toString();
        const QString coordinator = q.value(5).toString();
        const QString facility = q.value(6).toString();
        const int att = q.value(7).toInt();
        const int budget = q.value(8).toInt();
        const QString status = q.value(9).toString();

        m_eventsTable->insertRow(row);
        QTableWidgetItem* dtItem = new QTableWidgetItem(dt);
        dtItem->setData(Qt::UserRole, id);
        m_eventsTable->setItem(row, 0, dtItem);
        m_eventsTable->setItem(row, 1, new QTableWidgetItem(title));
        m_eventsTable->setItem(row, 2, new QTableWidgetItem(type));
        m_eventsTable->setItem(row, 3, new QTableWidgetItem(team));
        m_eventsTable->setItem(row, 4, new QTableWidgetItem(coordinator));
        m_eventsTable->setItem(row, 5, new QTableWidgetItem(facility.isEmpty() ? "Main Campus" : facility));
        m_eventsTable->setItem(row, 6, new QTableWidgetItem(att > 0 ? QString("%1 ppl").arg(att) : "—"));
        m_eventsTable->setItem(row, 7, new QTableWidgetItem(budget > 0 ? QString("$%1").arg(budget / 100.0, 0, 'f', 2) : "—"));
        m_eventsTable->setItem(row, 8, new QTableWidgetItem(status));
        row++;
    }
}

// -----------------------------------------------------------------------------
// Refresh Implementation: Agile Kanban Board
// -----------------------------------------------------------------------------
void PlanningWidget::refreshKanban() {
    const QStringList stages = {"Backlog", "Planned", "In-Progress", "Under Review", "Completed"};
    for (const QString& st : stages) {
        if (!m_kanbanColumns.contains(st)) continue;
        QVBoxLayout* lay = m_kanbanColumns[st];
        // Clear old card widgets (leaving spacer). deleteLater, not delete:
        // a refresh can be triggered while a drag that started on one of
        // these cards is still unwinding on the stack.
        while (lay->count() > 1) {
            QLayoutItem* item = lay->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(
        "SELECT t.id, t.title, COALESCE(g.name, t.team), "
        "       COALESCE(p.first_name || ' ' || p.last_name, t.owner), "
        "       COALESCE(t.status, 'In-Progress'), COALESCE(t.priority, 'Medium'), "
        "       COALESCE(t.category, 'Task'), COALESCE(t.progress_pct, 0), t.end_date "
        "FROM ministry_tasks t "
        "LEFT JOIN groups g ON g.id = t.team_id "
        "LEFT JOIN people p ON p.id = t.owner_id "
        "ORDER BY t.priority = 'Urgent' DESC, t.priority = 'High' DESC, t.end_date ASC", db);

    QMap<QString, int> stageCounts;
    for (const QString& st : stages) stageCounts[st] = 0;

    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    const QDate today = QDate::currentDate();

    while (q.next()) {
        const int id = q.value(0).toInt();
        const QString title = q.value(1).toString();
        const QString team = q.value(2).toString();
        const QString owner = q.value(3).toString();
        QString status = q.value(4).toString();
        if (!stages.contains(status)) status = "In-Progress";
        const QString priority = q.value(5).toString();
        const QString category = q.value(6).toString();
        const int progress = q.value(7).toInt();
        const QDate dueDate = QDate::fromString(q.value(8).toString(), Qt::ISODate);

        stageCounts[status]++;

        if (!m_kanbanColumns.contains(status)) continue;
        QVBoxLayout* colLay = m_kanbanColumns[status];

        // Create Kanban Card
        QFrame* card = new QFrame(m_kanbanContainer);
        card->setObjectName("kanbanCard");
        card->setStyleSheet(
            "QFrame#kanbanCard { background-color: palette(window); border: 1px solid palette(mid); border-radius: 6px; padding: 8px; }"
            "QFrame#kanbanCard:hover { border: 1px solid palette(highlight); }");
        // Drag & drop source identity (consumed by the shared eventFilter):
        // dragging a card between stage columns moves the task's status.
        card->setProperty("taskId", id);
        card->setProperty("taskStatus", status);
        card->setCursor(Qt::SizeAllCursor);
        card->installEventFilter(this);

        QVBoxLayout* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(4, 4, 4, 4);
        cardLay->setSpacing(5);

        // Header: Category & Priority
        QHBoxLayout* topRow = new QHBoxLayout();
        topRow->setSpacing(4);
        QLabel* catPill = new QLabel(category, card);
        catPill->setStyleSheet("font-size: 10px; color: palette(placeholder-text); text-transform: uppercase; font-weight: bold;");
        topRow->addWidget(catPill);
        topRow->addStretch();

        QColor prioCol = (priority == "Urgent") ? tok.accentDanger : ((priority == "High") ? tok.accentWarning : tok.textMuted);
        QLabel* prioPill = new QLabel(priority, card);
        prioPill->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: bold;").arg(prioCol.name()));
        topRow->addWidget(prioPill);
        cardLay->addLayout(topRow);

        // Title (double click card to edit)
        QLabel* titleLbl = new QLabel(title, card);
        titleLbl->setWordWrap(true);
        titleLbl->setStyleSheet("font-size: 12px; font-weight: bold; color: palette(text);");
        cardLay->addWidget(titleLbl);

        // Team & Assignee
        QLabel* teamLbl = new QLabel(team + " • " + owner, card);
        teamLbl->setStyleSheet("font-size: 11px; color: palette(placeholder-text);");
        cardLay->addWidget(teamLbl);

        // Progress mini-bar
        if (progress > 0 || status == "In-Progress") {
            QProgressBar* bar = new QProgressBar(card);
            bar->setRange(0, 100);
            bar->setValue(progress);
            bar->setTextVisible(false);
            bar->setFixedHeight(4);
            cardLay->addWidget(bar);
        }

        // Bottom row: Due Date pill & Move controls
        QHBoxLayout* botRow = new QHBoxLayout();
        botRow->setSpacing(4);

        if (dueDate.isValid()) {
            QLabel* duePill = new QLabel(dueDate.toString("M/d"), card);
            if (status != "Completed" && dueDate < today) {
                duePill->setText("⚠ Overdue " + dueDate.toString("M/d"));
                duePill->setStyleSheet("background-color: " + tok.accentDanger.name() + "; color: #ffffff; border-radius: 4px; padding: 1px 4px; font-size: 10px; font-weight: bold;");
            } else if (status != "Completed" && today.daysTo(dueDate) <= 3) {
                duePill->setText("⏳ " + dueDate.toString("M/d"));
                duePill->setStyleSheet("background-color: " + tok.accentWarning.name() + "; color: #111111; border-radius: 4px; padding: 1px 4px; font-size: 10px; font-weight: bold;");
            } else {
                duePill->setStyleSheet("color: palette(placeholder-text); font-size: 10px;");
            }
            botRow->addWidget(duePill);
        }
        botRow->addStretch();

        // Stage movement buttons
        int stageIdx = stages.indexOf(status);
        if (stageIdx > 0) {
            QToolButton* btnLeft = new QToolButton(card);
            btnLeft->setText("◀");
            btnLeft->setEnabled(!m_readOnly);
            btnLeft->setToolTip(m_readOnly
                ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
                : "Move to " + stages[stageIdx - 1]);
            connect(btnLeft, &QToolButton::clicked, this, [this, id, stages, stageIdx]() {
                moveTaskStage(id, stages[stageIdx - 1]);
            });
            botRow->addWidget(btnLeft);
        }
        if (stageIdx < stages.size() - 1) {
            QToolButton* btnRight = new QToolButton(card);
            btnRight->setText("▶");
            btnRight->setEnabled(!m_readOnly);
            btnRight->setToolTip(m_readOnly
                ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
                : "Move to " + stages[stageIdx + 1]);
            connect(btnRight, &QToolButton::clicked, this, [this, id, stages, stageIdx]() {
                moveTaskStage(id, stages[stageIdx + 1]);
            });
            botRow->addWidget(btnRight);
        }

        cardLay->addLayout(botRow);

        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QFrame::customContextMenuRequested, this, [this, id, title, team, owner, status, stages](const QPoint&) {
            const ThemeTokens tok = ThemeManager::instance().currentTokens();
            const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
            const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

            QMenu menu(this);
            QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Initiative Details...");
            QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Title & Assignee");

            QMenu* mMove = menu.addMenu(IconManager::instance().getIcon("arrow-right", tok.accentPrimary, 14), "Move to Stage");
            for (const QString& s : stages) {
                QAction* a = mMove->addAction(s);
                if (s == status) a->setEnabled(false);
            }

            menu.addSeparator();
            QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Initiative");

            if (isReadOnly) {
                actEdit->setEnabled(false);
                actEdit->setToolTip(roTooltip);
                mMove->setEnabled(false);
                mMove->setToolTip(roTooltip);
                actDelete->setEnabled(false);
                actDelete->setToolTip(roTooltip);
            }

            QAction* sel = menu.exec(QCursor::pos());
            if (!sel) return;

            if (sel == actEdit && !isReadOnly) {
                openTaskDialog(id);
            } else if (sel == actCopy) {
                QGuiApplication::clipboard()->setText(QString("%1 (%2 - %3)").arg(title, team, owner));
            } else if (sel == actDelete && !isReadOnly) {
                if (QMessageBox::question(this, "Confirm Delete", "Remove this initiative?") == QMessageBox::Yes) {
                    QSqlQuery d(DatabaseManager::instance().database());
                    d.prepare("DELETE FROM ministry_tasks WHERE id = ?");
                    d.addBindValue(id);
                    d.exec();
                    refreshKanban(); refreshRoadmap(); refreshGanttChart();
                }
            } else if (mMove->actions().contains(sel) && !isReadOnly) {
                moveTaskStage(id, sel->text());
            }
        });

        colLay->insertWidget(colLay->count() - 1, card);
    }

    for (const QString& st : stages) {
        if (m_kanbanCounters.contains(st)) {
            m_kanbanCounters[st]->setText(QString::number(stageCounts[st]));
        }
    }
}

// -----------------------------------------------------------------------------
// Refresh Implementation: Project Gantt Timeline
// -----------------------------------------------------------------------------
void PlanningWidget::refreshGanttChart() {
    if (!m_ganttTable || !m_anchorDate) return;
    QSqlDatabase db = DatabaseManager::instance().database();

    const QDate anchor = m_anchorDate->date();
    const int spanDays = m_ganttSpanCombo ? m_ganttSpanCombo->currentData().toInt() : 20;
    const int teamId = m_ganttTeamFilter ? m_ganttTeamFilter->currentData().toInt() : 0;

    QStringList ganttHeaders = {"Activity / Task", "Owner / Leader"};
    for (int i = 0; i < spanDays; ++i) {
        QDate curDay = anchor.addDays(i);
        ganttHeaders << curDay.toString("M/d");
    }

    m_ganttTable->setColumnCount(spanDays + 2);
    m_ganttTable->setHorizontalHeaderLabels(ganttHeaders);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_ganttTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int i = 2; i < spanDays + 2; ++i) {
        m_ganttTable->setColumnWidth(i, 40);
    }

    QList<QVariant> ganttBind;
    QString whereSql;
    if (teamId > 0) {
        whereSql = "WHERE t.team_id = ?";
        ganttBind << teamId;
    }

    QSqlQuery qFetch(db);
    qFetch.prepare(QString("SELECT t.id, t.title, COALESCE(p.first_name || ' ' || p.last_name, t.owner), "
                          "       t.start_date, t.end_date, COALESCE(t.status, 'In-Progress') "
                          "FROM ministry_tasks t LEFT JOIN people p ON p.id = t.owner_id "
                          "%1 ORDER BY t.start_date").arg(whereSql));
    for (const QVariant &bv : std::as_const(ganttBind)) qFetch.addBindValue(bv);
    qFetch.exec();

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
            int lastDay = qMin(spanDays - 1, anchor.daysTo(end)); // clamp to window
            for (int d = firstDay; d <= lastDay; ++d) {
                QTableWidgetItem* cell = new QTableWidgetItem();
                QString mark = (status == "Completed") ? " ✔ " : ((status == "Planned") ? " • " : " ⚡ ");
                cell->setText(mark);
                cell->setTextAlignment(Qt::AlignCenter);
                const QColor bg = statusChipColor(status);
                cell->setBackground(bg);
                cell->setForeground(ThemeManager::textOn(bg));
                cell->setToolTip(QString("%1\n%2 to %3\nStatus: %4").arg(title, start.toString("yyyy-MM-dd"), end.toString("yyyy-MM-dd"), status));
                m_ganttTable->setItem(row, d + 2, cell);
            }
        }
        row++;
    }
}

// -----------------------------------------------------------------------------
// Refresh Implementation: Strategic Goals & OKRs Framework
// -----------------------------------------------------------------------------
void PlanningWidget::refreshGoals() {
    if (!m_goalsLayout) return;

    // Clear existing goal pillar cards
    while (m_goalsLayout->count() > 1) {
        QLayoutItem* item = m_goalsLayout->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }

    struct Pillar {
        const char* name;
        const char* desc;
        const char* icon;
        const char* teamLike;
    };

    const Pillar pillars[] = {
        {"1. Discipleship & Spiritual Formation", "Deepen biblical literacy, small groups connection, and leadership mentoring.", "book-open", "%Group%"},
        {"2. Worship Arts & Sacramental Life", "Cultivate prayerful Sunday gatherings, choir excellence, and run-sheet precision.", "music", "%Worship%"},
        {"3. NextGen & Family Life", "Nurture children and youth through engaging faith milestones and family support.", "smile", "%Youth%"},
        {"4. Compassion, Outreach & Missions", "Mobilize local compassion initiatives, community food relief, and evangelism.", "heart", "%Outreach%"},
        {"5. Operational Stewardship & Facilities", "Faithful stewardship of finances, physical facilities, and digital infrastructure.", "shield", "%Facilities%"}
    };

    QSqlDatabase db = DatabaseManager::instance().database();
    const ThemeTokens tok = ThemeManager::instance().currentTokens();

    for (const auto& p : pillars) {
        QFrame* frame = new QFrame(m_goalsContainer);
        frame->setObjectName("pillarCard");
        frame->setStyleSheet("QFrame#pillarCard { background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; padding: 12px; }");
        QVBoxLayout* pLay = new QVBoxLayout(frame);
        pLay->setSpacing(8);

        // Header: Pillar Title & Progress Bar
        QHBoxLayout* head = new QHBoxLayout();
        head->setSpacing(8);
        QLabel* iconLbl = new QLabel(frame);
        iconLbl->setPixmap(IconManager::instance().getPixmap(p.icon, tok.accentPrimary, 18));
        head->addWidget(iconLbl);

        QLabel* titleLbl = new QLabel(p.name, frame);
        titleLbl->setStyleSheet("font-size: 13px; font-weight: bold; color: palette(text);");
        head->addWidget(titleLbl);
        head->addStretch();

        pLay->addLayout(head);

        QLabel* descLbl = new QLabel(p.desc, frame);
        descLbl->setStyleSheet("font-size: 11px; color: palette(placeholder-text); font-style: italic;");
        pLay->addWidget(descLbl);

        // Query initiatives linked to this pillar
        QSqlQuery q(db);
        q.prepare("SELECT t.id, t.title, t.status, t.progress_pct, t.end_date "
                  "FROM ministry_tasks t LEFT JOIN groups g ON g.id = t.team_id "
                  "WHERE t.team LIKE ? OR g.name LIKE ? ORDER BY t.status = 'Completed' ASC, t.end_date ASC LIMIT 5");
        q.addBindValue(p.teamLike);
        q.addBindValue(p.teamLike);

        int total = 0;
        int completed = 0;
        QVBoxLayout* itemsLay = new QVBoxLayout();
        itemsLay->setSpacing(4);

        if (q.exec()) {
            while (q.next()) {
                total++;
                const QString tTitle = q.value(1).toString();
                const QString tStatus = q.value(2).toString();
                const int tPct = q.value(3).toInt();
                if (tStatus == "Completed" || tPct == 100) completed++;

                QHBoxLayout* row = new QHBoxLayout();
                QLabel* bullet = new QLabel((tStatus == "Completed") ? "• [Done]" : "• [Pending]", frame);
                QLabel* name = new QLabel(tTitle, frame);
                name->setStyleSheet("font-size: 11px; color: palette(text);");
                QLabel* st = new QLabel(QString("[%1]").arg(tStatus), frame);
                st->setStyleSheet("font-size: 10px; color: palette(placeholder-text);");
                row->addWidget(bullet);
                row->addWidget(name, 1);
                row->addWidget(st);
                itemsLay->addLayout(row);
            }
        }

        if (total == 0) {
            QLabel* emptyLbl = new QLabel("No active initiatives currently mapped to this strategic pillar.", frame);
            emptyLbl->setStyleSheet("font-size: 11px; color: palette(placeholder-text);");
            itemsLay->addWidget(emptyLbl);
        }

        // Progress bar for pillar
        int pct = (total > 0) ? qRound(100.0 * completed / total) : 0;
        QHBoxLayout* progRow = new QHBoxLayout();
        QProgressBar* pBar = new QProgressBar(frame);
        pBar->setRange(0, 100);
        pBar->setValue(pct);
        pBar->setTextVisible(false);
        pBar->setFixedHeight(6);
        QLabel* pctLbl = new QLabel(QString("%1% Completed (%2/%3)").arg(pct).arg(completed).arg(total), frame);
        pctLbl->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(text);");
        progRow->addWidget(pBar, 1);
        progRow->addWidget(pctLbl);
        pLay->addLayout(progRow);

        pLay->addLayout(itemsLay);
        m_goalsLayout->insertWidget(m_goalsLayout->count() - 1, frame);
    }
}

// -----------------------------------------------------------------------------
// Dialogs & CRUD Operations
// -----------------------------------------------------------------------------
void PlanningWidget::onAddTask() {
    openTaskDialog(0);
}

void PlanningWidget::onEditTask() {
    const int id = selectedTaskId();
    if (id <= 0) {
        QMessageBox::warning(this, "Selection Required", "Please select an initiative or task to edit.");
        return;
    }
    openTaskDialog(id);
}

void PlanningWidget::onDeleteTask() {
    const int id = selectedTaskId();
    if (id <= 0) {
        QMessageBox::warning(this, "Selection Required", "Please select an initiative or task to delete.");
        return;
    }

    if (QMessageBox::question(this, "Confirm Delete", "Remove this initiative from Ministry Planning?",
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("DELETE FROM ministry_tasks WHERE id = ?");
        q.addBindValue(id);
        if (q.exec()) {
            refreshAll();
        }
    }
}

void PlanningWidget::onAddEvent() {
    openEventDialog(0);
}

void PlanningWidget::onEditEvent() {
    const int id = selectedEventId();
    if (id <= 0) {
        QMessageBox::warning(this, "Selection Required", "Please select an event row to edit.");
        return;
    }
    openEventDialog(id);
}

void PlanningWidget::onDeleteEvent() {
    const int id = selectedEventId();
    if (id <= 0) {
        QMessageBox::warning(this, "Selection Required", "Please select an event row to remove.");
        return;
    }

    if (QMessageBox::question(this, "Confirm Delete", "Remove this church event from the schedule?",
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("DELETE FROM ministry_events WHERE id = ?");
        q.addBindValue(id);
        if (q.exec()) {
            refreshEvents();
            refreshKPIs();
        }
    }
}

void PlanningWidget::onExportSchedule() {
    QString path = QFileDialog::getSaveFileName(this, "Export Ministry Planning Schedule", "ministry_planning_schedule.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Could not open file for writing: " + file.errorString());
        return;
    }

    QTextStream out(&file);
    out << "Type,Title,Ministry,Owner/Coordinator,Start,End,Status,Priority/Attendance,Progress/Budget,Notes\n";

    QSqlDatabase db = DatabaseManager::instance().database();

    // 1. Export Initiatives / Tasks
    QSqlQuery qTasks(
        "SELECT 'Task', t.title, COALESCE(g.name, t.team), "
        "       COALESCE(p.first_name || ' ' || p.last_name, t.owner), "
        "       t.start_date, t.end_date, t.status, t.priority, t.progress_pct, t.notes "
        "FROM ministry_tasks t "
        "LEFT JOIN groups g ON g.id = t.team_id "
        "LEFT JOIN people p ON p.id = t.owner_id ORDER BY t.start_date", db);
    while (qTasks.next()) {
        out << "\"" << qTasks.value(0).toString() << "\","
            << "\"" << qTasks.value(1).toString().replace("\"", "\"\"") << "\","
            << "\"" << qTasks.value(2).toString().replace("\"", "\"\"") << "\","
            << "\"" << qTasks.value(3).toString().replace("\"", "\"\"") << "\","
            << "\"" << qTasks.value(4).toString() << "\","
            << "\"" << qTasks.value(5).toString() << "\","
            << "\"" << qTasks.value(6).toString() << "\","
            << "\"" << qTasks.value(7).toString() << "\","
            << "\"" << qTasks.value(8).toInt() << "%\","
            << "\"" << qTasks.value(9).toString().replace("\"", "\"\"") << "\"\n";
    }

    // 2. Export Events
    QSqlQuery qEvents(
        "SELECT 'Event', e.title, COALESCE(g.name, 'Church-Wide'), "
        "       COALESCE(p.first_name || ' ' || p.last_name, 'Unassigned'), "
        "       e.start_datetime, e.end_datetime, e.status, "
        "       e.expected_attendance || ' ppl', '$' || (e.budget_cents / 100.0), e.description "
        "FROM ministry_events e "
        "LEFT JOIN groups g ON g.id = e.team_id "
        "LEFT JOIN people p ON p.id = e.coordinator_id ORDER BY e.start_datetime", db);
    while (qEvents.next()) {
        out << "\"" << qEvents.value(0).toString() << "\","
            << "\"" << qEvents.value(1).toString().replace("\"", "\"\"") << "\","
            << "\"" << qEvents.value(2).toString().replace("\"", "\"\"") << "\","
            << "\"" << qEvents.value(3).toString().replace("\"", "\"\"") << "\","
            << "\"" << qEvents.value(4).toString() << "\","
            << "\"" << qEvents.value(5).toString() << "\","
            << "\"" << qEvents.value(6).toString() << "\","
            << "\"" << qEvents.value(7).toString() << "\","
            << "\"" << qEvents.value(8).toString() << "\","
            << "\"" << qEvents.value(9).toString().replace("\"", "\"\"") << "\"\n";
    }

    file.close();
    QMessageBox::information(this, "Export Succeeded", "Ministry schedule exported successfully to:\n" + path);
}

void PlanningWidget::moveTaskStage(int taskId, const QString& newStatus) {
    if (m_readOnly) return;
    if (taskId <= 0 || newStatus.isEmpty()) return;
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE ministry_tasks SET status = ?, progress_pct = CASE WHEN ? = 'Completed' THEN 100 ELSE progress_pct END WHERE id = ?");
    q.addBindValue(newStatus);
    q.addBindValue(newStatus);
    q.addBindValue(taskId);
    if (q.exec()) {
        refreshAll();
    }
}

bool PlanningWidget::eventFilter(QObject* watched, QEvent* ev) {
    // Kanban drag & drop. Cards are plain QFrames inside per-stage layouts, so
    // both the drag sources (cards carrying taskId/taskStatus properties) and
    // the drop targets (stage containers carrying kanbanStage) come through
    // this one filter instead of per-card subclasses.
    static const QString kMime = "application/x-openekklesia-kanban-task";

    switch (ev->type()) {
    case QEvent::MouseButtonPress: {
        QWidget* w = static_cast<QWidget*>(watched);
        const int taskId = w->property("taskId").toInt();
        if (taskId > 0 && !m_readOnly) {
            m_dragTaskId = taskId;
            m_dragFromStatus = w->property("taskStatus").toString();
            m_dragStartPos = static_cast<QMouseEvent*>(ev)->pos();
        }
        break;
    }
    case QEvent::MouseMove: {
        QWidget* w = static_cast<QWidget*>(watched);
        if (m_dragTaskId <= 0
            || !(static_cast<QMouseEvent*>(ev)->buttons() & Qt::LeftButton))
            break;
        if ((static_cast<QMouseEvent*>(ev)->pos() - m_dragStartPos).manhattanLength() <= 14)
            break;

        QDrag drag(w);
        QMimeData* mime = new QMimeData;
        mime->setData(kMime, QByteArray::number(m_dragTaskId));
        drag.setMimeData(mime);
        drag.setPixmap(w->grab());
        drag.setHotSpot(QPoint(10, 10));
        const Qt::DropAction action = drag.exec(Qt::MoveAction);

        // The drop event only recorded the intent (m_pendingDrop); the DB
        // write and the board rebuild happen here, AFTER QDrag::exec() has
        // unwound — refreshAll() deletes the card widgets and this dragged
        // card's handler is still on the stack. The queued call fires once
        // the entire mouse-event chain has returned to the event loop.
        const bool moved = (action == Qt::MoveAction)
                           && m_pendingDrop.taskId == m_dragTaskId
                           && !m_pendingDrop.stage.isEmpty()
                           && m_pendingDrop.stage != m_dragFromStatus;
        const int taskId = m_dragTaskId;
        const QString targetStage = moved ? m_pendingDrop.stage : QString();
        m_dragTaskId = 0;
        m_dragFromStatus.clear();
        m_pendingDrop = {};
        if (moved) {
            QMetaObject::invokeMethod(this, [this, taskId, targetStage]() {
                moveTaskStage(taskId, targetStage);
            }, Qt::QueuedConnection);
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        m_dragTaskId = 0;
        m_dragFromStatus.clear();
        m_pendingDrop = {};
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        QWidget* w = static_cast<QWidget*>(watched);
        if (w->property("kanbanStage").toString().isEmpty())
            break;
        auto* de = static_cast<QDragMoveEvent*>(ev);
        if (de->mimeData()->hasFormat(kMime)) {
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        QWidget* w = static_cast<QWidget*>(watched);
        const QString stage = w->property("kanbanStage").toString();
        auto* de = static_cast<QDropEvent*>(ev);
        if (!stage.isEmpty() && de->mimeData()->hasFormat(kMime)) {
            m_pendingDrop.taskId = de->mimeData()->data(kMime).toInt();
            m_pendingDrop.stage = stage;
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, ev);
}

int PlanningWidget::selectedTaskId() const {
    if (m_activeTab == 0 && m_roadmapTable) {
        int r = m_roadmapTable->currentRow();
        if (r >= 0 && m_roadmapTable->item(r, 0)) return m_roadmapTable->item(r, 0)->data(Qt::UserRole).toInt();
    } else if (m_activeTab == 3 && m_ganttTable) {
        int r = m_ganttTable->currentRow();
        if (r >= 0 && m_ganttTable->item(r, 0)) return m_ganttTable->item(r, 0)->data(Qt::UserRole).toInt();
    }
    return 0;
}

int PlanningWidget::selectedEventId() const {
    if (m_eventsTable) {
        int r = m_eventsTable->currentRow();
        if (r >= 0 && m_eventsTable->item(r, 0)) return m_eventsTable->item(r, 0)->data(Qt::UserRole).toInt();
    }
    return 0;
}

void PlanningWidget::openTaskDialog(int editId, const QString& defaultStatus) {
    if (m_readOnly) return;
    QSqlDatabase db = DatabaseManager::instance().database();

    QString curTeam, curTitle, curOwner, curStatus = defaultStatus;
    QString curPriority = "Medium", curCategory = "Initiative", curNotes;
    QString curStartDate, curEndDate;
    int curTeamId = 0, curOwnerId = 0, curAnchorId = 0, curPct = 0;

    if (editId > 0) {
        QSqlQuery q(db);
        q.prepare("SELECT team, title, owner, status, team_id, owner_id, "
                  "       start_date, end_date, anchor_event_id, "
                  "       COALESCE(priority, 'Medium'), COALESCE(category, 'Initiative'), "
                  "       COALESCE(progress_pct, 0), COALESCE(notes, '') "
                  "FROM ministry_tasks WHERE id = ?");
        q.addBindValue(editId);
        if (q.exec() && q.next()) {
            curTeam = q.value(0).toString();
            curTitle = q.value(1).toString();
            curOwner = q.value(2).toString();
            curStatus = q.value(3).toString();
            curTeamId = q.value(4).toInt();
            curOwnerId = q.value(5).toInt();
            curStartDate = q.value(6).toString();
            curEndDate = q.value(7).toString();
            curAnchorId = q.value(8).toInt();
            curPriority = q.value(9).toString();
            curCategory = q.value(10).toString();
            curPct = q.value(11).toInt();
            curNotes = q.value(12).toString();
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(editId > 0 ? "Edit Ministry Initiative / Task" : "Add Ministry Initiative / Task");
    dlg.resize(420, 400);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(10);

    QComboBox* teamCombo = new QComboBox(&dlg);
    QSqlQuery qGroups("SELECT id, name FROM groups WHERE is_archived = 0 ORDER BY name", db);
    while (qGroups.next()) {
        teamCombo->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    if (teamCombo->count() == 0) {
        teamCombo->addItems({"Worship & Media Team", "Small Groups & Care", "Nursery & Youth", "Facilities & Operations", "Outreach & Evangelism"});
    }
    if (editId > 0) {
        int idx = curTeamId > 0 ? teamCombo->findData(curTeamId) : -1;
        if (idx < 0) idx = teamCombo->findText(curTeam);
        if (idx >= 0) teamCombo->setCurrentIndex(idx);
    }

    QComboBox* ownerCombo = new QComboBox(&dlg);
    QCheckBox* chkShowAllMembers = new QCheckBox("Show all church members (Cross-Ministry Delegation)", &dlg);
    chkShowAllMembers->setStyleSheet("font-size: 11px; color: palette(placeholder-text);");

    auto populateLeaders = [ownerCombo, teamCombo, chkShowAllMembers, db]() {
        int previousSelectedId = ownerCombo->currentData().toInt();
        ownerCombo->clear();
        int groupId = teamCombo->currentData().toInt();
        const bool showAll = chkShowAllMembers->isChecked();

        if (groupId > 0 && !showAll) {
            QSqlQuery qLeaders(db);
            qLeaders.prepare(
                "SELECT p.id, p.first_name || ' ' || p.last_name || ' (' || COALESCE(er.relationship_type, 'Member') || ')' "
                "FROM entity_relationships er "
                "JOIN people p ON er.source_id = p.id "
                "WHERE er.target_type = 'Group' AND er.target_id = ? "
                "ORDER BY (er.relationship_type LIKE '%Leader%') DESC, p.last_name, p.first_name");
            qLeaders.addBindValue(groupId);
            if (qLeaders.exec()) {
                while (qLeaders.next()) {
                    ownerCombo->addItem(qLeaders.value(1).toString(), qLeaders.value(0).toInt());
                }
            }
            if (ownerCombo->count() == 0) {
                ownerCombo->addItem("— No members enrolled in this ministry yet —", 0);
            }
        } else {
            QSqlQuery qAll("SELECT id, first_name || ' ' || last_name || ' (' || COALESCE(membership_status, 'Member') || ')' FROM people ORDER BY last_name, first_name", db);
            while (qAll.next()) {
                ownerCombo->addItem(qAll.value(1).toString(), qAll.value(0).toInt());
            }
        }

        if (previousSelectedId > 0) {
            int idx = ownerCombo->findData(previousSelectedId);
            if (idx >= 0) ownerCombo->setCurrentIndex(idx);
        }
    };
    QObject::connect(teamCombo, &QComboBox::currentIndexChanged, &dlg, populateLeaders);
    QObject::connect(chkShowAllMembers, &QCheckBox::toggled, &dlg, populateLeaders);
    populateLeaders();
    if (editId > 0 && !curOwner.isEmpty()) {
        int idx = curOwnerId > 0 ? ownerCombo->findData(curOwnerId) : -1;
        if (idx < 0) idx = ownerCombo->findText(curOwner);
        if (idx < 0) idx = ownerCombo->findText(cleanOwnerName(curOwner));
        if (idx < 0) {
            chkShowAllMembers->setChecked(true);
            idx = curOwnerId > 0 ? ownerCombo->findData(curOwnerId) : -1;
            if (idx < 0) idx = ownerCombo->findText(curOwner);
            if (idx < 0) idx = ownerCombo->findText(cleanOwnerName(curOwner));
        }
        if (idx >= 0) ownerCombo->setCurrentIndex(idx);
    }

    QLineEdit* titleEdit = new QLineEdit(curTitle, &dlg);
    titleEdit->setPlaceholderText("e.g. Easter Production, AV Upgrade");

    QComboBox* catCombo = new QComboBox(&dlg);
    catCombo->addItems({"Initiative", "Event", "Milestone", "Task"});
    catCombo->setCurrentText(curCategory);

    QComboBox* prioCombo = new QComboBox(&dlg);
    prioCombo->addItems({"Urgent", "High", "Medium", "Low"});
    prioCombo->setCurrentText(curPriority);

    QComboBox* statusCombo = new QComboBox(&dlg);
    statusCombo->addItems({"Backlog", "Planned", "In-Progress", "Under Review", "Completed"});
    statusCombo->setCurrentText(curStatus);

    QSlider* pctSlider = new QSlider(Qt::Horizontal, &dlg);
    pctSlider->setRange(0, 100);
    pctSlider->setValue(curPct);
    QLabel* pctLbl = new QLabel(QString("%1%").arg(curPct), &dlg);
    pctLbl->setMinimumWidth(38);
    connect(pctSlider, &QSlider::valueChanged, &dlg, [pctLbl](int v) { pctLbl->setText(QString("%1%").arg(v)); });

    QHBoxLayout* pctRow = new QHBoxLayout();
    pctRow->addWidget(pctSlider);
    pctRow->addWidget(pctLbl);

    QDate startDate = QDate::fromString(curStartDate, Qt::ISODate);
    if (!startDate.isValid()) startDate = QDate::currentDate();
    QDate endDate = QDate::fromString(curEndDate, Qt::ISODate);
    if (!endDate.isValid()) endDate = startDate.addDays(14);

    QDateEdit* startDateEdit = new QDateEdit(startDate, &dlg);
    startDateEdit->setCalendarPopup(true);
    startDateEdit->setDisplayFormat("yyyy-MM-dd");

    QDateEdit* endDateEdit = new QDateEdit(endDate, &dlg);
    endDateEdit->setCalendarPopup(true);
    endDateEdit->setDisplayFormat("yyyy-MM-dd");

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

    QLineEdit* notesEdit = new QLineEdit(curNotes, &dlg);
    notesEdit->setPlaceholderText("Optional notes, links, or dependencies");

    form->addRow("Ministry Team:", teamCombo);
    form->addRow("Owner / Lead:", ownerCombo);
    form->addRow("", chkShowAllMembers);
    form->addRow("Title:", titleEdit);
    form->addRow("Category:", catCombo);
    form->addRow("Priority:", prioCombo);
    form->addRow("Status:", statusCombo);
    form->addRow("Progress:", pctRow);
    form->addRow("Start Date:", startDateEdit);
    form->addRow("Due Date:", endDateEdit);
    form->addRow("Anchor Service:", anchorCombo);
    form->addRow("Notes:", notesEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString team = teamCombo->currentText();
        int teamId = teamCombo->currentData().toInt();
        QString owner = cleanOwnerName(ownerCombo->currentText());
        int ownerId = ownerCombo->currentData().toInt();
        QString title = titleEdit->text().trimmed();
        QString cat = catCombo->currentText();
        QString prio = prioCombo->currentText();
        QString stat = statusCombo->currentText();
        int pct = pctSlider->value();
        QDate sDate = startDateEdit->date();
        QDate eDate = endDateEdit->date();
        if (eDate < sDate) std::swap(sDate, eDate);
        int anchorId = anchorCombo->currentData().toInt();
        QString notes = notesEdit->text().trimmed();

        if (title.isEmpty() || owner.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Title and Owner are required.");
            return;
        }

        const int sDay = sDate.day();
        const int sMonth = sDate.month();
        const int dDays = qMax(1, static_cast<int>(sDate.daysTo(eDate)));
        const int dMonths = 1;

        QSqlQuery q(db);
        if (editId > 0) {
            q.prepare("UPDATE ministry_tasks SET team = ?, title = ?, owner = ?, status = ?, "
                      "start_day = ?, duration_days = ?, start_month = ?, duration_months = ?, "
                      "team_id = ?, owner_id = ?, start_date = ?, end_date = ?, anchor_event_id = ?, "
                      "priority = ?, category = ?, progress_pct = ?, notes = ? "
                      "WHERE id = ?");
        } else {
            q.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                      "start_month, duration_months, team_id, owner_id, start_date, end_date, anchor_event_id, "
                      "priority, category, progress_pct, notes) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        }
        q.addBindValue(team);
        q.addBindValue(title);
        q.addBindValue(owner);
        q.addBindValue(stat);
        q.addBindValue(sDay);
        q.addBindValue(dDays);
        q.addBindValue(sMonth);
        q.addBindValue(dMonths);
        q.addBindValue(teamId > 0 ? QVariant(teamId) : QVariant());
        q.addBindValue(ownerId > 0 ? QVariant(ownerId) : QVariant());
        q.addBindValue(sDate.toString(Qt::ISODate));
        q.addBindValue(eDate.toString(Qt::ISODate));
        q.addBindValue(anchorId > 0 ? QVariant(anchorId) : QVariant());
        q.addBindValue(prio);
        q.addBindValue(cat);
        q.addBindValue(pct);
        q.addBindValue(notes);
        if (editId > 0) q.addBindValue(editId);

        if (q.exec()) {
            refreshAll();
        } else {
            QMessageBox::critical(this, "Save Error", "Failed to save initiative: " + q.lastError().text());
        }
    }
}

void PlanningWidget::openEventDialog(int editId) {
    if (m_readOnly) return;
    QSqlDatabase db = DatabaseManager::instance().database();

    QString curTitle, curType = "Outreach", curFacility, curStatus = "Confirmed", curDesc;
    QString curStart = QDateTime::currentDateTime().addDays(7).toString("yyyy-MM-dd 10:00");
    QString curEnd = QDateTime::currentDateTime().addDays(7).toString("yyyy-MM-dd 13:00");
    int curTeamId = 0, curCoordId = 0, curAtt = 100, curBudget = 0;

    if (editId > 0) {
        QSqlQuery q(db);
        q.prepare("SELECT title, event_type, team_id, coordinator_id, facility_name, "
                  "       start_datetime, end_datetime, expected_attendance, budget_cents, status, description "
                  "FROM ministry_events WHERE id = ?");
        q.addBindValue(editId);
        if (q.exec() && q.next()) {
            curTitle = q.value(0).toString();
            curType = q.value(1).toString();
            curTeamId = q.value(2).toInt();
            curCoordId = q.value(3).toInt();
            curFacility = q.value(4).toString();
            curStart = q.value(5).toString();
            curEnd = q.value(6).toString();
            curAtt = q.value(7).toInt();
            curBudget = q.value(8).toInt();
            curStatus = q.value(9).toString();
            curDesc = q.value(10).toString();
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(editId > 0 ? "Edit Church Event" : "Create New Church Event");
    dlg.resize(420, 420);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(10);

    QLineEdit* titleEdit = new QLineEdit(curTitle, &dlg);
    titleEdit->setPlaceholderText("e.g. Easter Celebration, Fall Festival");

    QComboBox* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({"Worship / Service", "Outreach", "Youth / Children", "Conference / Retreat", "Training", "Fellowship"});
    typeCombo->setCurrentText(curType);

    QComboBox* teamCombo = new QComboBox(&dlg);
    teamCombo->addItem("Church-Wide (General)", 0);
    QSqlQuery qGroups("SELECT id, name FROM groups WHERE is_archived = 0 ORDER BY name", db);
    while (qGroups.next()) {
        teamCombo->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    int teamIdx = teamCombo->findData(curTeamId);
    if (teamIdx >= 0) teamCombo->setCurrentIndex(teamIdx);

    QComboBox* coordCombo = new QComboBox(&dlg);
    QCheckBox* chkShowAllCoords = new QCheckBox("Show all church members (Cross-Ministry Coordinator)", &dlg);
    chkShowAllCoords->setStyleSheet("font-size: 11px; color: palette(placeholder-text);");

    auto populateCoordinators = [coordCombo, teamCombo, chkShowAllCoords, db]() {
        int previousSelectedId = coordCombo->currentData().toInt();
        coordCombo->clear();
        coordCombo->addItem("— Unassigned —", 0);
        int teamId = teamCombo->currentData().toInt();
        const bool showAll = chkShowAllCoords->isChecked();

        if (teamId > 0 && !showAll) {
            QSqlQuery qLeaders(db);
            qLeaders.prepare(
                "SELECT p.id, p.first_name || ' ' || p.last_name || ' (' || COALESCE(er.relationship_type, 'Member') || ')' "
                "FROM entity_relationships er "
                "JOIN people p ON er.source_id = p.id "
                "WHERE er.target_type = 'Group' AND er.target_id = ? "
                "ORDER BY (er.relationship_type LIKE '%Leader%') DESC, p.last_name, p.first_name");
            qLeaders.addBindValue(teamId);
            if (qLeaders.exec()) {
                while (qLeaders.next()) {
                    coordCombo->addItem(qLeaders.value(1).toString(), qLeaders.value(0).toInt());
                }
            }
            if (coordCombo->count() == 1) {
                coordCombo->addItem("— No members enrolled in this ministry yet —", 0);
            }
        } else {
            QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name || ' (' || COALESCE(membership_status, 'Member') || ')' FROM people ORDER BY last_name, first_name", db);
            while (qPeople.next()) {
                coordCombo->addItem(qPeople.value(1).toString(), qPeople.value(0).toInt());
            }
        }

        if (previousSelectedId > 0) {
            int idx = coordCombo->findData(previousSelectedId);
            if (idx >= 0) coordCombo->setCurrentIndex(idx);
        }
    };
    QObject::connect(teamCombo, &QComboBox::currentIndexChanged, &dlg, populateCoordinators);
    QObject::connect(chkShowAllCoords, &QCheckBox::toggled, &dlg, populateCoordinators);
    populateCoordinators();
    if (curCoordId > 0) {
        int cIdx = coordCombo->findData(curCoordId);
        if (cIdx >= 0) coordCombo->setCurrentIndex(cIdx);
        else {
            chkShowAllCoords->setChecked(true);
            cIdx = coordCombo->findData(curCoordId);
            if (cIdx >= 0) coordCombo->setCurrentIndex(cIdx);
        }
    }

    QLineEdit* facilityEdit = new QLineEdit(curFacility, &dlg);
    facilityEdit->setPlaceholderText("e.g. Main Sanctuary, Fellowship Hall");

    QCheckBox* chkBookFacility = new QCheckBox("Auto-create reservation in Facilities Matrix", &dlg);
    chkBookFacility->setChecked(editId == 0);

    QDateTime startDt = QDateTime::fromString(curStart, "yyyy-MM-dd HH:mm");
    if (!startDt.isValid()) startDt = QDateTime::currentDateTime().addDays(7);
    QDateTime endDt = QDateTime::fromString(curEnd, "yyyy-MM-dd HH:mm");
    if (!endDt.isValid()) endDt = startDt.addSecs(3600 * 3);

    QDateTimeEdit* startEdit = new QDateTimeEdit(startDt, &dlg);
    startEdit->setCalendarPopup(true);
    startEdit->setDisplayFormat("yyyy-MM-dd HH:mm");

    QDateTimeEdit* endEdit = new QDateTimeEdit(endDt, &dlg);
    endEdit->setCalendarPopup(true);
    endEdit->setDisplayFormat("yyyy-MM-dd HH:mm");

    QSpinBox* attSpin = new QSpinBox(&dlg);
    attSpin->setRange(0, 10000);
    attSpin->setValue(curAtt);

    QSpinBox* budgetSpin = new QSpinBox(&dlg);
    budgetSpin->setRange(0, 1000000);
    budgetSpin->setPrefix("$");
    budgetSpin->setValue(curBudget / 100);

    QComboBox* statusCombo = new QComboBox(&dlg);
    statusCombo->addItems({"Confirmed", "Draft", "In-Progress", "Completed", "Cancelled"});
    statusCombo->setCurrentText(curStatus);

    QTextEdit* descEdit = new QTextEdit(&dlg);
    descEdit->setPlaceholderText("Description, schedule notes, volunteers needed...");
    descEdit->setPlainText(curDesc);
    descEdit->setFixedHeight(60);

    form->addRow("Event Title:", titleEdit);
    form->addRow("Event Type:", typeCombo);
    form->addRow("Host Ministry:", teamCombo);
    form->addRow("Coordinator:", coordCombo);
    form->addRow("", chkShowAllCoords);
    form->addRow("Facility Location:", facilityEdit);
    form->addRow("", chkBookFacility);
    form->addRow("Start Time:", startEdit);
    form->addRow("End Time:", endEdit);
    form->addRow("Expected Attendance:", attSpin);
    form->addRow("Event Budget:", budgetSpin);
    form->addRow("Status:", statusCombo);
    form->addRow("Description:", descEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString title = titleEdit->text().trimmed();
        if (title.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Event Title is required.");
            return;
        }

        QString type = typeCombo->currentText();
        int teamId = teamCombo->currentData().toInt();
        int coordId = coordCombo->currentData().toInt();
        QString facility = facilityEdit->text().trimmed();
        QString sTime = startEdit->dateTime().toString("yyyy-MM-dd HH:mm");
        QString eTime = endEdit->dateTime().toString("yyyy-MM-dd HH:mm");
        int att = attSpin->value();
        int budget = budgetSpin->value() * 100;
        QString status = statusCombo->currentText();
        QString desc = descEdit->toPlainText().trimmed();

        QSqlQuery q(db);
        if (editId > 0) {
            q.prepare("UPDATE ministry_events SET title = ?, event_type = ?, team_id = ?, coordinator_id = ?, "
                      "       facility_name = ?, start_datetime = ?, end_datetime = ?, expected_attendance = ?, "
                      "       budget_cents = ?, status = ?, description = ? "
                      "WHERE id = ?");
        } else {
            q.prepare("INSERT INTO ministry_events (title, event_type, team_id, coordinator_id, facility_name, "
                      "       start_datetime, end_datetime, expected_attendance, budget_cents, status, description) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        }
        q.addBindValue(title);
        q.addBindValue(type);
        q.addBindValue(teamId > 0 ? QVariant(teamId) : QVariant());
        q.addBindValue(coordId > 0 ? QVariant(coordId) : QVariant());
        q.addBindValue(facility);
        q.addBindValue(sTime);
        q.addBindValue(eTime);
        q.addBindValue(att);
        q.addBindValue(budget);
        q.addBindValue(status);
        q.addBindValue(desc);
        if (editId > 0) q.addBindValue(editId);

        if (startEdit->dateTime() >= endEdit->dateTime()) {
            QMessageBox::warning(this, "Validation Error",
                                 "The event end time must be after its start time.");
            return;
        }

        const bool bookFacility = chkBookFacility->isChecked() && !facility.isEmpty();
        const QString resDate = startEdit->date().toString("yyyy-MM-dd");
        const QString resStart = startEdit->time().toString("HH:mm");
        const QString resEnd = endEdit->time().toString("HH:mm");
        const QString resBy = coordCombo->currentText();

        // Save the event and its facility reservation atomically: a booking
        // clash (or a failed insert) must not leave a half-applied event.
        if (!db.transaction()) {
            QMessageBox::critical(this, "Save Error",
                                  "Failed to save church event: " + db.lastError().text());
            return;
        }
        bool ok = q.exec();
        QString failMsg = q.lastError().text();

        // Editing: remove this event's own previous reservation first, so the
        // room is either re-booked with the new values or released when the
        // booking box is unchecked — never duplicated.
        if (ok && editId > 0 && !curFacility.isEmpty()) {
            QDateTime prevStart = QDateTime::fromString(curStart, "yyyy-MM-dd HH:mm");
            QDateTime prevEnd = QDateTime::fromString(curEnd, "yyyy-MM-dd HH:mm");
            if (!prevStart.isValid()) prevStart = QDateTime::fromString(curStart, Qt::ISODate);
            if (!prevEnd.isValid()) prevEnd = QDateTime::fromString(curEnd, Qt::ISODate);
            QSqlQuery del(db);
            del.prepare("DELETE FROM facility_reservations WHERE facility_name = ? AND event_name = ? "
                        "AND reservation_date = ? AND start_time = ? AND end_time = ?");
            del.addBindValue(curFacility);
            del.addBindValue(curTitle);
            del.addBindValue(prevStart.date().toString("yyyy-MM-dd"));
            del.addBindValue(prevStart.time().toString("HH:mm"));
            del.addBindValue(prevEnd.time().toString("HH:mm"));
            if (!del.exec()) {
                ok = false;
                failMsg = del.lastError().text();
            }
        }

        if (ok && bookFacility) {
            // Same overlap rule the Facilities matrix uses: half-open
            // [start, end) windows on the same facility and date.
            QSqlQuery conflict(db);
            conflict.prepare("SELECT event_name, start_time, end_time FROM facility_reservations "
                             "WHERE facility_name = ? AND reservation_date = ? "
                             "  AND NOT (end_time <= ? OR start_time >= ?)");
            conflict.addBindValue(facility);
            conflict.addBindValue(resDate);
            conflict.addBindValue(resStart);
            conflict.addBindValue(resEnd);
            if (!conflict.exec()) {
                ok = false;
                failMsg = conflict.lastError().text();
            } else if (conflict.next()) {
                ok = false;
                failMsg = QString("'%1' is already reserved in %2 from %3 to %4.")
                              .arg(conflict.value(0).toString(), facility,
                                   conflict.value(1).toString(), conflict.value(2).toString());
            }

            if (ok) {
                QSqlQuery bk(db);
                bk.prepare("INSERT INTO facility_reservations (facility_name, event_name, reservation_date, start_time, end_time, reserved_by) "
                           "VALUES (?, ?, ?, ?, ?, ?)");
                bk.addBindValue(facility);
                bk.addBindValue(title);
                bk.addBindValue(resDate);
                bk.addBindValue(resStart);
                bk.addBindValue(resEnd);
                bk.addBindValue(resBy);
                if (!bk.exec()) {
                    ok = false;
                    failMsg = bk.lastError().text();
                }
            }
        }

        if (ok && !db.commit()) {
            ok = false;
            failMsg = db.lastError().text();
            db.rollback();
        } else if (!ok) {
            db.rollback();
        }

        if (ok) {
            refreshEvents();
            refreshKPIs();
        } else {
            QMessageBox::critical(this, "Save Error", "Failed to save church event: " + failMsg);
        }
    }
}

