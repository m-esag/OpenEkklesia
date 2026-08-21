#ifndef PLANNINGWIDGET_H
#define PLANNINGWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDateEdit>

class PlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlanningWidget(QWidget *parent = nullptr);
    ~PlanningWidget();

    // Role-gated editing: readonly (Guest) can view but cannot add/edit/delete.
    void applyRole(const QString& role);

private slots:
    void onAddTask();
    void onEditTask();
    void onDeleteTask();
    void onTabChanged(int index);

private:
    void setupUI();
    void refreshGanttChart();
    void refreshRoadmap();
    void openTaskDialog(int editId);
    int selectedTaskId() const;

    QTableWidget* m_roadmapTable;
    QTableWidget* m_ganttTable;
    QDateEdit* m_anchorDate; // Gantt rolling-window start (default: today)

    QPushButton* m_btnAddTask;
    QPushButton* m_btnEditTask;
    QPushButton* m_btnDeleteTask;

    int m_activeTab = 0;
    bool m_readOnly = false;
};

#endif // PLANNINGWIDGET_H
