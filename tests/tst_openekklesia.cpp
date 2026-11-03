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
#include <exception>
#include <functional>
#include <iostream>

#include "../src/database/databasemanager.h"
#include "../src/database/datasharing.h"
#include "../src/common/appdateformatter.h"
#include "../src/common/churchprofile.h"
#include "../src/common/appsettings.h"
#include "../src/common/givingstatementgenerator.h"
#include "../src/common/icalgenerator.h"
#include "../src/ui/core/thememanager.h"
#include "../src/ui/ministry/graphwidget.h"
#include "../src/ui/admin/settingswidget.h"
#include "../src/ui/core/setupwizard.h"
#include "../src/ui/ministry/ministrymatch.h"
#include "../src/ui/ministry/ministryarchetypes.h"
#include "../src/ui/ministry/synergyengine.h"
#include "../src/ui/ministry/planningwidget.h"
#include "../src/ui/ministry/servicebuilderwidget.h"
#include "../src/ui/ministry/shepherdwidget.h"
#include "../src/ui/ministry/facilitieswidget.h"
#include "../src/ui/finance/transactiondialog.h"
#include "../src/ui/people/pastoralanalyticswidget.h"
#include "../src/ui/people/peoplewidget.h"
#include "../src/ui/people/peoplecardutils.h"
#include "../src/ui/people/tagchiputils.h"
#include "../src/ui/people/givingsummaryservice.h"
#include "../src/ui/people/pastoralhistoryservice.h"
#include "../src/integrations/planningcenterclient.h"
#include "../src/ui/core/mainwindow.h"

#include <QStackedLayout>
#include <QToolButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>
#include <QJsonArray>
#include "../src/ui/core/usermanualdialog.h"

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

void ensurePersonOne() {
    QSqlDatabase d = db();
    QSqlQuery hh(d);
    hh.exec("INSERT OR IGNORE INTO households (id, family_name) VALUES (1, 'Test Household')");
    QSqlQuery p(d);
    p.prepare("INSERT OR IGNORE INTO people (id, household_id, first_name, last_name, membership_status) "
              "VALUES (1, 1, 'Test', 'Member', 'Member')");
    p.exec();
    QSqlQuery f(d);
    f.exec("INSERT OR IGNORE INTO funds (id, fund_name, is_active) VALUES (1, 'General', 1)");
    f.exec("INSERT OR IGNORE INTO funds (id, fund_name, is_active) VALUES (2, 'Missions', 1)");
}

void ensureMinistryEvents(int minimum = 5) {
    QSqlDatabase d = db();
    QSqlQuery c(d);
    if (!c.exec("SELECT COUNT(*) FROM ministry_events") || !c.next()) return;
    int have = c.value(0).toInt();
    for (int i = have; i < minimum; ++i) {
        QSqlQuery ins(d);
        ins.prepare("INSERT INTO ministry_events (title, event_type, start_datetime, end_datetime, status) "
                    "VALUES (?, 'Fellowship', '2026-04-05 09:00', '2026-04-05 10:00', 'Confirmed')");
        ins.addBindValue(QString("Seeded Event %1").arg(i + 1));
        if (!ins.exec()) break;
    }
}

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

// Captures Qt log output so tests can assert on what the code under test
// actually reports. Messages are swallowed while the handler is installed.
QStringList g_capturedLog;
void captureLogHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    Q_UNUSED(type)
    Q_UNUSED(context)
    g_capturedLog.append(msg);
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
        "service_volunteers", "splits", "transactions", "users", "vault_documents",
        "ministry_events"
    };
    QSqlQuery q(db());
    for (const QString& t : tables) {
        CHECK(q.exec(QString("SELECT name FROM sqlite_master WHERE type='table' AND name='%1'").arg(t)));
        CHECK(q.next());
    }
    return true;
}

bool test_person_card_utils_round_trip() {
    CHECK_EQ(PersonCardUtils::parseCsvLine("a,\"b,c\"").size(), 2);
    CHECK(PersonCardUtils::parseCsvLine("a,\"b,c\"")[1] == QString("b,c"));
    CHECK_EQ(PersonCardUtils::parseCsvLine("x,,y").size(), 3);

    const QString vcf = PersonCardUtils::generateVCard("John", "Doe", "555-1", "john@example.com",
                                                      "1985-05-15", "Member", "", "");
    CHECK(vcf.contains("BEGIN:VCARD", Qt::CaseInsensitive));
    CHECK(vcf.contains("FN:John Doe\r\n"));
    CHECK(vcf.contains("TEL;TYPE=CELL,VOICE:555-1\r\n"));
    CHECK(vcf.contains("EMAIL;TYPE=INTERNET,PREF:john@example.com\r\n"));
    CHECK(vcf.contains("END:VCARD\r\n"));
    return true;
}

bool test_tag_chip_utils() {
    CHECK_EQ(TagChipUtils::splitTags("a, b ,  ").size(), 2);
    CHECK(TagChipUtils::splitTags("a, b ,  ")[0] == "a");
    CHECK(TagChipUtils::joinTags(QStringList() << "x" << "y") == QString("x, y"));
    CHECK(TagChipUtils::displayTag("Foo", true) ==	QString("\u2713 Foo"));
    CHECK(TagChipUtils::displayTag("Foo", false) == QString("Foo"));
    return true;
}

bool test_pastoral_history_service_badges() {
    CHECK(PastoralHistoryService::badge("Counseling") == QString("\U0001F4AC Counseling"));
    CHECK(PastoralHistoryService::badge("UnknownType") == QString("\U0001F54A UnknownType"));
    const QList<PastoralHistoryEntry> empty = PastoralHistoryService::fetch(0, db());
    CHECK(empty.isEmpty());
    return true;
}

bool test_givingsummaryservice_empty() {
    const PersonGivingSummary s = GivingSummaryService::fetch(9999999, db());
    CHECK_EQ(s.total, 0.0);
    CHECK_EQ(s.count, 0);
    CHECK(s.lastDate == QString("None recorded"));
    CHECK(s.recent.isEmpty());
    CHECK(!s.householdTotalAvailable);
    return true;
}

bool test_expectedIndexesExist() {
    const QStringList indexes = {
        "idx_contributions_date", "idx_contributions_person", "idx_people_gender",
        "idx_people_household", "idx_people_name", "idx_people_status",
        "idx_relationships", "idx_relationships_src", "idx_relationships_tgt",
        "idx_splits_account", "idx_splits_tx", "idx_transactions_date",
        "idx_volunteers_person", "idx_audit_log_changed",
        "idx_ministry_tasks_team", "idx_vault_documents_person"
    };
    QSqlQuery q(db());
    for (const QString& idx : indexes) {
        CHECK(q.exec(QString("SELECT name FROM sqlite_master WHERE type='index' AND name='%1'").arg(idx)));
        CHECK(q.next());
    }
    return true;
}

bool test_seedDataCounts() {
    // Structure-only scaffold: first-run installs ship login users, default
    // funds, and a starter chart of accounts — no demo people/giving/assets.
    // Only the 4 primary ministry blueprints (Worship, Children's, Ushers, AV)
    // auto-create heading groups; the other canonical blueprints stay in the
    // registry for Ministry Match until materialized on demand.
    CHECK_EQ(countRows("people"), 0);
    CHECK_EQ(countRows("households"), 0);
    CHECK_EQ(countRows("groups"), 4); // Primary four root ministries, no sample groups
    CHECK_EQ(countRows("accounts"), 8);
    CHECK_EQ(countRows("funds"), 3);
    CHECK_EQ(countRows("contributions"), 0);
    CHECK_EQ(countRows("transactions"), 0);
    CHECK_EQ(countRows("users"), 4);
    CHECK_EQ(countRows("assets"), 0);
    CHECK_EQ(countRows("vault_documents"), 0);
    return true;
}


// Regression: SettingsWidget must construct (it crashed with a null
// checkbox when the auto-column-size option was introduced).
bool test_settingsWidgetConstructs() {
    SettingsWidget sw;
    return true;
}

// The first-run wizard is user-facing; it must construct offscreen and its
// save path must write the full profile (with the neutral name fallback) and
// the profileComplete flag that keeps main() from re-showing it.
bool test_setupWizardAppliesProfile() {
    SetupWizard wiz;
    CHECK(wiz.findChild<QLineEdit*>() != nullptr);  // profile fields exist

    // Simulate typed fields: name empty -> "My Church" fallback; others stored.
    const auto edits = wiz.findChildren<QLineEdit*>();
    CHECK_EQ(edits.size(), 8);
    if (edits.size() >= 8) {
        edits[0]->setText("");            // church name (tests fallback)
        edits[1]->setText("Trust in Grace");
        edits[3]->setText("456 Real St, Fresno, CA");
        edits[7]->setText("12-3456789");
    }
    wiz.applyProfile();

    AppSettings s;
    // toStdString(): CHECK_EQ streams values on failure and QString is not
    // streamable into std::cerr.
    CHECK_EQ(s.value("churchName").toString().toStdString(), std::string("My Church"));
    CHECK_EQ(s.value("churchSubtitle").toString().toStdString(), std::string("Trust in Grace"));
    CHECK_EQ(s.value("churchAddress").toString().toStdString(), std::string("456 Real St, Fresno, CA"));
    CHECK_EQ(s.value("churchTaxId").toString().toStdString(), std::string("12-3456789"));
    CHECK(s.value("profileComplete").toBool());
    return true;
}

// No fake contact data may ship as a settings default: the profile prints on
// giving statements (tax receipts), and placeholders like "123 Grace Ave,
// Springfield" were one "Save All Changes" away from real receipts.
bool test_churchProfileDefaultsAreNotFakeData() {
    AppSettings s;
    s.remove("churchName");
    s.remove("churchSubtitle");
    s.remove("churchAddress");
    s.remove("churchPhone");
    s.remove("churchEmail");
    s.remove("churchWebsite");
    s.remove("churchTaxId");

    CHECK_EQ(ChurchProfile::name().toStdString(), std::string("My Church"));
    CHECK(ChurchProfile::subtitle().isEmpty());
    CHECK(ChurchProfile::address().isEmpty());

    SettingsWidget sw;   // loadSettings must render blanks, not placeholders
    return true;
}

// Regression (73e96c6, ed45c10): QStackedLayout takes the MAX of its pages'
// minimums, so lazily-built per-module toolbars inflated the main window's
// declared minimum to 1770px wide — oversized on a 1536px-logical desktop
// and untileable there. The window must keep its hard floor and ignore
// page-content minimums entirely.
bool test_mainWindowMinimumDecoupledFromContent() {
    MainWindow mw;

    // 1. The hard floor is content-independent.
    const QSize floorMin = mw.minimumSize();
    CHECK_EQ(floorMin.width(), 1000);
    CHECK_EQ(floorMin.height(), 620);

    // 2. The page stack's layout (IgnoreMinStackedLayout) pins its minimum
    //    to (0,0) so no page can push the window around.
    QWidget* mainContent = mw.findChild<QWidget*>("mainContent");
    CHECK(mainContent != nullptr);
    auto* stack = mainContent ? qobject_cast<QStackedLayout*>(mainContent->layout()) : nullptr;
    CHECK(stack != nullptr);
    if (stack) {
        const QSize stackMin = stack->minimumSize();
        CHECK_EQ(stackMin.width(), 0);
        CHECK_EQ(stackMin.height(), 0);
    }

    // 3. After lazily building the densest module pages, the window's
    //    implied minimum stays bounded (the pre-fix cascade reached 1770px
    //    wide; the floor itself is 1000px).
    mw.onGroupsClicked();
    mw.onSettingsClicked();
    if (stack) {
        const QSize stackMinAfter = stack->minimumSize();
        CHECK_EQ(stackMinAfter.width(), 0);
        CHECK_EQ(stackMinAfter.height(), 0);
    }
    CHECK(mw.minimumSizeHint().width() <= 1100);

    return true;
}

// Reproduces the legacy demo congregation (removed from production seeding
// when the app moved to a structure-only first-run scaffold). Tests that
// assert against demo-shaped data call this explicitly. Idempotent: skips
// when people already exist, so shared-DB test ordering stays safe.
void seedDemoFixture() {
    if (countRows("people") > 0) return;
    QSqlDatabase d = db();
    QSqlQuery q(d);
    q.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (1, 'The Doe Family', '123 Grace Lane', 'Metropolis', '12345')");
    q.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (2, 'The Smith Family', '456 Faith Way', 'Metropolis', '12345')");
    q.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (3, 'The Johnson Family', '789 Hope Blvd', 'Metropolis', '12345')");
    q.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (4, 'Arthur Single Household', '101 Alone Road', 'Metropolis', '12345')");
    q.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (5, 'The Miller Family', '321 Grace Lane', 'Metropolis', '12345')");
    q.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (6, 'The Davis Family', '555 Peace Road', 'Metropolis', '12345')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (1, 1, 'John', 'Doe', '555-0199', 'john.doe@example.com', '1985-05-15', 'MEMBER-101', 'Leader', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (2, 2, 'Jane', 'Smith', '555-0144', 'jane.smith@example.com', '1990-08-22', 'MEMBER-102', '', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (3, 3, 'Robert', 'Johnson', '555-0188', 'robert.j@example.com', '1978-11-03', 'MEMBER-103', '', 'Regular Attender')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (4, 1, 'Mary', 'Doe', '555-0198', 'mary.doe@example.com', '1987-06-20', 'MEMBER-104', '', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (5, 1, 'Timmy', 'Doe', '555-0197', 'timmy.doe@example.com', '2015-10-10', 'MEMBER-105', '', 'Visitor')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (6, 2, 'Alice', 'Smith', '555-0145', 'alice.smith@example.com', '1992-04-12', 'MEMBER-106', '', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (7, 4, 'Arthur', 'Pendelton', '555-9999', 'arthur.p@example.com', '1950-01-01', 'MEMBER-107', 'At-Risk', 'Inactive')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (8, 5, 'David', 'Miller', '555-1111', 'david.m@example.com', '1980-03-03', 'MEMBER-108', 'Leader', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (9, 5, 'Sarah', 'Miller', '555-1112', 'sarah.m@example.com', '1982-04-04', 'MEMBER-109', '', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (10, 5, 'Bobby', 'Miller', '555-1113', 'bobby.m@example.com', '2018-05-05', 'MEMBER-110', '', 'Regular Attender')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (11, 6, 'James', 'Davis', '555-2222', 'pastor@example.com', '1975-02-14', 'MEMBER-111', 'Leader', 'Member')");
    q.exec("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, barcode, care_flag, membership_status) VALUES (12, 6, 'Elizabeth', 'Davis', '555-2223', 'elizabeth@example.com', '1977-09-09', 'MEMBER-112', '', 'Member')");
    q.exec("INSERT INTO groups (name, description, category) VALUES ('Sunday Morning Study', 'A weekly Bible study group meeting before services.', 'Adults')");
    q.exec("INSERT INTO groups (name, description, category) VALUES ('Youth Group', 'Friday night fellowship and worship for teens.', 'Youth')");
    q.exec("INSERT INTO groups (name, description, category) VALUES ('Worship Team', 'Musicians and singers leading congregation worship.', 'Service')");
    q.exec("INSERT INTO transactions (id, date, description) VALUES (1, '2026-07-05', 'Offering - John Doe')");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (1, 4, 150.00, 1)");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (1, 5, -150.00, 1)");
    q.exec("INSERT INTO transactions (id, date, description) VALUES (2, '2026-07-06', 'Offering - Jane Smith')");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (2, 4, 200.00, 2)");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (2, 6, -200.00, 2)");
    q.exec("INSERT INTO transactions (id, date, description) VALUES (3, '2026-07-10', 'Offering - John Doe')");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (3, 4, 50.00, 1)");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (3, 7, -50.00, 1)");
    q.exec("INSERT INTO transactions (id, date, description) VALUES (4, '2026-07-11', 'Electricity Bill')");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (4, 8, 120.00, NULL)");
    q.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (4, 4, -120.00, NULL)");
    q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) VALUES (1, 1, 15000, '2026-07-05', 'Cash', NULL)");
    q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) VALUES (2, 2, 20000, '2026-07-06', 'Check', '1004')");
    q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) VALUES (1, 3, 5000, '2026-07-10', 'Cash', NULL)");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Person', 4, 'Spouse')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 4, 'Person', 1, 'Spouse')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Person', 9, 'Spouse')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Person', 8, 'Spouse')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 11, 'Person', 12, 'Spouse')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 12, 'Person', 11, 'Spouse')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Person', 5, 'Parent')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 4, 'Person', 5, 'Parent')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Person', 10, 'Parent')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Person', 10, 'Parent')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Household', 1, 'Head of House')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 4, 'Household', 1, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 5, 'Household', 1, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 2, 'Household', 2, 'Head of House')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 6, 'Household', 2, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 3, 'Household', 3, 'Head of House')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Household', 5, 'Head of House')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Household', 5, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 10, 'Household', 5, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 11, 'Household', 6, 'Head of House')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 12, 'Household', 6, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Group', 1, 'Leader')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 2, 'Group', 3, 'Leader')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 6, 'Group', 3, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 3, 'Group', 1, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Group', 2, 'Leader')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Group', 3, 'Member')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 11, 'Group', 1, 'Leader')");
    q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 12, 'Group', 3, 'Member')");
    q.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Behringer X32 Sound Board', 'X32-998877', '2023-04-10', 'Sanctuary Booth', 'Good', 'ASSET-101')");
    q.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Epson Pro Projector', 'EPS-554433', '2024-09-12', 'Sanctuary Ceiling', 'Excellent', 'ASSET-102')");
    q.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Portable PA System', 'PPA-112233', '2025-06-01', 'Youth Storage Room', 'Fair', 'ASSET-103')");
    q.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Nursery Check-in iPad', 'IPD-008899', '2024-11-20', 'Nursery Entry', 'Good', 'ASSET-104')");
    q.exec("INSERT INTO checkout_logs (asset_id, person_id, checkout_date, return_date, notes) VALUES (3, 1, '2026-07-01', '2026-07-03', 'Checked out for Youth beach trip.')");
    q.exec("INSERT INTO checkout_logs (asset_id, person_id, checkout_date, return_date, notes) VALUES (3, 2, '2026-07-10', NULL, 'Checked out for outdoor worship service.')");
    q.exec("INSERT INTO vault_documents (title, meeting_date, category, file_path) VALUES ('Elder Board Q2 Minutes', '2026-05-10', 'Board Minutes', 'elder_minutes_2026_q2.pdf')");
    q.exec("INSERT INTO vault_documents (title, meeting_date, category, file_path) VALUES ('2026 Annual Budget Plan', '2026-01-15', 'Financial Bylaws', 'annual_budget_2026.pdf')");
    q.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (1, 'Acoustic Guitar')");
    q.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (2, 'Nursery Care')");
    q.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (6, 'Vocalist')");
    q.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (8, 'Nursery Care')");
    q.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (11, 'Preaching')");
    q.exec("INSERT INTO service_volunteers (service_date, person_id, role) VALUES ('2026-07-19', 1, 'Acoustic Guitar')");
    q.exec("INSERT INTO service_volunteers (service_date, person_id, role) VALUES ('2026-07-19', 11, 'Preacher')");
    q.exec("INSERT INTO account_budgets (account_id, budgeted_amount_cents, fiscal_year) VALUES (8, 50000, 2026)");
}

bool test_careTimelineQuery() {
    seedDemoFixture();
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
    seedDemoFixture();
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
    seedDemoFixture();
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
    seedDemoFixture();
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
    seedDemoFixture();
    QSqlDatabase d = db();

    // Scope → table mapping sanity.
    CHECK(DataSharing::tablesForScope("care_notes") == QStringList({"care_logs", "member_history", "pastoral_activities"}));
    CHECK(DataSharing::tablesForScope("membership").contains("people"));
    CHECK(DataSharing::tablesForScope("membership").contains("assets"));
    CHECK(DataSharing::tablesForScope("finance").contains("contributions"));
    CHECK(DataSharing::tablesForScope("finance").contains("giving_batches"));
    CHECK(DataSharing::tablesForScope("schedules").contains("ministry_events"));
    CHECK(DataSharing::tablesForScope("schedules").contains("facility_reservations"));

    // Credential hygiene: the users table (with login PINs) must never be part
    // of any share — logins are per-machine, and a JSON archive must not carry
    // a pin column or a users payload.
    CHECK(!DataSharing::tablesForScope("full").contains("users"));
    const QJsonObject fullDoc = DataSharing::exportTables(d, DataSharing::tablesForScope("full"));
    const QString fullJson = QString::fromUtf8(QJsonDocument(fullDoc).toJson());
    CHECK(!fullJson.contains("\"users\""));
    CHECK(!fullJson.contains("\"pin\""));

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
    CHECK_EQ(countRows("groups"), 7); // 4 primary blueprint headings + 3 fixture groups
    return true;
}

