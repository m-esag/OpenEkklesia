#ifndef ICALGENERATOR_H
#define ICALGENERATOR_H

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QPair>

namespace ICalGenerator {
// Escape text for an iCalendar (RFC 5545) value.
QString escape(const QString& s);

// Build a single-VEVENT calendar for the service builder's volunteer schedule.
// One VEVENT per (role, name) in roleNames, using date + startTime and an
// overall durationMinutes. Matches the historical servicebuilder widget output
// exactly.
QString volunteerServiceIcal(const QDate& date, const QTime& startTime,
                            const QList<QPair<QString, QString>>& roleNames,
                            int durationMinutes);
}

#endif // ICALGENERATOR_H
