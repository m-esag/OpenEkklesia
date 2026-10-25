#include "icalgenerator.h"
#include "appsettings.h"

#include <QDateTime>
#include <QSettings>
#include <QString>
#include <QTime>

namespace ICalGenerator {

namespace {

// RFC 5545 3.1: content lines are limited to 75 octets, excluding the line break.
constexpr int kMaxLineOctets = 75;

// Default reminder for generic events (the volunteer schedule keeps its own
// configurable "volunteerAlarmMinutes" setting).
constexpr int kDefaultAlarmMinutes = 15;

QString foldLineInternal(const QString& line) {
    if (line.toUtf8().size() <= kMaxLineOctets) return line;

    QString folded;
    QString current;
    int octets = 0;
    int i = 0;
    const int n = line.size();
    while (i < n) {
        // Keep surrogate pairs together so a fold never splits a code point.
        int len = 1;
        if (line.at(i).isHighSurrogate() && i + 1 < n && line.at(i + 1).isLowSurrogate()) {
            len = 2;
        }
        const QString piece = line.mid(i, len);
        const int pieceOctets = piece.toUtf8().size();
        if (octets + pieceOctets > kMaxLineOctets) {
            folded += current;
            folded += '\n';
            current = QStringLiteral(" "); // continuation line: one leading space
            octets = 1;
            continue; // retry this code point on the new line
        }
        current += piece;
        octets += pieceOctets;
        i += len;
    }
    folded += current;
    return folded;
}

QString contentLine(const QString& name, const QString& value) {
    return foldLineInternal(name + ':' + value) + '\n';
}

QString calendarStart(const QString& prodId) {
    QString out;
    out += "BEGIN:VCALENDAR\n";
    out += "VERSION:2.0\n";
    out += contentLine("PRODID", prodId);
    out += "CALSCALE:GREGORIAN\n";
    return out;
}

// Small deterministic FNV-1a hash so a UID stays stable across runs and exports
// without depending on Qt's (randomly seeded) container hash.
quint32 stableHash(const QString& s) {
    quint32 h = 2166136261u;
    const QByteArray utf8 = s.toUtf8();
    for (const char c : utf8) {
        h ^= static_cast<quint32>(static_cast<unsigned char>(c));
        h *= 16777619u;
    }
    return h;
}

QString eventUid(const QDate& date, const QTime& startTime, const QString& seed) {
    const QString timePart = startTime.isValid() ? startTime.toString("HHmmss")
                                                 : QStringLiteral("allday");
    return QString("oe-ev-%1T%2-%3@openekklesia.local")
        .arg(date.toString("yyyyMMdd"), timePart,
             QString::number(stableHash(seed), 16).rightJustified(8, '0'));
}

// One VEVENT: DTSTAMP/DTSTART/DTEND, escaped SUMMARY/DESCRIPTION/LOCATION and an
// optional DISPLAY alarm. `date` must be valid; timed when startTime is valid.
QString buildEvent(const QDate& date, const QTime& startTime, const QTime& endTime,
                   const QString& uid, const QString& summary,
                   const QString& description, const QString& location,
                   int alarmMinutes) {
    QString out;
    out += "BEGIN:VEVENT\n";
    out += contentLine("UID", uid);
    out += contentLine("DTSTAMP", QDateTime::currentDateTimeUtc().toString("yyyyMMddThhmmssZ"));

    if (startTime.isValid()) {
        QTime effectiveEnd = endTime;
        QDate effectiveEndDate = date;
        if (!effectiveEnd.isValid()) {
            effectiveEnd = startTime.addSecs(60 * 60); // default 1 hour
        } else if (effectiveEnd <= startTime) {
            effectiveEndDate = date.addDays(1); // overnight event
        }
        out += contentLine("DTSTART", date.toString("yyyyMMdd") + "T" + startTime.toString("HHmmss"));
        out += contentLine("DTEND", effectiveEndDate.toString("yyyyMMdd") + "T"
                                        + effectiveEnd.toString("HHmmss"));
    } else {
        // All-day event: DTEND is exclusive per RFC 5545, hence the next day.
        out += "DTSTART;VALUE=DATE:" + date.toString("yyyyMMdd") + "\n";
        out += "DTEND;VALUE=DATE:" + date.addDays(1).toString("yyyyMMdd") + "\n";
    }

    out += contentLine("SUMMARY", escape(summary));
    if (!description.isEmpty()) {
        out += contentLine("DESCRIPTION", escape(description));
    }
    if (!location.isEmpty()) {
        out += contentLine("LOCATION", escape(location));
    }
    out += "STATUS:CONFIRMED\n";
    if (alarmMinutes > 0) {
        out += "BEGIN:VALARM\n";
        out += QString("TRIGGER:-PT%1M\n").arg(alarmMinutes);
        out += "ACTION:DISPLAY\n";
        out += contentLine("DESCRIPTION", escape(QString("Reminder: %1").arg(summary)));
        out += "END:VALARM\n";
    }
    out += "END:VEVENT\n";
    return out;
}

} // namespace

QString escape(const QString& s) {
    QString t = s;
    // Normalise line breaks first, then escape backslash before the characters
    // whose escapes introduce one (otherwise the escapes would be doubled).
    t.replace("\r\n", "\n");
    t.replace('\r', '\n');
    t.replace('\\', "\\\\");
    t.replace(';', "\\;");
    t.replace(',', "\\,");
    t.replace('\n', "\\n");
    return t;
}

QString foldLine(const QString& line) {
    return foldLineInternal(line);
}

QString volunteerServiceIcal(const QDate& date, const QTime& startTime,
                             const QList<QPair<QString, QString>>& roleNames,
                             int durationMinutes) {
    if (durationMinutes <= 0) durationMinutes = 120;
    if (durationMinutes > 24 * 60) durationMinutes = 24 * 60;

    AppSettings s;
    const int alarmMins = s.value("volunteerAlarmMinutes", 60).toInt();
    const QString timeSuffix = startTime.isValid() ? startTime.toString("HHmmss")
                                                   : QStringLiteral("allday");

    QString out = calendarStart("-//OpenEkklesia//Service Builder//EN");
    int idx = 0;
    for (const auto& rn : roleNames) {
        ++idx;
        // The start time is part of the UID: two services on one date (09:00 and
        // 11:00) must not produce colliding UIDs that overwrite each other in
        // calendar clients.
        const QString uid = QString("oe-vol-%1-%2T%3@openekklesia.local")
                                .arg(idx)
                                .arg(date.toString("yyyyMMdd"), timeSuffix);
        const QTime endTime = startTime.isValid()
                                  ? startTime.addSecs(durationMinutes * 60)
                                  : QTime();
        const QString summary = QString("Serve as %1 (%2)").arg(rn.first, rn.second);
        const QString description = QString("Volunteer schedule for %1\nRole: %2")
                                        .arg(date.toString("MMMM d, yyyy"), rn.first);
        out += buildEvent(date, startTime, endTime, uid, summary, description,
                          QString(), alarmMins);
    }
    out += "END:VCALENDAR\n";
    return out;
}

QString eventIcal(const QDate& date, const QTime& startTime, const QTime& endTime,
                  const QString& summary, const QString& description,
                  const QString& location) {
    if (!date.isValid()) return QString();

    // Seed the UID with the date/time and summary only: re-exporting the same
    // event after editing its location must update the existing calendar entry
    // instead of creating a duplicate, as documented on eventIcal().
    const QString uid = eventUid(date, startTime, summary);
    QString out = calendarStart("-//OpenEkklesia//Calendar//EN");
    out += buildEvent(date, startTime, endTime, uid, summary, description, location,
                      kDefaultAlarmMinutes);
    out += "END:VCALENDAR\n";
    return out;
}

} // namespace ICalGenerator