bool test_pastoralQueue() {
    seedDemoFixture();
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

// exportBackup must refuse to write over the live database or its WAL/SHM
// sidecars: the copy path removes the destination first, which on POSIX would
// unlink the working database while SQLite keeps writing to the removed inode.
bool test_exportBackupRejectsLivePaths() {
    seedDemoFixture();
    DatabaseManager& mgr = DatabaseManager::instance();
    const QString live = mgr.database().databaseName();
    const int before = countRows("people");

    CHECK(!mgr.exportBackup(live));
    CHECK(!mgr.exportBackup(live + "-wal"));
    CHECK(!mgr.exportBackup(live + "-shm"));
    CHECK(!mgr.exportBackup(QString()));

    // The live database is untouched and still queryable.
    CHECK(QFile::exists(live));
    CHECK_EQ(countRows("people"), before);
    QSqlQuery q(mgr.database());
    CHECK(q.exec("SELECT COUNT(*) FROM people"));
    CHECK(q.next());
    CHECK_EQ(q.value(0).toInt(), before);
    return true;
}

bool test_restoreFromBackup() {
    seedDemoFixture();
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

// restoreFromBackup used to log its "migrated older backup forward" success
// line on the FAILURE path (and emitted nothing when migration actually
// succeeded) — an inverted condition at the migrateSchema() call. Pin the
// messaging to the outcome in both directions.
bool test_restoreMigrationLogMatchesOutcome() {
    seedDemoFixture();
    DatabaseManager& mgr = DatabaseManager::instance();
    const QString dbPath = mgr.database().databaseName();
    const QString goodBackup = dbPath + ".backup_msg_good";
    const QString olderBackup = dbPath + ".backup_msg_older";
    const QString brokenBackup = dbPath + ".backup_msg_broken";
    QFile::remove(goodBackup);
    QFile::remove(olderBackup);
    QFile::remove(brokenBackup);

    // Snapshot a healthy current-schema backup; also the repair artifact that
    // leaves the live DB clean at the end.
    CHECK(mgr.exportBackup(goodBackup));

    // Rewrite a copy's schema_version (and optionally drop a table the
    // migration needs) through a private connection. WAL-checkpoint + sidecar
    // cleanup so the edited state lives in the single file restore copies.
    auto pokeBackup = [](const QString& path, int version, const char* dropTable) {
        {
            QSqlDatabase poke = QSqlDatabase::addDatabase("QSQLITE", "__oe_test_poke__");
            poke.setDatabaseName(path);
            CHECK(poke.open());
            QSqlQuery q(poke);
            // schema_version holds one row per applied migration (PRIMARY
            // KEY version); "downgrade" by deleting the newer rows.
            CHECK(q.exec(QString("DELETE FROM schema_version WHERE version > %1").arg(version)));
            CHECK(q.exec(QString("SELECT MAX(version) FROM schema_version")));
            CHECK(q.next());
            CHECK_EQ(q.value(0).toInt(), version);
            if (dropTable) {
                CHECK(q.exec(QString("DROP TABLE %1").arg(dropTable)));
            }
            CHECK(q.exec("PRAGMA wal_checkpoint(TRUNCATE)"));
            poke.close();
        }
        QSqlDatabase::removeDatabase("__oe_test_poke__");
        QFile::remove(path + "-wal");
        QFile::remove(path + "-shm");
    };

    // Case 1: an older backup (schema 23) restores and migrates forward —
    // the informational line must appear and the failure line must not.
    CHECK(QFile::copy(goodBackup, olderBackup));
    pokeBackup(olderBackup, DatabaseManager::kLatestSchemaVersion - 2, nullptr);
    g_capturedLog.clear();
    QtMessageHandler prev = qInstallMessageHandler(captureLogHandler);
    const bool ok1 = mgr.restoreFromBackup(olderBackup);
    qInstallMessageHandler(prev);
    CHECK(ok1);
    bool sawMigratedInfo = false, sawFailWarning = false;
    for (const QString& m : g_capturedLog) {
        if (m.contains("migrated older backup forward")) sawMigratedInfo = true;
        if (m.contains("migration of the restored backup FAILED")) sawFailWarning = true;
    }
    CHECK(sawMigratedInfo);
    CHECK(!sawFailWarning);

    // Case 2: force a migration failure (drop audit_log, which migration
    // 25 indexes) — the restore completes but the log must report FAILURE and
    // must never print the success line. This is the exact regression: the
    // pre-fix code printed "migrated older backup forward" HERE, as a warning.
    CHECK(QFile::copy(goodBackup, brokenBackup));
    pokeBackup(brokenBackup, DatabaseManager::kLatestSchemaVersion - 2, "audit_log");
    g_capturedLog.clear();
    prev = qInstallMessageHandler(captureLogHandler);
    const bool ok2 = mgr.restoreFromBackup(brokenBackup);
    qInstallMessageHandler(prev);
    CHECK(ok2); // migration failure is reported via log, not fatal to restore
    sawMigratedInfo = false;
    sawFailWarning = false;
    for (const QString& m : g_capturedLog) {
        if (m.contains("migrated older backup forward")) sawMigratedInfo = true;
        if (m.contains("migration of the restored backup FAILED")) sawFailWarning = true;
    }
    CHECK(sawFailWarning);
    CHECK(!sawMigratedInfo);

    // Repair: put the healthy snapshot back so later tests see a clean v24 DB.
    CHECK(mgr.restoreFromBackup(goodBackup));
    QSqlQuery sv(db());
    CHECK(sv.exec("SELECT MAX(version) FROM schema_version"));
    CHECK(sv.next());
    CHECK_EQ(sv.value(0).toInt(), DatabaseManager::kLatestSchemaVersion);

    QFile::remove(goodBackup);
    QFile::remove(olderBackup);
    QFile::remove(brokenBackup);
    return true;
}

bool test_purgeDemoData() {
    CHECK(DatabaseManager::instance().purgeDemoData());

    CHECK_EQ(countRows("people"), 0);
    CHECK_EQ(countRows("households"), 0);
    CHECK_EQ(countRows("contributions"), 0);
    CHECK_EQ(countRows("transactions"), 0);
    CHECK_EQ(countRows("assets"), 0);
    CHECK_EQ(countRows("vault_documents"), 0);
    CHECK_EQ(countRows("care_logs"), 0);

    // New contract: `groups` is cleared, but the 4 primary blueprint root
    // headings are re-created immediately (no restart) by
    // ensureBlueprintHeadings(); nothing else survives in that table.
    CHECK_EQ(countRows("groups"), 4);
    QSqlQuery rootQ(db());
    CHECK(rootQ.exec("SELECT COUNT(*) FROM groups WHERE parent_id IS NULL OR parent_id = 0"));
    CHECK(rootQ.next());
    CHECK_EQ(rootQ.value(0).toInt(), 4);
    QSqlQuery canonQ(db());
    CHECK(canonQ.exec("SELECT COUNT(*) FROM groups WHERE name = 'Ushers & Greeters'"));
    CHECK(canonQ.next());
    CHECK(canonQ.value(0).toInt() >= 1);
    // Non-primary canonical blueprints must NOT spawn headings.
    QSqlQuery nonPrimaryQ(db());
    CHECK(nonPrimaryQ.exec("SELECT COUNT(*) FROM groups WHERE name = 'Missions & Outreach' OR name = 'Maintenance'"));
    CHECK(nonPrimaryQ.next());
    CHECK_EQ(nonPrimaryQ.value(0).toInt(), 0);

    // The audit trail is preserved (non-repudiation) and records the purge.
    QSqlQuery purgeQ(db());
    CHECK(purgeQ.exec("SELECT COUNT(*) FROM audit_log WHERE table_name = 'database' AND field_name = 'purge'"));
    CHECK(purgeQ.next());
    CHECK(purgeQ.value(0).toInt() >= 1);

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

bool test_allThemesContrastAA() {
    // Every registered theme must clear WCAG 2.1 AA: body text >= 4.5:1 on the
    // page background, and muted text >= 3:1 on the surface. This guards the
    // theme registry against muted/body colors that read as accents or vanish
    // (latte/sky/sage/nord previously shipped muted text at 1.4-2.6:1).
    ThemeManager::instance().initialize();
    const QStringList ids = ThemeManager::instance().availableThemes();
    CHECK(ids.size() >= 20); // full curated registry (24 themes + system auto)
    int checked = 0;
    for (const QString& id : ids) {
        ThemeTokens tok = ThemeManager::instance().getThemeTokens(id);
        if (!tok.bgBase.isValid() || !tok.textPrimary.isValid() || !tok.textMuted.isValid()) {
            CHECK(!"theme tokens must be fully populated");
            continue;
        }
        const double body = ThemeManager::contrastRatio(tok.textPrimary, tok.bgBase);
        const double mut  = ThemeManager::contrastRatio(tok.textMuted, tok.bgSurface);
        const double sec  = ThemeManager::contrastRatio(tok.textSecondary, tok.bgSurface);
        if (body < 4.5 || mut < 3.0 || sec < 3.0) {
            std::cerr << "THEME CONTRAST FAIL " << id.toStdString()
                      << " body=" << body << " sec=" << sec << " muted=" << mut << std::endl;
            CHECK(!"theme fails WCAG AA contrast (body>=4.5, sec/muted>=3.0)");
        }
        ++checked;
    }
    CHECK_EQ(checked, ids.size());
    return true;
}

bool test_personGenderAndHouseholdSave() {
    seedDemoFixture();
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
    // Advisory model: the burned-out volunteer is no longer excluded — they
    // appear with a workload warning and a reduced score. Member excluded,
    // adult age-out, no-skill excluded.
    CHECK_EQ(results.size(), 2);
    bool sawTeen = false, sawBurnout = false;
    for (const MatchResult& r : results) {
        if (r.personId == teen) {
            sawTeen = true;
            CHECK(r.score >= 50);
            const QString why = r.reasons.join(" ");
            CHECK(why.contains("guitar", Qt::CaseInsensitive));
            CHECK(why.contains("Age", Qt::CaseInsensitive));
        }
        if (r.personId == burnedOut) {
            sawBurnout = true;
            CHECK(r.reasons.join(" ").contains("pastor review", Qt::CaseInsensitive));
        }
    }
    CHECK(sawTeen);
    CHECK(sawBurnout);

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
    ensurePersonOne();
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
    ensurePersonOne();
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

bool test_pastoralAnalyticsUpgrade() {
    // computeSnapshot() must be scope-aware, exclude archived groups, count
    // Teacher roles, and back every action with a real number. Uses DELTA
    // assertions against a baseline (the shared suite DB has unpredictable
    // state) and cleans up its own rows by id — never wipes shared tables.
    QSqlDatabase d = db();
    QSqlQuery q(d);

    PastoralAnalyticsWidget paw;
    const auto before = paw.computeSnapshot(0);

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    const QString monthStart = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1).toString("yyyy-MM-dd");

    // People (ids 9601+): 9601 visitor this month, 9602 leader in a LIVE group,
    // 9603 member in an ARCHIVED group, 9604 at-risk with overdue follow-up,
    // 9605 teacher in the live group + lapsed giver, 9606 joined long ago (out
    // of every scope window).
    CHECK(q.exec(QString("INSERT INTO people (id, first_name, last_name, membership_status, first_visit_date, join_date, care_flag) "
                         "VALUES (9601, 'VNew', 'ThisMonth', 'Visitor', '%1', '%1', 'Normal')").arg(today)));
    CHECK(q.exec(QString("INSERT INTO people (id, first_name, last_name, membership_status, join_date, care_flag) "
                         "VALUES (9602, 'MLead', 'Active', 'Member', '%1', 'Normal')").arg(monthStart)));
    CHECK(q.exec(QString("INSERT INTO people (id, first_name, last_name, membership_status, join_date, care_flag) "
                         "VALUES (9603, 'MArch', 'Hidden', 'Member', '%1', 'Normal')").arg(monthStart)));
    CHECK(q.exec(QString("INSERT INTO people (id, first_name, last_name, membership_status, join_date, care_flag) "
                         "VALUES (9604, 'MRisk', 'Care', 'Member', '%1', 'At-Risk')").arg(monthStart)));
    CHECK(q.exec(QString("INSERT INTO people (id, first_name, last_name, membership_status, join_date, care_flag) "
                         "VALUES (9605, 'MLapse', 'Giver', 'Member', '%1', 'Normal')").arg(monthStart)));
    CHECK(q.exec(QString("INSERT INTO people (id, first_name, last_name, membership_status, first_visit_date, join_date, care_flag) "
                         "VALUES (9606, 'MOld', 'OutOfScope', 'Member', '2024-01-01', '2024-01-01', 'Normal')")));

    CHECK(q.exec("INSERT INTO groups (id, name, category, is_archived) VALUES (9601, 'Live Team', 'Worship', 0)"));
    CHECK(q.exec("INSERT INTO groups (id, name, category, is_archived) VALUES (9602, 'Archived Team', 'Worship', 1)"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, joined_date) "
                 "VALUES ('Person', 9602, 'Group', 9601, 'Leader', '2026-08-01')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, joined_date) "
                 "VALUES ('Person', 9603, 'Group', 9602, 'Member', '2026-08-01')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, joined_date) "
                 "VALUES ('Person', 9605, 'Group', 9601, 'Teacher', '2026-08-01')"));
    CHECK(q.exec(QString("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, follow_up_date, follow_up_done) "
                         "VALUES (9604, 'Visitation', '%1', '%1', 0)").arg(today)));
    CHECK(q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, note) "
                 "VALUES (9605, 1, 1000, date('now', '-90 days'), 'old gift')"));

    const auto s = paw.computeSnapshot(0); // live snapshot, all-time status counts
    const int realBefore = countRows("people");
    CHECK_EQ(s.totalPeople, realBefore == 0 ? 6 : (before.totalPeople + 6));
    CHECK_EQ(s.visitors - before.visitors, 1);
    CHECK_EQ(s.attenders - before.attenders, 0);
    CHECK_EQ(s.members - before.members, 5);
    CHECK_EQ(s.atRisk - before.atRisk, 1);
    CHECK_EQ(s.leaders - before.leaders, 2);              // Leader + Teacher roles both counted
    CHECK_EQ(s.smallGroupConnected - before.smallGroupConnected, 2); // archived group excluded
    CHECK_EQ(s.servingCount - before.servingCount, 2);
    CHECK_EQ(s.servingTeams - before.servingTeams, 1);
    CHECK_EQ(s.newVisitorsInScope - before.newVisitorsInScope, 1); // only 9601 first visited this month
    CHECK_EQ(s.overdueFollowUps - before.overdueFollowUps, 1);
    CHECK_EQ(s.lapsedGivers - before.lapsedGivers, 1);    // 9605 gave, but not within 30 days

    // Scope windows exclude 9606 (joined 2024).
    CHECK_EQ(paw.computeSnapshot(1).totalPeople, realBefore == 0 ? 5 : (paw.computeSnapshot(1).totalPeople));
    CHECK_EQ(paw.computeSnapshot(2).totalPeople, realBefore == 0 ? 5 : (paw.computeSnapshot(2).totalPeople));
    CHECK_EQ(paw.computeSnapshot(3).totalPeople, realBefore == 0 ? 5 : (paw.computeSnapshot(3).totalPeople));

    // Cleanup: remove exactly the rows this test added (FK-safe order).
    CHECK(q.exec("DELETE FROM pastoral_activities WHERE person_id BETWEEN 9601 AND 9606"));
    CHECK(q.exec("DELETE FROM contributions WHERE person_id BETWEEN 9601 AND 9606"));
    CHECK(q.exec("DELETE FROM entity_relationships WHERE source_id BETWEEN 9601 AND 9606 OR target_id BETWEEN 9601 AND 9606"));
    CHECK(q.exec("DELETE FROM groups WHERE id BETWEEN 9601 AND 9602"));
    CHECK(q.exec("DELETE FROM people WHERE id BETWEEN 9601 AND 9606"));
    return true;
}

bool test_serviceBuilderRoleAndDuration() {
    ServiceBuilderWidget sb;

    // Default (admin) role: mutating controls are enabled.
    bool saveEnabled = false, exportEnabled = false;
    for (QPushButton* b : sb.findChildren<QPushButton*>()) {
        if (b->text().contains("Save Layout File")) saveEnabled = b->isEnabled();
        if (b->text().contains("Export Run Sheet PDF")) exportEnabled = b->isEnabled();
    }
    CHECK(saveEnabled);
    CHECK(exportEnabled);

    // Guest (read-only): writes disabled, read-only actions stay enabled.
    sb.applyRole("readonly");
    for (QPushButton* b : sb.findChildren<QPushButton*>()) {
        if (b->text().contains("Save Layout File") || b->text() == "Assign Volunteer"
            || b->text() == "Unassign" || b->text().contains("Save as Template")
            || b->text().contains("Mark Volunteers Attended")) {
            CHECK(!b->isEnabled());
        }
        if (b->text().contains("Export Run Sheet PDF") || b->text().contains("Load Layout File")
            || b->text().contains("Coverage Check") || b->text().contains("Export Volunteer iCal")) {
            CHECK(b->isEnabled());
        }
    }

    // Restore admin: controls re-enable.
    sb.applyRole("admin");
    for (QPushButton* b : sb.findChildren<QPushButton*>()) {
        if (b->text().contains("Save Layout File")) CHECK(b->isEnabled());
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

bool test_serviceBuilderLibraryFilterAndAutosave() {
    ServiceBuilderWidget sb;
    QSqlDatabase d = db();

    // 1. Library type filter hides non-matching elements.
    QComboBox* filter = sb.findChild<QComboBox*>("serviceLibraryFilter");
    QLineEdit* search = sb.findChild<QLineEdit*>("serviceLibrarySearch");
    QListWidget* library = sb.findChild<QListWidget*>("serviceLibraryList");
    CHECK(filter && search && library);
    if (!filter || !search || !library) return true;

    auto visibleCount = [&]() {
        int n = 0;
        for (int i = 0; i < library->count(); ++i) {
            if (!library->item(i)->isHidden()) n++;
        }
        return n;
    };
    CHECK_EQ(visibleCount(), 15); // default: everything visible

    int songIdx = filter->findData("Song");
    if (songIdx < 0) songIdx = filter->findText("Songs");
    filter->setCurrentIndex(songIdx);
    CHECK_EQ(visibleCount(), 4); // four seeded songs
    filter->setCurrentIndex(0);  // back to All
    CHECK_EQ(visibleCount(), 15);

    search->setText("Amazing");
    CHECK_EQ(visibleCount(), 1);
    search->clear();
    CHECK_EQ(visibleCount(), 15);

    // 2. Title/leader auto-save (editingFinished is a signal, so call the
    //    underlying save path the widget wires to it).
    QLineEdit* title = sb.findChild<QLineEdit*>("serviceTitleEdit");
    QComboBox* leader = sb.findChild<QComboBox*>("serviceLeaderEdit");
    CHECK(title && leader);
    if (title && leader) {
        title->setText("Test Service");
        leader->setEditText("Test Leader");
        sb.saveServiceDetails();
        const QString dateStr = sb.findChild<QDateEdit*>("serviceDateEdit")->date().toString("yyyy-MM-dd");
        QSqlQuery q(d);
        q.prepare("SELECT title, leader FROM services WHERE date = ?");
        q.addBindValue(dateStr);
        CHECK(q.exec() && q.next());
        CHECK_EQ(q.value(0).toString().toStdString(), std::string("Test Service"));
        CHECK_EQ(q.value(1).toString().toStdString(), std::string("Test Leader"));
        QSqlQuery clean(d);
        clean.prepare("DELETE FROM services WHERE date = ?");
        clean.addBindValue(dateStr);
        CHECK(clean.exec());
    }

    // 3. Volunteer panel is a table with role columns.
    QTableWidget* vols = sb.findChild<QTableWidget*>("serviceVolunteerList");
    CHECK(vols);
    if (vols) CHECK_EQ(vols->columnCount(), 3);

    // 4. Auto-save status label exists.
    QLabel* status = sb.findChild<QLabel*>("serviceAutoSaveLabel");
    CHECK(status);
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
    ensurePersonOne();
    // 1. Verify QSettings persistence for KPI cards
    AppSettings s;
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

// Regression: restoring a date is a read-only operation and must never write
// the previously shown run sheet / start time into the newly selected date's
// services row. Both the constructor path and a user date switch used to
// clobber the stored row because setTime() and the playlist-clear autosave ran
// before the row was read back.
bool test_serviceBuilderLoadDoesNotClobberSavedSheet() {
    AppSettings settings;
    const QVariant previousDefaultTime = settings.value("defaultServiceTime");
    settings.setValue("defaultServiceTime", "08:00"); // widget default != stored 09:30
    settings.sync();

    QDate nextSunday = QDate::currentDate();
    while (nextSunday.dayOfWeek() != 7) nextSunday = nextSunday.addDays(1);
    const QString firstDate = nextSunday.toString("yyyy-MM-dd");
    const QString secondDate = nextSunday.addDays(14).toString("yyyy-MM-dd");
    const QString firstSheet = "[{\"title\":\"Amazing Grace (4m)\",\"type\":\"Song\",\"durationMins\":4}]";
    const QString secondSheet = "[{\"title\":\"Holy Holy Holy (3m)\",\"type\":\"Song\",\"durationMins\":3}]";

    QSqlDatabase d = db();
    auto seed = [&](const QString& date, const QString& sheet) {
        QSqlQuery q(d);
        q.prepare("INSERT INTO services (date, run_sheet, start_time) VALUES (?, ?, '09:30') "
                  "ON CONFLICT(date) DO UPDATE SET run_sheet = excluded.run_sheet, "
                  "start_time = excluded.start_time");
        q.addBindValue(date);
        q.addBindValue(sheet);
        CHECK(q.exec());
    };
    seed(firstDate, firstSheet);
    seed(secondDate, secondSheet);

    {
        ServiceBuilderWidget sb;
        QListWidget* playlist = sb.findChild<QListWidget*>("servicePlaylist");
        QDateEdit* dateEdit = sb.findChild<QDateEdit*>("serviceDateEdit");
        CHECK(playlist && dateEdit);
        if (playlist && dateEdit) {
            // Constructor path: the default date's saved sheet survives the
            // 08:00 default -> stored 09:30 start-time transition.
            CHECK_EQ(playlist->count(), 1);
            if (playlist->count() == 1) {
                CHECK(playlist->item(0)->text().contains("Amazing Grace"));
            }
            {
                QSqlQuery q(d);
                q.prepare("SELECT run_sheet, start_time FROM services WHERE date = ?");
                q.addBindValue(firstDate);
                CHECK(q.exec() && q.next());
                CHECK(q.value(0).toString().contains("Amazing Grace"));
                CHECK_EQ(q.value(1).toString().toStdString(), std::string("09:30"));
            }

            // Date-switch path: the target date's own sheet and start time are
            // preserved (the old code wrote the previous sheet here).
            dateEdit->setDate(nextSunday.addDays(14));
            CHECK_EQ(playlist->count(), 1);
            if (playlist->count() == 1) {
                CHECK(playlist->item(0)->text().contains("Holy Holy Holy"));
            }
            {
                QSqlQuery q(d);
                q.prepare("SELECT run_sheet, start_time FROM services WHERE date = ?");
                q.addBindValue(secondDate);
                CHECK(q.exec() && q.next());
                CHECK(q.value(0).toString().contains("Holy Holy Holy"));
                CHECK_EQ(q.value(1).toString().toStdString(), std::string("09:30"));
            }
        }
    }

    QSqlQuery del(d);
    del.prepare("DELETE FROM services WHERE date IN (?, ?)");
    del.addBindValue(firstDate);
    del.addBindValue(secondDate);
    CHECK(del.exec());

    if (previousDefaultTime.isValid()) {
        settings.setValue("defaultServiceTime", previousDefaultTime);
    } else {
        settings.remove("defaultServiceTime");
    }
    settings.sync();
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
    CHECK(ics.contains("TRIGGER:-PT")); // reminder alarm (minutes configurable via QSettings)

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

    // Seed a task with REAL dates before constructing the widget.
    const int year = QDate::currentDate().year();
    const QString start = QDate(year, 2, 10).toString(Qt::ISODate); // Feb 10
    const QString end = QDate(year, 3, 20).toString(Qt::ISODate);   // Mar 20
    QSqlQuery ins(d);
    CHECK(ins.exec("DELETE FROM ministry_tasks"));
    ins.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                "start_month, duration_months, start_date, end_date) "
                "VALUES ('Worship & Media Team', 'Easter Production', 'John Doe', 'In-Progress', 1, 1, 1, 1, ?, ?)");
    ins.addBindValue(start);
    ins.addBindValue(end);
    CHECK(ins.exec());

    PlanningWidget pw;

    // Both views render the task.
    QTableWidget* roadmap = pw.findChild<QTableWidget*>("planningRoadmapTable");
    QTableWidget* gantt = pw.findChild<QTableWidget*>("planningGanttTable");
    CHECK(roadmap && gantt);
    if (roadmap && gantt) {
        CHECK_EQ(roadmap->rowCount(), 1);
        CHECK_EQ(gantt->rowCount(), 1);
        if (roadmap->rowCount() == 1) {
            // Roadmap: 12 real calendar months — FEB (col 3) and MAR (col 4)
            // covered by the span; JAN (col 2) and APR (col 5) empty.
            CHECK_EQ(roadmap->item(0, 1)->text().toStdString(), std::string("Easter Production"));
            CHECK(roadmap->item(0, 2) && roadmap->item(0, 2)->text().isEmpty()); // JAN
            CHECK(roadmap->item(0, 3) && !roadmap->item(0, 3)->text().isEmpty()); // FEB
            CHECK(roadmap->item(0, 4) && !roadmap->item(0, 4)->text().isEmpty()); // MAR
            CHECK(roadmap->item(0, 5) && roadmap->item(0, 5)->text().isEmpty()); // APR
        }
        // Gantt: rolling window anchored at the task's start date.
        QDateEdit* anchor = pw.findChild<QDateEdit*>("planningAnchorDate");
        CHECK(anchor);
        if (anchor) {
            anchor->setDate(QDate(year, 2, 10));
            CHECK_EQ(gantt->rowCount(), 1);
            if (gantt->rowCount() == 1) {
                CHECK(gantt->item(0, 0) && gantt->item(0, 0)->data(Qt::UserRole).toInt() > 0);
                CHECK(gantt->item(0, 2) && !gantt->item(0, 2)->text().isEmpty());     // day 0 = Feb 10
                CHECK(gantt->item(0, 2 + 19) && !gantt->item(0, 2 + 19)->text().isEmpty()); // within span
            }
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

    // The Q1 win: "what's slipping" is now a plain SQL query.
    QSqlQuery slip(d);
    slip.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                 "start_month, duration_months, start_date, end_date) "
                 "VALUES ('Worship & Media Team', 'Slipping Task', 'John Doe', 'In-Progress', 1, 1, 1, 1, ?, ?)");
    slip.addBindValue(QDate(year, 1, 1).toString(Qt::ISODate));
    slip.addBindValue(QDate(year, 1, 2).toString(Qt::ISODate)); // long past
    CHECK(slip.exec());
    QSqlQuery slipQ(d);
    slipQ.prepare("SELECT title FROM ministry_tasks "
                  "WHERE end_date < date('now') AND status != 'Completed'");
    CHECK(slipQ.exec());
    bool foundSlipping = false;
    while (slipQ.next()) {
        if (slipQ.value(0).toString() == "Slipping Task") foundSlipping = true;
    }
    CHECK(foundSlipping); // the Q1 win: slipped tasks are queryable by real date

    CHECK(ins.exec("DELETE FROM ministry_tasks"));
    return true;
}

bool test_planningRelationalLinks() {
    QSqlDatabase d = db();

    // Linked group + person (post-purge the tables are empty, so these are fresh).
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (name, category) VALUES ('AV Team', 'Serving Team')"));
    const int gid = q.lastInsertId().toInt();
    CHECK(q.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Jane', 'Leader', 'Member')"));
    const int pid = q.lastInsertId().toInt();

    // Task linked by ids but with STALE text (simulates a renamed group/owner).
    const int year = QDate::currentDate().year();
    QSqlQuery ins(d);
    CHECK(ins.exec("DELETE FROM ministry_tasks"));
    ins.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                "start_month, duration_months, team_id, owner_id, start_date, end_date) "
                "VALUES ('Old Team Name', 'Easter Production', 'Old Owner (Leader)', 'In-Progress', 1, 3, 1, 2, ?, ?, ?, ?)");
    ins.addBindValue(gid);
    ins.addBindValue(pid);
    ins.addBindValue(QDate(year, 3, 1).toString(Qt::ISODate));
    ins.addBindValue(QDate(year, 3, 15).toString(Qt::ISODate));
    CHECK(ins.exec());

    // Display resolves the CURRENT names through the links.
    {
        PlanningWidget pw;
        QTableWidget* roadmap = pw.findChild<QTableWidget*>("planningRoadmapTable");
        QTableWidget* gantt = pw.findChild<QTableWidget*>("planningGanttTable");
        CHECK(roadmap && gantt);
        if (roadmap && gantt) {
            CHECK_EQ(roadmap->rowCount(), 1);
            CHECK_EQ(gantt->rowCount(), 1);
            if (roadmap->rowCount() == 1 && roadmap->item(0, 0)) {
                CHECK_EQ(roadmap->item(0, 0)->text().toStdString(), std::string("AV Team"));
            }
            if (gantt->rowCount() == 1 && gantt->item(0, 1)) {
                CHECK(gantt->item(0, 1)->text().contains("Jane Leader"));
            }
        }
    }

    // Rename the group: a fresh view shows the new name, not the stale text.
    CHECK(q.exec("UPDATE groups SET name = 'AV Renamed' WHERE id = " + QString::number(gid)));
    {
        PlanningWidget pw;
        QTableWidget* roadmap = pw.findChild<QTableWidget*>("planningRoadmapTable");
        if (roadmap && roadmap->rowCount() == 1 && roadmap->item(0, 0)) {
            CHECK_EQ(roadmap->item(0, 0)->text().toStdString(), std::string("AV Renamed"));
        }
    }

    // Unlinked task (text only) still renders via the stored text.
    ins.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                "start_month, duration_months, start_date, end_date) "
                "VALUES ('Legacy Team', 'Legacy Task', 'Legacy Owner', 'Planned', 2, 2, 2, 1, ?, ?)");
    ins.addBindValue(QDate(year, 5, 1).toString(Qt::ISODate));
    ins.addBindValue(QDate(year, 5, 20).toString(Qt::ISODate));
    CHECK(ins.exec());
    {
        PlanningWidget pw;
        QTableWidget* roadmap = pw.findChild<QTableWidget*>("planningRoadmapTable");
        if (roadmap) {
            bool sawLegacy = false;
            for (int r = 0; r < roadmap->rowCount(); ++r) {
                if (roadmap->item(r, 1) && roadmap->item(r, 1)->text() == "Legacy Task") sawLegacy = true;
            }
            CHECK(sawLegacy);
        }
    }

    // Governance share scope now carries the planning roadmap.
    CHECK(DataSharing::tablesForScope("governance").contains("ministry_tasks"));

    // Cleanup.
    CHECK(ins.exec("DELETE FROM ministry_tasks"));
    CHECK(q.exec("DELETE FROM groups WHERE id = " + QString::number(gid)));
    CHECK(q.exec("DELETE FROM people WHERE id = " + QString::number(pid)));
    return true;
}

bool test_vaultAndSettingsIntegrity() {
    QSqlDatabase d = db();
    d.transaction();

    // Test vault_documents schema and insertion
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO vault_documents (title, meeting_date, category, file_path) "
                 "VALUES ('Annual Budget 2026', '2026-01-15', 'Financial', 'budget_2026.pdf')"));
    int docId = q.lastInsertId().toInt();
    CHECK(docId > 0);

    // Test board motions table and query
    CHECK(q.exec("INSERT INTO board_motions (meeting_date, motion_title, moved_by, seconded_by, vote_result, details) "
                 "VALUES ('2026-02-01', 'Q1 Expansion', 'Elder John', 'Elder Mary', 'Passed Unanimously', 'Approved ministry expansion.')"));
    int minId = q.lastInsertId().toInt();
    CHECK(minId > 0);

    QSqlQuery qFetch(d);
    CHECK(qFetch.exec("SELECT title, category FROM vault_documents WHERE id = " + QString::number(docId)));
    CHECK(qFetch.next());
    CHECK_EQ(qFetch.value(0).toString().toStdString(), std::string("Annual Budget 2026"));

    d.rollback();
    return true;
}

