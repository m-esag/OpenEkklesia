// Data-layer integration tests for OpenEkklesia.
//
// Runs against a fresh, throwaway SQLite database (never a user's real data).
// Uses a minimal self-contained assertion harness so it does not depend on the
// QtTest component being installed.

#include <QCoreApplication>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QDate>
#include <QSet>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <cmath>
#include <functional>
#include <iostream>

#include "../src/database/databasemanager.h"
#include "../src/database/datasharing.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::cerr << "CHECK FAILED: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        }                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++g_checks;                                                                    \
        auto va = (actual);                                                            \
        auto ve = (expected);                                                          \
        if (!(va == ve)) {                                                             \
            ++g_failures;                                                              \
            std::cerr << "CHECK_EQ FAILED: " << #actual << "=" << va                   \
                      << " expected=" << ve << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        }                                                                              \
    } while (0)

QString g_dbPath;
QSqlDatabase db() { return DatabaseManager::instance().database(); }

int countRows(const QString& table) {
    QSqlQuery q(db());
    if (!q.exec(QString("SELECT COUNT(*) FROM %1").arg(table))) {
        std::cerr << "countRows failed for " << table.toStdString()
                  << ": " << q.lastError().text().toStdString() << std::endl;
        return -1;
    }
    q.next();
    return q.value(0).toInt();
}

// --- Tests ----------------------------------------------------------------

bool test_schemaVersionIsLatest() {
    QSqlQuery q(db());
    CHECK(q.exec("SELECT MAX(version) FROM schema_version"));
    CHECK(q.next());
    CHECK_EQ(q.value(0).toInt(), DatabaseManager::kLatestSchemaVersion);
    return true;
}

bool test_expectedTablesExist() {
    const QStringList tables = {
        "account_budgets", "accounts", "assets", "attendance", "audit_log",
        "board_motions", "care_logs", "checkout_logs", "contributions",
        "entity_relationships", "facility_reservations", "funds", "graph_layout",
        "groups", "households", "member_history", "member_skills", "ministry_tasks",
        "people", "schema_version", "services", "service_volunteers", "splits",
        "transactions", "users", "vault_documents"
    };
    QSqlQuery q(db());
    for (const QString& t : tables) {
        CHECK(q.exec(QString("SELECT name FROM sqlite_master WHERE type='table' AND name='%1'").arg(t)));
        CHECK(q.next());
    }
    return true;
}

bool test_expectedIndexesExist() {
    const QStringList indexes = {
        "idx_contributions_date", "idx_contributions_person", "idx_people_gender",
        "idx_people_household", "idx_people_name", "idx_people_status",
        "idx_relationships", "idx_relationships_src", "idx_relationships_tgt",
        "idx_splits_account", "idx_splits_tx", "idx_transactions_date",
        "idx_volunteers_person"
    };
    QSqlQuery q(db());
    for (const QString& idx : indexes) {
        CHECK(q.exec(QString("SELECT name FROM sqlite_master WHERE type='index' AND name='%1'").arg(idx)));
        CHECK(q.next());
    }
    return true;
}

bool test_seedDataCounts() {
    CHECK_EQ(countRows("people"), 12);
    CHECK_EQ(countRows("households"), 6);
    CHECK_EQ(countRows("groups"), 3);
    CHECK_EQ(countRows("accounts"), 8);
    CHECK_EQ(countRows("funds"), 3);
    CHECK_EQ(countRows("contributions"), 3);
    CHECK_EQ(countRows("transactions"), 4);
    CHECK_EQ(countRows("users"), 4);
    CHECK_EQ(countRows("assets"), 4);
    CHECK_EQ(countRows("vault_documents"), 2);
    return true;
}

bool test_careTimelineQuery() {
    QSqlDatabase d = db();

    QSqlQuery ins(d);
    CHECK(ins.exec("INSERT INTO member_history (person_id, activity_type, details) VALUES (1, 'Contribution', 'test activity')"));
    CHECK(ins.exec("INSERT INTO care_logs (person_id, care_type, notes, follow_up) VALUES (1, 'Counseling', 'test care note', 1)"));

    QSqlQuery q(d);
    q.prepare(
        "SELECT timestamp, type, text, follow_up FROM ("
        "  SELECT timestamp, activity_type AS type, details AS text, 0 AS follow_up "
        "  FROM member_history WHERE person_id = ? "
        "  UNION ALL "
        "  SELECT timestamp, care_type AS type, notes AS text, follow_up "
        "  FROM care_logs WHERE person_id = ?"
        ") ORDER BY timestamp DESC LIMIT 15");
    q.addBindValue(1);
    q.addBindValue(1);
    CHECK(q.exec());

    int rows = 0;
    bool sawCare = false, sawActivity = false;
    while (q.next()) {
        ++rows;
        if (q.value(1).toString() == "Counseling") { sawCare = true; CHECK_EQ(q.value(3).toInt(), 1); }
        if (q.value(1).toString() == "Contribution") sawActivity = true;
    }
    CHECK_EQ(rows, 2);
    CHECK(sawCare);
    CHECK(sawActivity);

    CHECK(ins.exec("DELETE FROM member_history WHERE details = 'test activity'"));
    CHECK(ins.exec("DELETE FROM care_logs WHERE notes = 'test care note'"));
    return true;
}

