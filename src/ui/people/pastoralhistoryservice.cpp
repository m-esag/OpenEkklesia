#include "pastoralhistoryservice.h"
#include <QSqlQuery>

QList<PastoralHistoryEntry> PastoralHistoryService::fetch(int personId, const QSqlDatabase& db) {
    QList<PastoralHistoryEntry> out;
    if (!db.isValid()) return out;
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, timestamp, type, text, follow_up, src FROM ("
        "  SELECT id, timestamp, activity_type AS type, details AS text, 0 AS follow_up, 'member_history' AS src "
        "  FROM member_history WHERE person_id = ? "
        "  UNION ALL "
        "  SELECT id, timestamp, care_type AS type, notes AS text, follow_up, 'care_logs' AS src "
        "  FROM care_logs WHERE person_id = ?"
        ") ORDER BY timestamp DESC LIMIT 20");
    q.addBindValue(personId);
    q.addBindValue(personId);
    if (q.exec()) {
        while (q.next()) {
            PastoralHistoryEntry e;
            e.id = q.value(0).toInt();
            e.timestamp = q.value(1).toString();
            e.type = q.value(2).toString();
            e.text = q.value(3).toString();
            e.followUp = q.value(4).toInt() != 0;
            e.sourceTable = q.value(5).toString();
            out.append(e);
        }
    }
    return out;
}

QString PastoralHistoryService::badge(const QString& type) {
    QString badge = "🕊️ " + type;
    if (type == "Counseling") badge = "💬 " + type;
    else if (type == "Visitation" || type == "Hospital Visit") badge = "🏥 " + type;
    else if (type == "Prayer") badge = "🙏 " + type;
    else if (type == "Follow-up Call") badge = "📞 " + type;
    else if (type == "Benevolence") badge = "🤝 " + type;
    return badge;
}
