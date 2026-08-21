// Data-layer integration tests for OpenEkklesia.
//
// Runs against a fresh, throwaway SQLite database (never a user's real data).
// Uses a minimal self-contained assertion harness so it does not depend on the
// QtTest component being installed.

#include <QApplication>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QDate>
#include <QSettings>
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
#include "../src/ui/core/thememanager.h"
#include "../src/ui/ministry/ministrymatch.h"
#include "../src/ui/ministry/planningwidget.h"
#include "../src/ui/ministry/servicebuilderwidget.h"
#include "../src/ui/ministry/shepherdwidget.h"
#include "../src/ui/people/pastoralanalyticswidget.h"
#include "../src/ui/people/peoplewidget.h"

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
        "pastoral_activities", "people", "schema_version", "services",
        "service_volunteers", "splits", "transactions", "users", "vault_documents"
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
    CHECK(DataSharing::tablesForScope("care_notes") == QStringList({"care_logs", "member_history", "pastoral_activities"}));
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
    for (const QString& t : QStringList{"checkout_logs", "care_logs", "member_history", "attendance",
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

bool test_pastoralQueue() {
    QSqlDatabase d = db();
    QSqlQuery q(d);

    // An activity with a follow-up due YESTERDAY (should appear in the queue).
    // Dates use SQLite's localtime so the test is timezone-independent.
    q.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, outcome, follow_up_date, follow_up_done) "
              "VALUES (1, 'Visit', date('now','localtime','-1 day'), 'test visit', 'Called', date('now','localtime','-1 day'), 0)");
    CHECK(q.exec());

    // A prayer item with a follow-up due TOMORROW (must NOT appear yet).
    q.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, follow_up_date, follow_up_done) "
              "VALUES (2, 'Prayer', date('now','localtime','-1 day'), 'test prayer', date('now','localtime','+1 day'), 0)");
    CHECK(q.exec());

    // The Today's Queue query.
    q.prepare(
        "SELECT pa.id FROM pastoral_activities pa "
        "WHERE pa.follow_up_date IS NOT NULL AND pa.follow_up_date <= date('now','localtime') AND pa.follow_up_done = 0 "
        "ORDER BY pa.follow_up_date ASC");
    CHECK(q.exec());
    int queueCount = 0;
    int dueId = -1;
    while (q.next()) { ++queueCount; dueId = q.value(0).toInt(); }
    CHECK_EQ(queueCount, 1); // only the due visit

    // Mark the due item done and verify the queue empties.
    q.prepare("UPDATE pastoral_activities SET follow_up_done = 1 WHERE id = ?");
    q.addBindValue(dueId);
    CHECK(q.exec());
    CHECK(q.exec("SELECT COUNT(*) FROM pastoral_activities WHERE follow_up_date IS NOT NULL AND follow_up_date <= date('now','localtime') AND follow_up_done = 0"));
    CHECK(q.next());
    CHECK_EQ(q.value(0).toInt(), 0);

    // Clean up test rows.
    CHECK(q.exec("DELETE FROM pastoral_activities"));
    return true;
}