bool test_connectionsQuery() {
    QSqlQuery q(db());
    q.prepare(
        "SELECT g.name, er.relationship_type FROM entity_relationships er "
        "JOIN groups g ON er.target_id = g.id "
        "WHERE er.source_type = 'Person' AND er.target_type = 'Group' AND er.source_id = ?");
    q.addBindValue(1);
    CHECK(q.exec());

    int rows = 0;
    bool isLeader = false;
    while (q.next()) {
        ++rows;
        if (q.value(1).toString() == "Leader") isLeader = true;
    }
    CHECK_EQ(rows, 1);
    CHECK(isLeader);
    return true;
}

bool test_unconnectedFilter() {
    QSqlQuery q(db());
    CHECK(q.exec(
        "SELECT id FROM people WHERE id NOT IN "
        "(SELECT DISTINCT source_id FROM entity_relationships "
        " WHERE source_type = 'Person' AND target_type = 'Group')"));

    QSet<int> ids;
    while (q.next()) ids.insert(q.value(0).toInt());
    CHECK(ids == QSet<int>({4, 5, 7, 10}));
    return true;
}

bool test_attendanceCount() {
    CHECK_EQ(countRows("attendance"), 0);

    QSqlQuery ins(db());
    CHECK(ins.exec("INSERT INTO attendance (service_id, person_id, attended) VALUES (NULL, 1, 1)"));

    QSqlQuery q(db());
    CHECK(q.exec("SELECT COUNT(*) FROM attendance WHERE attended = 1"));
    CHECK(q.next());
    CHECK_EQ(q.value(0).toInt(), 1);

    CHECK(ins.exec("DELETE FROM attendance"));
    return true;
}

bool test_ytdIncomeAndExpensesFilterByYear() {
    QSqlDatabase d = db();
    int fiscalYear = QDate::currentDate().year();

    auto ytdIncome = [&](QSqlDatabase dbc) {
        QSqlQuery q(dbc);
        q.prepare(
            "SELECT COALESCE(-SUM(s.amount), 0) FROM splits s "
            "JOIN transactions t ON s.transaction_id = t.id "
            "JOIN accounts a ON s.account_id = a.id "
            "WHERE a.type = 'INCOME' AND substr(t.date, 1, 4) = ?");
        q.addBindValue(QString::number(fiscalYear));
        q.exec(); q.next();
        return q.value(0).toDouble();
    };
    auto ytdExpenses = [&](QSqlDatabase dbc) {
        QSqlQuery q(dbc);
        q.prepare(
            "SELECT COALESCE(SUM(s.amount), 0) FROM splits s "
            "JOIN transactions t ON s.transaction_id = t.id "
            "JOIN accounts a ON s.account_id = a.id "
            "WHERE a.type = 'EXPENSE' AND substr(t.date, 1, 4) = ?");
        q.addBindValue(QString::number(fiscalYear));
        q.exec(); q.next();
        return q.value(0).toDouble();
    };

    double baseIncome = ytdIncome(d);
    double baseExpense = ytdExpenses(d);

    QSqlQuery q(d);
    q.prepare("INSERT INTO transactions (date, description) VALUES (?, 'TEST current-year')");
    q.addBindValue(QString("%1-06-15").arg(fiscalYear));
    CHECK(q.exec());
    int curTx = q.lastInsertId().toInt();
    q.prepare("INSERT INTO splits (transaction_id, account_id, amount) VALUES (?, 5, -100)");
    q.addBindValue(curTx); CHECK(q.exec());
    q.prepare("INSERT INTO splits (transaction_id, account_id, amount) VALUES (?, 8, 40)");
    q.addBindValue(curTx); CHECK(q.exec());

    q.prepare("INSERT INTO transactions (date, description) VALUES ('2000-06-15', 'TEST old-year')");
    CHECK(q.exec());
    int oldTx = q.lastInsertId().toInt();
    q.prepare("INSERT INTO splits (transaction_id, account_id, amount) VALUES (?, 5, -999)");
    q.addBindValue(oldTx); CHECK(q.exec());
    q.prepare("INSERT INTO splits (transaction_id, account_id, amount) VALUES (?, 8, 999)");
    q.addBindValue(oldTx); CHECK(q.exec());

    CHECK(std::fabs(ytdIncome(d) - (baseIncome + 100.0)) < 0.01);
    CHECK(std::fabs(ytdExpenses(d) - (baseExpense + 40.0)) < 0.01);
    return true;
}

