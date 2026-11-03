#include "traymanager.h"
#include "appsettings.h"

#include <QAction>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QMenu>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>
#include <QVariant>
#include <QSystemTrayIcon>
#include <QTimeZone>

#include "databasemanager.h"
#include "iconmanager.h"
#include "thememanager.h"
#include "mainwindow.h"

TrayManager::TrayManager(MainWindow* window, QObject* parent)
    : QObject(parent), m_window(window), m_tray(nullptr), m_menu(nullptr),
      m_show(nullptr), m_refresh(nullptr), m_toggle(nullptr), m_quit(nullptr),
      m_timer(nullptr), m_enabled(true), m_leadMinutes(30) {
}

TrayManager::~TrayManager() {
    // QSystemTrayIcon::setContextMenu() does not take ownership and a QMenu
    // needs a QWidget parent, so TrayManager deletes its menu explicitly.
    delete m_menu;
    m_menu = nullptr;
}

bool TrayManager::isSystemTrayAvailable() {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool TrayManager::initialize() {
    if (!isSystemTrayAvailable())
        return false;

    if (!loadConfig())
        return false;

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(IconManager::instance().getIcon("calendar", QColor(), 16));
    m_tray->setToolTip("OpenEkklesia");

    buildMenu();
    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayManager::showMainWindow);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &TrayManager::refreshReminders);
    m_timer->setInterval(15 * 60 * 1000);

    m_tray->show();
    m_timer->start();
    refreshReminders();
    return true;
}

bool TrayManager::loadConfig() {
    AppSettings settings;
    m_enabled = settings.value("tray/enabled", true).toBool();
    m_leadMinutes = qBound(1, settings.value("tray/leadMinutes", 30).toInt(), 120);
    return true;
}

void TrayManager::buildMenu() {
    m_menu = new QMenu();
    m_show = m_menu->addAction("Show OpenEkklesia");
    connect(m_show, &QAction::triggered, this, &TrayManager::showMainWindow);

    m_menu->addSeparator();
    m_refresh = m_menu->addAction("Refresh Reminders");
    connect(m_refresh, &QAction::triggered, this, &TrayManager::refreshReminders);

    m_menu->addSeparator();
    m_toggle = m_menu->addAction("Disable Reminder Notifications");
    m_toggle->setCheckable(true);
    m_toggle->setChecked(!m_enabled);
    connect(m_toggle, &QAction::triggered, this, &TrayManager::toggleEnabled);

    m_menu->addSeparator();
    m_quit = m_menu->addAction("Quit OpenEkklesia");
    connect(m_quit, &QAction::triggered, this, &TrayManager::quit);
}

