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

private slots:
    void onAddTask();
    void onDeleteTask();
    void onTabChanged(int index);

private:
    void setupUI();
    void refreshGanttChart();
    void refreshRoadmap();

    QTableWidget* m_ganttTable;
    QTableWidget* m_roadmapTable;

    QPushButton* m_btnAddTask;
    QPushButton* m_btnDeleteTask;
};

#endif // PLANNINGWIDGET_H
