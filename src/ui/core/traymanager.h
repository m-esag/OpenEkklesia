#ifndef TRAYMANAGER_H
#define TRAYMANAGER_H

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QSettings>
#include <QSqlDatabase>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTimer>

struct Reminder {
    QString type;
    int recordId = 0;
    QString title;
    QDateTime dueAt;
};

class MainWindow;
class QMenu;
class QAction;

class TrayManager : public QObject {
    Q_OBJECT
public:
    explicit TrayManager(MainWindow* window, QObject* parent = nullptr);

    bool initialize();
    static bool isSystemTrayAvailable();

    void refreshReminders();

private slots:
    void showMainWindow();
    void toggleEnabled();
    void onActivated(QSystemTrayIcon::ActivationReason reason);
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
    int m_leadMinutes;

    bool loadConfig();
    void buildMenu();
    static QList<Reminder> collectReminders(const QSqlDatabase& db, const QSettings& settings);
    void notifyReminders(const QList<Reminder>& reminders);
    static void enqueueReminder(QStringList& messages, const Reminder& reminder);
};

#endif // TRAYMANAGER_H