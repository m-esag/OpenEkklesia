#ifndef SERVICEBUILDERWIDGET_H
#define SERVICEBUILDERWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QDateEdit>
#include <QDate>
#include <QLabel>
#include <QLineEdit>

class ServiceBuilderWidget : public QWidget {
    Q_OBJECT

public:
    explicit ServiceBuilderWidget(QWidget *parent = nullptr);
    ~ServiceBuilderWidget();

    // Role-gated editing: readonly (Guest) can view and export but cannot
    // modify the run sheet, volunteers, or service details.
    void applyRole(const QString& role);

private slots:
    void onSaveService();
    void onLoadService();
    void onClearPlaylist();
    void onRemoveItem();
    void onScheduleVolunteer();
    void onUnscheduleVolunteer();
    void onServiceDateChanged(const QDate& date);
    void onExportRunSheet();
    void updateDurationCalculator();
    void onSaveServiceDetails();

private:
    void setupUI();
    void seedLibrary();
    void loadVolunteers();
    void loadServiceDetails();

    QListWidget* m_libraryList;
    QListWidget* m_playlist;
    QLabel* m_lblTotalDuration;

    QPushButton* m_btnSave;
    QPushButton* m_btnLoad;
    QPushButton* m_btnClear;
    QPushButton* m_btnRemove;
    QPushButton* m_btnExportPDF;

    // Volunteer Scheduling
    QDateEdit* m_serviceDateEdit;
    QListWidget* m_volunteerList;
    QPushButton* m_btnSchedule;
    QPushButton* m_btnUnschedule;

    // Service Plan Details (persisted to the services table)
    QLineEdit* m_serviceTitleEdit;
    QLineEdit* m_leaderEdit;
    QPushButton* m_btnSaveServiceDetails;

    bool m_readOnly = false;
};

#endif // SERVICEBUILDERWIDGET_H
