#include "icalgenerator.h"

#include <QDateTime>
#include <QSettings>
#include <QTime>

namespace ICalGenerator {

QString escape(const QString& s) {
    QString t = s;
    t.replace(",", "\\,");
    t.replace(";", "\\;");
    t.replace("\n", "\\n");
    return t;
}

QString volunteerServiceIcal(const QDate& date, const QTime& startTime,
                             const QList<QPair<QString, QString>>& roleNames,
                             int durationMinutes) {
    if (durationMinutes <= 0) durationMinutes = 120;
    if (durationMinutes > 24*60) durationMinutes = 24*60;

    QString out;
    out += "BEGIN:VCALENDAR\n";
    out += "VERSION:2.0\n";
    out += "PRODID:-//OpenEkklesia//Service Builder//EN\n";
    out += "CALSCALE:GREGORIAN\n";

    const QString dtStart = date.toString("yyyyMMdd") + "T" + startTime.toString("HHmmss");
    QTime endTime = startTime.addSecs(durationMinutes * 60);
    if (endTime < startTime) endTime = QTime(23, 59);
    const QString dtEnd = date.toString("yyyyMMdd") + "T" + endTime.toString("HHmmss");
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddThhmmssZ");

    int idx = 0;
    for (const auto& rn : roleNames) {
        ++idx;
        out += "BEGIN:VEVENT\n";
        out += QString("UID:oe-vol-%1-%2@openekklesia.local\n").arg(idx).arg(date.toString("yyyyMMdd"));
        out += QString("DTSTAMP:%1\n").arg(stamp);
        out += QString("DTSTART:%1\n").arg(dtStart);
        out += QString("DTEND:%1\n").arg(dtEnd);
        out += QString("SUMMARY:Serve as %1 (%2)\n").arg(escape(rn.first), escape(rn.second));
        out += QString("DESCRIPTION:Volunteer schedule for %1 \\nRole: %2\n")
                   .arg(date.toString("MMMM d, yyyy"), escape(rn.first));
        QSettings s("OpenEkklesia", "CHMS");
        const int alarmMins = s.value("volunteerAlarmMinutes", 60).toInt();
        out += "STATUS:CONFIRMED\n";
        out += "BEGIN:VALARM\n";
        out += QString("TRIGGER:-PT%1M\n").arg(alarmMins);
        out += "ACTION:DISPLAY\n";
        out += QString("DESCRIPTION:Reminder: %1 serves as %2\n").arg(escape(rn.second), escape(rn.first));
        out += "END:VALARM\n";
        out += "END:VEVENT\n";
    }
    out += "END:VCALENDAR\n";
    return out;
}

} // namespace ICalGenerator