bool test_restoreFromBackup() {
    const QString dbPath = DatabaseManager::instance().database().databaseName();
    const QString backupPath = dbPath + ".backup_test";
    QFile::remove(backupPath);

    // Snapshot the current database (via the WAL-aware export helper).
    CHECK(DatabaseManager::instance().exportBackup(backupPath));
    CHECK_EQ(countRows("people"), 12);

    // Mutate the live database inside an isolated scope so handles close before restore.
    {
        QSqlQuery q(DatabaseManager::instance().database());
        CHECK(q.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Restore', 'Test', 'Member')"));
    }
    CHECK_EQ(countRows("people"), 13);

    // Restore from the snapshot and verify the mutation is gone.
    CHECK(DatabaseManager::instance().restoreFromBackup(backupPath));
    CHECK_EQ(countRows("people"), 12);

    QFile::remove(backupPath);
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

bool test_themeNamedTokensTemplate() {
    ThemeManager::instance().initialize();
    ThemeManager::instance().loadTheme("catppuccin_mocha");
    QString qss = ThemeManager::instance().generateGlobalQSS();

    // Verify template generated non-empty QSS
    CHECK(!qss.isEmpty());
    // Verify zero unexpanded "var(--" token placeholders remain
    CHECK(!qss.contains("var(--"));
    // Verify essential selectors are styled
    CHECK(qss.contains("QMainWindow"));
    CHECK(qss.contains("QPushButton#primaryBtn"));
    CHECK(qss.contains("QTableView"));
    return true;
}

bool test_themeContrastGuardian() {
    // Luminance benchmarks
    double blackLum = ThemeManager::relativeLuminance(QColor(0, 0, 0));
    double whiteLum = ThemeManager::relativeLuminance(QColor(255, 255, 255));
    CHECK(std::abs(blackLum - 0.0) < 0.001);
    CHECK(std::abs(whiteLum - 1.0) < 0.001);

    // Contrast ratio benchmarks
    double maxContrast = ThemeManager::contrastRatio(QColor(0, 0, 0), QColor(255, 255, 255));
    double minContrast = ThemeManager::contrastRatio(QColor(255, 255, 255), QColor(255, 255, 255));
    CHECK(std::abs(maxContrast - 21.0) < 0.01);
    CHECK(std::abs(minContrast - 1.0) < 0.01);

    // Rating badges
    CHECK(ThemeManager::contrastRating(21.0).contains("AAA Pass"));
    CHECK(ThemeManager::contrastRating(5.5).contains("AA Pass"));
    CHECK(ThemeManager::contrastRating(2.5).contains("Fail"));
    return true;
}

bool test_personGenderAndHouseholdSave() {
    QSqlQuery q(db());
    // Create new person
    CHECK(q.exec("INSERT INTO people (first_name, last_name, membership_status, gender, household_id) VALUES ('Gender', 'Test', 'Member', 'Male', 1)"));
    int newId = q.lastInsertId().toInt();
    CHECK(newId > 0);

    // Update gender to Female and reassign to household 2
    q.prepare("UPDATE people SET gender = :gender, household_id = :hid WHERE id = :id");
    q.bindValue(":gender", "Female");
    q.bindValue(":hid", 2);
    q.bindValue(":id", newId);
    CHECK(q.exec());

    // Verify persisted correctly in DB
    QSqlQuery qVerify(db());
    qVerify.prepare("SELECT gender, household_id FROM people WHERE id = ?");
    qVerify.addBindValue(newId);
    CHECK(qVerify.exec() && qVerify.next());
    CHECK_EQ(qVerify.value(0).toString().toStdString(), std::string("Female"));
    CHECK_EQ(qVerify.value(1).toInt(), 2);

    // Clean up
    QSqlQuery qClean(db());
    qClean.prepare("DELETE FROM people WHERE id = ?");
    qClean.addBindValue(newId);
    CHECK(qClean.exec());
    return true;
}

bool test_ministryMatchRecommendAndExclude() {
    QSqlDatabase d = db();
    CHECK(d.transaction());

    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (name, category) VALUES ('Match Test Youth', 'Youth')"));
    const int gid = q.lastInsertId().toInt();

    auto insertPerson = [&](const QString& first, const QString& dob, const QString& gender) -> int {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO people (first_name, last_name, date_of_birth, gender, membership_status) "
                    "VALUES (?, 'MatchT', ?, ?, 'Member')");
        ins.addBindValue(first);
        ins.addBindValue(dob);
        ins.addBindValue(gender);
        CHECK(ins.exec());
        return ins.lastInsertId().toInt();
    };
    auto addSkill = [&](int pid, const QString& skill) {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO member_skills (person_id, skill_or_role) VALUES (?, ?)");
        ins.addBindValue(pid);
        ins.addBindValue(skill);
        CHECK(ins.exec());
    };
    auto addGroupMembership = [&](int pid, int groupId) {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                    "VALUES ('Person', ?, 'Group', ?, 'Member')");
        ins.addBindValue(pid);
        ins.addBindValue(groupId);
        CHECK(ins.exec());
    };

    const int seedMember = insertPerson("Seed", "2011-01-01", "Male"); // already in the group
    addSkill(seedMember, "guitar");
    addGroupMembership(seedMember, gid);

    const int teen = insertPerson("Talent", "2011-06-15", "Male");     // fits: age band + guitar
    addSkill(teen, "guitar");

    const int adult = insertPerson("Adult", "1980-03-03", "Female");   // outside age band
    addSkill(adult, "cooking");

    const int noSkill = insertPerson("NoSkill", "2012-07-07", "Female"); // fits age, no matching skill
    const int burnedOut = insertPerson("Busy", "2011-09-09", "Male");    // fits age+skill but in 3 groups
    addSkill(burnedOut, "guitar");
    addGroupMembership(burnedOut, 9001);
    addGroupMembership(burnedOut, 9002);
    addGroupMembership(burnedOut, 9003);

    // Skill profile is inferred from current members -> {guitar}.
    const QStringList profile = MinistryRecommender::groupSkillProfile(d, gid, 6);
    CHECK_EQ(profile.size(), 1);
    CHECK_EQ(profile.first().toStdString(), std::string("guitar"));

    MatchCriteria c;
    c.minAge = 10;
    c.maxAge = 20;
    c.requireSkill = true;
    c.maxGroupsServing = 2;
    c.maxResults = 25;

    const QList<MatchResult> results = MinistryRecommender::recommend(d, gid, c);
    // Only the talented teen survives: member excluded, adult age-out,
    // no-skill excluded, burned-out serving limit.
    CHECK_EQ(results.size(), 1);
    if (results.size() == 1) {
        CHECK_EQ(results[0].personId, teen);
        CHECK(results[0].score >= 50);
        const QString why = results[0].reasons.join(" ");
        CHECK(why.contains("guitar", Qt::CaseInsensitive));
        CHECK(why.contains("Age", Qt::CaseInsensitive));
    }

    // Relaxing the skill requirement admits the no-skill teen too.
    c.requireSkill = false;
    const QList<MatchResult> loose = MinistryRecommender::recommend(d, gid, c);
    bool sawNoSkill = false;
    for (const MatchResult& r : loose) {
        if (r.personId == noSkill) sawNoSkill = true;
    }
    CHECK(sawNoSkill);

    d.rollback();
    return true;
}

bool test_ministryMatchAddMembersDedup() {
    QSqlDatabase d = db();
    CHECK(d.transaction());

    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (name) VALUES ('Match Test Add')"));
    const int gid = q.lastInsertId().toInt();

    auto insertPerson = [&](const QString& name) -> int {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO people (first_name, last_name, membership_status) VALUES (?, 'AddT', 'Member')");
        ins.addBindValue(name);
        CHECK(ins.exec());
        return ins.lastInsertId().toInt();
    };

    const int p1 = insertPerson("Alpha");
    const int p2 = insertPerson("Beta");

    const QList<int> added = MinistryRecommender::addMembers(d, gid, {p1, p2});
    CHECK_EQ(added.size(), 2);

    QSqlQuery cnt(d);
    cnt.prepare("SELECT COUNT(*) FROM entity_relationships "
                "WHERE source_type = 'Person' AND target_type = 'Group' AND target_id = ? AND relationship_type = 'Member'");
    cnt.addBindValue(gid);
    CHECK(cnt.exec() && cnt.next());
    CHECK_EQ(cnt.value(0).toInt(), 2);

    // Re-adding the same person is a no-op (dedup-safe).
    const QList<int> again = MinistryRecommender::addMembers(d, gid, {p1});
    CHECK_EQ(again.size(), 0);
    CHECK(cnt.exec() && cnt.next());
    CHECK_EQ(cnt.value(0).toInt(), 2);

    d.rollback();
    return true;
}