void TrayManager::showMainWindow() {
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void TrayManager::toggleEnabled() {
    m_enabled = !m_toggle->isChecked();
    AppSettings settings;
    settings.setValue("tray/enabled", m_enabled);
    if (!m_enabled)
        m_notified.clear();
}

void TrayManager::quit() {
    QApplication::quit();
}

QList<Reminder> TrayManager::collectReminders(const QSqlDatabase& db,
                                              const AppSettings& settings) {
    QList<Reminder> reminders;
    if (!db.isOpen())
        return reminders;

    const bool servicesEnabled = settings.value("tray/services", true).toBool();
    const bool eventsEnabled = settings.value("tray/ministryEvents", true).toBool();
    const bool tasksEnabled = settings.value("tray/ministryTasks", true).toBool();
    const bool reservationsEnabled = settings.value("tray/facilityReservations", true).toBool();
    const bool pastoralEnabled = settings.value("tray/pastoralFollowUps", false).toBool();

    const QDate today = QDate::currentDate();
    const QDate horizon = today.addDays(7);
    const QString todayText = today.toString(Qt::ISODate);
    const QString horizonText = horizon.toString(Qt::ISODate);

    if (servicesEnabled) {
        QSqlQuery q(db);
        q.prepare("SELECT id, date, title, leader, start_time FROM services "
                  "WHERE date >= ? AND date <= ? ORDER BY date ASC, start_time ASC");
        q.addBindValue(todayText);
        q.addBindValue(horizonText);
        if (q.exec()) {
            while (q.next()) {
                const int id = q.value(0).toInt();
                const QDate serviceDate = QDate::fromString(q.value(1).toString(), Qt::ISODate);
                const QString title = q.value(2).toString().trimmed();
                const QString leader = q.value(3).toString().trimmed();
                const QTime start = QTime::fromString(q.value(4).toString(), "HH:mm");
                if (!serviceDate.isValid() || title.isEmpty())
                    continue;
                if (!start.isValid())
                    continue;
                QDateTime due(serviceDate, start.isValid() ? start : QTime(0, 0), QTimeZone::LocalTime);
                const QString displayName = leader.isEmpty() ? title : title + " (" + leader + ")";
                reminders.append({"service", id, displayName, due});
            }
        }
    }

    if (eventsEnabled) {
        QSqlQuery q(db);
        q.prepare("SELECT id, start_datetime, end_datetime, title, event_type, facility_name, status "
                  "FROM ministry_events "
                  "WHERE start_datetime >= datetime('now','localtime') "
                  "  AND start_datetime <= datetime('now','localtime','+7 days') "
                  "  AND status <> 'Cancelled' ORDER BY start_datetime ASC");
        if (q.exec()) {
            while (q.next()) {
                const int id = q.value(0).toInt();
                const QDateTime due = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
                if (!due.isValid())
                    continue;
                const QString title = q.value(3).toString().trimmed();
                const QString type = q.value(4).toString().trimmed();
                const QString facility = q.value(5).toString().trimmed();
                QString displayName = title.isEmpty() ? "Untitled Event" : title;
                if (!type.isEmpty())
                    displayName += " [" + type + "]";
                if (!facility.isEmpty())
                    displayName += " - " + facility;
                reminders.append({"ministry_event", id, displayName, due});
            }
        }
    }

    if (tasksEnabled) {
        QSqlQuery q(db);
        q.prepare("SELECT id, title, owner, status, start_date FROM ministry_tasks "
                  "WHERE status <> ? AND start_date IS NOT NULL "
                  "  AND start_date >= ? AND start_date <= ? "
                  "ORDER BY start_date ASC");
        q.addBindValue("Completed");
        q.addBindValue(todayText);
        q.addBindValue(horizonText);
        if (q.exec()) {
            while (q.next()) {
                const int id = q.value(0).toInt();
                const QString title = q.value(1).toString().trimmed();
                const QString owner = q.value(2).toString().trimmed();
                const QDate dueDate = QDate::fromString(q.value(4).toString(), Qt::ISODate);
                if (!dueDate.isValid())
                    continue;
                const QDateTime due(dueDate, QTime(0, 0), QTimeZone::LocalTime);
                const QString displayName = owner.isEmpty() ? title : title + " (" + owner + ")";
                reminders.append({"ministry_task", id, displayName, due});
            }
        }
    }

    if (reservationsEnabled) {
        QSqlQuery q(db);
        q.prepare("SELECT id, reservation_date, start_time, end_time, event_name, facility_name, reserved_by "
                  "FROM facility_reservations "
                  "WHERE reservation_date >= ? AND reservation_date <= ? "
                  "ORDER BY reservation_date ASC, start_time ASC");
        q.addBindValue(todayText);
        q.addBindValue(horizonText);
        if (q.exec()) {
            while (q.next()) {
                const int id = q.value(0).toInt();
                const QDate resDate = QDate::fromString(q.value(1).toString(), Qt::ISODate);
                const QTime start = QTime::fromString(q.value(2).toString(), "HH:mm");
                if (!resDate.isValid())
                    continue;
                const QDateTime due(resDate, start.isValid() ? start : QTime(0, 0), QTimeZone::LocalTime);
                const QString eventName = q.value(4).toString().trimmed();
                const QString facility = q.value(5).toString().trimmed();
                const QString displayName = facility.isEmpty() ? eventName : eventName + " - " + facility;
                reminders.append({"facility_reservation", id, displayName, due});
            }
        }
    }

    if (pastoralEnabled) {
        QSqlQuery q(db);
        q.prepare("SELECT pa.id, pa.follow_up_date, pa.activity_type, "
                  "       p.first_name, p.last_name "
                  "FROM pastoral_activities pa "
                  "JOIN people p ON p.id = pa.person_id "
                  "WHERE pa.follow_up_done = 0 AND pa.follow_up_date IS NOT NULL "
                  "  AND pa.follow_up_date >= ? AND pa.follow_up_date <= ? "
                  "ORDER BY pa.follow_up_date ASC");
        q.addBindValue(todayText);
        q.addBindValue(horizonText);
        if (q.exec()) {
            while (q.next()) {
                const int id = q.value(0).toInt();
                const QDate followDate = QDate::fromString(q.value(1).toString(), Qt::ISODate);
                if (!followDate.isValid())
                    continue;
                const QString type = q.value(2).toString().trimmed();
                const QString displayName = type + " follow-up: " + q.value(3).toString().trimmed()
                                            + " " + q.value(4).toString().trimmed();
                reminders.append({"pastoral_followup", id, displayName,
                                 QDateTime(followDate, QTime(0, 0), QTimeZone::LocalTime)});
            }
        }
    }

    return reminders;
}

void TrayManager::refreshReminders() {
    if (!m_tray || !m_tray->isVisible())
        return;
    if (!loadConfig())
        return;
    if (!m_enabled)
        return;

    const QList<Reminder> reminders =
        collectReminders(DatabaseManager::instance().database(), AppSettings());
    notifyReminders(reminders);
}

void TrayManager::notifyReminders(const QList<Reminder>& reminders) {
    if (!m_tray || !m_enabled)
        return;

    const QDateTime now = QDateTime::currentDateTime();
    int leadMinutes = qMax(1, m_leadMinutes);
    QStringList dueMessages;
    for (const Reminder& reminder : reminders) {
        if (!reminder.dueAt.isValid())
            continue;

        const QDateTime threshold = reminder.dueAt.addSecs(-leadMinutes * 60);
        if (now < threshold)
            continue;
        if (now > reminder.dueAt.addSecs(60 * 60)) // max age one hour
            continue;

        QString key = reminder.type + ":" + QString::number(reminder.recordId) + ":" +
                      reminder.dueAt.toString(Qt::ISODate);
        if (m_notified.contains(key))
            continue;

        const int minutes = static_cast<int>(now.secsTo(reminder.dueAt) / 60.0);
        QString eta;
        if (minutes <= 0)
            eta = "now";
        else if (minutes < leadMinutes + 1)
            eta = "in " + QString::number(minutes) + " min";
        else
            eta = reminder.dueAt.toString("HH:mm");
        dueMessages << eta + " - " + reminder.title;
        m_notified.insert(key);
    }

    if (dueMessages.isEmpty())
        return;

    QString title = "OpenEkklesia Reminder";
    if (dueMessages.size() == 1)
        title = "OpenEkklesia Reminder";
    else
        title = QString("OpenEkklesia - %1 reminders").arg(dueMessages.size());
    m_tray->showMessage(title, dueMessages.join("\n"),
                        QSystemTrayIcon::Information, 5000);
}