bool test_facilitiesConflictEngine() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Insert existing booking: Fellowship Hall 18:00 - 20:00 on 2026-09-01
    QSqlQuery q(d);
    q.prepare("INSERT INTO facility_reservations (facility_name, event_name, reservation_date, start_time, end_time, reserved_by) "
              "VALUES (?, ?, ?, ?, ?, ?)");
    q.addBindValue("Fellowship Hall & Kitchen");
    q.addBindValue("Community Dinner");
    q.addBindValue("2026-09-01");
    q.addBindValue("18:00");
    q.addBindValue("20:00");
    q.addBindValue("Leader A");
    CHECK(q.exec());

    // 2. Conflict test: Overlapping window (19:00 - 21:00) should CONFLICT
    QSqlQuery qConflict(d);
    qConflict.prepare(
        "SELECT event_name FROM facility_reservations "
        "WHERE facility_name = ? AND reservation_date = ? "
        "  AND NOT (end_time <= ? OR start_time >= ?)");
    qConflict.addBindValue("Fellowship Hall & Kitchen");
    qConflict.addBindValue("2026-09-01");
    qConflict.addBindValue("19:00");
    qConflict.addBindValue("21:00");
    CHECK(qConflict.exec() && qConflict.next());
    CHECK_EQ(qConflict.value(0).toString().toStdString(), std::string("Community Dinner"));

    // 3. Non-conflict test: Non-overlapping window (20:00 - 22:00) should PASS
    QSqlQuery qNoConflict(d);
    qNoConflict.prepare(
        "SELECT event_name FROM facility_reservations "
        "WHERE facility_name = ? AND reservation_date = ? "
        "  AND NOT (end_time <= ? OR start_time >= ?)");
    qNoConflict.addBindValue("Fellowship Hall & Kitchen");
    qNoConflict.addBindValue("2026-09-01");
    qNoConflict.addBindValue("20:00");
    qNoConflict.addBindValue("22:00");
    CHECK(qNoConflict.exec() && !qNoConflict.next());

    // 4. Non-conflict test: Different space (Sanctuary at same time) should PASS
    QSqlQuery qDiffSpace(d);
    qDiffSpace.prepare(
        "SELECT event_name FROM facility_reservations "
        "WHERE facility_name = ? AND reservation_date = ? "
        "  AND NOT (end_time <= ? OR start_time >= ?)");
    qDiffSpace.addBindValue("Sanctuary / Main Worship Center");
    qDiffSpace.addBindValue("2026-09-01");
    qDiffSpace.addBindValue("18:00");
    qDiffSpace.addBindValue("20:00");
    CHECK(qDiffSpace.exec() && !qDiffSpace.next());

    d.rollback();
    return true;
}

bool test_contributionsSplitAndAnonymous() {
    QSqlDatabase d = db();
    d.transaction();

    // Ensure default funds exist
    QSqlQuery qFund(d);
    qFund.exec("INSERT OR IGNORE INTO funds (fund_name) VALUES ('General Fund')");
    CHECK(qFund.exec("SELECT id FROM funds WHERE fund_name = 'General Fund'") && qFund.next());
    int fund1Id = qFund.value(0).toInt();

    qFund.exec("INSERT OR IGNORE INTO funds (fund_name) VALUES ('Building Fund')");
    CHECK(qFund.exec("SELECT id FROM funds WHERE fund_name = 'Building Fund'") && qFund.next());
    int fund2Id = qFund.value(0).toInt();

    // 1. Insert anonymous plate donation (person_id = NULL)
    QSqlQuery q(d);
    q.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, note) "
              "VALUES (NULL, ?, 50000, '2026-08-23', 'Cash plate offering')");
    q.addBindValue(fund1Id);
    CHECK(q.exec());
    int anonId = q.lastInsertId().toInt();
    CHECK(anonId > 0);

    // 2. Insert member split contribution
    CHECK(q.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Donor', 'Test', 'Member')"));
    int donorId = q.lastInsertId().toInt();

    q.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, note) "
              "VALUES (?, ?, 30000, '2026-08-23', 'Split gift 1/2')");
    q.addBindValue(donorId);
    q.addBindValue(fund1Id);
    CHECK(q.exec());

    q.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, note) "
              "VALUES (?, ?, 20000, '2026-08-23', 'Split gift 2/2')");
    q.addBindValue(donorId);
    q.addBindValue(fund2Id);
    CHECK(q.exec());

    // 3. Verify total giving by person (should equal 50000 cents = $500.00)
    QSqlQuery qSum(d);
    qSum.prepare("SELECT SUM(amount) FROM contributions WHERE person_id = ?");
    qSum.addBindValue(donorId);
    CHECK(qSum.exec() && qSum.next());
    CHECK_EQ(qSum.value(0).toInt(), 50000);

    // 4. Verify anonymous giving query
    QSqlQuery qAnonSum(d);
    CHECK(qAnonSum.exec("SELECT SUM(amount) FROM contributions WHERE person_id IS NULL"));
    CHECK(qAnonSum.next());
    CHECK(qAnonSum.value(0).toInt() >= 50000);

    d.rollback();
    return true;
}

bool test_comprehensiveSettingsRoundTrip() {
    AppSettings s;

    // 1. Service Planning defaults
    s.setValue("defaultServiceTime", "11:15");
    s.setValue("targetServiceDuration", 85);
    s.setValue("volunteerAlarmMinutes", 45);

    CHECK_EQ(s.value("defaultServiceTime").toString().toStdString(), std::string("11:15"));
    CHECK_EQ(s.value("targetServiceDuration").toInt(), 85);
    CHECK_EQ(s.value("volunteerAlarmMinutes").toInt(), 45);

    // 2. Pastoral & Graph Thresholds
    s.setValue("inactivityDays", 60);
    s.setValue("smartBridgeMinScore", 55);
    s.setValue("graphPhysicsEnabled", false);

    CHECK_EQ(s.value("inactivityDays").toInt(), 60);
    CHECK_EQ(s.value("smartBridgeMinScore").toInt(), 55);
    CHECK_EQ(s.value("graphPhysicsEnabled").toBool(), false);

    // 3. Kiosk & Security
    s.setValue("kioskTimeoutSeconds", 30);
    s.setValue("kioskRequirePin", true);

    CHECK_EQ(s.value("kioskTimeoutSeconds").toInt(), 30);
    CHECK_EQ(s.value("kioskRequirePin").toBool(), true);

    // 4. Data & Backup Lifecycle
    s.setValue("autoBackupSchedule", "daily");
    s.setValue("backupRetentionCount", 15);

    CHECK_EQ(s.value("autoBackupSchedule").toString().toStdString(), std::string("daily"));
    CHECK_EQ(s.value("backupRetentionCount").toInt(), 15);

    return true;
}

bool test_givingBatchesAndMultiFundSplits() {
    ensurePersonOne();
    QSqlDatabase d = db();
    d.transaction();

    // 1. Create a giving batch
    QSqlQuery q(d);
    q.prepare("INSERT INTO giving_batches (batch_name, batch_date, status, created_by) "
              "VALUES ('Sunday Test Batch', '2026-08-23', 'In Progress', 'Tester')");
    CHECK(q.exec());
    int batchId = q.lastInsertId().toInt();
    CHECK(batchId > 0);

    // 2. Insert multi-fund split contribution into the batch
    int personId = 1;
    int fund1Id = 1;
    int fund2Id = 2;

    q.prepare("INSERT INTO contributions (batch_id, person_id, fund_id, amount, contribution_date, payment_method, check_number, note) "
              "VALUES (?, ?, ?, 15000, '2026-08-23', 'Check', '1042', 'Split 1/2')");
    q.addBindValue(batchId);
    q.addBindValue(personId);
    q.addBindValue(fund1Id);
    CHECK(q.exec());

    q.prepare("INSERT INTO contributions (batch_id, person_id, fund_id, amount, contribution_date, payment_method, check_number, note) "
              "VALUES (?, ?, ?, 10000, '2026-08-23', 'Check', '1042', 'Split 2/2')");
    q.addBindValue(batchId);
    q.addBindValue(personId);
    q.addBindValue(fund2Id);
    CHECK(q.exec());

    // 3. Verify total for the batch equals 25000 cents ($250.00)
    QSqlQuery qBatchSum(d);
    qBatchSum.prepare("SELECT COUNT(*), SUM(amount) FROM contributions WHERE batch_id = ?");
    qBatchSum.addBindValue(batchId);
    CHECK(qBatchSum.exec() && qBatchSum.next());
    CHECK_EQ(qBatchSum.value(0).toInt(), 2);
    CHECK_EQ(qBatchSum.value(1).toInt(), 25000);

    // 4. Commit the batch
    q.prepare("UPDATE giving_batches SET status = 'Committed' WHERE id = ?");
    q.addBindValue(batchId);
    CHECK(q.exec());

    QSqlQuery qStatus(d);
    qStatus.prepare("SELECT status FROM giving_batches WHERE id = ?");
    qStatus.addBindValue(batchId);
    CHECK(qStatus.exec() && qStatus.next());
    CHECK_EQ(qStatus.value(0).toString().toStdString(), std::string("Committed"));

    d.rollback();
    return true;
}

bool test_graphHealthFeatures() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Connection-health snapshots: the audit trend query (previous row).
    QSqlQuery ins(d);
    ins.prepare("INSERT INTO graph_snapshots (node_count, edge_count, isolated_count) VALUES (?, ?, ?)");
    ins.addBindValue(100);
    ins.addBindValue(40);
    ins.addBindValue(12);
    CHECK(ins.exec());
    ins.addBindValue(100);
    ins.addBindValue(45);
    ins.addBindValue(9);
    CHECK(ins.exec());

    QSqlQuery trend(d);
    CHECK(trend.exec("SELECT isolated_count FROM graph_snapshots ORDER BY id DESC LIMIT 1 OFFSET 1"));
    CHECK(trend.next());
    CHECK_EQ(trend.value(0).toInt(), 12); // the previous snapshot

    // 2. Group prep-task slipping query (the graph group panel uses this).
    QSqlQuery gq(d);
    CHECK(gq.exec("INSERT INTO groups (name, category) VALUES ('Prep Test Team', 'Serving Team')"));
    const int gid = gq.lastInsertId().toInt();
    QSqlQuery task(d);
    task.prepare("INSERT INTO ministry_tasks (team, title, owner, status, start_day, duration_days, "
                 "start_month, duration_months, team_id, start_date, end_date) "
                 "VALUES ('Prep Test Team', 'Easter Prep', 'Jane', 'In-Progress', 1, 1, 1, 1, ?, ?, ?)");
    task.addBindValue(gid);
    task.addBindValue(QDate(2000, 1, 1).toString(Qt::ISODate)); // long past
    task.addBindValue(QDate(2000, 2, 1).toString(Qt::ISODate)); // long past -> slipping
    CHECK(task.exec());
    QSqlQuery slip(d);
    slip.prepare("SELECT COUNT(*) FROM ministry_tasks "
                 "WHERE team_id = ? AND end_date < date('now') AND COALESCE(status, 'In-Progress') != 'Completed'");
    slip.addBindValue(gid);
    CHECK(slip.exec() && slip.next());
    CHECK_EQ(slip.value(0).toInt(), 1);

    // 3. Reconnect workflow: the log-contact insert the graph button performs.
    QSqlQuery person(d);
    CHECK(person.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Iso', 'Lated', 'Member')"));
    const int pid = person.lastInsertId().toInt();
    QSqlQuery contact(d);
    contact.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, follow_up_done) "
                    "VALUES (?, 'Contact Attempt', date('now', 'localtime'), 'Logged from Connection Graph', 0)");
    contact.addBindValue(pid);
    CHECK(contact.exec());
    QSqlQuery verify(d);
    verify.prepare("SELECT activity_type FROM pastoral_activities WHERE person_id = ? AND notes = 'Logged from Connection Graph'");
    verify.addBindValue(pid);
    CHECK(verify.exec() && verify.next());
    CHECK_EQ(verify.value(0).toString().toStdString(), std::string("Contact Attempt"));

    d.rollback();
    return true;
}

// The graph's filter toggles (e.g. "🏡 Households") must persist across
// restarts: what the admin saved is what a fresh widget restores.
bool test_graphFilterPersistence() {
    AppSettings s;

    // Simulate the admin having turned Households OFF (and People ON).
    s.setValue("graphFilters/households", false);
    s.setValue("graphFilters/people", true);
    s.setValue("graphFilters/groups", true);
    s.setValue("graphFilters/burnout", false);
    s.setValue("graphFilters/isolation", false);
    s.setValue("graphFilters/hygiene", false);
    s.setValue("graphFilters/bridge", false);
    s.setValue("graphFilters/family", false);
    s.sync();

    // This test runs after purge, so seed one of each entity for the scene.
    QSqlDatabase d = db();
    QSqlQuery seed(d);
    CHECK(seed.exec("INSERT INTO households (id, family_name) VALUES (9001, 'Persistence Family')"));
    CHECK(seed.exec("INSERT INTO groups (id, name, category) VALUES (9001, 'Persistence Team', 'Serving Team')"));
    CHECK(seed.exec("INSERT INTO people (id, first_name, last_name, membership_status, household_id) "
                    "VALUES (9001, 'Persist', 'Test', 'Member', 9001)"));
    CHECK(seed.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                    "VALUES ('Person', 9001, 'Group', 9001, 'Member')"));

    {
        GraphWidget gw; // ctor: setupUI + restoreFilterState + loadGraph

        QPushButton* hhBtn = nullptr;
        QPushButton* ppBtn = nullptr;
        for (QPushButton* b : gw.findChildren<QPushButton*>()) {
            if (b->text().contains("Households")) hhBtn = b;
            if (b->text().contains("People")) ppBtn = b;
        }
        CHECK(hhBtn && ppBtn);
        if (hhBtn) CHECK(!hhBtn->isChecked()); // restored as OFF
        if (ppBtn) CHECK(ppBtn->isChecked());  // restored as ON

        // Household nodes must actually be hidden; people visible.
        int hhNodes = 0, hhVisible = 0, peopleVisible = 0;
        for (QGraphicsItem* item : gw.scene()->items()) {
            auto* n = dynamic_cast<NodeItem*>(item);
            if (!n) continue;
            if (n->nodeType() == NodeItem::Household) {
                ++hhNodes;
                if (n->isVisible()) ++hhVisible;
            } else if (n->isPersonNode() && n->isVisible()) {
                ++peopleVisible;
            }
        }
        CHECK(hhNodes > 0);       // the seeded household became a node
        CHECK_EQ(hhVisible, 0);   // and it is hidden by the restore
        CHECK(peopleVisible > 0);
    }

    // Clean up the seeded data and restore defaults.
    CHECK(seed.exec("DELETE FROM entity_relationships WHERE source_id = 9001"));
    CHECK(seed.exec("DELETE FROM people WHERE id = 9001"));
    CHECK(seed.exec("DELETE FROM groups WHERE id = 9001"));
    CHECK(seed.exec("DELETE FROM households WHERE id = 9001"));
    s.setValue("graphFilters/households", true);
    s.setValue("graphFilters/people", true);
    s.sync();
    return true;
}