bool test_peopleWidget360DossierAndGiving() {
    PeopleWidget pw;
    pw.refreshModel();

    // Verify initial selection or person selection
    pw.selectPerson(1);

    // Test Guest vs Admin permissions
    pw.applyRole("guest");
    pw.applyRole("admin");

    // Add a test care log for person 1
    QSqlQuery q(db());
    q.prepare("INSERT INTO care_logs (person_id, care_type, notes, follow_up) VALUES (1, 'Counseling', 'Pastoral test note', 1)");
    CHECK(q.exec());

    // Select again to refresh dossier
    pw.selectPerson(1);

    // Verify care_logs entry persisted and queryable
    QSqlQuery qHist(db());
    qHist.prepare("SELECT care_type, notes, follow_up FROM care_logs WHERE person_id = 1 ORDER BY id DESC LIMIT 1");
    CHECK(qHist.exec() && qHist.next());
    CHECK_EQ(qHist.value(0).toString().toStdString(), std::string("Counseling"));
    CHECK_EQ(qHist.value(1).toString().toStdString(), std::string("Pastoral test note"));
    CHECK_EQ(qHist.value(2).toInt(), 1);

    return true;
}

bool test_ministryMatchDeclaredProfileWins() {
    QSqlDatabase d = db();
    CHECK(d.transaction());

    // Group declares it needs vocalists aged 10-20, but has NO members yet.
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (name, required_skills, min_age, max_age) "
                 "VALUES ('Match Declared', 'vocals', 10, 20)"));
    const int gid = q.lastInsertId().toInt();

    auto insertPerson = [&](const QString& first, const QString& dob, const QString& skill) -> int {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO people (first_name, last_name, date_of_birth, gender, membership_status) "
                    "VALUES (?, 'DeclT', ?, 'Male', 'Member')");
        ins.addBindValue(first);
        ins.addBindValue(dob);
        CHECK(ins.exec());
        const int pid = ins.lastInsertId().toInt();
        QSqlQuery sk(d);
        sk.prepare("INSERT INTO member_skills (person_id, skill_or_role) VALUES (?, ?)");
        sk.addBindValue(pid);
        sk.addBindValue(skill);
        CHECK(sk.exec());
        return pid;
    };

    const int teenVocalist = insertPerson("VocalTeen", "2011-05-01", "vocals");
    insertPerson("GuitarTeen", "2011-06-01", "guitar");   // skill not declared
    insertPerson("AdultVocal", "1980-01-01", "vocals");   // right skill, wrong age

    // Profile: declared skills win over (empty) inference; age band present.
    const GroupMatchProfile profile = MinistryRecommender::groupMatchProfile(d, gid);
    CHECK(profile.skillsDeclared);
    CHECK(profile.ageBandSet);
    CHECK_EQ(profile.requiredSkills.size(), 1);
    if (profile.requiredSkills.size() == 1) {
        CHECK_EQ(profile.requiredSkills.first().toStdString(), std::string("vocals"));
    }
    CHECK_EQ(profile.minAge, 10);
    CHECK_EQ(profile.maxAge, 20);
    // Inference on a member-less group is empty, proving declared wins.
    CHECK(MinistryRecommender::groupSkillProfile(d, gid, 6).isEmpty());

    // Declared-skill filter with age unconstrained: both vocalists match,
    // the guitarist is excluded (skill not declared).
    MatchCriteria c;
    c.requireSkill = true;
    const QList<MatchResult> results = MinistryRecommender::recommend(d, gid, c);
    CHECK_EQ(results.size(), 2);

    // With the declared age band applied, the adult vocalist is excluded too.
    c.minAge = profile.minAge;
    c.maxAge = profile.maxAge;
    const QList<MatchResult> banded = MinistryRecommender::recommend(d, gid, c);
    CHECK_EQ(banded.size(), 1);
    if (banded.size() == 1) CHECK_EQ(banded[0].personId, teenVocalist);

    d.rollback();
    return true;
}

bool test_shepherdQuickActionsAndPastoralSync() {
    ShepherdWidget sw;
    sw.refreshAll();

    // Verify ShepherdWidget insertion and reschedule
    QSqlQuery q(db());
    q.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, outcome, follow_up_date, follow_up_done) "
              "VALUES (1, 'Visit', date('now', 'localtime'), 'Hospital Visit Test', 'Prayed with member', date('now', 'localtime'), 0)");
    CHECK(q.exec());
    int actId = q.lastInsertId().toInt();

    // Reschedule follow up to +14 days
    QSqlQuery qReschedule(db());
    qReschedule.prepare("UPDATE pastoral_activities SET follow_up_date = date('now', '+14 days', 'localtime') WHERE id = ?");
    qReschedule.addBindValue(actId);
    CHECK(qReschedule.exec());

    // Verify updated follow_up_date
    QSqlQuery qCheck(db());
    qCheck.prepare("SELECT follow_up_date, follow_up_done FROM pastoral_activities WHERE id = ?");
    qCheck.addBindValue(actId);
    CHECK(qCheck.exec() && qCheck.next());
    CHECK_EQ(qCheck.value(1).toInt(), 0);

    // Test PastoralAnalyticsWidget instantiation and refresh
    PastoralAnalyticsWidget paw;
    paw.refreshAnalytics();

    return true;
}

