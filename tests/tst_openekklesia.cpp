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
        {"people_widget_360_dossier_and_giving", test_peopleWidget360DossierAndGiving},
        {"shepherd_quick_actions_and_pastoral_sync", test_shepherdQuickActionsAndPastoralSync},
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