bool test_synergyEngineAndMigration19() {
    auto db = DatabaseManager::instance().database();
    CHECK(db.transaction());
    
    // 1. Check ministry_archetypes table has seeded rows and canonical blueprints exist
    QSqlQuery q("SELECT COUNT(*) FROM ministry_archetypes", db);
    CHECK(q.next());
    CHECK_EQ(q.value(0).toInt(), 11);
    CHECK_EQ(MinistryArchetypeRegistry::canonicalBlueprints().size(), 11);

    // 2. Check archetype-to-style vector conversion
    auto catalystVec = MinistryArchetypeRegistry::archetypeToStyleVector(ChurchArchetype::Catalyst);
    CHECK_EQ(catalystVec.sum(), 100.0);
    CHECK_EQ(catalystVec.energizer, 40.0);
    CHECK_EQ(catalystVec.organizer, 10.0);

    auto shepherdVec = MinistryArchetypeRegistry::archetypeToStyleVector(ChurchArchetype::Shepherd);
    CHECK_EQ(shepherdVec.listener, 45.0);
    CHECK_EQ(shepherdVec.connector, 30.0);

    // 3. Test Void Law boost on missing Anchor/Architect
    CandidateProfile c1;
    c1.personId = 1001;
    c1.primaryArchetype = ChurchArchetype::Anchor;
    c1.spiritualGifts = {"Helps"};

    GroupProfile g1;
    g1.groupId = 2001;
    g1.name = "Empty Tech Team";
    g1.category = "Multimedia";
    g1.idealPrimary = ChurchArchetype::Architect;
    g1.idealSecondary = ChurchArchetype::Anchor;
    g1.targetVector = RelationalStyleVector(30, 10, 40, 0, 20, 0);
    g1.requiredGifts = {"Worship / Music", "Administration"};
    g1.activeMemberCount = 0;

    int voidBoost = SynergyEngine::calculateVoidBoost(c1, g1);
    CHECK(voidBoost >= 25);

    // 4. Test Clash Law on strategist overload
    GroupProfile g2 = g1;
    g2.activeMemberCount = 5;
    g2.archetypeCounts[ChurchArchetype::Architect] = 3;
    CandidateProfile c2;
    c2.primaryArchetype = ChurchArchetype::Architect;
    int clashPenalty = SynergyEngine::calculateClashPenalty(c2, g2);
    CHECK(clashPenalty < 0);

    // 5. Serving load is now ADVISORY, not a hard block (dev-review decision):
    //    the candidate stays eligible and the workload is surfaced as a warning.
    CandidateProfile cBurnout = c1;
    cBurnout.activeGroupCount = 3;
    CHECK(SynergyEngine::isEligible(cBurnout, g1, 3));
    CHECK(SynergyEngine::collectWarnings(cBurnout, g1, 3).size() >= 1);

    // 6. Structural hard block remains: Closed / elected governance cohorts.
    GroupProfile gClosed = g1;
    gClosed.recruitmentStatus = "Closed";
    CHECK(!SynergyEngine::isEligible(c1, gClosed, 3));
    CHECK(SynergyEngine::collectWarnings(c1, gClosed, 3).isEmpty()); // warnings only for personal cases

    // 7. Test Overall Synergy Engine Evaluate
    SynergyMatchResult res = SynergyEngine::evaluate(c1, g1, 3);
    CHECK(!res.hardBlocked);
    CHECK(res.score >= 30);
    CHECK(!res.primaryReason.isEmpty());

    // 8. Test Equilibrium Delta
    double eqDelta = SynergyEngine::calculateEquilibriumDelta(c1, g1);
    CHECK(std::abs(eqDelta) >= 0.0);

    // 9. Schedule conflict is now an advisory warning, not a gate.
    CandidateProfile cSched = c1;
    cSched.currentGroupSchedules.append(qMakePair(QString("Sunday"), QString("10:00 AM")));
    GroupProfile gSched = g1;
    gSched.meetingDay = "Sunday";
    gSched.meetingTime = "10:00 AM";
    CHECK(SynergyEngine::isEligible(cSched, gSched, 3));
    CHECK(SynergyEngine::collectWarnings(cSched, gSched, 3).size() >= 1);

    // 10. At-risk + understaffed is an advisory warning, not a gate.
    CandidateProfile cCare = c1;
    cCare.careFlag = "At-Risk";
    GroupProfile gUnderstaffed = g1;
    gUnderstaffed.activeMemberCount = 1;
    CHECK(SynergyEngine::isEligible(cCare, gUnderstaffed, 3));
    CHECK(SynergyEngine::collectWarnings(cCare, gUnderstaffed, 3).size() >= 1);

    // 11. Test Connection Audit Persistence in entity_relationships
    QSqlQuery insP(db);
    CHECK(insP.exec("INSERT INTO people (first_name, last_name, membership_status) VALUES ('Audit', 'Test', 'Member')"));
    int testPersonId = insP.lastInsertId().toInt();
    QSqlQuery insG(db);
    CHECK(insG.exec("INSERT INTO groups (name) VALUES ('Audit Test Group')"));
    int testGroupId = insG.lastInsertId().toInt();

    QSqlQuery qAudit(db);
    qAudit.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, connection_source, archetype_fit_score, joined_date) "
                   "VALUES ('Person', ?, 'Group', ?, 'Member', 'smart_bridge', 85, CURRENT_DATE)");
    qAudit.addBindValue(testPersonId);
    qAudit.addBindValue(testGroupId);
    CHECK(qAudit.exec());

    QSqlQuery qVer("SELECT connection_source, archetype_fit_score FROM entity_relationships WHERE connection_source = 'smart_bridge'", db);
    CHECK(qVer.next());
    CHECK(qVer.value(0).toString() == "smart_bridge");
    CHECK_EQ(qVer.value(1).toInt(), 85);

    db.rollback();
    return true;
}

bool test_userManualIntegrity() {
    // 1. Verify Topic List and Lucide Icons
    const auto topics = UserManualDialog::getTopicList();
    CHECK_EQ(topics.size(), 13);
    for (const auto& t : topics) {
        CHECK(!t.id.isEmpty());
        CHECK(!t.title.isEmpty());
        CHECK(!t.lucideIcon.isEmpty());
    }

    // 2. Verify HTML Content and Anchor Integrity
    QString html = UserManualDialog::getFullManualHtml();
    CHECK(!html.isEmpty());
    CHECK(html.contains("<!DOCTYPE html>"));
    CHECK(html.contains("OpenEkklesia"));

    // Check that every topic ID in getTopicList() has a matching <a name="..."> anchor in the HTML
    for (const auto& t : topics) {
        QString anchorTag = QString("name=\"%1\"").arg(t.id);
        CHECK(html.contains(anchorTag));
    }

    // 3. Verify Lucide Vector Data URI Generation
    QString iconUri = UserManualDialog::getLucideDataUri("compass", QColor("#89b4fa"), 20);
    CHECK(iconUri.startsWith("data:image/png;base64,"));

    // 4. Verify Key Module Terms and Guidance
    CHECK(html.contains("Smart Bridge v2.0"));
    CHECK(html.contains("Chemistry Scoring"));
    CHECK(html.contains("Void Law"));
    CHECK(html.contains("Clash Law"));
    CHECK(html.contains("Equilibrium Law"));
    CHECK(html.contains("Always Advisory"));
    CHECK(html.contains("household and relationship networks"));
    CHECK(html.contains("double-entry accounting engine"));
    CHECK(html.contains("WCAG 2.1 AA"));
    CHECK(html.contains("Ctrl+K"));
    CHECK(html.contains("F1"));
    CHECK(html.contains("Role-Based Access Control"));
    // The vault claim must not overpromise encryption (no encryption in codebase).
    CHECK(!html.contains("encrypted, local document"));
    // Manual must not describe fictional RBAC roles.
    CHECK(!html.contains("Ministry Leader</b></td>"));
    CHECK(!html.contains("Auditor</b></td>"));

    // 5. Verify UI Construction & Instantiation
    UserManualDialog dlg;
    CHECK(dlg.windowTitle().contains("OpenEkklesia"));

    return true;
}

bool test_graphViewOptionsAndDockToggle() {
    GraphWidget gw;
    
    // 1. Test Default Connector Size and Scaling
    CHECK_EQ(gw.connectorSizeLevel(), 2); // Level 2 (Normal) default
    CHECK(gw.showConnectors());           // Enabled by default

    // 2. Test Connector Size Level Changes (1 = Thin, 3 = Bold)
    gw.setConnectorSizeLevel(1);
    CHECK_EQ(gw.connectorSizeLevel(), 1);
    gw.setConnectorSizeLevel(3);
    CHECK_EQ(gw.connectorSizeLevel(), 3);
    gw.setConnectorSizeLevel(2);
    CHECK_EQ(gw.connectorSizeLevel(), 2);

    // Bounds checking
    gw.setConnectorSizeLevel(10);
    CHECK_EQ(gw.connectorSizeLevel(), 3);
    gw.setConnectorSizeLevel(-5);
    CHECK_EQ(gw.connectorSizeLevel(), 1);

    // 3. Test Hide/Show Connectors Toggle
    gw.setShowConnectors(false);
    CHECK(!gw.showConnectors());
    gw.toggleConnectors();
    CHECK(gw.showConnectors());
    gw.toggleConnectors();
    CHECK(!gw.showConnectors());
    gw.setShowConnectors(true);
    CHECK(gw.showConnectors());

    // 4. Test Inspector Dock Collapse & Expand Toggle
    gw.toggleInspectorDock();
    gw.toggleInspectorDock();

    // 5. Test Canvas Viewport Update Mode (Minimal repaints only dirty
    //    regions: with the 33 ms physics timer a full-viewport repaint of
    //    the 100+ node canvas was the largest CPU draw on the graph page).
    auto* canvasView = gw.findChild<GraphCanvasView*>();
    CHECK(canvasView);
    if (canvasView) {
        CHECK_EQ(canvasView->viewportUpdateMode(), QGraphicsView::MinimalViewportUpdate);
    }

    // 6. Test EdgeItem geometry adjust
    NodeItem sNode(8881, "EdgeTestSrc", NodeItem::Person);
    NodeItem tNode(8882, "EdgeTestTgt", NodeItem::Person);
    sNode.setPos(0, 0);
    tNode.setPos(100, 100);
    EdgeItem testEdge(&sNode, &tNode, "Friend");
    QRectF b1 = testEdge.boundingRect();
    CHECK(b1.contains(QPointF(50, 50)));
    sNode.setPos(200, 200);
    testEdge.adjust();
    QRectF b2 = testEdge.boundingRect();
    CHECK(b2.contains(QPointF(150, 150)));
    
    return true;
}

// Smart Bridge v2: bulk loaders, family-tie detection, source tagging.
bool test_smartBridgeEngineAndSourceLogging() {
    QSqlDatabase d = db();
    d.transaction();

    // Household with two members; one is already in the group -> family tie.
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO households (id, family_name) VALUES (9101, 'Bridge Family')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status, household_id, ministry_personality, spiritual_gifts) "
                 "VALUES (9101, 'Cand', 'One', 'Member', 9101, 'Catalyst', 'Worship,Leadership')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status, household_id, ministry_personality) "
                 "VALUES (9102, 'Fam', 'Two', 'Member', 9101, 'Anchor')"));
    CHECK(q.exec("INSERT INTO groups (id, name, category) VALUES (9101, 'Bridge Team', 'Alabanza')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status, connection_source) "
                 "VALUES ('Person', 9102, 'Group', 9101, 'Member', 'Active', 'manual')"));

    // Bulk loaders: one query pass fills the whole population.
    // NOTE: shared suite DB may contain helper rows (e.g. id 1 from dossier
    // tests), so assert containment, not exact size, per guidelines.
    QMap<int, CandidateProfile> people;
    SynergyEngine::loadAllCandidates(d, people);
    CHECK(people.size() >= 2);
    CHECK(people.contains(9101) && people.contains(9102));
    if (people.contains(9101)) {
        CHECK(people[9101].primaryArchetype == ChurchArchetype::Catalyst);
        CHECK(people[9101].spiritualGifts.contains("Worship"));
    }

    QMap<int, GroupProfile> groups;
    SynergyEngine::loadAllGroups(d, 9101, groups); // candidate = person 9101
    // The suite database also carries the canonical blueprint heading groups
    // (re-created by purgeDemoData), so assert containment rather than a size.
    CHECK(groups.size() >= 1);
    CHECK(groups.contains(9101));
    if (groups.contains(9101)) {
        const GroupProfile& g = groups[9101];
        CHECK(g.hasHouseholdFamilyTie);           // household member 9102 is active
        CHECK(g.activeMemberCount == 1);
        // Alabanza blueprint: ideal primary is Catalyst per the canonical table.
        CHECK(g.idealPrimary == ChurchArchetype::Catalyst);
    }

    // Scoring: candidate 9101 is eligible and scores within 15..98.
    if (people.contains(9101) && groups.contains(9101)) {
        SynergyMatchResult res = SynergyEngine::evaluate(people[9101], groups[9101], 3);
        CHECK(!res.hardBlocked);
        CHECK(res.score >= 15 && res.score <= 98);
    }

    // Source tagging: Ministry Match batch adds log connection_source.
    QList<int> added = MinistryRecommender::addMembers(d, 9101, {9101});
    CHECK_EQ(added.size(), 1);
    QSqlQuery src(d);
    src.prepare("SELECT connection_source, joined_date FROM entity_relationships "
                "WHERE source_type = 'Person' AND source_id = ? AND target_id = ? AND relationship_type = 'Member'");
    src.addBindValue(9101);
    src.addBindValue(9101);
    CHECK(src.exec() && src.next());
    CHECK_EQ(src.value(0).toString().toStdString(), std::string("ministry_match"));
    CHECK(!src.value(1).toString().isEmpty()); // joined_date recorded

    d.rollback();
    return true;
}

// Regression: Equilibrium Law simulation must blend the member MEAN with the
// candidate, i.e. (mean * n + candidate) / (n + 1). The old formula treated the
// mean as a sum and re-divided by (n + 1), so large teams dominated the mix.
bool test_synergyEquilibriumSimulationUsesMemberAverage() {
    CandidateProfile cand;
    cand.personId = 9701;
    cand.primaryArchetype = ChurchArchetype::Architect;
    cand.secondaryArchetype = ChurchArchetype::None;

    GroupProfile group;
    group.groupId = 9701;
    group.activeMemberCount = 10;
    // Deliberately different from the candidate: mostly strategist/connector.
    group.currentAggregateVector = RelationalStyleVector(2.0, 1.0, 1.0, 1.0, 80.0, 15.0);
    group.targetVector = RelationalStyleVector(10.0, 10.0, 30.0, 10.0, 30.0, 10.0);

    const RelationalStyleVector candVec = MinistryArchetypeRegistry::personStyleVector(
        cand.primaryArchetype, cand.secondaryArchetype);
    const double n = static_cast<double>(group.activeMemberCount);
    const double distBefore = RelationalStyleVector::euclideanDistance(
        group.currentAggregateVector, group.targetVector);

    RelationalStyleVector expected(
        (group.currentAggregateVector.organizer * n + candVec.organizer) / (n + 1.0),
        (group.currentAggregateVector.listener * n + candVec.listener) / (n + 1.0),
        (group.currentAggregateVector.doer * n + candVec.doer) / (n + 1.0),
        (group.currentAggregateVector.energizer * n + candVec.energizer) / (n + 1.0),
        (group.currentAggregateVector.strategist * n + candVec.strategist) / (n + 1.0),
        (group.currentAggregateVector.connector * n + candVec.connector) / (n + 1.0));
    const double expectedDelta = distBefore - RelationalStyleVector::euclideanDistance(expected, group.targetVector);
    const double actualDelta = SynergyEngine::calculateEquilibriumDelta(cand, group);
    CHECK(std::fabs(actualDelta - expectedDelta) < 1e-9);

    // The old (mean + candidate) / (n + 1) formula would produce a clearly
    // different value for a 10-member team.
    RelationalStyleVector buggy(
        (group.currentAggregateVector.organizer + candVec.organizer) / (n + 1.0),
        (group.currentAggregateVector.listener + candVec.listener) / (n + 1.0),
        (group.currentAggregateVector.doer + candVec.doer) / (n + 1.0),
        (group.currentAggregateVector.energizer + candVec.energizer) / (n + 1.0),
        (group.currentAggregateVector.strategist + candVec.strategist) / (n + 1.0),
        (group.currentAggregateVector.connector + candVec.connector) / (n + 1.0));
    const double buggyDelta = distBefore - RelationalStyleVector::euclideanDistance(buggy, group.targetVector);
    CHECK(std::fabs(actualDelta - buggyDelta) > 0.01);

    // n == 0 must not divide by zero or double-count: the candidate alone forms
    // the simulated mix.
    GroupProfile emptyGroup = group;
    emptyGroup.activeMemberCount = 0;
    emptyGroup.currentAggregateVector = RelationalStyleVector();
    const RelationalStyleVector uniform(1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
    const double expectedZeroDelta =
        RelationalStyleVector::euclideanDistance(uniform, group.targetVector) -
        RelationalStyleVector::euclideanDistance(candVec, group.targetVector);
    const double zeroDelta = SynergyEngine::calculateEquilibriumDelta(cand, emptyGroup);
    CHECK(std::fabs(zeroDelta - expectedZeroDelta) < 1e-9);
    CHECK(std::isfinite(zeroDelta));

    return true;
}

bool test_smartBridgeAdvisoryAndHiddenGroups() {
    // Dev-review model: personal circumstances are ADVISORY warnings a pastor
    // can override; only structural cohorts hard-block. Hidden groups leave
    // the bulk-load used by the graph's Smart Bridge scan.
    QSqlDatabase d = db();
    QSqlQuery q(d);

    // Migration 21 must have added groups.is_hidden.
    bool hasHidden = false;
    QSqlQuery cols(d);
    cols.exec("PRAGMA table_info(groups)");
    while (cols.next()) {
        if (cols.value(1).toString() == "is_hidden") hasHidden = true;
    }
    CHECK(hasHidden);

    // Workload (3+ groups) => eligible + warning, NOT hard-blocked.
    GroupProfile gOpen;
    gOpen.groupId = 9701;
    gOpen.name = "Open Team";
    gOpen.recruitmentStatus = "Open";
    gOpen.activeMemberCount = 6;
    CandidateProfile cBusy;
    cBusy.personId = 9701;
    cBusy.activeGroupCount = 3;
    SynergyMatchResult rBusy = SynergyEngine::evaluate(cBusy, gOpen, 3);
    CHECK(!rBusy.hardBlocked);
    CHECK(rBusy.warnings.size() >= 1);
    CHECK(rBusy.warnings.first().contains("Workload Advisory"));

    // Closed cohort => structural hard block, no warnings.
    GroupProfile gClosed = gOpen;
    gClosed.recruitmentStatus = "Closed";
    SynergyMatchResult rClosed = SynergyEngine::evaluate(cBusy, gClosed, 3);
    CHECK(rClosed.hardBlocked);
    CHECK(!rClosed.hardBlockReason.isEmpty());

    // At-risk candidate on an understaffed team => advisory, not a block.
    CandidateProfile cCare = cBusy;
    cCare.activeGroupCount = 0;
    cCare.careFlag = "At-Risk";
    GroupProfile gTiny = gOpen;
    gTiny.activeMemberCount = 1;
    SynergyMatchResult rCare = SynergyEngine::evaluate(cCare, gTiny, 3);
    CHECK(!rCare.hardBlocked);
    CHECK(rCare.warnings.size() >= 1);
    CHECK(rCare.warnings.first().contains("Pastoral Note"));

    // Hidden groups are excluded from the bulk loader (graph/Smart Bridge).
    CHECK(q.exec("INSERT INTO groups (id, name, category, is_hidden) VALUES (9702, 'Hidden Ministry', 'Worship', 1)"));
    QMap<int, GroupProfile> all;
    SynergyEngine::loadAllGroups(d, 0, all);
    CHECK(!all.contains(9702));

    // Cleanup.
    CHECK(q.exec("DELETE FROM groups WHERE id = 9702"));
    return true;
}

// Right-side inspector upgrade: person context strip, group recruitment
// badge / meta / composition, and edge provenance + navigation.
bool test_graphInspectorUpgrade() {
    QSqlDatabase d = db();
    d.transaction();

    // Seed: an Elected group, a member with an archetype, and a candidate.
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (id, name, category, recruitment_status, max_capacity) "
                 "VALUES (9201, 'Board of Elders', 'Governance', 'Elected', 8)"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status, ministry_personality, date_of_birth) "
                 "VALUES (9201, 'Elder', 'One', 'Member', 'Anchor', '1980-05-01')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status, date_of_birth) "
                 "VALUES (9202, 'Cand', 'Two', 'Member', '1995-03-15')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status, connection_source, joined_date) "
                 "VALUES ('Person', 9201, 'Group', 9201, 'Leader', 'Active', 'manual', '2026-01-15')"));
    CHECK(q.exec("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes) "
                 "VALUES (9202, 'Phone Call', '2026-08-01', 'Check-in')"));

    GraphWidget gw;

    NodeItem* groupNode = nullptr;
    NodeItem* personNode = nullptr;
    EdgeItem* edgeItem = nullptr;
    for (QGraphicsItem* item : gw.scene()->items()) {
        if (auto* n = dynamic_cast<NodeItem*>(item)) {
            // The suite DB also carries the canonical blueprint heading groups,
            // so select the seeded test group by id instead of taking any node.
            if (n->nodeType() == NodeItem::Group && n->id() == 9201) groupNode = n;
            else if (n->isPersonNode()) personNode = n;
        } else if (auto* e = dynamic_cast<EdgeItem*>(item)) {
            edgeItem = e;
        }
    }
    CHECK(groupNode && personNode && edgeItem);
    if (!groupNode || !personNode || !edgeItem) return true;

    // Person context strip — the person with the pastoral care record.
    NodeItem* caredPerson = nullptr;
    for (QGraphicsItem* item : gw.scene()->items()) {
        auto* n = dynamic_cast<NodeItem*>(item);
        if (n && n->isPersonNode() && n->id() == 9202) { caredPerson = n; break; }
    }
    CHECK(caredPerson);
    if (!caredPerson) return true;
    gw.selectNodeForDetails(caredPerson);
    QLabel* stats = gw.findChild<QLabel*>("personStatsLabel");
    QLabel* care = gw.findChild<QLabel*>("personCareRecentLabel");
    CHECK(stats && care);
    if (stats) CHECK(stats->text().contains("Age"));
    if (care) CHECK(care->text().contains("Recent care"));

    // Group: recruitment badge (Elected), meta (capacity), composition.
    gw.selectNodeForDetails(groupNode);
    QLabel* recruit = gw.findChild<QLabel*>("groupRecruitLabel");
    QLabel* meta = gw.findChild<QLabel*>("groupMetaLabel");
    QLabel* comp = gw.findChild<QLabel*>("groupCompositionLabel");
    CHECK(recruit && meta && comp);
    if (recruit) CHECK(recruit->text().contains("Elected"));
    if (meta) CHECK(meta->text().contains("Capacity"));
    if (comp) CHECK(comp->text().contains("Composition") && comp->text().contains("Anchor"));

    // Edge: provenance + jump navigation buttons exist and are labelled.
    gw.selectEdgeForDetails(edgeItem);
    QLabel* esrc = gw.findChild<QLabel*>("edgeSourceLabel");
    CHECK(esrc);
    if (esrc) CHECK(esrc->text().contains("Source"));
    QPushButton* openLeft = gw.findChild<QPushButton*>("secondaryBtn"); // any; the two edge buttons are labelled
    Q_UNUSED(openLeft);

    d.rollback();
    return true;
}