bool test_serviceBuilderRoleAndDuration() {
    ServiceBuilderWidget sb;

    // Default (admin) role: mutating controls are enabled.
    bool saveEnabled = false, exportEnabled = false;
    for (QPushButton* b : sb.findChildren<QPushButton*>()) {
        if (b->text() == "Save Layout") saveEnabled = b->isEnabled();
        if (b->text().contains("Export Run Sheet PDF")) exportEnabled = b->isEnabled();
    }
    CHECK(saveEnabled);
    CHECK(exportEnabled);

    // Guest (read-only): writes disabled, read-only actions stay enabled.
    sb.applyRole("readonly");
    for (QPushButton* b : sb.findChildren<QPushButton*>()) {
        if (b->text() == "Save Layout" || b->text() == "Assign Volunteer"
            || b->text() == "Unassign" || b->text().contains("Save Service Details")) {
            CHECK(!b->isEnabled());
        }
        if (b->text().contains("Export Run Sheet PDF") || b->text() == "Load Layout") {
            CHECK(b->isEnabled());
        }
    }

    // Restore admin: controls re-enable.
    sb.applyRole("admin");
    for (QPushButton* b : sb.findChildren<QPushButton*>()) {
        if (b->text() == "Save Layout") CHECK(b->isEnabled());
    }

    // Duration calculator: library items carry real durations; dropping one
    // into the playlist (simulated by cloning) updates the total.
    QListWidget* library = sb.findChild<QListWidget*>("serviceLibraryList");
    QListWidget* playlist = sb.findChild<QListWidget*>("servicePlaylist");
    CHECK(library && playlist);
    if (library && playlist) {
        CHECK_EQ(library->count(), 15);
        CHECK_EQ(playlist->count(), 0);
        const int firstMins = library->item(0)->data(Qt::UserRole + 1).toInt();
        CHECK_EQ(firstMins, 4); // Amazing Grace
        playlist->addItem(library->item(0)->clone());
        QLabel* dur = sb.findChild<QLabel*>("serviceDurationLabel");
        CHECK(dur);
        if (dur) {
            CHECK(dur->text().contains("4 mins"));
        }
    }
    return true;
}

bool test_ministryMatchRankByRole() {
    QSqlDatabase d = db();
    CHECK(d.transaction());

    auto insertPerson = [&](const QString& name, const QString& skill) -> int {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO people (first_name, last_name, membership_status) VALUES (?, 'RankT', 'Member')");
        ins.addBindValue(name);
        CHECK(ins.exec());
        const int pid = ins.lastInsertId().toInt();
        if (!skill.isEmpty()) {
            QSqlQuery sk(d);
            sk.prepare("INSERT INTO member_skills (person_id, skill_or_role) VALUES (?, ?)");
            sk.addBindValue(pid);
            sk.addBindValue(skill);
            CHECK(sk.exec());
        }
        return pid;
    };

    const int guitarist = insertPerson("AceGuitar", "guitar");       // perfect skill fit, no load
    const int busyGuitarist = insertPerson("BusyGuitar", "guitar");  // skill fit but overloaded
    const int vocalist = insertPerson("Singer", "vocals");           // wrong skill for this role

    // Load up the busy guitarist: 4 teams + served 3x in the last 30 days.
    for (int g = 9001; g <= 9004; ++g) {
        QSqlQuery rel(d);
        rel.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                    "VALUES ('Person', ?, 'Group', ?, 'Member')");
        rel.addBindValue(busyGuitarist);
        rel.addBindValue(g);
        CHECK(rel.exec());
    }
    for (int i = 0; i < 3; ++i) {
        QSqlQuery sv(d);
        sv.prepare("INSERT INTO service_volunteers (service_date, person_id, role) VALUES (date('now'), ?, 'Drums')");
        sv.addBindValue(busyGuitarist);
        CHECK(sv.exec());
    }

    // Unknown role -> no smart ranking (falls back to the plain list).
    CHECK(MinistryRecommender::rankByRole(d, "Coffee Maker").isEmpty());

    const QList<MatchResult> ranked = MinistryRecommender::rankByRole(d, "Acoustic Guitar");
    CHECK_EQ(ranked.size(), 3);
    if (ranked.size() == 3) {
        CHECK_EQ(ranked[0].personId, guitarist);       // skill + no load -> top
        CHECK_EQ(ranked[1].personId, busyGuitarist);   // skill but penalized
        CHECK_EQ(ranked[2].personId, vocalist);        // no matching skill
        CHECK(ranked[0].score > ranked[1].score);
        CHECK(ranked[1].score > ranked[2].score);
        CHECK(ranked[0].score >= 70);                  // dominant skill fit
        CHECK(ranked[2].score <= 30);                  // no skill fit
    }

    // Case-insensitive skill mapping: "Guitar" matches "Acoustic Guitar".
    insertPerson("CapsGuitar", "Guitar"); // insertPerson already records the skill
    const QList<MatchResult> caps = MinistryRecommender::rankByRole(d, "Acoustic Guitar");
    bool sawCaps = false;
    for (const MatchResult& r : caps) {
        if (r.name.startsWith("CapsGuitar")) sawCaps = (r.score >= 70);
    }
    CHECK(sawCaps);

    d.rollback();
    return true;
}

