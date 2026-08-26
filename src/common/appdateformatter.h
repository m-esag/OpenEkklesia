#ifndef APPDATEFORMATTER_H
#define APPDATEFORMATTER_H

// Central date/time formatting for OpenEkklesia.
//
// Team decision (Settings brief Q2): implement a single configurable date
// format via QSettings "dateFormat" (default yyyy-MM-dd); timezone and
// start-of-week settings were removed as dead weight for this offline,
// single-locale app.

#include <QString>
#include <QDate>
#include <QDateTime>

class AppDateFormatter {
public:
    // Formats a date per the active "dateFormat" setting.
    static QString formatDate(const QDate& date);

    // Date (per setting) + local time.
    static QString formatDateTime(const QDateTime& dt);
};

#endif // APPDATEFORMATTER_H