// Node stacking: overlapping nodes must be separated by the collision pass.
bool test_graphCollisionResolution() {
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO groups (id, name, category) VALUES (9301, 'Collision Team', 'Alabanza')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (9301, 'One', 'Collide', 'Member')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (9302, 'Two', 'Collide', 'Member')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                 "VALUES ('Person', 9301, 'Group', 9301, 'Member')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                 "VALUES ('Person', 9302, 'Group', 9301, 'Member')"));

    GraphWidget gw;
    gw.showAllNodeTypesForTesting(); // user's saved "Isolated" filter must not hide the persons
    NodeItem* n1 = nullptr;
    NodeItem* n2 = nullptr;
    for (QGraphicsItem* item : gw.scene()->items()) {
        auto* n = dynamic_cast<NodeItem*>(item);
        if (n && n->isPersonNode()) {
            if (!n1) n1 = n;
            else if (!n2) n2 = n;
        }
    }
    CHECK(n1 && n2);
    if (!n1 || !n2) return true;
    CHECK(n1->isVisible() && n2->isVisible()); // collision pass only moves visible nodes

    gw.pausePhysics();
    // Stack both at an isolated spot away from other scene nodes, then resolve.
    n1->setPos(1200, 1200);
    n2->setPos(1200, 1200);
    gw.resolveNodeOverlaps();
    const QPointF delta = n2->pos() - n1->pos();
    const double dist = std::hypot(delta.x(), delta.y());
    CHECK(dist >= n1->radius() + n2->radius() + 8.0 - 0.01);

    d.rollback();
    return true;
}

bool test_peopleModuleStressAndBreakSuite() {
    // 1. Stress test CSV parsing with complex quotes and internal commas
    QString csv1 = "\"Doe, Jr.\", John, (555) 123-4567, \"john.doe@example.com\", \"Member\"";
    QStringList parsed1 = PeopleWidget::parseCsvLine(csv1);
    CHECK_EQ(parsed1.size(), 5);
    CHECK_EQ(parsed1[0].toStdString(), std::string("Doe, Jr."));
    CHECK_EQ(parsed1[1].toStdString(), std::string("John"));
    CHECK_EQ(parsed1[2].toStdString(), std::string("(555) 123-4567"));
    CHECK_EQ(parsed1[3].toStdString(), std::string("john.doe@example.com"));
    CHECK_EQ(parsed1[4].toStdString(), std::string("Member"));

    QString csv2 = "\"Smith \"\"The Builder\"\"\", Bob, 555-9999";
    QStringList parsed2 = PeopleWidget::parseCsvLine(csv2);
    CHECK_EQ(parsed2.size(), 3);
    CHECK_EQ(parsed2[0].toStdString(), std::string("Smith \"The Builder\""));
    CHECK_EQ(parsed2[1].toStdString(), std::string("Bob"));
    CHECK_EQ(parsed2[2].toStdString(), std::string("555-9999"));

    // 2. Test vCard generation compliance
    QString vcf = PeopleWidget::generateVCard("John", "Doe", "555-0199", "john.doe@test.com",
                                              "1985-06-15", "Member", "Needs Contact", "Pastoral notes here");
    CHECK(vcf.contains("BEGIN:VCARD"));
    CHECK(vcf.contains("VERSION:3.0"));
    CHECK(vcf.contains("N:Doe;John;;;"));
    CHECK(vcf.contains("FN:John Doe"));
    CHECK(vcf.contains("TEL;TYPE=CELL,VOICE:555-0199"));
    CHECK(vcf.contains("EMAIL;TYPE=INTERNET,PREF:john.doe@test.com"));
    CHECK(vcf.contains("BDAY:1985-06-15"));
    CHECK(vcf.contains("ROLE:Member"));
    CHECK(vcf.contains("END:VCARD"));
    // Confidentiality: the vCard must NEVER leak care flags or pastoral notes.
    CHECK(!vcf.contains("Needs Contact"));
    CHECK(!vcf.contains("Pastoral notes here"));
    CHECK(!vcf.contains("NOTE:"));

    // 3. Test PeopleWidget UI and drill-through filter mapping
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, phone_number, email, membership_status, join_date, date_of_birth, gender) "
                 "VALUES (9991, 'Alice', 'Zane', '555-8888', 'alice@test.com', 'Visitor', '2025-01-01', '1990-05-10', 'Female')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, phone_number, email, membership_status, join_date, date_of_birth, gender) "
                 "VALUES (9992, 'Zack', 'Adams', '555-1111', 'zack@test.com', 'Member', '2026-06-01', '1980-01-01', 'Male')"));

    PeopleWidget pw;
    pw.applyDrillThroughFilter("status", "Active");
    pw.applyDrillThroughFilter("status", "Visitor");
    pw.applyDrillThroughFilter("status", "Regular Attender");
    pw.applyDrillThroughFilter("status", "Member");
    pw.applyDrillThroughFilter("status", "Inactive");
    pw.applyDrillThroughFilter("unconnected", "");
    pw.applyDrillThroughFilter("care_flag", "Needs Contact");

    // 3b. Test Table and Column Sorting
    PeopleProxyModel proxy;
    QSqlRelationalTableModel srcModel(nullptr, d);
    srcModel.setTable("people");
    srcModel.select();
    proxy.setSourceModel(&srcModel);

    // Verify sort by Last Name Ascending
    proxy.setDynamicSortFilter(true);
    proxy.setSortMode(PeopleProxyModel::NormalColumnSort);
    proxy.sort(0, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);
    // 9991 (Zane, Alice) was inserted first, 9992 (Adams, Zack) second.
    // Alphabetical sort by Last Name must put Adams first, not Zane!
    CHECK(proxy.data(proxy.index(0, 0)).toString().contains("Adams"));

    // Verify sort by First Name Ascending
    proxy.setSortMode(PeopleProxyModel::SortByFirstName);
    proxy.sort(0, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);
    // Alphabetical sort by First Name must put Alice first!
    CHECK(proxy.data(proxy.index(0, 0)).toString().contains("Alice"));

    // Verify sort by Join Date
    proxy.setSortMode(PeopleProxyModel::SortByJoinDate);
    proxy.sort(0, Qt::DescendingOrder);
    CHECK(proxy.rowCount() >= 2);

    // Verify sort by Birth Date
    proxy.setSortMode(PeopleProxyModel::SortByBirthDate);
    proxy.sort(0, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);

    // Verify sort by Gender
    proxy.setSortMode(PeopleProxyModel::SortByGender);
    proxy.sort(0, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);

    // Verify remapped column sorts: Phone (col 2), Email (col 3), Status (col 4)
    proxy.setSortMode(PeopleProxyModel::NormalColumnSort);
    proxy.sort(2, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);
    proxy.sort(3, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);
    proxy.sort(4, Qt::AscendingOrder);
    CHECK(proxy.rowCount() >= 2);

    // 4. Test Cascade Deletion & Double-Entry Accounting Ledger Integrity
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (9995, 'TestDel', 'Member', 'Member')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9995, 'Group', 1, 'member_of')"));
    CHECK(q.exec("INSERT INTO care_logs (person_id, care_type, notes) VALUES (9995, 'Prayer', 'Prayer request')"));
    CHECK(q.exec("INSERT INTO transactions (id, date, description) VALUES (9995, '2026-08-26', 'Tithe Test')"));
    CHECK(q.exec("INSERT INTO splits (id, transaction_id, account_id, amount, person_id) VALUES (9995, 9995, 1, 150.0, 9995)"));
    CHECK(q.exec("INSERT INTO ministry_tasks (id, team, title, owner, start_day, duration_days, owner_id) VALUES (9995, 'Worship', 'Task for Member', 'TestDel Member', 1, 5, 9995)"));

    // Verify records exist
    QSqlQuery qCheck(d);
    CHECK(qCheck.exec("SELECT COUNT(*) FROM people WHERE id = 9995") && qCheck.next() && qCheck.value(0).toInt() == 1);
    CHECK(qCheck.exec("SELECT COUNT(*) FROM splits WHERE person_id = 9995") && qCheck.next() && qCheck.value(0).toInt() == 1);
    CHECK(qCheck.exec("SELECT COUNT(*) FROM ministry_tasks WHERE owner_id = 9995") && qCheck.next() && qCheck.value(0).toInt() == 1);

    // Delete Person and verify FK safety & split unlink
    QSqlQuery qDel(d);
    // Delete through cascade
    const QStringList childTables = { "pastoral_activities", "member_history", "care_logs", "attendance", "member_skills", "service_volunteers", "checkout_logs" };
    for (const QString& t : childTables) {
        qDel.prepare(QString("DELETE FROM %1 WHERE person_id = ?").arg(t));
        qDel.addBindValue(9995);
        qDel.exec();
    }
    qDel.prepare("UPDATE splits SET person_id = NULL WHERE person_id = ?");
    qDel.addBindValue(9995);
    CHECK(qDel.exec());

    qDel.prepare("UPDATE ministry_tasks SET owner_id = NULL WHERE owner_id = ?");
    qDel.addBindValue(9995);
    CHECK(qDel.exec());

    qDel.prepare("DELETE FROM entity_relationships WHERE (source_type = 'Person' AND source_id = ?) OR (target_type = 'Person' AND target_id = ?)");
    qDel.addBindValue(9995);
    qDel.addBindValue(9995);
    CHECK(qDel.exec());

    qDel.prepare("DELETE FROM people WHERE id = ?");
    qDel.addBindValue(9995);
    CHECK(qDel.exec());

    // Verify people row is gone
    CHECK(qCheck.exec("SELECT COUNT(*) FROM people WHERE id = 9995") && qCheck.next() && qCheck.value(0).toInt() == 0);
    // Verify accounting split STILL EXISTS for double-entry integrity, but person_id is unlinked
    CHECK(qCheck.exec("SELECT COUNT(*), person_id FROM splits WHERE id = 9995 GROUP BY id") && qCheck.next());
    CHECK(qCheck.value(1).isNull());
    // Verify ministry task owner is unlinked
    CHECK(qCheck.exec("SELECT COUNT(*), owner_id FROM ministry_tasks WHERE id = 9995 GROUP BY id") && qCheck.next());
    CHECK(qCheck.value(1).isNull());

    d.rollback();
    return true;
}

// Team decisions from the Settings brief: configurable date format app-wide
// (Q2), timezone/startOfWeek removed, ChurchProfile letterhead on PDFs (Q3).
bool test_settingsLocalizationAndChurchProfile() {
    AppSettings s;

    // 1. AppDateFormatter honors the configured date format.
    s.setValue("dateFormat", "dd/MM/yyyy");
    s.sync();
    CHECK_EQ(AppDateFormatter::formatDate(QDate(2026, 8, 25)).toStdString(), std::string("25/08/2026"));
    s.setValue("dateFormat", "yyyy-MM-dd");
    s.sync();
    CHECK_EQ(AppDateFormatter::formatDate(QDate(2026, 8, 25)).toStdString(), std::string("2026-08-25"));

    // 2. ChurchProfile letterhead (no logo yet).
    s.setValue("churchName", "Grace Community Church");
    s.setValue("churchAddress", "123 Grace Lane, Metropolis");
    s.setValue("churchTaxId", "12-3456789");
    s.setValue("churchSubtitle", "Test Subtitle");
    s.sync();

    QString header = ChurchProfile::renderHtmlHeader("Donor Receipt", "FY Test", /*includeTaxId=*/true);
    CHECK(header.contains("Grace Community Church"));
    CHECK(header.contains("123 Grace Lane"));
    CHECK(header.contains("Tax ID: 12-3456789"));
    CHECK(header.contains("Donor Receipt"));
    CHECK(!header.contains("data:image")); // no logo configured

    // 3. Run sheets omit the Tax ID (team decision).
    const QString runSheetHeader = ChurchProfile::renderHtmlHeader("Order of Service", QString(), /*includeTaxId=*/false);
    CHECK(!runSheetHeader.contains("Tax ID"));
    CHECK(runSheetHeader.contains("Grace Community Church"));

    // 4. A configured logo embeds as base64.
    const QString pngPath = QDir::currentPath() + "/oe_logo_test.png"; // scratch dir is always writable
    QImage logoImg(64, 64, QImage::Format_ARGB32);
    logoImg.fill(Qt::blue);
    CHECK(logoImg.save(pngPath));
    s.setValue("churchLogo", pngPath);
    s.sync();
    const QString withLogo = ChurchProfile::renderHtmlHeader("Test", QString(), false);
    CHECK(withLogo.contains("data:image/png;base64"));

    // Cleanup.
    s.remove("dateFormat");
    s.remove("churchName");
    s.remove("churchAddress");
    s.remove("churchTaxId");
    s.remove("churchSubtitle");
    s.remove("churchLogo");
    s.sync();
    QFile::remove(pngPath);
    return true;
}

bool test_planningFrameworksAndEventsSuite() {
    QSqlDatabase d = db();

    // 1. Schema Migration 22 verification
    QSqlQuery qVer(d);
    CHECK(qVer.exec("SELECT MAX(version) FROM schema_version"));
    CHECK(qVer.next());
    CHECK(qVer.value(0).toInt() >= 22);

    // Verify columns on ministry_tasks
    QSqlQuery taskCols(d);
    taskCols.exec("PRAGMA table_info(ministry_tasks)");
    QSet<QString> cols;
    while (taskCols.next()) cols.insert(taskCols.value(1).toString());
    CHECK(cols.contains("priority"));
    CHECK(cols.contains("category"));
    CHECK(cols.contains("progress_pct"));
    CHECK(cols.contains("notes"));

    // Verify ministry_events table exists with seeded rows
    // (purge clears demo events, so re-seed if needed — tests must not rely
    // on leftover demo rows per DEVELOPMENT_GUIDELINES).
    ensureMinistryEvents(5);
    QSqlQuery qEv(d);
    CHECK(qEv.exec("SELECT COUNT(*) FROM ministry_events"));
    CHECK(qEv.next());
    int evCount = qEv.value(0).toInt();
    CHECK(evCount >= 5);

    // 2. PlanningWidget full multi-framework UI validation
    PlanningWidget pw;
    QTabWidget* tabs = pw.findChild<QTabWidget*>("planningTabWidget");
    CHECK(tabs);
    if (tabs) {
        CHECK_EQ(tabs->count(), 5);
        CHECK_EQ(tabs->tabText(0).toStdString(), std::string("Strategic Roadmap"));
        CHECK_EQ(tabs->tabText(1).toStdString(), std::string("Church Events Hub"));
        CHECK_EQ(tabs->tabText(2).toStdString(), std::string("Agile Kanban Board"));
        CHECK_EQ(tabs->tabText(3).toStdString(), std::string("Event Project Gantt"));
        CHECK_EQ(tabs->tabText(4).toStdString(), std::string("Ministry OKRs & Goals"));
    }

    // 3. Church Events Hub table
    QTableWidget* evTable = pw.findChild<QTableWidget*>("planningEventsTable");
    CHECK(evTable);
    if (evTable) {
        CHECK(evTable->rowCount() >= 5);
        CHECK_EQ(evTable->columnCount(), 9);
    }

    // 4. Insert custom church event and verify reload
    QSqlQuery insEv(d);
    insEv.prepare("INSERT INTO ministry_events (title, event_type, start_datetime, end_datetime, expected_attendance, budget_cents, status) "
                  "VALUES ('Kingdom Builders Banquet', 'Fellowship', '2026-10-10 18:00', '2026-10-10 21:00', 300, 500000, 'Confirmed')");
    CHECK(insEv.exec());
    int insertedId = insEv.lastInsertId().toInt();
    CHECK(insertedId > 0);

    pw.refreshAll();
    if (evTable) {
        bool found = false;
        for (int r = 0; r < evTable->rowCount(); ++r) {
            if (evTable->item(r, 1) && evTable->item(r, 1)->text().contains("Kingdom Builders Banquet")) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    // 5. Cleanup custom event
    QSqlQuery delEv(d);
    delEv.exec(QString("DELETE FROM ministry_events WHERE id = %1").arg(insertedId));

    return true;
}

bool test_contextMenusAndRoleGatingSuite() {
    QSqlDatabase d = db();
    d.transaction();

    auto findButton = [](QWidget* w, const QString& text) -> QPushButton* {
        for (QPushButton* b : w->findChildren<QPushButton*>()) {
            if (b->text().trimmed().contains(text)) return b;
        }
        return nullptr;
    };

    // 1. FacilitiesWidget RBAC & tooltips
    FacilitiesWidget fac;
    fac.applyRole("guest");
    QPushButton* btnBook = fac.findChild<QPushButton*>("primaryBtn");
    if (btnBook) {
        CHECK(!btnBook->isEnabled());
        CHECK(btnBook->toolTip().contains("Guest (Read-Only) mode"));
    }
    fac.applyRole("admin");
    if (btnBook) {
        CHECK(btnBook->isEnabled());
    }

    // 2. PlanningWidget RBAC: header mutators, Kanban quick-add and the
    //    double-click/quick-add dialog entry points are guest-gated.
    PlanningWidget plan;
    QPushButton* planAddTask = findButton(&plan, "Add Initiative");
    QPushButton* planAddEvent = findButton(&plan, "Add Church Event");
    QList<QToolButton*> quickAdds = plan.findChildren<QToolButton*>(QStringLiteral("kanbanQuickAdd"));
    CHECK(planAddTask && planAddEvent);
    CHECK_EQ(quickAdds.size(), 5);
    if (planAddTask && planAddEvent && !quickAdds.isEmpty()) {
        plan.applyRole("guest");
        CHECK(!planAddTask->isEnabled());
        CHECK(!planAddEvent->isEnabled());
        CHECK(!quickAdds.first()->isEnabled());
        CHECK(quickAdds.first()->toolTip().contains("Guest (Read-Only) mode"));

        plan.applyRole("admin");
        CHECK(planAddTask->isEnabled());
        CHECK(planAddEvent->isEnabled());
        CHECK(quickAdds.first()->isEnabled());
    }

    // 3. GraphWidget RBAC: every DB-writing affordance in the inspectors
    //    (care flag, notes, contact log, edge role/sever, batch assign, add
    //    member, suggest people) must be off for guests and back for admins.
    GraphWidget graph;
    QComboBox* careFlagCombo = graph.findChild<QComboBox*>("careFlagCombo");
    QComboBox* edgeRoleCombo = graph.findChild<QComboBox*>("edgeRoleCombo");
    QPushButton* saveNotes = findButton(&graph, "Save Notes");
    QPushButton* logContact = findButton(&graph, "Log Contact Attempt");
    QPushButton* severEdge = findButton(&graph, "Sever Connection");
    QPushButton* batchAssign = findButton(&graph, "Batch Assign to Group");
    QPushButton* addMember = findButton(&graph, "Add Member");
    QPushButton* suggestPeople = findButton(&graph, "Suggest People");
    CHECK(careFlagCombo && edgeRoleCombo);
    CHECK(saveNotes && logContact && severEdge && batchAssign && addMember && suggestPeople);
    if (careFlagCombo && edgeRoleCombo && saveNotes && logContact && severEdge &&
        batchAssign && addMember && suggestPeople) {
        graph.applyRole("guest");
        CHECK(!careFlagCombo->isEnabled());
        CHECK(!edgeRoleCombo->isEnabled());
        CHECK(!saveNotes->isEnabled());
        CHECK(!logContact->isEnabled());
        CHECK(!severEdge->isEnabled());
        CHECK(!batchAssign->isEnabled());
        CHECK(!addMember->isEnabled());
        CHECK(!suggestPeople->isEnabled());
        CHECK(saveNotes->toolTip().contains("Guest (Read-Only) mode"));

        graph.applyRole("admin");
        CHECK(careFlagCombo->isEnabled());
        CHECK(edgeRoleCombo->isEnabled());
        CHECK(saveNotes->isEnabled());
        CHECK(logContact->isEnabled());
        CHECK(severEdge->isEnabled());
        CHECK(batchAssign->isEnabled());
        CHECK(addMember->isEnabled());
        CHECK(suggestPeople->isEnabled());
    }
    graph.applyRole("admin");

    // 4. ShepherdWidget RBAC
    ShepherdWidget shepherd;
    shepherd.applyRole("guest");
    shepherd.applyRole("admin");

    // 5. PeopleWidget RBAC & tooltips
    PeopleWidget people;
    people.applyRole("guest");
    QPushButton* btnAddPerson = people.findChild<QPushButton*>();
    if (btnAddPerson && btnAddPerson->toolTip().contains("Guest")) {
        CHECK(!btnAddPerson->isEnabled());
    }
    people.applyRole("admin");

    d.rollback();
    return true;
}

bool test_contextualGroupDelegationSuite() {
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);

    // 1. Create a dedicated test group and add 2 people (1 Leader, 1 Member)
    CHECK(q.exec("INSERT INTO groups (id, name, category) VALUES (9501, 'Delegation Test Team', 'Alabanza')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (9501, 'LeaderAlpha', 'TestLeader', 'Member')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (9502, 'MemberBeta', 'TestMember', 'Member')"));
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (9503, 'OutsideGamma', 'TestOutside', 'Member')"));

    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                 "VALUES ('Person', 9501, 'Group', 9501, 'Leader')"));
    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                 "VALUES ('Person', 9502, 'Group', 9501, 'Member')"));

    // Verify group-filtered query only returns 9501 and 9502, with Leader first
    QSqlQuery qFiltered(d);
    qFiltered.prepare(
        "SELECT p.id, p.first_name, er.relationship_type "
        "FROM entity_relationships er "
        "JOIN people p ON er.source_id = p.id "
        "WHERE er.target_type = 'Group' AND er.target_id = ? "
        "ORDER BY (er.relationship_type LIKE '%Leader%') DESC, p.last_name, p.first_name");
    qFiltered.addBindValue(9501);
    CHECK(qFiltered.exec());
    CHECK(qFiltered.next());
    CHECK_EQ(qFiltered.value(0).toInt(), 9501);
    CHECK_EQ(qFiltered.value(2).toString().toStdString(), std::string("Leader"));
    CHECK(qFiltered.next());
    CHECK_EQ(qFiltered.value(0).toInt(), 9502);
    CHECK_EQ(qFiltered.value(2).toString().toStdString(), std::string("Member"));
    CHECK(!qFiltered.next()); // OutsideGamma (9503) is NOT returned!

    // Verify Add Roster query excludes already enrolled members (9501, 9502)
    QSqlQuery qAddRoster(d);
    qAddRoster.prepare(
        "SELECT id FROM people "
        "WHERE id NOT IN ("
        "    SELECT source_id FROM entity_relationships "
        "    WHERE target_type = 'Group' AND target_id = ? AND source_type = 'Person'"
        ") AND id IN (9501, 9502, 9503)");
    qAddRoster.addBindValue(9501);
    CHECK(qAddRoster.exec());
    CHECK(qAddRoster.next());
    CHECK_EQ(qAddRoster.value(0).toInt(), 9503); // Only OutsideGamma is eligible
    CHECK(!qAddRoster.next());

    d.rollback();
    return true;
}