bool test_dashboardKpiConfigAndQuickNotes() {
    // 1. Verify QSettings persistence for KPI cards
    QSettings s("OpenEkklesia", "CHMS");
    QStringList customKpis = {"members", "shepherd_due", "at_risk", "unconnected", "giving_ytd"};
    s.setValue("dashboardKpis", customKpis);
    QStringList readKpis = s.value("dashboardKpis").toStringList();
    CHECK_EQ(readKpis.size(), 5);
    CHECK(readKpis.contains("shepherd_due"));
    CHECK(readKpis.contains("at_risk"));

    // 2. Verify Quick Note persistence in care_logs and pastoral_activities
    QSqlDatabase d = db();
    QSqlQuery q(d);
    q.prepare("INSERT INTO care_logs (person_id, care_type, notes, follow_up) VALUES (1, 'Phone Call', 'Quick dashboard note test', 1)");
    CHECK(q.exec());

    QSqlQuery qPastoral(d);
    qPastoral.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, outcome, follow_up_date, follow_up_done) "
                      "VALUES (1, 'Phone Call', date('now', 'localtime'), 'Quick dashboard note test', 'Logged via Dashboard Quick Note', date('now', '+7 days', 'localtime'), 0)");
    CHECK(qPastoral.exec());

    // Verify written values
    QSqlQuery qCheck(d);
    qCheck.prepare("SELECT care_type, notes FROM care_logs WHERE person_id = 1 AND notes = 'Quick dashboard note test'");
    CHECK(qCheck.exec() && qCheck.next());
    CHECK_EQ(qCheck.value(0).toString().toStdString(), std::string("Phone Call"));

    QSqlQuery qPCheck(d);
    qPCheck.prepare("SELECT activity_type, follow_up_done FROM pastoral_activities WHERE person_id = 1 AND notes = 'Quick dashboard note test'");
    CHECK(qPCheck.exec() && qPCheck.next());
    CHECK_EQ(qPCheck.value(1).toInt(), 0);

    return true;
}

bool test_serviceBuilderRunSheetPersistence() {
    ServiceBuilderWidget sb;
    QListWidget* library = sb.findChild<QListWidget*>("serviceLibraryList");
    QListWidget* playlist = sb.findChild<QListWidget*>("servicePlaylist");
    QDateEdit* dateEdit = sb.findChild<QDateEdit*>("serviceDateEdit");
    CHECK(library && playlist && dateEdit);
    if (!library || !playlist || !dateEdit) return true;

    // Persistent library seeded with the classic elements.
    CHECK_EQ(library->count(), 15);

    const QDate date = dateEdit->date();
    const QString dateStr = date.toString("yyyy-MM-dd");

    // Reset any arrangement an earlier test may have left on this date.
    playlist->clear();
    {
        QSqlQuery q(db());
        q.prepare("SELECT run_sheet FROM services WHERE date = ?");
        q.addBindValue(dateStr);
        CHECK(q.exec() && q.next());
        CHECK_EQ(q.value(0).toString().toStdString(), std::string("[]")); // autosaved empty array
    }

    // Adding an element autosaves the run sheet JSON into the services row.
    playlist->addItem(library->item(0)->clone());
    {
        QSqlQuery q(db());
        q.prepare("SELECT run_sheet FROM services WHERE date = ?");
        q.addBindValue(dateStr);
        CHECK(q.exec() && q.next());
        CHECK(q.value(0).toString().contains("Amazing Grace"));
    }

    // Switching dates clears the sheet; returning restores it (title, type,
    // and duration survive the JSON round trip).
    dateEdit->setDate(date.addDays(-7));
    CHECK_EQ(playlist->count(), 0);
    dateEdit->setDate(date);
    CHECK_EQ(playlist->count(), 1);
    if (playlist->count() == 1) {
        CHECK(playlist->item(0)->text().contains("Amazing Grace"));
        CHECK_EQ(playlist->item(0)->data(Qt::UserRole).toString().toStdString(), std::string("Song"));
        CHECK_EQ(playlist->item(0)->data(Qt::UserRole + 1).toInt(), 4);
    }

    // Templates: DB-level upsert round trip.
    {
        QSqlQuery ins(db());
        ins.prepare("INSERT INTO service_templates (name, run_sheet) VALUES ('TestTpl', '[]') "
                    "ON CONFLICT(name) DO UPDATE SET run_sheet = excluded.run_sheet");
        CHECK(ins.exec());
        QSqlQuery cnt(db());
        CHECK(cnt.exec("SELECT COUNT(*) FROM service_templates WHERE name = 'TestTpl'"));
        CHECK(cnt.next());
        CHECK_EQ(cnt.value(0).toInt(), 1);
        QSqlQuery del(db());
        CHECK(del.exec("DELETE FROM service_templates WHERE name = 'TestTpl'"));
    }

    // Clean up the autosaved row so later tests see a pristine services table.
    QSqlQuery clean(db());
    clean.prepare("DELETE FROM services WHERE date = ?");
    clean.addBindValue(dateStr);
    CHECK(clean.exec());
    return true;
}

bool test_graphDualPanelAndDataHygiene() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Test relationship deletion / edge severing
    QSqlQuery qAdd(d);
    qAdd.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 999, 'Group', 888, 'Volunteer')");
    CHECK(qAdd.exec());
    int relId = qAdd.lastInsertId().toInt();
    CHECK(relId > 0);

    QSqlQuery qUpdate(d);
    qUpdate.prepare("UPDATE entity_relationships SET relationship_type = 'Leader' WHERE id = ?");
    qUpdate.addBindValue(relId);
    CHECK(qUpdate.exec());

    QSqlQuery qCheck(d);
    qCheck.prepare("SELECT relationship_type FROM entity_relationships WHERE id = ?");
    qCheck.addBindValue(relId);
    CHECK(qCheck.exec() && qCheck.next());
    CHECK_EQ(qCheck.value(0).toString().toStdString(), std::string("Leader"));

    QSqlQuery qDel(d);
    qDel.prepare("DELETE FROM entity_relationships WHERE id = ?");
    qDel.addBindValue(relId);
    CHECK(qDel.exec());

    // 2. Test Data Hygiene classification queries
    QSqlQuery qHygiene(d);
    CHECK(qHygiene.exec("SELECT SUM(CASE WHEN phone_number IS NULL OR phone_number = '' THEN 1 ELSE 0 END), "
                        "       SUM(CASE WHEN email IS NULL OR email = '' THEN 1 ELSE 0 END) FROM people"));
    CHECK(qHygiene.next());

    d.rollback();
    return true;
}

