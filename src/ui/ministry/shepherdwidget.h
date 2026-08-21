#ifndef SHEPHERDWIDGET_H
#define SHEPHERDWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>

class ShepherdWidget : public QWidget {
    Q_OBJECT

public:
    explicit ShepherdWidget(QWidget *parent = nullptr);
    ~ShepherdWidget() override = default;

    void refreshAll();

signals:
    void navigateToMember(int personId);

private slots:
    void onLogActivity();
    void onLogFollowUp();
    void onMarkFollowUpDone();
    void onRescheduleFollowUp();
    void onExportCalendarEvent();
    void onPersonChanged(int index);
    void onQueueActivated(int row, int column);
    void onQueueContextMenu(const QPoint& pos);

private:
    void setupUI();
    void refreshStats();
    void refreshQueue();
    void refreshTimeline();
    void refreshPrayerList();
    // Returns true if the user saved an activity.
    bool showActivityDialog(int prefillPersonId = -1, const QString& prefillType = QString());

    QLabel* m_lblDueCount;
    QLabel* m_lblAtRiskCount;
    QLabel* m_lblPrayerCount;

    QTabWidget* m_tabs;

    // Today's Queue tab
    QTableWidget* m_queueTable;
    QPushButton* m_btnMarkDone;
    QPushButton* m_btnReschedule;
    QPushButton* m_btnCalendar;
    QPushButton* m_btnLogFollowUp;

    // Member Activity tab
    QComboBox* m_personCombo;
    QListWidget* m_timelineList;
    QPushButton* m_btnLogActivity;

    // Prayer List tab
    QListWidget* m_prayerList;
    QPushButton* m_btnAddPrayer;
    QPushButton* m_btnPrayerDone;
};

#endif // SHEPHERDWIDGET_H