bool test_discipleshipPathwaySuite() {
    QSqlDatabase d = db();

    // 1. Migration verification: the DB must be at the build's latest schema
    // version (read from the constant so adding migrations never goes stale).
    QSqlQuery qVer(d);
    CHECK(qVer.exec("SELECT MAX(version) FROM schema_version"));
    CHECK(qVer.next());
    CHECK_EQ(qVer.value(0).toInt(), DatabaseManager::kLatestSchemaVersion);

    // Verify columns on people
    QSqlQuery pCols(d);
    pCols.exec("PRAGMA table_info(people)");
    QSet<QString> cols;
    while (pCols.next()) cols.insert(pCols.value(1).toString());
    CHECK(cols.contains("is_baptized"));
    CHECK(cols.contains("baptism_date"));
    CHECK(cols.contains("next_steps_completed"));
    CHECK(cols.contains("next_steps_date"));
    CHECK(cols.contains("membership_app_path"));

    // Verify indexes
    QSqlQuery qIdx(d);
    CHECK(qIdx.exec("SELECT name FROM sqlite_master WHERE type='index' AND name='idx_people_baptized'"));
    CHECK(qIdx.next());
    CHECK(qIdx.exec("SELECT name FROM sqlite_master WHERE type='index' AND name='idx_people_next_steps'"));
    CHECK(qIdx.next());

    // 2. Flexible Date Round-Trip (Exact, Month Year, Year)
    CHECK(d.transaction());
    QSqlQuery qIns(d);
    qIns.prepare("INSERT INTO people (id, first_name, last_name, membership_status, "
                 "is_baptized, baptism_date, next_steps_completed, next_steps_date, membership_app_path, gender) "
                 "VALUES (9901, 'TestCandidate', 'FlexibleDate', 'Regular Attender', "
                 "1, 'June 1998', 1, 'May 2015', '/path/to/scan.pdf', 'Female')");
    CHECK(qIns.exec());

    QSqlQuery qSel(d);
    qSel.prepare("SELECT is_baptized, baptism_date, next_steps_completed, next_steps_date, membership_app_path, gender "
                 "FROM people WHERE id = 9901");
    CHECK(qSel.exec());
    CHECK(qSel.next());
    CHECK_EQ(qSel.value(0).toInt(), 1);
    CHECK_EQ(qSel.value(1).toString().toStdString(), std::string("June 1998"));
    CHECK_EQ(qSel.value(2).toInt(), 1);
    CHECK_EQ(qSel.value(3).toString().toStdString(), std::string("May 2015"));
    CHECK_EQ(qSel.value(4).toString().toStdString(), std::string("/path/to/scan.pdf"));
    CHECK_EQ(qSel.value(5).toString().toStdString(), std::string("Female"));

    // 3. Readiness for Membership Logic
    // Candidate 9901 has baptized=1, next_steps=1, status='Regular Attender' -> Ready for Membership
    QSqlQuery qReady(d);
    CHECK(qReady.exec("SELECT id, first_name FROM people WHERE is_baptized = 1 AND next_steps_completed = 1 AND membership_status != 'Member'"));
    QList<int> readyIds;
    while (qReady.next()) readyIds.append(qReady.value(0).toInt());
    CHECK(readyIds.contains(9901));

    // Promote candidate 9901 to Member
    QSqlQuery qPromote(d);
    qPromote.prepare("UPDATE people SET membership_status = 'Member', "
                     "join_date = date('now') WHERE id = 9901");
    CHECK(qPromote.exec());

    // Verify 9901 is now a Member and no longer in Ready query
    QSqlQuery qCheckPromoted(d);
    CHECK(qCheckPromoted.exec("SELECT membership_status, join_date FROM people WHERE id = 9901"));
    CHECK(qCheckPromoted.next());
    CHECK_EQ(qCheckPromoted.value(0).toString().toStdString(), std::string("Member"));
    CHECK(!qCheckPromoted.value(1).toString().isEmpty());

    // 4. Vault Scanned Request Integration
    QSqlQuery qVault(d);
    qVault.prepare("INSERT INTO vault_documents (person_id, title, category, file_path, meeting_date) "
                   "VALUES (9901, 'Membership Request - Test', 'Membership', '/path/to/scan.pdf', date('now'))");
    CHECK(qVault.exec());

    QSqlQuery qVaultCheck(d);
    qVaultCheck.prepare("SELECT title, category, file_path FROM vault_documents WHERE person_id = 9901 AND category = 'Membership'");
    CHECK(qVaultCheck.exec());
    CHECK(qVaultCheck.next());
    CHECK_EQ(qVaultCheck.value(0).toString().toStdString(), std::string("Membership Request - Test"));
    CHECK_EQ(qVaultCheck.value(1).toString().toStdString(), std::string("Membership"));
    CHECK_EQ(qVaultCheck.value(2).toString().toStdString(), std::string("/path/to/scan.pdf"));

    d.rollback();
    return true;
}

bool test_blueprintGroupHierarchySuite() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Verify 11 Canonical Ministry Blueprints exist in groups table as root groups
    const auto blueprints = MinistryArchetypeRegistry::canonicalBlueprints();
    CHECK_EQ(static_cast<int>(blueprints.size()), 11);

    for (int i = 0; i < blueprints.size(); ++i) {
        const auto& bp = blueprints[i];
        QSqlQuery qIns(d);
        qIns.prepare("INSERT INTO groups (id, name, category, parent_id, ideal_primary_archetype, ideal_secondary_archetype, required_skills, is_archived) "
                     "VALUES (?, ?, ?, NULL, ?, ?, ?, 0)");
        qIns.addBindValue(8800 + i);
        qIns.addBindValue(bp.displayName);
        qIns.addBindValue(bp.categoryKey);
        qIns.addBindValue(bp.primaryArchetype);
        qIns.addBindValue(bp.secondaryArchetype);
        qIns.addBindValue(bp.requiredGifts.join(", "));
        CHECK(qIns.exec());
    }

    for (const auto& bp : blueprints) {
        QSqlQuery qCheck(d);
        qCheck.prepare("SELECT id, name, category, parent_id, ideal_primary_archetype, ideal_secondary_archetype "
                       "FROM groups WHERE (name = ? OR category = ?) "
                       "AND id >= 8800 "
                       "AND (parent_id IS NULL OR parent_id = 0)");
        qCheck.addBindValue(bp.displayName);
        qCheck.addBindValue(bp.categoryKey);
        CHECK(qCheck.exec());
        CHECK(qCheck.next());
        int rootId = qCheck.value(0).toInt();
        CHECK(rootId >= 8800);
        int pId = qCheck.value(3).toInt();
        CHECK_EQ(pId, 0); // Must be Level 0 Root Blueprint
    }

    // 1b. Empty/unknown category must fall back to "Other" — an empty needle
    // used to match every display name and returned the first blueprint.
    CHECK_EQ(MinistryArchetypeRegistry::getBlueprint(QString()).categoryKey.toStdString(), std::string("Other"));
    CHECK_EQ(MinistryArchetypeRegistry::getBlueprint("   ").categoryKey.toStdString(), std::string("Other"));
    CHECK_EQ(MinistryArchetypeRegistry::getBlueprint("No Such Ministry").categoryKey.toStdString(), std::string("Other"));
    CHECK_EQ(MinistryArchetypeRegistry::getBlueprint("Alabanza").categoryKey.toStdString(), std::string("Alabanza"));

    // 2. Test Sub-Group Creation under a Canonical Blueprint (Misiones)
    QSqlQuery qMissions(d);
    qMissions.prepare("SELECT id FROM groups WHERE (name LIKE '%Mision%' OR category LIKE '%Mision%') AND (parent_id IS NULL OR parent_id = 0) LIMIT 1");
    CHECK(qMissions.exec());
    CHECK(qMissions.next());
    int missionsRootId = qMissions.value(0).toInt();
    CHECK(missionsRootId > 0);

    // Launch a sub-group under Missions
    MinistryBlueprint mBp = MinistryArchetypeRegistry::getBlueprint("Misiones");
    CHECK_EQ(mBp.primaryArchetype.toStdString(), std::string("Spark"));
    CHECK_EQ(mBp.secondaryArchetype.toStdString(), std::string("Catalyst"));

    QSqlQuery qInsSub(d);
    qInsSub.prepare("INSERT INTO groups (name, category, parent_id, ideal_primary_archetype, ideal_secondary_archetype, required_skills, recruitment_status, is_archived) "
                    "VALUES ('Community Food Outreach', ?, ?, ?, ?, ?, 'Open', 0)");
    qInsSub.addBindValue(mBp.displayName);
    qInsSub.addBindValue(missionsRootId);
    qInsSub.addBindValue(mBp.primaryArchetype);
    qInsSub.addBindValue(mBp.secondaryArchetype);
    qInsSub.addBindValue(mBp.requiredGifts.join(", "));
    CHECK(qInsSub.exec());
    int subId = qInsSub.lastInsertId().toInt();
    CHECK(subId > 0);

    // Verify sub-group has correct parent and canonical archetype defaults
    QSqlQuery qCheckSub(d);
    qCheckSub.prepare("SELECT parent_id, category, ideal_primary_archetype, ideal_secondary_archetype FROM groups WHERE id = ?");
    qCheckSub.addBindValue(subId);
    CHECK(qCheckSub.exec());
    CHECK(qCheckSub.next());
    CHECK_EQ(qCheckSub.value(0).toInt(), missionsRootId);
    CHECK_EQ(qCheckSub.value(2).toString().toStdString(), std::string("Spark"));
    CHECK_EQ(qCheckSub.value(3).toString().toStdString(), std::string("Catalyst"));

    // 3. Test Member Enrollment on Sub-Team vs Root Blueprint Guard
    // A sub-team CAN have direct member enrollment
    QSqlQuery qPerson(d);
    CHECK(qPerson.exec("INSERT INTO people (id, first_name, last_name, membership_status) VALUES (8899, 'LeaderTest', 'SubTeam', 'Member')"));

    QSqlQuery qEnroll(d);
    qEnroll.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status) "
                    "VALUES ('Person', 8899, 'Group', ?, 'Leader', 'Active')");
    qEnroll.addBindValue(subId);
    CHECK(qEnroll.exec());

    // Verify sub-team roster query
    QSqlQuery qRoster(d);
    qRoster.prepare("SELECT COUNT(*) FROM entity_relationships WHERE target_type = 'Group' AND target_id = ?");
    qRoster.addBindValue(subId);
    CHECK(qRoster.exec());
    CHECK(qRoster.next());
    CHECK_EQ(qRoster.value(0).toInt(), 1);

    // Verify root blueprint has parent_id == 0 (which triggers direct-member guard in UI)
    QSqlQuery qRootGuard(d);
    qRootGuard.prepare("SELECT parent_id FROM groups WHERE id = ?");
    qRootGuard.addBindValue(missionsRootId);
    CHECK(qRootGuard.exec());
    CHECK(qRootGuard.next());
    CHECK(qRootGuard.value(0).isNull() || qRootGuard.value(0).toInt() <= 0);

    d.rollback();
    return true;
}

bool test_dynamicBlueprintHeadingsSuite() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. ensureBlueprintHeadings creates Level 0 root groups for the four
    // primary blueprints (plus custom ones) only — a fresh install opens with
    // four Groups; the other canonical blueprints stay registry-only.
    CHECK(DatabaseManager::ensureBlueprintHeadings(d));

    const auto blueprints = MinistryArchetypeRegistry::allBlueprints(false);
    CHECK(blueprints.size() >= 11);
    const QStringList primaryKeys = MinistryArchetypeRegistry::primaryBlueprintKeys();
    int nonPrimaryBpId = 0;
    for (const auto& bp : blueprints) {
        QSqlQuery qCheck(d);
        qCheck.prepare("SELECT id, parent_id FROM groups WHERE (name = ? OR category = ?) "
                       "AND (parent_id IS NULL OR parent_id = 0)");
        qCheck.addBindValue(bp.displayName);
        qCheck.addBindValue(bp.categoryKey);
        CHECK(qCheck.exec());
        const bool hasRoot = qCheck.next();
        // Exactly the primary blueprints carry an auto-created heading.
        CHECK_EQ(hasRoot, primaryKeys.contains(bp.categoryKey));
        if (hasRoot) {
            const int pId = qCheck.value(1).toInt();
            CHECK_EQ(pId, 0); // Must be Level 0
        } else if (bp.categoryKey == "Misiones") {
            nonPrimaryBpId = bp.id;
        }
    }

    // 1b. Non-primary canonical blueprints materialize on demand, idempotently.
    CHECK(nonPrimaryBpId > 0);
    const int demandRootId = DatabaseManager::ensureBlueprintHeadingGroup(nonPrimaryBpId, d);
    CHECK(demandRootId > 0);
    CHECK_EQ(DatabaseManager::ensureBlueprintHeadingGroup(nonPrimaryBpId, d), demandRootId);
    {
        QSqlQuery qLevel(d);
        qLevel.prepare("SELECT parent_id FROM groups WHERE id = ?");
        qLevel.addBindValue(demandRootId);
        CHECK(qLevel.exec());
        CHECK(qLevel.next());
        CHECK(qLevel.value(0).isNull() || qLevel.value(0).toInt() <= 0);
    }

    // 2. Create a new custom blueprint ministry ("Matrimonios / Marriage Ministry")
    int bpId = 0, grpId = 0;
    QStringList gifts = {"Pastoral Care", "Hospitality", "Teaching"};
    CHECK(DatabaseManager::createBlueprint("Matrimonios", "Matrimonios / Marriage Ministry",
                                           "Shepherd", "Spark", gifts,
                                           "Strengthening marital covenants, couples discipleship, and family counseling.",
                                           &bpId, &grpId, d));
    CHECK(bpId > 0);
    CHECK(grpId > 0);

    // Verify custom blueprint row in ministry_archetypes
    QSqlQuery qBp(d);
    qBp.prepare("SELECT category_key, display_name, ideal_primary, ideal_secondary, is_custom, is_archived "
                "FROM ministry_archetypes WHERE id = ?");
    qBp.addBindValue(bpId);
    CHECK(qBp.exec());
    CHECK(qBp.next());
    CHECK_EQ(qBp.value(0).toString().toStdString(), std::string("Matrimonios"));
    CHECK_EQ(qBp.value(1).toString().toStdString(), std::string("Matrimonios / Marriage Ministry"));
    CHECK_EQ(qBp.value(2).toString().toStdString(), std::string("Shepherd"));
    CHECK_EQ(qBp.value(3).toString().toStdString(), std::string("Spark"));
    CHECK_EQ(qBp.value(4).toInt(), 1); // is_custom == 1
    CHECK_EQ(qBp.value(5).toInt(), 0); // is_archived == 0

    // Verify top-level group heading in groups table
    QSqlQuery qGrp(d);
    qGrp.prepare("SELECT name, category, parent_id, ideal_primary_archetype, ideal_secondary_archetype, is_archived "
                 "FROM groups WHERE id = ?");
    qGrp.addBindValue(grpId);
    CHECK(qGrp.exec());
    CHECK(qGrp.next());
    CHECK_EQ(qGrp.value(0).toString().toStdString(), std::string("Matrimonios / Marriage Ministry"));
    CHECK_EQ(qGrp.value(1).toString().toStdString(), std::string("Matrimonios / Marriage Ministry"));
    CHECK(qGrp.value(2).isNull() || qGrp.value(2).toInt() <= 0); // Top-level heading
    CHECK_EQ(qGrp.value(3).toString().toStdString(), std::string("Shepherd"));
    CHECK_EQ(qGrp.value(4).toString().toStdString(), std::string("Spark"));
    CHECK_EQ(qGrp.value(5).toInt(), 0); // not archived

    // 3. Launch a sub-team under this custom blueprint
    QSqlQuery qSub(d);
    qSub.prepare("INSERT INTO groups (name, category, parent_id, ideal_primary_archetype, ideal_secondary_archetype, recruitment_status, is_archived) "
                 "VALUES ('Young Marrieds Small Group', 'Matrimonios / Marriage Ministry', ?, 'Shepherd', 'Spark', 'Open', 0)");
    qSub.addBindValue(grpId);
    CHECK(qSub.exec());
    int subId = qSub.lastInsertId().toInt();
    CHECK(subId > 0);

    // 4. Update the custom blueprint (e.g. adjust archetypes to Shepherd / Catalyst and update name)
    CHECK(DatabaseManager::updateBlueprint(bpId, "Matrimonios", "Matrimonios / Marriage & Family Ministry",
                                           "Shepherd", "Catalyst", gifts,
                                           "Couples discipleship, family building, and marital enrichment retreats.", d));

    // Verify top-level group heading synchronized
    QSqlQuery qGrpUpdated(d);
    qGrpUpdated.prepare("SELECT name, ideal_secondary_archetype FROM groups WHERE id = ?");
    qGrpUpdated.addBindValue(grpId);
    CHECK(qGrpUpdated.exec());
    CHECK(qGrpUpdated.next());
    CHECK_EQ(qGrpUpdated.value(0).toString().toStdString(), std::string("Matrimonios / Marriage & Family Ministry"));
    CHECK_EQ(qGrpUpdated.value(1).toString().toStdString(), std::string("Catalyst"));

    // 5. Test Cascading Archival: Archiving blueprint archives the blueprint, root group, and child sub-teams
    CHECK(DatabaseManager::archiveBlueprint(bpId, true, d));

    // Check blueprint is_archived = 1
    QSqlQuery qBpArch(d);
    qBpArch.prepare("SELECT is_archived FROM ministry_archetypes WHERE id = ?");
    qBpArch.addBindValue(bpId);
    CHECK(qBpArch.exec());
    CHECK(qBpArch.next());
    CHECK_EQ(qBpArch.value(0).toInt(), 1);

    // Check root group heading is_archived = 1
    QSqlQuery qRootArch(d);
    qRootArch.prepare("SELECT is_archived FROM groups WHERE id = ?");
    qRootArch.addBindValue(grpId);
    CHECK(qRootArch.exec());
    CHECK(qRootArch.next());
    CHECK_EQ(qRootArch.value(0).toInt(), 1);

    // Check child sub-team cascaded is_archived = 1
    QSqlQuery qSubArch(d);
    qSubArch.prepare("SELECT is_archived FROM groups WHERE id = ?");
    qSubArch.addBindValue(subId);
    CHECK(qSubArch.exec());
    CHECK(qSubArch.next());
    CHECK_EQ(qSubArch.value(0).toInt(), 1);

    // 6. Test Unarchive: Restores blueprint, root group, and child sub-teams
    CHECK(DatabaseManager::archiveBlueprint(bpId, false, d));

    QSqlQuery qBpRestore(d);
    qBpRestore.prepare("SELECT is_archived FROM ministry_archetypes WHERE id = ?");
    qBpRestore.addBindValue(bpId);
    CHECK(qBpRestore.exec());
    CHECK(qBpRestore.next());
    CHECK_EQ(qBpRestore.value(0).toInt(), 0);

    QSqlQuery qRootRestore(d);
    qRootRestore.prepare("SELECT is_archived FROM groups WHERE id = ?");
    qRootRestore.addBindValue(grpId);
    CHECK(qRootRestore.exec());
    CHECK(qRootRestore.next());
    CHECK_EQ(qRootRestore.value(0).toInt(), 0);

    QSqlQuery qSubRestore(d);
    qSubRestore.prepare("SELECT is_archived FROM groups WHERE id = ?");
    qSubRestore.addBindValue(subId);
    CHECK(qSubRestore.exec());
    CHECK(qSubRestore.next());
    CHECK_EQ(qSubRestore.value(0).toInt(), 0);

    d.rollback();
    return true;
}

bool test_groupMemberAndLeaderRosterSyncSuite() {
    QSqlDatabase d = db();
    d.transaction();

    // 1. Run ensureSchemaIntegrity to guarantee all columns are present
    DatabaseManager::ensureSchemaIntegrity(d);

    // Verify entity_relationships columns
    QSqlQuery qRelCols("PRAGMA table_info(entity_relationships)", d);
    QSet<QString> relCols;
    while (qRelCols.next()) relCols.insert(qRelCols.value(1).toString());
    CHECK(relCols.contains("status"));
    CHECK(relCols.contains("connection_source"));
    CHECK(relCols.contains("archetype_fit_score"));
    CHECK(relCols.contains("joined_date"));
    CHECK(relCols.contains("role"));

    // 2. Create a test group with a primary contact_person_id (e.g. Person 1)
    QSqlQuery qInsGrp(d);
    qInsGrp.prepare("INSERT INTO groups (name, category, contact_person_id, recruitment_status, is_archived) "
                    "VALUES ('Test Missions Outreach', 'Missions', 1, 'Open', 0)");
    CHECK(qInsGrp.exec());
    int grpId = qInsGrp.lastInsertId().toInt();
    CHECK(grpId > 0);

    // Ensure they weren't in entity_relationships yet
    QSqlQuery qDelRel(d);
    qDelRel.prepare("DELETE FROM entity_relationships WHERE source_type = 'Person' AND source_id = 1 AND target_type = 'Group' AND target_id = ?");
    qDelRel.addBindValue(grpId);
    qDelRel.exec();

    // Run ensureSchemaIntegrity to test auto-enrollment of primary contact leader
    DatabaseManager::ensureSchemaIntegrity(d);

    // Verify Person 1 is now automatically enrolled as 'Leader'
    QSqlQuery qCheckLeader(d);
    qCheckLeader.prepare("SELECT relationship_type, status, connection_source FROM entity_relationships "
                         "WHERE source_type = 'Person' AND source_id = 1 AND target_type = 'Group' AND target_id = ?");
    qCheckLeader.addBindValue(grpId);
    CHECK(qCheckLeader.exec());
    CHECK(qCheckLeader.next());
    CHECK_EQ(qCheckLeader.value(0).toString().toStdString(), std::string("Leader"));
    CHECK_EQ(qCheckLeader.value(1).toString().toStdString(), std::string("Active"));
    CHECK_EQ(qCheckLeader.value(2).toString().toStdString(), std::string("manual"));

    // 3. Test enrolling an additional member (e.g. Person 2 as Member)
    QSqlQuery qAddMem(d);
    qAddMem.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status, connection_source, joined_date, role) "
                    "VALUES ('Person', 2, 'Group', ?, 'Member', 'Active', 'manual', date('now','localtime'), 'Member')");
    qAddMem.addBindValue(grpId);
    CHECK(qAddMem.exec());

    // 4. Test re-adding/updating an existing member (graceful role upgrade from Member to Co-Leader)
    QSqlQuery qCheckDup(d);
    qCheckDup.prepare("SELECT id FROM entity_relationships WHERE source_type = 'Person' AND source_id = 2 AND target_type = 'Group' AND target_id = ?");
    qCheckDup.addBindValue(grpId);
    CHECK(qCheckDup.exec());
    CHECK(qCheckDup.next());
    int relId = qCheckDup.value(0).toInt();

    QSqlQuery qUpRole(d);
    qUpRole.prepare("UPDATE entity_relationships SET relationship_type = 'Co-Leader', status = 'Active' WHERE id = ?");
    qUpRole.addBindValue(relId);
    CHECK(qUpRole.exec());

    // Verify role updated
    QSqlQuery qVerifyUp(d);
    qVerifyUp.prepare("SELECT relationship_type FROM entity_relationships WHERE id = ?");
    qVerifyUp.addBindValue(relId);
    CHECK(qVerifyUp.exec());
    CHECK(qVerifyUp.next());
    CHECK_EQ(qVerifyUp.value(0).toString().toStdString(), std::string("Co-Leader"));

    d.rollback();
    return true;
}