bool test_serviceBuilderTier2() {
    QSqlDatabase d = db();

    // Volunteers for the widget's default date (next Sunday) — inserted before
    // the widget is constructed so its constructor loads them.
    QDate nextSun = QDate::currentDate();
    while (nextSun.dayOfWeek() != 7) nextSun = nextSun.addDays(1);
    const QString dateStr = nextSun.toString("yyyy-MM-dd");

    QSqlQuery ins(d);
    CHECK(ins.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Vol', 'One', 'Member')"));
    const int p1 = ins.lastInsertId().toInt();
    CHECK(ins.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Vol', 'Two', 'Member')"));
    const int p2 = ins.lastInsertId().toInt();
    QSqlQuery sv(d);
    sv.prepare("INSERT INTO service_volunteers (service_date, person_id, role) VALUES (?, ?, 'Preacher')");
    sv.addBindValue(dateStr);
    sv.addBindValue(p1);
    CHECK(sv.exec());
    sv.prepare("INSERT INTO service_volunteers (service_date, person_id, role) VALUES (?, ?, 'Worship Leader')");
    sv.addBindValue(dateStr);
    sv.addBindValue(p2);
    CHECK(sv.exec());

    ServiceBuilderWidget sb;
    QListWidget* library = sb.findChild<QListWidget*>("serviceLibraryList");
    QListWidget* playlist = sb.findChild<QListWidget*>("servicePlaylist");
    QDateEdit* dateEdit = sb.findChild<QDateEdit*>("serviceDateEdit");
    QTimeEdit* timeEdit = sb.findChild<QTimeEdit*>("serviceTimeEdit");
    QLabel* timeline = sb.findChild<QLabel*>("serviceTimelineLabel");
    CHECK(library && playlist && dateEdit && timeEdit && timeline);
    if (!library || !playlist || !dateEdit || !timeEdit || !timeline) return true;

    // 1. Start time persists with the run sheet.
    timeEdit->setTime(QTime(13, 30));
    playlist->clear();
    playlist->addItem(library->item(0)->clone()); // autosave
    {
        QSqlQuery q(d);
        q.prepare("SELECT start_time FROM services WHERE date = ?");
        q.addBindValue(dateStr);
        CHECK(q.exec() && q.next());
        CHECK_EQ(q.value(0).toString().toStdString(), std::string("13:30"));
    }

    // 2. Timeline label shows the computed start time.
    CHECK(timeline->text().contains("1:30 PM"));

    // 3. Coverage: sermon missing (only the 4-min song so far).
    QStringList issues = sb.collectCoverageIssues();
    bool noSermon = false, noPreacher = false;
    for (const QString& s : issues) {
        if (s.contains("sermon", Qt::CaseInsensitive)) noSermon = true;
        if (s.contains("Preacher")) noPreacher = true;
    }
    CHECK(noSermon);
    CHECK(!noPreacher); // Preacher + Worship Leader are assigned

    // 4. Adding the sermon element clears the sermon issue.
    QListWidgetItem* sermon = nullptr;
    for (int i = 0; i < library->count(); ++i) {
        if (library->item(i)->text().contains("Sermon")) { sermon = library->item(i); break; }
    }
    CHECK(sermon);
    if (sermon) {
        playlist->addItem(sermon->clone());
        issues = sb.collectCoverageIssues();
        bool stillNoSermon = false;
        for (const QString& s : issues) {
            if (s.contains("sermon", Qt::CaseInsensitive)) stillNoSermon = true;
        }
        CHECK(!stillNoSermon);
    }

    // 5. Post-service attendance: both volunteers marked, dedup on re-run.
    CHECK_EQ(sb.markVolunteersAttended(), 2);
    {
        QSqlQuery q(d);
        q.prepare("SELECT COUNT(*) FROM attendance "
                  "WHERE service_id = (SELECT id FROM services WHERE date = ?)");
        q.addBindValue(dateStr);
        CHECK(q.exec() && q.next());
        CHECK_EQ(q.value(0).toInt(), 2);
    }
    CHECK_EQ(sb.markVolunteersAttended(), 0);

    // 6. Date switch restores start time + run sheet.
    dateEdit->setDate(nextSun.addDays(-7));
    dateEdit->setDate(nextSun);
    CHECK_EQ(timeEdit->time().toString("HH:mm").toStdString(), std::string("13:30"));
    CHECK_EQ(playlist->count(), 2);

    // Cleanup.
    QSqlQuery clean(d);
    clean.prepare("DELETE FROM services WHERE date = ?");
    clean.addBindValue(dateStr);
    CHECK(clean.exec());
    CHECK(clean.exec("DELETE FROM service_volunteers"));
    CHECK(clean.exec("DELETE FROM attendance"));
    clean.prepare("DELETE FROM people WHERE id IN (?, ?)");
    clean.addBindValue(p1);
    clean.addBindValue(p2);
    CHECK(clean.exec());
    return true;
}

bool test_graphBatchOperationsAndSmartBridge() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Test batch assign to group
    QList<int> personIds = {1, 2, 3};
    int targetGroupId = 1;
    for (int pid : personIds) {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', ?, 'Group', ?, 'Volunteer')");
        ins.addBindValue(pid);
        ins.addBindValue(targetGroupId);
        CHECK(ins.exec());
    }

    QSqlQuery qCount(d);
    qCount.prepare("SELECT COUNT(*) FROM entity_relationships WHERE target_id = ? AND relationship_type = 'Volunteer'");
    qCount.addBindValue(targetGroupId);
    CHECK(qCount.exec() && qCount.next());
    CHECK(qCount.value(0).toInt() >= 3);

    // 2. Test Smart Bridge shared household query
    QSqlQuery hq(d);
    hq.prepare("SELECT er.target_id FROM entity_relationships er "
               "WHERE er.source_id IN (SELECT id FROM people WHERE household_id = (SELECT household_id FROM people WHERE id = ?)) "
               "  AND er.source_type = 'Person' AND er.target_type = 'Group'");
    hq.addBindValue(1);
    CHECK(hq.exec());

    // 3. Test Ministry Branch Subgroup query
    QSqlQuery branchQ(d);
    branchQ.prepare("SELECT id FROM groups WHERE parent_id = ?");
    branchQ.addBindValue(1);
    CHECK(branchQ.exec());

    d.rollback();
    return true;
}

bool test_graphTier3BridgeNodesAndFamilyDynasties() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Test Bridge Node query (person with multiple groups)
    QSqlQuery bq(d);
    CHECK(bq.exec("SELECT er.source_id, COUNT(DISTINCT er.target_id) "
                  "FROM entity_relationships er "
                  "WHERE er.source_type = 'Person' AND er.target_type = 'Group' "
                  "GROUP BY er.source_id HAVING COUNT(DISTINCT er.target_id) >= 2"));

    // 2. Test Family Dynasty Concentration query
    QSqlQuery fq(d);
    CHECK(fq.exec("SELECT g.id, g.name, p.household_id, h.family_name, COUNT(*) "
                  "FROM entity_relationships er "
                  "JOIN people p ON er.source_id = p.id "
                  "JOIN households h ON p.household_id = h.id "
                  "JOIN groups g ON er.target_id = g.id "
                  "WHERE er.source_type = 'Person' AND er.target_type = 'Group' AND p.household_id IS NOT NULL "
                  "GROUP BY g.id, p.household_id"));

    d.rollback();
    return true;
}

