#include "givingsummaryservice.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

PersonGivingSummary GivingSummaryService::fetch(int personId, const QSqlDatabase& db) {
    PersonGivingSummary summary;
    if (!db.isValid()) return summary;

    QSqlQuery q(db);
    q.prepare("SELECT COALESCE(SUM(amount), 0), COUNT(id), MAX(contribution_date) FROM contributions WHERE person_id = ?");
    q.addBindValue(personId);
    if (q.exec() && q.next()) {
        summary.total = q.value(0).toDouble() / 100.0;
        summary.count = q.value(1).toInt();
        QString last = q.value(2).toString();
        summary.lastDate = last.isEmpty() ? QStringLiteral("None recorded") : last;
    }

    // Household view: spouse lookup both directions.
    int spouseId = 0;
    QSqlQuery spouse(db);
    spouse.prepare("SELECT CASE WHEN source_type = 'Person' AND source_id = :me THEN target_id "
                   "        ELSE source_id END AS other "
                   "FROM entity_relationships "
                   "WHERE relationship_type = 'Spouse' "
                   "  AND ((source_type = 'Person' AND source_id = :me) OR (target_type = 'Person' AND target_id = :me)) "
                   "LIMIT 1");
    spouse.bindValue(":me", personId);
    if (spouse.exec() && spouse.next()) {
        spouseId = spouse.value(0).toInt();
    }
    summary.spouseId = spouseId;
    if (spouseId > 0) {
        QSqlQuery hq(db);
        hq.prepare("SELECT COALESCE(SUM(amount), 0) FROM contributions WHERE person_id IN (?, ?)");
        hq.addBindValue(personId);
        hq.addBindValue(spouseId);
        if (hq.exec() && hq.next()) {
            summary.householdTotal = hq.value(0).toDouble() / 100.0;
            summary.householdTotalAvailable = true;
        }
    }

    QSqlQuery qGifts(db);
    qGifts.prepare("SELECT c.contribution_date, f.fund_name, c.amount, COALESCE(NULLIF(c.payment_method, ''), '—'), COALESCE(c.note, '') "
                   "FROM contributions c JOIN funds f ON c.fund_id = f.id "
                   "WHERE c.person_id = ? ORDER BY c.contribution_date DESC LIMIT 10");
    qGifts.addBindValue(personId);
    if (qGifts.exec()) {
        while (qGifts.next()) {
            PersonGiftRecent item;
            item.date = qGifts.value(0).toString();
            item.fund = qGifts.value(1).toString();
            item.amount = qGifts.value(2).toDouble() / 100.0;
            item.payment = qGifts.value(3).toString();
            item.note = qGifts.value(4).toString();
            summary.recent.append(item);
        }
    }
    if (summary.lastDate == QStringLiteral("—")) {}
    return summary;
}