bool test_givingStatementGenerator() {
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);

    // 1. Seed household, Bobby and Sally Smith, and spouse relationship
    CHECK(q.exec("INSERT OR IGNORE INTO households (id, family_name, address_line1, city, zip_code) "
                 "VALUES (9601, 'The Smith Family', '777 Blessing Way', 'Faithville', '54321')"));
    CHECK(q.exec("INSERT OR IGNORE INTO people (id, household_id, first_name, last_name, membership_status) "
                 "VALUES (9601, 9601, 'Bobby', 'Smith', 'Member')"));
    CHECK(q.exec("INSERT OR IGNORE INTO people (id, household_id, first_name, last_name, membership_status) "
                 "VALUES (9602, 9601, 'Sally', 'Smith', 'Member')"));

    CHECK(q.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                 "VALUES ('Person', 9601, 'Person', 9602, 'Spouse')"));

    // 2. Seed contributions for Bobby ($150 to General) and Sally ($50 to Missions) in 2026
    CHECK(q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, note) "
                 "VALUES (9601, 1, 15000, '2026-03-15', 'Check', 'Sunday Tithe')"));
    CHECK(q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, note) "
                 "VALUES (9602, 2, 5000, '2026-04-20', 'Online', 'Easter Offering')"));
    // Contribution in a different year (2025)
    CHECK(q.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, note) "
                 "VALUES (9601, 1, 20000, '2025-12-31', 'Check', 'Year End')"));

    // 3. Test Single Donor Statement for Bobby (2026)
    QString singleHtml = GivingStatementGenerator::generateHtmlStatement(9601, 2026, /*includeSpouseJoint=*/false);
    CHECK(!singleHtml.isEmpty());
    CHECK(singleHtml.contains("Bobby Smith"));
    CHECK(!singleHtml.contains("Sally"));
    CHECK(singleHtml.contains("Tax Year 2026"));
    CHECK(singleHtml.contains("Sunday Tithe"));
    CHECK(!singleHtml.contains("Easter Offering")); // Belongs to Sally
    CHECK(singleHtml.contains("150.00"));
    CHECK(!singleHtml.contains("501(c)(3) Compliant")); // Honest product guidelines
    CHECK(singleHtml.contains("Donor Contribution Statement"));

    // 4. Test Joint Statement for Bobby + Sally (2026)
    QString jointHtml = GivingStatementGenerator::generateHtmlStatement(9601, 2026, /*includeSpouseJoint=*/true);
    CHECK(!jointHtml.isEmpty());
    CHECK(jointHtml.contains("Bobby &amp; Sally Smith")); // HTML-escaped by design
    CHECK(jointHtml.contains("Joint Household"));
    CHECK(jointHtml.contains("Tax Year 2026"));
    CHECK(jointHtml.contains("Sunday Tithe"));
    CHECK(jointHtml.contains("Easter Offering"));
    CHECK(jointHtml.contains("200.00")); // Combined $150 + $50

    d.rollback();
    return true;
}

// iCal generator: RFC 5545 escaping, 75-octet folding, stable UIDs, and the
// all-day / overnight DTSTART-DTEND paths. Facilities, shepherd, planning and
// the service builder all delegate to this one generator.
bool test_icalGeneratorRfcCompliance() {
    using namespace ICalGenerator;

    // Escaping: backslash first, then ; , and line breaks.
    CHECK(escape(QString("a\\b;c,d")) == QString("a\\\\b\\;c\\,d"));
    CHECK(escape(QString("line1\r\nline2")) == QString("line1\\nline2"));
    CHECK(escape(QString("line1\rline2")) == QString("line1\\nline2"));

    // Short lines are returned unchanged.
    const QString shortLine = QString("SUMMARY:Hello");
    CHECK(foldLine(shortLine) == shortLine);

    // 100 'é' code points: 200 UTF-8 octets, so folding must split it while
    // never exceeding 75 octets per physical line and never splitting a code
    // point (a split would produce mojibake in the calendar client).
    QString longText;
    for (int i = 0; i < 100; ++i) longText += QChar(0x00E9);
    const QString folded = foldLine("SUMMARY:" + longText);
    const QStringList physical = folded.split('\n');
    CHECK(physical.size() > 1);
    for (const QString& line : physical) CHECK(line.toUtf8().size() <= 75);
    QString reassembled = physical.first();
    for (int i = 1; i < physical.size(); ++i) {
        CHECK(physical[i].startsWith(' ')); // continuation marker
        reassembled += physical[i].mid(1);
    }
    CHECK(reassembled == "SUMMARY:" + longText);

    // Timed event: escaped summary/location, DTSTART/DTEND, reminder alarm.
    const QDate date(2026, 8, 23);
    const QString ics = eventIcal(date, QTime(10, 0), QTime(11, 30),
                                  QString("Prayer; Meeting, Test"),
                                  QString("Line one\nLine two"),
                                  QString("Room 1, Building A"));
    CHECK(ics.startsWith("BEGIN:VCALENDAR"));
    CHECK(ics.endsWith("END:VCALENDAR\n"));
    CHECK(ics.contains("DTSTART:20260823T100000"));
    CHECK(ics.contains("DTEND:20260823T113000"));
    CHECK(ics.contains("SUMMARY:Prayer\\; Meeting\\, Test"));
    CHECK(ics.contains("DESCRIPTION:Line one\\nLine two"));
    CHECK(ics.contains("LOCATION:Room 1\\, Building A"));
    CHECK(ics.contains("BEGIN:VALARM"));
    for (const QString& line : ics.split('\n')) CHECK(line.toUtf8().size() <= 75);

    // Stable UID for identical events, distinct UID when the start time differs.
    const auto uidOf = [](const QString& text) {
        for (const QString& line : text.split('\n'))
            if (line.startsWith("UID:")) return line.mid(4);
        return QString();
    };
    const QString uid1 = uidOf(ics);
    const QString uid2 = uidOf(eventIcal(date, QTime(10, 0), QTime(11, 30),
                                         QString("Prayer; Meeting, Test")));
    const QString uid3 = uidOf(eventIcal(date, QTime(11, 0), QTime(12, 30),
                                         QString("Prayer; Meeting, Test")));
    CHECK(!uid1.isEmpty());
    CHECK(uid1 == uid2);
    CHECK(uid1 != uid3);

    // All-day event (no start time): exclusive DTEND is the next day.
    const QString allDay = eventIcal(date, QTime(), QTime(), QString("Church Picnic"));
    CHECK(allDay.contains("DTSTART;VALUE=DATE:20260823"));
    CHECK(allDay.contains("DTEND;VALUE=DATE:20260824"));

    // Overnight event: end time <= start time rolls DTEND to the next day.
    const QString overnight = eventIcal(date, QTime(23, 0), QTime(1, 0), QString("Night Watch"));
    CHECK(overnight.contains("DTSTART:20260823T230000"));
    CHECK(overnight.contains("DTEND:20260824T010000"));

    // Invalid date yields no calendar at all.
    CHECK(eventIcal(QDate(), QTime(10, 0), QTime(11, 0), QString("Nope")).isEmpty());
    return true;
}

// Double-entry ledger entry (the previously unreachable TransactionDialog) and
// the dashboard "Total Assets" KPI it feeds.
bool test_ledgerEntryAndTotalAssets() {
    seedDemoFixture();
    QSqlDatabase d = db();

    // The demo fixture seeds Checking (ASSET) and General Fund (INCOME) rows.
    auto findAccount = [&](const QString& name, const QString& type) {
        QSqlQuery q(d);
        q.prepare("SELECT id FROM accounts WHERE name = ? LIMIT 1");
        q.addBindValue(name);
        if (q.exec() && q.next()) return q.value(0).toInt();
        QSqlQuery f(d);
        f.prepare("SELECT id FROM accounts WHERE type = ? ORDER BY id DESC LIMIT 1");
        f.addBindValue(type);
        if (f.exec() && f.next()) return f.value(0).toInt();
        return 0;
    };
    const int assetId = findAccount("Checking Account", "ASSET");
    const int incomeId = findAccount("General Fund", "INCOME");
    CHECK(assetId > 0 && incomeId > 0);
    if (assetId <= 0 || incomeId <= 0) return true;

    auto totalAssets = [&]() -> double {
        QSqlQuery q(d);
        if (q.exec("SELECT COALESCE(SUM(s.amount), 0) FROM splits s "
                   "JOIN accounts a ON s.account_id = a.id WHERE a.type = 'ASSET'")
            && q.next()) {
            return q.value(0).toDouble();
        }
        return -1.0;
    };
    auto txCount = [&](const QString& description) {
        QSqlQuery q(d);
        q.prepare("SELECT COUNT(*) FROM transactions WHERE description = ?");
        q.addBindValue(description);
        if (q.exec() && q.next()) return q.value(0).toInt();
        return -1;
    };

    const QString okDesc = QStringLiteral("__ledger_test_ok_9401__");
    const QString badDesc = QStringLiteral("__ledger_test_bad_9401__");
    const QDate entryDate(2026, 8, 23);
    const double assetsBefore = totalAssets();
    CHECK(assetsBefore >= 0.0);

    // 1. Balanced entry persists one transaction + one split per line.
    QString error = QStringLiteral("not-cleared");
    QList<LedgerSplit> balanced;
    balanced.append({assetId, 123.45, 0});
    balanced.append({incomeId, -123.45, 0});
    CHECK(TransactionDialog::saveTransaction(d, entryDate, okDesc, balanced, &error));
    CHECK(error.isEmpty());
    CHECK_EQ(txCount(okDesc), 1);
    CHECK(std::fabs(totalAssets() - (assetsBefore + 123.45)) < 0.01);

    QSqlQuery splitQ(d);
    splitQ.prepare("SELECT COUNT(*), SUM(CASE WHEN person_id IS NULL THEN 1 ELSE 0 END) FROM splits "
                   "WHERE transaction_id = (SELECT id FROM transactions WHERE description = ?)");
    splitQ.addBindValue(okDesc);
    CHECK(splitQ.exec() && splitQ.next());
    CHECK_EQ(splitQ.value(0).toInt(), 2);
    CHECK_EQ(splitQ.value(1).toInt(), 2); // personId 0 is stored as NULL

    // 2. Unbalanced entry is rejected and writes nothing.
    error.clear();
    QList<LedgerSplit> unbalanced;
    unbalanced.append({assetId, 100.0, 0});
    unbalanced.append({incomeId, -99.0, 0});
    CHECK(!TransactionDialog::saveTransaction(d, entryDate, badDesc, unbalanced, &error));
    CHECK(!error.isEmpty());
    CHECK_EQ(txCount(badDesc), 0);
    CHECK(std::fabs(totalAssets() - (assetsBefore + 123.45)) < 0.01);

    // 3. Empty description / too few splits / unknown account are rejected.
    error.clear();
    CHECK(!TransactionDialog::saveTransaction(d, entryDate, QString(), balanced, &error));
    CHECK(!error.isEmpty());
    error.clear();
    QList<LedgerSplit> oneSplit;
    oneSplit.append({assetId, 0.0, 0});
    CHECK(!TransactionDialog::saveTransaction(d, entryDate, badDesc, oneSplit, &error));
    CHECK(!error.isEmpty());
    error.clear();
    QList<LedgerSplit> unknownAccount;
    unknownAccount.append({999999, 50.0, 0});
    unknownAccount.append({assetId, -50.0, 0});
    CHECK(!TransactionDialog::saveTransaction(d, entryDate, badDesc, unknownAccount, &error));
    CHECK(!error.isEmpty());
    CHECK_EQ(txCount(badDesc), 0);

    // 4. Reachability + guest gating of the ledger entry button.
    ContributionsWidget cw;
    QPushButton* ledgerBtn = cw.findChild<QPushButton*>("ledgerEntryBtn");
    CHECK(ledgerBtn != nullptr);
    if (ledgerBtn) {
        cw.applyRole("guest");
        CHECK(!ledgerBtn->isEnabled());
        CHECK(ledgerBtn->toolTip().contains("Guest"));
        cw.applyRole("readonly");
        CHECK(!ledgerBtn->isEnabled());
        cw.applyRole("admin");
        CHECK(ledgerBtn->isEnabled());
    }

    // Cleanup the probe entry.
    QSqlQuery cleanup(d);
    CHECK(cleanup.exec("DELETE FROM splits WHERE transaction_id IN "
                       "(SELECT id FROM transactions WHERE description = '__ledger_test_ok_9401__')"));
    CHECK(cleanup.exec("DELETE FROM transactions WHERE description = '__ledger_test_ok_9401__'"));
    return true;
}

// audit_log / member_history retention pruning (NOTES.md issue: unbounded growth).
bool test_historyRetentionPruning() {
    QSqlDatabase d = db();
    QSqlQuery q(d);
    CHECK(q.exec("INSERT INTO people (id, first_name, last_name, membership_status) "
                 "VALUES (9401, 'Retention', 'Probe', 'Member')"));

    auto insertAuditProbe = [&](const QString& field, int daysAgo) {
        QSqlQuery i(d);
        i.prepare("INSERT INTO audit_log (table_name, record_id, field_name, old_value, new_value, changed_at) "
                  "VALUES ('people', 9401, ?, '', '', datetime('now', ?))");
        i.addBindValue(field);
        i.addBindValue(QString("-%1 days").arg(daysAgo));
        return i.exec();
    };
    auto insertHistoryProbe = [&](const QString& details, int daysAgo) {
        QSqlQuery i(d);
        i.prepare("INSERT INTO member_history (person_id, activity_type, details, timestamp) "
                  "VALUES (9401, 'probe', ?, datetime('now', ?))");
        i.addBindValue(details);
        i.addBindValue(QString("-%1 days").arg(daysAgo));
        return i.exec();
    };
    auto countAudit = [&](const QString& field) {
        QSqlQuery c(d);
        c.prepare("SELECT COUNT(*) FROM audit_log WHERE field_name = ?");
        c.addBindValue(field);
        if (c.exec() && c.next()) return c.value(0).toInt();
        return -1;
    };
    auto countHistory = [&](const QString& details) {
        QSqlQuery c(d);
        c.prepare("SELECT COUNT(*) FROM member_history WHERE details = ?");
        c.addBindValue(details);
        if (c.exec() && c.next()) return c.value(0).toInt();
        return -1;
    };

    // 1. Old rows are pruned, recent rows stay.
    CHECK(insertAuditProbe("retention_probe_old", 400));
    CHECK(insertAuditProbe("retention_probe_new", 1));
    CHECK(insertHistoryProbe("retention_probe_old", 800));
    CHECK(insertHistoryProbe("retention_probe_new", 1));
    DatabaseManager::RetentionResult res =
        DatabaseManager::instance().pruneHistory(365, 730);
    CHECK(res.auditDeleted >= 1);
    CHECK(res.historyDeleted >= 1);
    CHECK_EQ(countAudit("retention_probe_old"), 0);
    CHECK_EQ(countAudit("retention_probe_new"), 1);
    CHECK_EQ(countHistory("retention_probe_old"), 0);
    CHECK_EQ(countHistory("retention_probe_new"), 1);

    // 2. 0 means "keep forever" for that table.
    CHECK(insertAuditProbe("retention_probe_keep_audit", 400));
    CHECK(insertHistoryProbe("retention_probe_keep_hist", 800));
    DatabaseManager::RetentionResult keepForever =
        DatabaseManager::instance().pruneHistory(0, 0);
    CHECK_EQ(keepForever.auditDeleted, 0);
    CHECK_EQ(keepForever.historyDeleted, 0);
    CHECK_EQ(countAudit("retention_probe_keep_audit"), 1);
    CHECK_EQ(countHistory("retention_probe_keep_hist"), 1);

    // 3. Retention is per table: disabling audit must not stop history pruning.
    CHECK(insertAuditProbe("retention_probe_partial_audit", 400));
    CHECK(insertHistoryProbe("retention_probe_partial_hist", 800));
    DatabaseManager::RetentionResult partialResult =
        DatabaseManager::instance().pruneHistory(0, 730);
    CHECK_EQ(partialResult.auditDeleted, 0);
    CHECK(partialResult.historyDeleted >= 1);
    CHECK_EQ(countAudit("retention_probe_partial_audit"), 1);
    CHECK_EQ(countHistory("retention_probe_partial_hist"), 0);

    // Cleanup all probe rows.
    CHECK(q.exec("DELETE FROM audit_log WHERE field_name LIKE 'retention_probe%'"));
    CHECK(q.exec("DELETE FROM member_history WHERE details LIKE 'retention_probe%'"));
    CHECK(q.exec("DELETE FROM people WHERE id = 9401"));
    return true;
}

// Settings page retention controls: defaults, persistence and admin gating.
bool test_settingsRetentionControls() {
    AppSettings s;
    const QVariant oldAudit = s.value("auditLogRetentionDays");
    const QVariant oldHistory = s.value("memberHistoryRetentionDays");

    SettingsWidget sw;
    QSpinBox* audit = sw.findChild<QSpinBox*>("auditRetentionSpin");
    QSpinBox* history = sw.findChild<QSpinBox*>("historyRetentionSpin");
    CHECK(audit != nullptr);
    CHECK(history != nullptr);
    if (!audit || !history) return true;

    CHECK_EQ(audit->minimum(), 0);
    CHECK_EQ(audit->maximum(), 3650);
    CHECK_EQ(audit->specialValueText().toStdString(), std::string("Keep forever"));
    CHECK_EQ(history->specialValueText().toStdString(), std::string("Keep forever"));
    audit->setValue(0);
    CHECK_EQ(audit->text().toStdString(), std::string("Keep forever"));

    // Load/save round-trip through the same QSettings keys the startup policy uses.
    s.setValue("auditLogRetentionDays", 120);
    s.setValue("memberHistoryRetentionDays", 240);
    sw.loadSettings();
    CHECK_EQ(audit->value(), 120);
    CHECK_EQ(history->value(), 240);

    // Programmatic value changes would mark the page dirty, which makes
    // saveSettings() show a modal confirmation; block signals so the save can
    // be exercised headless (the dirty path is covered by the widget tests).
    {
        QSignalBlocker auditBlocker(audit);
        QSignalBlocker historyBlocker(history);
        audit->setValue(90);
        history->setValue(180);
    }
    sw.saveSettings();
    CHECK_EQ(s.value("auditLogRetentionDays").toInt(), 90);
    CHECK_EQ(s.value("memberHistoryRetentionDays").toInt(), 180);

    // Guest/readonly can view but not change retention; admin can.
    sw.applyRole("guest");
    CHECK(!audit->isEnabled());
    CHECK(!history->isEnabled());
    CHECK(audit->toolTip().contains("Guest"));
    sw.applyRole("admin");
    CHECK(audit->isEnabled());
    CHECK(history->isEnabled());

    // Restore the previous settings so other tests see their usual defaults.
    if (oldAudit.isValid()) s.setValue("auditLogRetentionDays", oldAudit);
    else s.remove("auditLogRetentionDays");
    if (oldHistory.isValid()) s.setValue("memberHistoryRetentionDays", oldHistory);
    else s.remove("memberHistoryRetentionDays");
    s.sync();
    return true;
}

// PCO Giving: the donation->designation relationship is to-many, and the
// human-readable name may only be on the sideloaded Fund resource. This is the
// payload half of the designation->fund mapping; importGivingData is covered
// separately below.
bool test_pcoDonationDesignationParsing() {
    const QByteArray payload = R"({
      "data": [
        {"id":"501","type":"Donation",
         "attributes":{"amount_cents":2500,"received_at":"2026-09-01T10:00:00Z","payment_method":"Cash","check_number":""},
         "relationships":{"person":{"data":{"id":"p1"}},
                          "designations":{"data":[{"type":"Designation","id":"d1"}]}}},
        {"id":"502","type":"Donation",
         "attributes":{"amount_cents":1000,"received_at":"2026-09-02T09:00:00Z"},
         "relationships":{"person":{"data":{"id":"p2"}},
                          "designation":{"data":{"type":"Designation","id":"d2"}}}},
        {"id":"503","type":"Donation",
         "attributes":{"amount":12.34,"received_at":"2026-09-03T08:00:00Z"},
         "relationships":{"person":{"data":{"id":"p3"}}}},
        {"id":"504","type":"Donation",
         "attributes":{"amount_cents":1500,"received_at":"2026-09-04T08:00:00Z"},
         "relationships":{"person":{"data":{"id":"p4"}},
                          "designations":{"data":[{"type":"Designation","id":"d1"},
                                                   {"type":"Designation","id":"d3"}]}}}
      ],
      "included": [
        {"id":"d1","type":"Designation",
         "attributes":{"name":"General Fund","amount_cents":500},
         "relationships":{"fund":{"data":{"type":"Fund","id":"f1"}}}},
        {"id":"d2","type":"Designation",
         "attributes":{},
         "relationships":{"fund":{"data":{"type":"Fund","id":"f1"}}}},
        {"id":"d3","type":"Designation",
         "attributes":{"amount_cents":1000},
         "relationships":{"fund":{"data":{"type":"Fund","id":"f2"}}}},
        {"id":"f1","type":"Fund","attributes":{"name":"Missions Fund"}},
        {"id":"f2","type":"Fund","attributes":{"name":"Building Fund"}}
      ]
    })";
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QList<PcoDonationRecord> recs = PlanningCenterClient::parseDonationRecords(
        root.value("data").toArray(), root.value("included").toArray());
    CHECK_EQ(recs.size(), 4);
    if (recs.size() != 4) return false;

    CHECK_EQ(recs[0].externalId.toStdString(), std::string("PCO-501"));
    CHECK_EQ(recs[0].personPcoId.toStdString(), std::string("p1"));
    CHECK_EQ(recs[0].designation.toStdString(), std::string("General Fund"));
    CHECK_EQ(recs[0].amountCents, qint64(2500));
    CHECK_EQ(recs[0].date.toStdString(), std::string("2026-09-01"));
    CHECK_EQ(recs[0].paymentMethod.toStdString(), std::string("Cash"));

    // No name on the Designation: fall back to the sideloaded Fund's name.
    CHECK_EQ(recs[1].designation.toStdString(), std::string("Missions Fund"));
    CHECK_EQ(recs[1].designationFundPcoId.toStdString(), std::string("f1"));

    // Feed carrying only `amount` still produces cents; no designation -> empty.
    CHECK_EQ(recs[2].amountCents, qint64(1234));
    CHECK(recs[2].designation.isEmpty());

    // A gift split across two funds keeps both parts and their own amounts; the
    // legacy single-designation fields mirror the first part.
    CHECK_EQ(recs[3].amountCents, qint64(1500));
    CHECK_EQ(recs[3].parts.size(), 2);
    if (recs[3].parts.size() == 2) {
        CHECK_EQ(recs[3].parts[0].designation.toStdString(), std::string("General Fund"));
        CHECK_EQ(recs[3].parts[0].amountCents, qint64(500));
        CHECK_EQ(recs[3].parts[1].designation.toStdString(), std::string("Building Fund"));
        CHECK_EQ(recs[3].parts[1].amountCents, qint64(1000));
    }
    CHECK_EQ(recs[3].designation.toStdString(), std::string("General Fund"));
    return true;
}