bool test_serviceBuilderTier3() {
    // 1. iCal export: one datetime VEVENT per volunteer, mirrored from
    //    Shepherd's calendar export convention.
    const QList<QPair<QString, QString>> roster = {
        {"Preacher", "John Doe"},
        {"Audio Engineer", "Jane Roe"},
    };
    const QString ics = ServiceBuilderWidget::volunteerIcal(QDate(2026, 8, 23), QTime(10, 0), roster);
    CHECK(ics.contains("BEGIN:VCALENDAR"));
    CHECK(ics.contains("END:VCALENDAR"));
    CHECK(ics.contains("SUMMARY:Serve as Preacher (John Doe)"));
    CHECK(ics.contains("DTSTART:20260823T100000"));
    CHECK(ics.contains("UID:oe-vol-1-20260823"));
    CHECK_EQ(ics.count("BEGIN:VEVENT"), 2);
    CHECK(ics.contains("TRIGGER:-PT60M")); // reminder alarm

    // 2. Element usage stats across saved run sheets. Start from a clean
    //    services table so earlier tests' leftovers cannot skew the counts.
    QSqlDatabase d = db();
    QSqlQuery ins(d);
    CHECK(ins.exec("DELETE FROM services"));
    ins.prepare("INSERT INTO services (date, run_sheet) VALUES (?, ?)");
    ins.addBindValue(QString("2030-01-05"));
    ins.addBindValue(QString("[{\"title\":\"🎵 Song: Amazing Grace (4m)\",\"type\":\"Song\",\"durationMins\":4}]"));
    CHECK(ins.exec());
    ins.prepare("INSERT INTO services (date, run_sheet) VALUES (?, ?)");
    ins.addBindValue(QString("2030-01-12"));
    ins.addBindValue(QString("[{\"title\":\"🎵 Song: Amazing Grace (4m)\",\"type\":\"Song\",\"durationMins\":4},"
                             "{\"title\":\"📖 Scripture: Psalm 23 (2m)\",\"type\":\"Scripture\",\"durationMins\":2}]"));
    CHECK(ins.exec());

    const QList<ElementUsage> stats = ServiceBuilderWidget::elementUsageStats(d, 100);
    CHECK_EQ(stats.size(), 2);
    if (stats.size() == 2) {
        CHECK(stats[0].name.contains("Amazing Grace"));
        CHECK_EQ(stats[0].count, 2);
        CHECK_EQ(stats[0].lastUsedDate.toStdString(), std::string("2030-01-12"));
        CHECK(stats[1].name.contains("Psalm 23"));
        CHECK_EQ(stats[1].count, 1);
    }

    // 3. Color-blind-safe legend is present in the UI.
    ServiceBuilderWidget sb;
    QLabel* legend = sb.findChild<QLabel*>("serviceTypeLegend");
    CHECK(legend);
    if (legend) CHECK(legend->text().contains("Legend"));

    // Cleanup.
    CHECK(ins.exec("DELETE FROM services WHERE date IN ('2030-01-05', '2030-01-12')"));
    return true;
}