bool test_splitSharingRoundTrip() {
    QSqlDatabase d = db();

    // Scope → table mapping sanity.
    CHECK(DataSharing::tablesForScope("care_notes") == QStringList({"care_logs", "member_history"}));
    CHECK(DataSharing::tablesForScope("membership").contains("people"));
    CHECK(DataSharing::tablesForScope("finance").contains("contributions"));

    // Round-trip the membership scope through a portable JSON payload.
    const QJsonObject doc = DataSharing::exportTables(d, DataSharing::tablesForScope("membership"));
    const QByteArray payload = QJsonDocument(doc).toJson();
    CHECK(payload.size() > 1000);

    // Wipe membership tables (FK checks deferred so finance rows that reference
    // people do not block the delete — this mirrors a fresh receive-only copy).
    QSqlQuery q(d);
    q.exec("PRAGMA foreign_keys = OFF");
    for (const QString& t : {"checkout_logs", "care_logs", "member_history", "attendance",
                             "member_skills", "entity_relationships", "people", "groups", "households"}) {
        CHECK(q.exec(QString("DELETE FROM %1").arg(t)));
    }
    q.exec("PRAGMA foreign_keys = ON");

    CHECK_EQ(countRows("people"), 0);
    CHECK_EQ(countRows("households"), 0);
    CHECK_EQ(countRows("groups"), 0);

    // Import and verify the data is restored.
    const QJsonObject restored = QJsonDocument::fromJson(payload).object();
    CHECK(DataSharing::importDocument(d, restored));
    CHECK_EQ(countRows("people"), 12);
    CHECK_EQ(countRows("households"), 6);
    CHECK_EQ(countRows("groups"), 3);
    return true;
}

bool test_purgeDemoData() {
    CHECK(DatabaseManager::instance().purgeDemoData());

    CHECK_EQ(countRows("people"), 0);
    CHECK_EQ(countRows("households"), 0);
    CHECK_EQ(countRows("groups"), 0);
    CHECK_EQ(countRows("contributions"), 0);
    CHECK_EQ(countRows("transactions"), 0);
    CHECK_EQ(countRows("assets"), 0);
    CHECK_EQ(countRows("vault_documents"), 0);
    CHECK_EQ(countRows("care_logs"), 0);

    CHECK_EQ(countRows("accounts"), 8);
    CHECK_EQ(countRows("funds"), 3);
    CHECK_EQ(countRows("users"), 4);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("openekklesia_tests");

    // Fresh, unique database file in the temp directory.
    g_dbPath = QDir::tempPath() + QString("/oe_test_%1.db").arg(QCoreApplication::applicationPid());
    QFile::remove(g_dbPath);

    if (!DatabaseManager::instance().initialize(g_dbPath)) {
        std::cerr << "FATAL: DatabaseManager failed to initialize a fresh database" << std::endl;
        return 2;
    }

    struct Test { const char* name; std::function<bool()> fn; };
    const Test tests[] = {
        {"schema_version_is_latest", test_schemaVersionIsLatest},
        {"expected_tables_exist", test_expectedTablesExist},
        {"expected_indexes_exist", test_expectedIndexesExist},
        {"seed_data_counts", test_seedDataCounts},
        {"care_timeline_query", test_careTimelineQuery},
        {"connections_query", test_connectionsQuery},
        {"unconnected_filter", test_unconnectedFilter},
        {"attendance_count", test_attendanceCount},
        {"ytd_income_and_expenses_filter_by_year", test_ytdIncomeAndExpensesFilterByYear},
        {"split_sharing_round_trip", test_splitSharingRoundTrip},
        {"purge_demo_data", test_purgeDemoData},
    };

    for (const Test& t : tests) {
        bool ok = false;
        try {
            ok = t.fn();
        } catch (...) {
            ok = false;
            std::cerr << "EXCEPTION in test " << t.name << std::endl;
        }
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << t.name << std::endl;
    }

    std::cout << "Ran " << g_checks << " checks, " << g_failures << " failed" << std::endl;

    DatabaseManager::instance().close();
    QFile::remove(g_dbPath);

    return g_failures == 0 ? 0 : 1;
}