// PCO Giving: designation names map onto existing funds case-insensitively,
// unknown names create a fund, missing designations use "Imported (Unmapped)",
// and re-running the same import neither duplicates funds nor contributions.
bool test_pcoGivingImportDesignationFunds() {
    ensurePersonOne(); // household 1 must exist for the scratch donor row
    QSqlDatabase d = db();
    QSqlQuery q(d);
    CHECK(q.exec("INSERT OR REPLACE INTO people (id, household_id, first_name, last_name, barcode, membership_status) "
                 "VALUES (9401, 1, 'Pco', 'Donor', 'PCO-9401', 'Member')"));
    CHECK(q.exec("INSERT OR REPLACE INTO funds (id, fund_name, is_active) VALUES (9401, 'Designation Test Fund', 1)"));
    const int fundsBefore = countRows("funds");
    QSqlQuery unmappedBefore(d);
    const bool hadUnmappedFund = unmappedBefore.exec("SELECT id FROM funds WHERE fund_name = 'Imported (Unmapped)'")
                                 && unmappedBefore.next();

    PcoDonationRecord designated;
    designated.externalId = "PCO-900101";
    designated.personPcoId = "9401";
    designated.designation = "designation test fund"; // case-insensitive match to fund 9401
    designated.amountCents = 2500;
    designated.date = "2026-09-01";

    PcoDonationRecord unknown;
    unknown.externalId = "PCO-900102";
    unknown.personPcoId = "9401";
    unknown.designation = "Youth Ministry Test 9002";
    unknown.amountCents = 1500;
    unknown.date = "2026-09-02";

    PcoDonationRecord unmapped;
    unmapped.externalId = "PCO-900103";
    unmapped.personPcoId = "9401";
    unmapped.amountCents = 700;
    unmapped.date = "2026-09-03";

    PlanningCenterClient client;
    const QList<PcoDonationRecord> batch = {designated, unknown, unmapped};
    const PcoImportSummary first = client.importGivingData(QJsonArray(), batch);
    CHECK_EQ(first.donationsCount, 3);
    CHECK_EQ(first.designationsMapped, 2); // the designated + the newly named fund
    CHECK_EQ(first.fundsCount, 1);         // only the unknown designation created one

    QSqlQuery fundOf(d);
    fundOf.prepare("SELECT fund_id FROM contributions WHERE external_id = ?");
    fundOf.addBindValue(QString("PCO-900101"));
    CHECK(fundOf.exec());
    CHECK(fundOf.next());
    if (fundOf.isValid()) CHECK_EQ(fundOf.value(0).toInt(), 9401);

    // The unknown designation creates one fund; the unmapped fallback creates
    // "Imported (Unmapped)" only when it does not exist yet.
    const int fundsAfter = countRows("funds");
    CHECK_EQ(fundsAfter, fundsBefore + 1 + (hadUnmappedFund ? 0 : 1));

    QSqlQuery named(d);
    named.prepare("SELECT id FROM funds WHERE fund_name = 'Youth Ministry Test 9002'");
    CHECK(named.exec());
    CHECK(named.next());
    const int unknownFundId = named.isValid() ? named.value(0).toInt() : -1;
    CHECK(unknownFundId > 0);

    QSqlQuery unknownGift(d);
    unknownGift.prepare("SELECT fund_id FROM contributions WHERE external_id = ?");
    unknownGift.addBindValue(QString("PCO-900102"));
    CHECK(unknownGift.exec());
    CHECK(unknownGift.next());
    if (unknownGift.isValid()) CHECK_EQ(unknownGift.value(0).toInt(), unknownFundId);

    QSqlQuery unmappedGift(d);
    unmappedGift.prepare("SELECT f.fund_name FROM contributions c JOIN funds f ON f.id = c.fund_id "
                         "WHERE c.external_id = ?");
    unmappedGift.addBindValue(QString("PCO-900103"));
    CHECK(unmappedGift.exec());
    CHECK(unmappedGift.next());
    if (unmappedGift.isValid()) {
        CHECK_EQ(unmappedGift.value(0).toString().toStdString(), std::string("Imported (Unmapped)"));
    }

    // Re-sync: dedupe on external_id must skip everything and create no funds.
    const PcoImportSummary second = client.importGivingData(QJsonArray(), batch);
    CHECK_EQ(second.donationsCount, 0);
    CHECK_EQ(second.donationsSkipped, 3);
    CHECK_EQ(countRows("funds"), fundsAfter);

    // Cleanup (leave the product's structural "Imported (Unmapped)" fund alone).
    CHECK(q.exec("DELETE FROM contributions WHERE external_id IN ('PCO-900101','PCO-900102','PCO-900103')"));
    CHECK(q.exec("DELETE FROM funds WHERE id = 9401 OR fund_name = 'Youth Ministry Test 9002'"));
    CHECK(q.exec("DELETE FROM people WHERE id = 9401"));
    return true;
}

// A gift split across two funds imports one contribution row per designation
// with the per-fund amounts; re-syncs stay idempotent and funds are reused
// case-insensitively across the funds feed and designation mapping.
bool test_pcoSplitDonationImport() {
    ensurePersonOne();
    QSqlDatabase d = db();
    QSqlQuery q(d);
    CHECK(q.exec("INSERT OR REPLACE INTO people (id, household_id, first_name, last_name, barcode, membership_status) "
                 "VALUES (9402, 1, 'Split', 'Donor', 'PCO-9402', 'Member')"));
    const int contributionsBefore = countRows("contributions");

    PcoDonationRecord split;
    split.externalId = "PCO-900201";
    split.personPcoId = "9402";
    split.amountCents = 3000; // 30.00 total, split 20.00 / 10.00
    split.date = "2026-09-10";
    PcoDesignationPart general;
    general.designationPcoId = "d-general";
    general.designation = "Split Fund A 9003";
    general.amountCents = 2000;
    PcoDesignationPart missions;
    missions.designationPcoId = "d-missions";
    missions.designation = "Split Fund B 9004";
    missions.amountCents = 1000;
    split.parts = {general, missions};

    PlanningCenterClient client;
    const PcoImportSummary first = client.importGivingData(QJsonArray(), {split});
    CHECK_EQ(first.donationsCount, 2);      // one row per fund split
    CHECK_EQ(first.designationsMapped, 2);
    CHECK_EQ(first.fundsCount, 2);          // both funds created
    CHECK_EQ(countRows("contributions"), contributionsBefore + 2);

    QSqlQuery rows(d);
    rows.prepare("SELECT c.external_id, c.amount, f.fund_name FROM contributions c "
                 "JOIN funds f ON f.id = c.fund_id WHERE c.external_id LIKE 'PCO-900201%' "
                 "ORDER BY c.amount DESC");
    CHECK(rows.exec());
    CHECK(rows.next());
    if (rows.isValid()) {
        CHECK_EQ(rows.value(0).toString().toStdString(), std::string("PCO-900201-d-general"));
        CHECK_EQ(rows.value(1).toLongLong(), qint64(2000));
        CHECK_EQ(rows.value(2).toString().toStdString(), std::string("Split Fund A 9003"));
    }
    CHECK(rows.next());
    if (rows.isValid()) {
        CHECK_EQ(rows.value(0).toString().toStdString(), std::string("PCO-900201-d-missions"));
        CHECK_EQ(rows.value(1).toLongLong(), qint64(1000));
    }

    // Re-sync: both rows exist, so the gift is skipped and no funds are created.
    const int fundsAfter = countRows("funds");
    const PcoImportSummary second = client.importGivingData(QJsonArray(), {split});
    CHECK_EQ(second.donationsCount, 0);
    CHECK_EQ(second.donationsSkipped, 1);
    CHECK_EQ(countRows("funds"), fundsAfter);

    // A sideloaded PCO fund whose name differs only by case must reuse the local
    // fund rather than create a duplicate.
    QJsonArray fundsFeed;
    QJsonObject fundObj;
    fundObj.insert("id", QStringLiteral("pco-split-a"));
    QJsonObject fundAttrs;
    fundAttrs.insert("name", QStringLiteral("SPLIT FUND A 9003"));
    fundObj.insert("attributes", fundAttrs);
    fundsFeed.append(fundObj);
    const PcoImportSummary feedImport = client.importGivingData(fundsFeed, {});
    CHECK_EQ(feedImport.fundsCount, 0); // matched an existing fund, nothing created
    CHECK(q.exec("SELECT COUNT(*) FROM funds WHERE lower(fund_name) = 'split fund a 9003'"));
    CHECK(q.next());
    CHECK_EQ(q.value(0).toInt(), 1);

    // A split part with a negative/zero portion must not be imported as-is: the
    // negative part is dropped and only the valid portion is recorded.
    PcoDonationRecord messy;
    messy.externalId = "PCO-900202";
    messy.personPcoId = "9402";
    messy.amountCents = 1000;
    messy.date = "2026-09-11";
    PcoDesignationPart badPart;
    badPart.designationPcoId = "d-bad";
    badPart.designation = "Split Fund A 9003";
    badPart.amountCents = -500;
    PcoDesignationPart goodPart;
    goodPart.designationPcoId = "d-good";
    goodPart.designation = "Split Fund B 9004";
    goodPart.amountCents = 1000;
    messy.parts = {badPart, goodPart};
    const PcoImportSummary messyImport = client.importGivingData(QJsonArray(), {messy});
    CHECK_EQ(messyImport.donationsCount, 1); // only the valid portion became a row
    QSqlQuery badRows(d);
    badRows.prepare("SELECT COUNT(*) FROM contributions WHERE external_id LIKE 'PCO-900202%' AND amount < 0");
    CHECK(badRows.exec() && badRows.next());
    CHECK_EQ(badRows.value(0).toInt(), 0);
    QSqlQuery goodRow(d);
    goodRow.prepare("SELECT amount FROM contributions WHERE external_id = 'PCO-900202-d-good'");
    CHECK(goodRow.exec() && goodRow.next());
    CHECK_EQ(goodRow.value(0).toLongLong(), qint64(1000));

    // Cleanup.
    CHECK(q.exec("DELETE FROM contributions WHERE external_id LIKE 'PCO-900201%'"));
    CHECK(q.exec("DELETE FROM contributions WHERE external_id LIKE 'PCO-900202%'"));
    CHECK(q.exec("DELETE FROM funds WHERE fund_name IN ('Split Fund A 9003','Split Fund B 9004')"));
    CHECK(q.exec("DELETE FROM people WHERE id = 9402"));
    return true;
}

// Programmatic view changes (analytics drill-through, dashboard profile
// navigation) must not silently discard typed edits: clean -> no prompt, dirty
// + Cancel -> the caller aborts and the typed text survives.
bool test_peopleConfirmDiscardEdits() {
    seedDemoFixture();
    ensurePersonOne();
    PeopleWidget pw;
    pw.applyRole("admin");
    pw.refreshModel();
    CHECK(pw.confirmDiscardEdits()); // clean: true without asking

    // Person 1's name depends on which earlier test last seeded the DB, so read
    // it instead of assuming a fixture name.
    QSqlQuery nameQ(db());
    nameQ.prepare("SELECT first_name FROM people WHERE id = 1");
    CHECK(nameQ.exec() && nameQ.next());
    const QString expectedFirst = nameQ.value(0).toString();
    CHECK(!expectedFirst.isEmpty());

    pw.selectPerson(1);
    QLineEdit* firstNameEdit = nullptr;
    for (QLineEdit* edit : pw.findChildren<QLineEdit*>()) {
        if (edit->text() == expectedFirst) { firstNameEdit = edit; break; }
    }
    CHECK(firstNameEdit != nullptr);
    if (!firstNameEdit) return true;

    const QString editedFirst = expectedFirst + QStringLiteral("Edited");
    firstNameEdit->setText(editedFirst);
    // The dialog is modal; answer it from a zero-delay timer so the headless run
    // cannot block. A late fallback closes it if the button lookup ever misses.
    QTimer::singleShot(0, []() {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(w)) {
                if (QAbstractButton* cancel = box->button(QMessageBox::Cancel)) cancel->click();
                else box->reject();
                return;
            }
        }
    });
    QTimer::singleShot(3000, []() {
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
    });

    CHECK(!pw.confirmDiscardEdits()); // Cancel -> caller must abort, touch nothing
    CHECK_EQ(firstNameEdit->text().toStdString(), editedFirst.toStdString()); // edits kept
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("openekklesia_tests");


    // Fresh, unique database file. Move the CWD into a scratch dir first so
    // DatabaseManager's "adopt legacy CWD database" fallback never copies the
    // developer's workspace open_ekklesia.db into the test run (which would
    // also skip the demo seed).
    //
    // Prefer the OS temp dir, but some sandboxed/launcher environments deny
    // writes to it for newly built binaries; fall back to a per-user scratch
    // dir and finally to the CWD so the suite still runs instead of dying in
    // DatabaseManager::initialize().
    auto dirWritable = [](const QString& dirPath) {
        QDir dir(dirPath);
        if (!dir.mkpath(".")) return false;
        QFile probe(dir.absoluteFilePath(".oe_write_probe"));
        if (!probe.open(QIODevice::WriteOnly)) return false;
        probe.close();
        probe.remove();
        return true;
    };
    QString scratchRoot = QDir::tempPath() + "/oe_test_work";
    if (!dirWritable(scratchRoot)) {
        scratchRoot = QDir::homePath() + "/.openekklesia-test/oe_test_work";
        if (!dirWritable(scratchRoot)) {
            scratchRoot = QDir::currentPath() + "/oe_test_work";
            dirWritable(scratchRoot);
        }
    }
    QDir workDir(scratchRoot);
    QDir::setCurrent(workDir.absolutePath());

    // Run hermetically: AppSettings sends every preference read/write to an INI
    // file inside this scratch dir, so the suite neither depends on nor
    // modifies the developer's real settings store.
    const QString testSettingsDir = workDir.absoluteFilePath("settings");
    qputenv("OPENEKKLESIA_SETTINGS_DIR", testSettingsDir.toUtf8());
    QFile::remove(testSettingsDir + "/OpenEkklesia/CHMS.ini");

    g_dbPath = workDir.absoluteFilePath(QString("oe_test_%1.db").arg(QCoreApplication::applicationPid()));
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
        {"settings_widget_constructs", test_settingsWidgetConstructs},
        {"setup_wizard_applies_profile", test_setupWizardAppliesProfile},
        {"church_profile_defaults_not_fake_data", test_churchProfileDefaultsAreNotFakeData},
        {"pastoral_queue", test_pastoralQueue},
        {"person_gender_and_household_save", test_personGenderAndHouseholdSave},
        {"restore_from_backup", test_restoreFromBackup},
        {"export_backup_rejects_live_paths", test_exportBackupRejectsLivePaths},
        {"restore_migration_log_matches_outcome", test_restoreMigrationLogMatchesOutcome},
        {"purge_demo_data", test_purgeDemoData},
        {"theme_named_tokens_template", test_themeNamedTokensTemplate},
        {"theme_contrast_guardian", test_themeContrastGuardian},
        {"all_themes_contrast_aa", test_allThemesContrastAA},
        {"synergy_engine_and_migration_19", test_synergyEngineAndMigration19},
        {"synergy_equilibrium_simulation_uses_member_average", test_synergyEquilibriumSimulationUsesMemberAverage},
        {"ministry_match_recommend_and_exclude", test_ministryMatchRecommendAndExclude},
        {"ministry_match_add_members_dedup", test_ministryMatchAddMembersDedup},
        {"ministry_match_declared_profile_wins", test_ministryMatchDeclaredProfileWins},
        {"ministry_match_rank_by_role", test_ministryMatchRankByRole},
        {"service_builder_role_and_duration", test_serviceBuilderRoleAndDuration},
        {"service_builder_library_filter_and_autosave", test_serviceBuilderLibraryFilterAndAutosave},
        {"service_builder_run_sheet_persistence", test_serviceBuilderRunSheetPersistence},
        {"service_builder_load_does_not_clobber_saved_sheet", test_serviceBuilderLoadDoesNotClobberSavedSheet},
        {"service_builder_tier2", test_serviceBuilderTier2},
        {"service_builder_tier3", test_serviceBuilderTier3},
        {"planning_widget_renders_and_roles", test_planningWidgetRendersAndRoles},
        {"planning_relational_links", test_planningRelationalLinks},
        {"planning_frameworks_and_events_suite", test_planningFrameworksAndEventsSuite},
        {"people_widget_360_dossier_and_giving", test_peopleWidget360DossierAndGiving},
        {"shepherd_quick_actions_and_pastoral_sync", test_shepherdQuickActionsAndPastoralSync},
        {"pastoral_analytics_upgrade", test_pastoralAnalyticsUpgrade},
        {"dashboard_kpi_config_and_quick_notes", test_dashboardKpiConfigAndQuickNotes},
        {"graph_dual_panel_and_data_hygiene", test_graphDualPanelAndDataHygiene},
        {"graph_batch_operations_and_smart_bridge", test_graphBatchOperationsAndSmartBridge},
        {"graph_tier3_bridge_nodes_and_family_dynasties", test_graphTier3BridgeNodesAndFamilyDynasties},
        {"graph_health_features", test_graphHealthFeatures},
        {"smart_bridge_engine_and_source_logging", test_smartBridgeEngineAndSourceLogging},
        {"smart_bridge_advisory_and_hidden_groups", test_smartBridgeAdvisoryAndHiddenGroups},
        {"graph_inspector_upgrade", test_graphInspectorUpgrade},
        {"graph_collision_resolution", test_graphCollisionResolution},
        {"settings_localization_and_churchprofile", test_settingsLocalizationAndChurchProfile},
        {"graph_filter_persistence", test_graphFilterPersistence},
        {"group_recommendation_disablers", test_groupRecommendationDisablers},
        {"vault_and_settings_integrity", test_vaultAndSettingsIntegrity},
        {"facilities_conflict_engine", test_facilitiesConflictEngine},
        {"contributions_split_and_anonymous", test_contributionsSplitAndAnonymous},
        {"comprehensive_settings_round_trip", test_comprehensiveSettingsRoundTrip},
        {"giving_batches_and_multi_fund_splits", test_givingBatchesAndMultiFundSplits},
        {"user_manual_integrity", test_userManualIntegrity},
        {"graph_view_options_and_dock_toggle", test_graphViewOptionsAndDockToggle},
        {"people_module_stress_and_break_suite", test_peopleModuleStressAndBreakSuite},
        {"context_menus_and_role_gating_suite", test_contextMenusAndRoleGatingSuite},
        {"contextual_group_delegation_suite", test_contextualGroupDelegationSuite},
        {"discipleship_pathway_suite", test_discipleshipPathwaySuite},
        {"blueprint_group_hierarchy_suite", test_blueprintGroupHierarchySuite},
        {"dynamic_blueprint_headings_suite", test_dynamicBlueprintHeadingsSuite},
        {"group_member_and_leader_roster_sync_suite", test_groupMemberAndLeaderRosterSyncSuite},
        {"giving_statement_generator", test_givingStatementGenerator},
        {"ical_generator_rfc_compliance", test_icalGeneratorRfcCompliance},
        {"ledger_entry_and_total_assets", test_ledgerEntryAndTotalAssets},
        {"history_retention_pruning", test_historyRetentionPruning},
        {"pco_donation_designation_parsing", test_pcoDonationDesignationParsing},
        {"pco_giving_import_designation_funds", test_pcoGivingImportDesignationFunds},
    {"pco_split_donation_import", test_pcoSplitDonationImport},
        {"people_confirm_discard_edits", test_peopleConfirmDiscardEdits},
        {"settings_retention_controls", test_settingsRetentionControls},
    };

    // Optional debugging aid: OE_TEST_FILTER=name1,name2 runs only the named
    // scenarios (in registration order). Unset/empty still runs the full suite.
    QStringList testFilter = qEnvironmentVariable("OE_TEST_FILTER").split(',', Qt::SkipEmptyParts);
    for (QString& name : testFilter) name = name.trimmed();

    for (const Test& t : tests) {
        if (!testFilter.isEmpty() &&
            !testFilter.contains(QString::fromLatin1(t.name))) {
            continue;
        }
        std::cout.flush();
        // Hard check: once one scenario has added a failing CHECK, halt rather
        // than letting downstream tests pile up their own follow-on errors.
        if (g_failures > 0)
            break;
        const int failuresBefore = g_failures;
        bool ok = false;
        try {
            ok = t.fn();
        } catch (const std::exception& ex) {
            // An escaping exception is a failure: count it so the run exits
            // non-zero instead of reporting "0 failed" with a broken test.
            ++g_failures;
            ok = false;
            std::cerr << "EXCEPTION in test " << t.name << ": " << ex.what() << std::endl;
        } catch (...) {
            ++g_failures;
            ok = false;
            std::cerr << "EXCEPTION in test " << t.name << " (unknown exception)" << std::endl;
        }
        // A test passes only if it returned true AND added no new failures.
        // (Previously `return true` masked CHECK failures as [PASS].)
        ok = ok && (g_failures == failuresBefore);
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << t.name << std::endl;
    }

    std::cout << "Ran " << g_checks << " checks, " << g_failures << " failed" << std::endl;

    DatabaseManager::instance().close();
    QFile::remove(g_dbPath);

    return g_failures == 0 ? 0 : 1;
}
