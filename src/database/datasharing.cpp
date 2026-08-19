#include "datasharing.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QVariant>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QDebug>

namespace DataSharing {

namespace {

// All user tables in FK-safe (parent-first) import order.
QStringList allTables() {
    return {
        "households", "groups", "people", "accounts", "funds", "transactions",
        "services", "splits", "contributions", "entity_relationships",
        "member_skills", "attendance", "member_history", "care_logs",
        "pastoral_activities", "service_volunteers", "account_budgets", "assets",
        "checkout_logs", "vault_documents", "board_motions", "ministry_tasks",
        "facility_reservations", "graph_layout", "audit_log", "users"
    };
}

} // namespace

QStringList availableScopes() {
    return {"full", "membership", "care_notes", "schedules", "finance", "governance"};
}

QStringList tablesForScope(const QString& scope) {
    if (scope == "full") {
        return allTables();
    }
    if (scope == "membership") {
        // Congregation directory: families, people, groups, relationships,
        // skills, attendance and the pastoral/care trail tied to people.
        return {"households", "groups", "people", "entity_relationships",
                "member_skills", "attendance", "member_history", "care_logs",
                "pastoral_activities", "checkout_logs"};
    }
    if (scope == "care_notes") {
        // Pastoral notes and activity history only (small, safe to share).
        return {"care_logs", "member_history", "pastoral_activities"};
    }
    if (scope == "schedules") {
        // Worship services, volunteers, room bookings, and ministry roadmap.
        return {"services", "service_volunteers", "facility_reservations",
                "ministry_tasks"};
    }
    if (scope == "finance") {
        // Double-entry accounting: chart of accounts, funds, ledger, giving, budgets.
        return {"accounts", "funds", "transactions", "splits", "contributions",
                "account_budgets"};
    }
    if (scope == "governance") {
        // Board minutes/motions and document vault metadata.
        return {"board_motions", "vault_documents"};
    }
    return {};
}

QJsonObject exportTables(const QSqlDatabase& db, const QStringList& tables) {
    QJsonObject doc;
    doc["format"] = "openekklesia-data";
    doc["version"] = 1;
    doc["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonObject tablesObj;
    for (const QString& table : tables) {
        QSqlQuery q(db);
        if (!q.exec(QString("SELECT * FROM %1").arg(table))) {
            qWarning() << "[DataSharing] export failed for" << table << q.lastError().text();
            continue;
        }

        QJsonArray rows;
        while (q.next()) {
            QJsonObject row;
            const QSqlRecord rec = q.record();
            for (int i = 0; i < rec.count(); ++i) {
                const QString name = rec.fieldName(i);
                const QVariant v = rec.value(i);
                if (v.isNull()) {
                    row[name] = QJsonValue::Null;
                } else if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong) {
                    row[name] = v.toLongLong();
                } else if (v.typeId() == QMetaType::Double) {
                    row[name] = v.toDouble();
                } else if (v.typeId() == QMetaType::Bool) {
                    row[name] = v.toBool();
                } else {
                    row[name] = v.toString();
                }
            }
            rows.append(row);
        }
        tablesObj[table] = rows;
    }
    doc["tables"] = tablesObj;
    return doc;
}

bool importDocument(QSqlDatabase& db, const QJsonObject& doc) {
    if (doc.value("format").toString() != "openekklesia-data") {
        qWarning() << "[DataSharing] import aborted: unrecognized format";
        return false;
    }

    const QJsonObject tablesObj = doc.value("tables").toObject();
    if (tablesObj.isEmpty()) {
        return false;
    }

    // Defer foreign-key checks so the table order never causes a partial import.
    QSqlQuery pragma(db);
    pragma.exec("PRAGMA foreign_keys = OFF");

    bool ok = db.transaction();
    if (!ok) {
        qWarning() << "[DataSharing] could not begin import transaction:" << db.lastError().text();
        pragma.exec("PRAGMA foreign_keys = ON");
        return false;
    }

    bool success = true;
    for (const QString& table : tablesObj.keys()) {
        const QJsonArray rows = tablesObj.value(table).toArray();
        if (rows.isEmpty()) continue;

        // Derive the column set from the first row (preserves NULL-vs-missing).
        const QJsonObject first = rows.first().toObject();
        const QStringList columns = first.keys();

        QStringList placeholders;
        for (int i = 0; i < columns.size(); ++i) placeholders << "?";

        QSqlQuery ins(db);
        ins.prepare(QString("INSERT OR REPLACE INTO %1 (%2) VALUES (%3)")
                        .arg(table, columns.join(", "), placeholders.join(", ")));

        for (const QJsonValue& rv : rows) {
            const QJsonObject row = rv.toObject();
            for (const QString& col : columns) {
                const QJsonValue val = row.value(col);
                if (val.isNull()) {
                    ins.addBindValue(QVariant(QMetaType(QMetaType::QString))); // SQL NULL
                } else if (val.isDouble()) {
                    ins.addBindValue(val.toDouble());
                } else if (val.isBool()) {
                    ins.addBindValue(val.toBool());
                } else if (val.isString()) {
                    ins.addBindValue(val.toString());
                } else {
                    ins.addBindValue(QVariant(val.toVariant()));
                }
            }
            if (!ins.exec()) {
                qWarning() << "[DataSharing] import failed for" << table << ":" << ins.lastError().text();
                success = false;
                break;
            }
        }
        if (!success) break;
    }

    if (success) {
        db.commit();
    } else {
        db.rollback();
    }
    pragma.exec("PRAGMA foreign_keys = ON");
    return success;
}

} // namespace DataSharing
