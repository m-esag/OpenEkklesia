#ifndef SERVICEBUILDERWIDGET_H
#define SERVICEBUILDERWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDate>
#include <QTime>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QPair>
#include <QSqlDatabase>

class QSqlDatabase;

// How often each element appears across saved service run sheets.
struct ElementUsage {
    QString name;
    QString type;
    int count = 0;
    QString lastUsedDate;
};

class ServiceBuilderWidget : public QWidget {
    Q_OBJECT

public:
    explicit ServiceBuilderWidget(QWidget *parent = nullptr);
    ~ServiceBuilderWidget();

    // Role-gated editing: readonly (Guest) can view and export but cannot
    // modify the run sheet, volunteers, or service details.
    void applyRole(const QString& role);

    // Tier 2 helpers (no dialogs) exposed for headless tests.
    QStringList collectCoverageIssues() const;
    int markVolunteersAttended();

    // Tier 3 helpers (no dialogs) exposed for headless tests.
    // One VEVENT per (role, name) on the given date at the given start time.
    static QString volunteerIcal(const QDate& date, const QTime& startTime,
                                 const QList<QPair<QString, QString>>& roleNames);
    // Aggregates element reuse across all saved service run sheets.
    static QList<ElementUsage> elementUsageStats(const QSqlDatabase& db, int limit = 100);

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
    void onAddElement();
    void onEditElement(QListWidgetItem* item);
    void onRemoveElement();
    void onSaveTemplate();
    void onApplyTemplate();
    void onMarkAttended();
    void onCoverageCheck();
    void onExportIcal();
    void onElementUsage();

private:
    void setupUI();
    void loadLibrary();
    void loadVolunteers();
    void loadServiceDetails();
    void persistRunSheet();
    void loadRunSheetFromDb(const QDate& date);
    void updateTimeline();
    QJsonArray playlistToJson() const;
    void loadPlaylistFromJson(const QJsonArray& array);

    QListWidget* m_libraryList;
    QListWidget* m_playlist;
    QLabel* m_lblTotalDuration;
    QLabel* m_lblTimeline;

    QPushButton* m_btnSave;
    QPushButton* m_btnLoad;
    QPushButton* m_btnClear;
    QPushButton* m_btnRemove;
    QPushButton* m_btnExportPDF;
    QPushButton* m_btnSaveTemplate;
    QPushButton* m_btnApplyTemplate;
    QPushButton* m_btnCoverageCheck;
    QPushButton* m_btnMarkAttended;
    QPushButton* m_btnExportIcal;
    QPushButton* m_btnUsageStats;
    QLabel* m_lblTypeLegend;

    // Element library editing
    QPushButton* m_btnAddElement;
    QPushButton* m_btnRemoveElement;

    // Volunteer Scheduling
    QDateEdit* m_serviceDateEdit;
    QTimeEdit* m_serviceTimeEdit;
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