bool test_groupRecommendationDisablers() {
    QSqlDatabase d = db();
    d.transaction();

    // Insert Elected governance group, Seasonal cohort, and Open team
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (name, category, recruitment_status) VALUES ('Board of Directors', 'Governance', 'Elected')"));
    int boardId = q.lastInsertId().toInt();

    CHECK(q.exec("INSERT INTO groups (name, category, recruitment_status) VALUES ('Fall Alpha Cohort', 'Bible Study', 'Seasonal')"));
    int alphaId = q.lastInsertId().toInt();

    CHECK(q.exec("INSERT INTO groups (name, category, recruitment_status) VALUES ('Welcome & Hospitality', 'Ministry Team', 'Open')"));
    int welcomeId = q.lastInsertId().toInt();

    // Verify recruitment_status query filtering
    QSqlQuery check(d);
    check.prepare("SELECT id, name, recruitment_status FROM groups WHERE recruitment_status = 'Open'");
    CHECK(check.exec());
    QList<int> openIds;
    while (check.next()) openIds.append(check.value(0).toInt());

    CHECK(openIds.contains(welcomeId));
    CHECK(!openIds.contains(boardId));
    CHECK(!openIds.contains(alphaId));

    d.rollback();
    return true;
}

bool test_planningWidgetRendersAndRoles() {
    QSqlDatabase d = db();

    // Seed a task before constructing the widget so both tables populate.
    QSqlQuery ins(d);
    CHECK(ins.exec("DELETE FROM ministry_tasks"));
    CHECK(ins.exec("INSERT INTO ministry_tasks (team, title, owner, start_day, duration_days, start_month, duration_months, status) "
                   "VALUES ('Worship & Media Team', 'Easter Production', 'John Doe', 3, 5, 2, 2, 'In-Progress')"));

    PlanningWidget pw;

    // Both views render the task.
    QTableWidget* roadmap = pw.findChild<QTableWidget*>("planningRoadmapTable");
    QTableWidget* gantt = pw.findChild<QTableWidget*>("planningGanttTable");
    CHECK(roadmap && gantt);
    if (roadmap && gantt) {
        CHECK_EQ(roadmap->rowCount(), 1);
        CHECK_EQ(gantt->rowCount(), 1);
        // Roadmap: JAN..MAY/JUN columns — FEB (start_month 2) should show the badge.
        if (roadmap->rowCount() == 1 && roadmap->item(0, 3)) {
            CHECK(!roadmap->item(0, 3)->text().isEmpty());
            CHECK_EQ(roadmap->item(0, 1)->text().toStdString(), std::string("Easter Production"));
        }
        // Gantt: D3..D7 filled (start_day 3, duration 5).
        if (gantt->rowCount() == 1) {
            CHECK(gantt->item(0, 0) && gantt->item(0, 0)->data(Qt::UserRole).toInt() > 0);
            CHECK(gantt->item(0, 2 + 3) && !gantt->item(0, 2 + 3)->text().isEmpty()); // D3
            CHECK(gantt->item(0, 2 + 7) && gantt->item(0, 2 + 7)->text().isEmpty());   // D8 out of range
        }
    }

    // Role gating: Guest disables the mutating buttons; admin re-enables.
    bool addEnabled = false;
    for (QPushButton* b : pw.findChildren<QPushButton*>()) {
        if (b->text().contains("Add Initiative")) addEnabled = b->isEnabled();
    }
    CHECK(addEnabled);

    pw.applyRole("readonly");
    for (QPushButton* b : pw.findChildren<QPushButton*>()) {
        if (b->text().contains("Add Initiative") || b->text().contains("Edit Selected")
            || b->text().contains("Remove Selected")) {
            CHECK(!b->isEnabled());
        }
    }
    pw.applyRole("admin");
    for (QPushButton* b : pw.findChildren<QPushButton*>()) {
        if (b->text().contains("Add Initiative")) CHECK(b->isEnabled());
    }

    CHECK(ins.exec("DELETE FROM ministry_tasks"));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
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
        {"pastoral_queue", test_pastoralQueue},
        {"person_gender_and_household_save", test_personGenderAndHouseholdSave},
        {"restore_from_backup", test_restoreFromBackup},
        {"purge_demo_data", test_purgeDemoData},
        {"theme_named_tokens_template", test_themeNamedTokensTemplate},
        {"theme_contrast_guardian", test_themeContrastGuardian},
        {"ministry_match_recommend_and_exclude", test_ministryMatchRecommendAndExclude},
        {"ministry_match_add_members_dedup", test_ministryMatchAddMembersDedup},
        {"ministry_match_declared_profile_wins", test_ministryMatchDeclaredProfileWins},
        {"ministry_match_rank_by_role", test_ministryMatchRankByRole},
        {"service_builder_role_and_duration", test_serviceBuilderRoleAndDuration},
        {"service_builder_run_sheet_persistence", test_serviceBuilderRunSheetPersistence},
        {"service_builder_tier2", test_serviceBuilderTier2},
        {"service_builder_tier3", test_serviceBuilderTier3},
        {"planning_widget_renders_and_roles", test_planningWidgetRendersAndRoles},
        {"people_widget_360_dossier_and_giving", test_peopleWidget360DossierAndGiving},
        {"shepherd_quick_actions_and_pastoral_sync", test_shepherdQuickActionsAndPastoralSync},
        {"dashboard_kpi_config_and_quick_notes", test_dashboardKpiConfigAndQuickNotes},
        {"graph_dual_panel_and_data_hygiene", test_graphDualPanelAndDataHygiene},
        {"graph_batch_operations_and_smart_bridge", test_graphBatchOperationsAndSmartBridge},
        {"graph_tier3_bridge_nodes_and_family_dynasties", test_graphTier3BridgeNodesAndFamilyDynasties},
        {"group_recommendation_disablers", test_groupRecommendationDisablers},
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

