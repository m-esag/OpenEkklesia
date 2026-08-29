#ifndef PLANNINGWIDGET_H
#define PLANNINGWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDateEdit>
#include <QLineEdit>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMap>

class PlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlanningWidget(QWidget *parent = nullptr);
    ~PlanningWidget();

    // Role-gated editing: readonly (Guest) can view but cannot add/edit/delete.
    void applyRole(const QString& role);

    // Refresh all sub-frameworks
    void refreshAll();

private slots:
    void onAddTask();
    void onEditTask();
    void onDeleteTask();
    void onAddEvent();
    void onEditEvent();
    void onDeleteEvent();
    void onExportSchedule();
    void onTabChanged(int index);

private:
    void setupUI();
    void setupKPIBar(QVBoxLayout* parentLayout);
    void setupRoadmapTab(QTabWidget* tabs);
    void setupEventsTab(QTabWidget* tabs);
    void setupKanbanTab(QTabWidget* tabs);
    void setupGanttTab(QTabWidget* tabs);
    void setupGoalsTab(QTabWidget* tabs);

    void refreshKPIs();
    void refreshRoadmap();
    void refreshEvents();
    void refreshKanban();
    void refreshGanttChart();
    void refreshGoals();

    void openTaskDialog(int editId, const QString& defaultStatus = "In-Progress");
    void openEventDialog(int editId);
    void moveTaskStage(int taskId, const QString& newStatus);
    int selectedTaskId() const;
    int selectedEventId() const;

    // KPI labels
    QLabel* m_lblTotalTasks = nullptr;
    QLabel* m_lblActiveTasks = nullptr;
    QLabel* m_lblUpcomingEvents = nullptr;
    QLabel* m_lblUrgentTasks = nullptr;
    QLabel* m_lblCompletionRate = nullptr;

    // Tab 0: Roadmap
    QTableWidget* m_roadmapTable = nullptr;
    QComboBox* m_horizonCombo = nullptr;
    QComboBox* m_roadmapTeamFilter = nullptr;
    QComboBox* m_roadmapPriorityFilter = nullptr;
    QComboBox* m_roadmapStatusFilter = nullptr;
    QLineEdit* m_roadmapSearch = nullptr;

    // Tab 1: Events Hub
    QTableWidget* m_eventsTable = nullptr;
    QComboBox* m_eventTypeFilter = nullptr;
    QLineEdit* m_eventSearch = nullptr;
    QPushButton* m_btnAddEvent = nullptr;
    QPushButton* m_btnEditEvent = nullptr;
    QPushButton* m_btnDeleteEvent = nullptr;

    // Tab 2: Kanban Board
    QWidget* m_kanbanContainer = nullptr;
    QMap<QString, QVBoxLayout*> m_kanbanColumns;
    QMap<QString, QLabel*> m_kanbanCounters;

    // Tab 3: Gantt Timeline
    QTableWidget* m_ganttTable = nullptr;
    QDateEdit* m_anchorDate = nullptr; // Gantt rolling-window start (default: today)
    QComboBox* m_ganttSpanCombo = nullptr;
    QComboBox* m_ganttTeamFilter = nullptr;

    // Tab 4: Strategic Goals & OKRs
    QWidget* m_goalsContainer = nullptr;
    QVBoxLayout* m_goalsLayout = nullptr;

    // Action Buttons
    QPushButton* m_btnAddTask = nullptr;
    QPushButton* m_btnEditTask = nullptr;
    QPushButton* m_btnDeleteTask = nullptr;
    QPushButton* m_btnExport = nullptr;

    QTabWidget* m_tabWidget = nullptr;
    int m_activeTab = 0;
    bool m_readOnly = false;
    QString m_userRole;
};

#endif // PLANNINGWIDGET_H
