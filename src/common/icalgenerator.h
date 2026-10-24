#ifndef ICALGENERATOR_H
#define ICALGENERATOR_H

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTime>

namespace ICalGenerator {
// Escape text for an iCalendar (RFC 5545) TEXT value. Escapes backslash, ';',
// ',', CR and LF so user-supplied text can never break the property structure.
QString escape(const QString& s);

// Fold one content line to at most 75 octets (RFC 5545 3.1). Continuation lines
// are prefixed with a single space and never split a UTF-8 code point.
QString foldLine(const QString& line);

// Build a single-VEVENT calendar for the service builder's volunteer schedule.
// One VEVENT per (role, name) in roleNames, using date + startTime and an
// overall durationMinutes. Matches the historical servicebuilder widget output.
QString volunteerServiceIcal(const QDate& date, const QTime& startTime,
                            const QList<QPair<QString, QString>>& roleNames,
                            int durationMinutes);

// Build a one-VEVENT VCALENDAR for a single church / facility / pastoral event.
// Timed event when startTime is valid; all-day event when it is not. endTime
// falls back to startTime + 1 hour, and an endTime <= startTime is treated as
// ending on the following day (overnight booking). The UID is derived from the
// date, time and summary, so re-exporting the same event updates the existing
// calendar entry instead of creating a duplicate. Summary, description and
// location are escaped and folded here; callers pass raw text. Returns an empty
// string when `date` is invalid.
QString eventIcal(const QDate& date, const QTime& startTime, const QTime& endTime,
                  const QString& summary, const QString& description = QString(),
                  const QString& location = QString());
}

#endif // ICALGENERATOR_H
