#ifndef TRAYMANAGER_H
#define TRAYMANAGER_H

#include "appsettings.h"
#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QSettings>
#include <QSqlDatabase>
#include <QSet>
#include <QObject>
#include <QTimer>

struct Reminder {
    QString type;
    int recordId = 0;
    QString title;
    QDateTime dueAt;
};

class MainWindow;
class QMenu;

class QSystemTrayIcon;
class QAction;

class TrayManager : public QObject {
    Q_OBJECT
public:
    explicit TrayManager(MainWindow* window, QObject* parent = nullptr);
    ~TrayManager() override;

    bool initialize();
    static bool isSystemTrayAvailable();

    void refreshReminders();

    // Public so the test suite can exercise the reminder queries without a
    // live system tray and without Toast notifications.
    static QList<Reminder> collectReminders(const QSqlDatabase& db,
                                           const AppSettings& settings);

private slots:
    void showMainWindow();
    void toggleEnabled();

    void quit();

private:
    MainWindow* m_window;
    QSystemTrayIcon* m_tray;
    QMenu* m_menu;
    QAction* m_show;
    QAction* m_refresh;
    QAction* m_toggle;
    QAction* m_quit;
    QTimer* m_timer;

    bool m_enabled;
    QSet<QString> m_notified;
    QList<Reminder> m_lastReminders;   // last collected set, shown in the tray menu
    QMenu* m_upcomingMenu = nullptr;   // "Coming up" section fed by m_lastReminders

    void rebuildUpcomingMenu();
    int m_leadMinutes;

    bool loadConfig();
    void buildMenu();
    void notifyReminders(const QList<Reminder>& reminders);
};

#endif // TRAYMANAGER_H
