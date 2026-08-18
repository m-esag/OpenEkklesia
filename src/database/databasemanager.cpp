#include "databasemanager.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::initialize(const QString& dbPath) {
    // Resolve a stable, user-writable database location. We deliberately do
    // NOT keep the database next to the binary: that path is frequently
    // read-only on installed systems (/usr/bin, /app for Flatpak, the AppImage
    // mount point) and would either fail to open or silently fragment data.
    QString resolvedPath = dbPath;
    if (resolvedPath.isEmpty()) {
        const QString camelCasePath = QDir::homePath() + "/.local/share/OpenEkklesia/open_ekklesia.db";
        const QString lowerCasePath = QDir::homePath() + "/.local/share/openekklesia/open_ekklesia.db";
        const QString cwdPath = QFileInfo("open_ekklesia.db").absoluteFilePath();

        if (QFile::exists(camelCasePath)) {
            resolvedPath = camelCasePath;
        } else if (QFile::exists(lowerCasePath)) {
            resolvedPath = lowerCasePath;
        } else if (QFile::exists(cwdPath)) {
            resolvedPath = cwdPath;
        } else {
            QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (dataDir.isEmpty()) {
                dataDir = QDir::homePath() + "/.local/share/OpenEkklesia";
            }
            QDir().mkpath(dataDir);
            resolvedPath = dataDir + "/open_ekklesia.db";
        }
    }

    bool dbExists = QFile::exists(resolvedPath);

    // Legacy fallback: v1.5.0 stored the DB as "open_ekklesia.db" in the CWD.
    // If the canonical path does not exist but a sibling-of-binary copy does,
    // adopt it so existing single-user installs keep their data on first run.
    if (!dbExists) {
        const QString legacyCwd = QFileInfo("open_ekklesia.db").absoluteFilePath();
        if (QFile::exists(legacyCwd)) {
            qInfo() << "Adopting legacy database from" << legacyCwd
                    << "->" << resolvedPath;
            if (QFile::copy(legacyCwd, resolvedPath)) {
                dbExists = true;
            } else {
                qWarning() << "Could not copy legacy DB; starting fresh at" << resolvedPath;
            }
        }
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(resolvedPath);

    if (!m_db.open()) {
        qCritical() << "Error: Connection with database failed:" << m_db.lastError().text();
        return false;
    }

    // --- Automated Weekly Database Backup Snapshot ---
    QString backupDir = QFileInfo(resolvedPath).absolutePath() + "/backups";
    QDir().mkpath(backupDir);
    QString lastBackupDateFile = backupDir + "/last_backup.txt";
    QDate lastBackupDate;
    if (QFile::exists(lastBackupDateFile)) {
        QFile f(lastBackupDateFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            lastBackupDate = QDate::fromString(QString::fromUtf8(f.readAll()).trimmed(), "yyyy-MM-dd");
            f.close();
        }
    }
    if (!lastBackupDate.isValid() || lastBackupDate.daysTo(QDate::currentDate()) >= 7) {
        QString autoBackupPath = QString("%1/auto_backup_%2.db").arg(backupDir, QDate::currentDate().toString("yyyyMMdd"));
        if (QFile::copy(resolvedPath, autoBackupPath)) {
            qInfo() << "Automated weekly database backup created at:" << autoBackupPath;
            QFile f(lastBackupDateFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(QDate::currentDate().toString("yyyy-MM-dd").toUtf8());
                f.close();
            }
        }
    }

    // Enforce foreign keys, WAL mode, memory caching, and memory-mapped IO for ultra-fast queries & low CPU usage.
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA foreign_keys = ON;");
    pragma.exec("PRAGMA journal_mode = WAL;");
    pragma.exec("PRAGMA synchronous = NORMAL;");
    pragma.exec("PRAGMA cache_size = -8000;"); // 8 MB memory cache
    pragma.exec("PRAGMA temp_store = MEMORY;");
    pragma.exec("PRAGMA mmap_size = 33554432;"); // 32 MB memory-mapped zero-copy IO
    pragma.exec("PRAGMA auto_vacuum = INCREMENTAL;");

    qInfo() << "Database opened successfully at:" << resolvedPath;

    if (!createTables()) {
        return false;
    }
    if (!ensureSchemaVersionTable()) {
        return false;
    }
    if (!migrateSchema()) {
        return false;
    }

    if (!dbExists) {
        seedInitialData();
    }

    return true;
}

void DatabaseManager::close() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

QSqlDatabase DatabaseManager::database() const {
    return QSqlDatabase::database(m_db.connectionName());
}

bool DatabaseManager::createTables() {
    QSqlQuery query;

    // Create households table
    QString createHouseholds = 
        "CREATE TABLE IF NOT EXISTS households ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  family_name TEXT NOT NULL,"
        "  address_line1 TEXT,"
        "  city TEXT,"
        "  zip_code TEXT"
        ");";
    if (!query.exec(createHouseholds)) {
        qCritical() << "Failed to create households table:" << query.lastError().text();
        return false;
    }

    // Create people table
    QString createPeople = 
        "CREATE TABLE IF NOT EXISTS people ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  household_id INTEGER,"
        "  first_name TEXT NOT NULL,"
        "  last_name TEXT NOT NULL,"
        "  phone_number TEXT,"
        "  email TEXT,"
        "  date_of_birth TEXT,"
        "  avatar_path TEXT,"
        "  barcode TEXT UNIQUE,"
        "  care_flag TEXT,"
        "  admin_notes TEXT,"
        "  membership_status TEXT NOT NULL DEFAULT 'Member',"
        "  first_visit_date TEXT,"
        "  join_date TEXT,"
        "  gender TEXT DEFAULT 'Unspecified',"
        "  FOREIGN KEY(household_id) REFERENCES households(id)"
        ");";
    if (!query.exec(createPeople)) {
        qCritical() << "Failed to create people table:" << query.lastError().text();
        return false;
    }

    // Create groups table
    QString createGroups = 
        "CREATE TABLE IF NOT EXISTS groups ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  category TEXT,"
        "  parent_id INTEGER REFERENCES groups(id),"
        "  is_archived INTEGER DEFAULT 0,"
        "  meeting_day TEXT,"
        "  meeting_time TEXT,"
        "  location TEXT,"
        "  contact_person_id INTEGER REFERENCES people(id),"
        "  max_capacity INTEGER DEFAULT 0,"
        "  notes TEXT"
        ");";
    if (!query.exec(createGroups)) {
        qCritical() << "Failed to create groups table:" << query.lastError().text();
        return false;
    }

    // Create accounts table
    QString createAccounts = 
        "CREATE TABLE IF NOT EXISTS accounts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  type TEXT NOT NULL,"
        "  parent_id INTEGER,"
        "  FOREIGN KEY(parent_id) REFERENCES accounts(id)"
        ");";
    if (!query.exec(createAccounts)) {
        qCritical() << "Failed to create accounts table:" << query.lastError().text();
        return false;
    }

    // Create transactions table
    QString createTransactions = 
        "CREATE TABLE IF NOT EXISTS transactions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  date TEXT NOT NULL,"
        "  description TEXT"
        ");";
    if (!query.exec(createTransactions)) {
        qCritical() << "Failed to create transactions table:" << query.lastError().text();
        return false;
    }

    // Create splits table
    QString createSplits = 
        "CREATE TABLE IF NOT EXISTS splits ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  transaction_id INTEGER NOT NULL,"
        "  account_id INTEGER NOT NULL,"
        "  amount REAL NOT NULL,"
        "  person_id INTEGER,"
        "  FOREIGN KEY(transaction_id) REFERENCES transactions(id),"
        "  FOREIGN KEY(account_id) REFERENCES accounts(id),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createSplits)) {
        qCritical() << "Failed to create splits table:" << query.lastError().text();
        return false;
    }

    // Create funds table
    QString createFunds = 
        "CREATE TABLE IF NOT EXISTS funds ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  fund_name TEXT UNIQUE NOT NULL,"
        "  is_active INTEGER DEFAULT 1"
        ");";
    if (!query.exec(createFunds)) {
        qCritical() << "Failed to create funds table:" << query.lastError().text();
        return false;
    }

    // Create contributions table
    QString createContributions = 
        "CREATE TABLE IF NOT EXISTS contributions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  person_id INTEGER NOT NULL,"
        "  fund_id INTEGER NOT NULL,"
        "  amount INTEGER NOT NULL,"
        "  contribution_date TEXT NOT NULL,"
        "  payment_method TEXT NOT NULL,"
        "  check_number TEXT,"
        "  FOREIGN KEY(person_id) REFERENCES people(id),"
        "  FOREIGN KEY(fund_id) REFERENCES funds(id)"
        ");";
    if (!query.exec(createContributions)) {
        qCritical() << "Failed to create contributions table:" << query.lastError().text();
        return false;
    }

    // Create entity_relationships table
    QString createRelationships = 
        "CREATE TABLE IF NOT EXISTS entity_relationships ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source_type TEXT NOT NULL,"
        "  source_id INTEGER NOT NULL,"
        "  target_type TEXT NOT NULL,"
        "  target_id INTEGER NOT NULL,"
        "  relationship_type TEXT NOT NULL"
        ");";
    if (!query.exec(createRelationships)) {
        qCritical() << "Failed to create entity_relationships table:" << query.lastError().text();
        return false;
    }

    // Create assets table
    QString createAssets = 
        "CREATE TABLE IF NOT EXISTS assets ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  serial_number TEXT,"
        "  purchase_date TEXT,"
        "  location TEXT NOT NULL,"
        "  condition TEXT,"
        "  barcode TEXT UNIQUE"
        ");";
    if (!query.exec(createAssets)) {
        qCritical() << "Failed to create assets table:" << query.lastError().text();
        return false;
    }

    // Create checkout_logs table
    QString createCheckoutLogs = 
        "CREATE TABLE IF NOT EXISTS checkout_logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  asset_id INTEGER NOT NULL,"
        "  person_id INTEGER NOT NULL,"
        "  checkout_date TEXT NOT NULL,"
        "  return_date TEXT,"
        "  notes TEXT,"
        "  FOREIGN KEY(asset_id) REFERENCES assets(id),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createCheckoutLogs)) {
        qCritical() << "Failed to create checkout_logs table:" << query.lastError().text();
        return false;
    }

    // Create vault_documents table
    QString createVault = 
        "CREATE TABLE IF NOT EXISTS vault_documents ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT NOT NULL,"
        "  meeting_date TEXT NOT NULL,"
        "  category TEXT NOT NULL,"
        "  file_path TEXT NOT NULL"
        ");";
    if (!query.exec(createVault)) {
        qCritical() << "Failed to create vault_documents table:" << query.lastError().text();
        return false;
    }

    // Create audit_log table
    QString createAuditLog = 
        "CREATE TABLE IF NOT EXISTS audit_log ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  table_name TEXT,"
        "  record_id INTEGER,"
        "  field_name TEXT,"
        "  old_value TEXT,"
        "  new_value TEXT,"
        "  changed_at TEXT DEFAULT (datetime('now')),"
        "  changed_by TEXT DEFAULT 'system'"
        ");";
    if (!query.exec(createAuditLog)) {
        qCritical() << "Failed to create audit_log table:" << query.lastError().text();
        return false;
    }

    // Create member_history table
    QString createMemberHistory = 
        "CREATE TABLE IF NOT EXISTS member_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  person_id INTEGER,"
        "  activity_type TEXT,"
        "  details TEXT,"
        "  timestamp TEXT DEFAULT (datetime('now')),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createMemberHistory)) {
        qCritical() << "Failed to create member_history table:" << query.lastError().text();
        return false;
    }

    // Create care_logs table (pastoral care entries & follow-up flags)
    QString createCareLogs =
        "CREATE TABLE IF NOT EXISTS care_logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  person_id INTEGER NOT NULL,"
        "  care_type TEXT NOT NULL,"
        "  notes TEXT,"
        "  follow_up INTEGER DEFAULT 0,"
        "  timestamp TEXT DEFAULT (datetime('now')),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createCareLogs)) {
        qCritical() << "Failed to create care_logs table:" << query.lastError().text();
        return false;
    }

    // Create attendance table
    QString createAttendance = 
        "CREATE TABLE IF NOT EXISTS attendance ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  service_id INTEGER,"
        "  person_id INTEGER,"
        "  attended INTEGER DEFAULT 1,"
        "  timestamp TEXT DEFAULT (datetime('now')),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createAttendance)) {
        qCritical() << "Failed to create attendance table:" << query.lastError().text();
        return false;
    }

    // Create graph_layout table (Connection Graph node-position cache)
    QString createGraphLayout = 
        "CREATE TABLE IF NOT EXISTS graph_layout ("
        "  entity_type TEXT NOT NULL,"
        "  entity_id INTEGER NOT NULL,"
        "  pos_x REAL,"
        "  pos_y REAL,"
        "  PRIMARY KEY(entity_type, entity_id)"
        ");";
    if (!query.exec(createGraphLayout)) {
        qCritical() << "Failed to create graph_layout table:" << query.lastError().text();
        return false;
    }

    // Create users table
    QString createUsers = 
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  role TEXT NOT NULL,"
        "  pin TEXT NOT NULL"
        ");";
    if (!query.exec(createUsers)) {
        qCritical() << "Failed to create users table:" << query.lastError().text();
        return false;
    }

    // Create member_skills table
    QString createSkills = 
        "CREATE TABLE IF NOT EXISTS member_skills ("
        "  person_id INTEGER NOT NULL,"
        "  skill_or_role TEXT NOT NULL,"
        "  PRIMARY KEY(person_id, skill_or_role),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createSkills)) {
        qCritical() << "Failed to create member_skills table:" << query.lastError().text();
        return false;
    }

    // Create service_volunteers table
    QString createVolunteers = 
        "CREATE TABLE IF NOT EXISTS service_volunteers ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  service_date TEXT NOT NULL,"
        "  person_id INTEGER NOT NULL,"
        "  role TEXT NOT NULL,"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createVolunteers)) {
        qCritical() << "Failed to create service_volunteers table:" << query.lastError().text();
        return false;
    }

    // Create ministry_tasks table (planning / initiative roadmap)
    QString createMinistryTasks =
        "CREATE TABLE IF NOT EXISTS ministry_tasks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  team TEXT NOT NULL,"
        "  title TEXT NOT NULL,"
        "  owner TEXT NOT NULL,"
        "  start_day INTEGER NOT NULL,"
        "  duration_days INTEGER NOT NULL,"
        "  start_month INTEGER DEFAULT 1,"
        "  duration_months INTEGER DEFAULT 2,"
        "  status TEXT DEFAULT 'In-Progress'"
        ");";
    if (!query.exec(createMinistryTasks)) {
        qCritical() << "Failed to create ministry_tasks table:" << query.lastError().text();
        return false;
    }

    // Create facility_reservations table (room / equipment bookings)
    QString createFacilityReservations =
        "CREATE TABLE IF NOT EXISTS facility_reservations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  facility_name TEXT NOT NULL,"
        "  event_name TEXT NOT NULL,"
        "  reservation_date TEXT NOT NULL,"
        "  start_time TEXT NOT NULL,"
        "  end_time TEXT NOT NULL,"
        "  reserved_by TEXT NOT NULL"
        ");";
    if (!query.exec(createFacilityReservations)) {
        qCritical() << "Failed to create facility_reservations table:" << query.lastError().text();
        return false;
    }

    // Create board_motions table (governance / board minutes)
    QString createBoardMotions =
        "CREATE TABLE IF NOT EXISTS board_motions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  meeting_date TEXT NOT NULL,"
        "  motion_title TEXT NOT NULL,"
        "  moved_by TEXT NOT NULL,"
        "  seconded_by TEXT NOT NULL,"
        "  vote_result TEXT NOT NULL,"
        "  details TEXT"
        ");";
    if (!query.exec(createBoardMotions)) {
        qCritical() << "Failed to create board_motions table:" << query.lastError().text();
        return false;
    }

    // Create account_budgets table
    QString createBudgets = 
        "CREATE TABLE IF NOT EXISTS account_budgets ("
        "  account_id INTEGER PRIMARY KEY,"
        "  budgeted_amount_cents INTEGER NOT NULL,"
        "  fiscal_year INTEGER NOT NULL,"
        "  FOREIGN KEY(account_id) REFERENCES accounts(id)"
        ");";
    if (!query.exec(createBudgets)) {
        qCritical() << "Failed to create account_budgets table:" << query.lastError().text();
        return false;
    }

    // Create services table (worship service plans surfaced on the dashboard)
    QString createServices =
        "CREATE TABLE IF NOT EXISTS services ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  date TEXT NOT NULL UNIQUE,"
        "  title TEXT,"
        "  leader TEXT"
        ");";
    if (!query.exec(createServices)) {
        qCritical() << "Failed to create services table:" << query.lastError().text();
        return false;
    }

    // Create schema_version table (tracks which migrations have been applied)
    QString createSchemaVersion =
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "  version INTEGER PRIMARY KEY,"
        "  applied_at TEXT DEFAULT (datetime('now'))"
        ");";
    if (!query.exec(createSchemaVersion)) {
        qCritical() << "Failed to create schema_version table:" << query.lastError().text();
        return false;
    }

    // Performance Indexes: Accelerate people search, giving reports, ledger
    // lookups, and Connection Graph rendering. These run after every table
    // above exists so they can never silently fail on a fresh database.
    query.exec("CREATE INDEX IF NOT EXISTS idx_people_name ON people(last_name, first_name);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_people_status ON people(membership_status);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_people_household ON people(household_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_people_gender ON people(gender);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_contributions_person ON contributions(person_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_contributions_date ON contributions(contribution_date);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_splits_tx ON splits(transaction_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_splits_account ON splits(account_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_relationships ON entity_relationships(source_type, source_id, target_type, target_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_relationships_src ON entity_relationships(source_type, source_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_relationships_tgt ON entity_relationships(target_type, target_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_volunteers_person ON service_volunteers(person_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_transactions_date ON transactions(date);");

    return true;
}

bool DatabaseManager::ensureSchemaVersionTable() {
    // createTables() already issues CREATE IF NOT EXISTS, but a freshly opened
    // legacy DB may not have run through that path if it predates the table.
    // Idempotent: safe to call unconditionally.
    QSqlQuery query(m_db);
    if (!query.exec("CREATE TABLE IF NOT EXISTS schema_version ("
                    "  version INTEGER PRIMARY KEY,"
                    "  applied_at TEXT DEFAULT (datetime('now'))"
                    ");")) {
        qCritical() << "Failed to ensure schema_version table:" << query.lastError().text();
        return false;
    }
    return true;
}

int DatabaseManager::readSchemaVersion() {
    QSqlQuery query(m_db);
    if (!query.exec("SELECT MAX(version) FROM schema_version")) {
        return 0;
    }
    if (query.next()) {
        bool ok = false;
        int v = query.value(0).toInt(&ok);
        return ok ? v : 0;
    }
    return 0;
}

bool DatabaseManager::recordSchemaVersion(int version) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO schema_version (version) VALUES (?)");
    query.addBindValue(version);
    if (!query.exec()) {
        qCritical() << "Failed to record schema version" << version << ":"
                    << query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::migrateSchema() {
    const int current = readSchemaVersion();

    if (current > kLatestSchemaVersion) {
        qWarning() << "Database schema version" << current
                   << "is newer than this build supports (" << kLatestSchemaVersion
                   << "). Proceeding, but some features may misbehave.";
        return true;
    }
    if (current == kLatestSchemaVersion) {
        return true;
    }

    QSqlQuery q(m_db);

    // Each migration is gated by its version number so an out-of-date DB
    // runs every intervening migration in order. Wrap each step in a
    // transaction so a partial migration never leaves the DB inconsistent.

    if (current < 1) {
        // Migration 1: split the old overloaded care_flag column.
        //
        // Before: people.care_flag held BOTH pastoral-care values ("At-Risk",
        // "Leader") AND membership-status values ("Active Member", "New
        // Visitor", or empty). A person could hold only one, so "a leader who
        // is also an active member" was unrepresentable, and the dashboard
        // inactivity alert fired for every visitor (who would never give).
        //
        // After: membership_status is its own NOT NULL column with a
        // four-stage lifecycle (Visitor -> Regular Attender -> Member ->
        // Inactive), and care_flag is reserved purely for pastoral care.
        //
        // Backfill rules (applied to pre-existing rows only):
        //   "Active Member"  -> status='Member',      care_flag=NULL
        //   "New Visitor"    -> status='Visitor',     care_flag=NULL
        //   "" / NULL        -> status='Member',      care_flag=NULL
        //   "At-Risk"/"Leader" etc. -> status='Member', care_flag preserved
        qInfo() << "Applying schema migration 1: membership_status columns";

        if (!m_db.transaction()) {
            qCritical() << "Migration 1: could not begin transaction";
            return false;
        }

        // SQLite accepts ADD COLUMN even if the column already exists only by
        // erroring, so guard with a pragma check first.
        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(people)");
        bool hasStatus = false, hasFirstVisit = false, hasJoinDate = false;
        while (check.next()) {
            const QString name = check.value(1).toString();
            if (name == "membership_status") hasStatus = true;
            else if (name == "first_visit_date") hasFirstVisit = true;
            else if (name == "join_date") hasJoinDate = true;
        }

        if (!hasStatus && !q.exec("ALTER TABLE people ADD COLUMN membership_status TEXT NOT NULL DEFAULT 'Member'")) {
            qCritical() << "Migration 1: add membership_status failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!hasFirstVisit && !q.exec("ALTER TABLE people ADD COLUMN first_visit_date TEXT")) {
            qCritical() << "Migration 1: add first_visit_date failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!hasJoinDate && !q.exec("ALTER TABLE people ADD COLUMN join_date TEXT")) {
            qCritical() << "Migration 1: add join_date failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }

        // Promote the old "New Visitor" / "Active Member" pseudo-flags into a
        // real membership_status, then NULL out the care_flag for those rows
        // so the field is no longer overloaded.
        if (!q.exec("UPDATE people SET membership_status='Visitor',    care_flag=NULL WHERE care_flag='New Visitor'")) {
            qCritical() << "Migration 1: backfill Visitor failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!q.exec("UPDATE people SET membership_status='Member',     care_flag=NULL WHERE care_flag='Active Member'")) {
            qCritical() << "Migration 1: backfill Member failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
        // Rows with a true care flag (At-Risk, Leader) or empty: leave the
        // care_flag alone and default membership_status to 'Member'.

        if (!m_db.commit()) {
            qCritical() << "Migration 1: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(1)) {
            return false;
        }
        qInfo() << "Schema migration 1 applied.";
    }

    if (current < 2) {
        qInfo() << "Applying schema migration 2: groups.parent_id subgroup hierarchy column";
        if (!m_db.transaction()) {
            qCritical() << "Migration 2: could not begin transaction";
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(groups)");
        bool hasParentId = false;
        while (check.next()) {
            if (check.value(1).toString() == "parent_id") {
                hasParentId = true;
                break;
            }
        }

        if (!hasParentId && !q.exec("ALTER TABLE groups ADD COLUMN parent_id INTEGER REFERENCES groups(id)")) {
            qCritical() << "Migration 2: add parent_id failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 2: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(2)) {
            return false;
        }
        qInfo() << "Schema migration 2 applied successfully.";
    }

    if (current < 3) {
        qInfo() << "Applying schema migration 3: groups.is_archived column";
        if (!m_db.transaction()) {
            qCritical() << "Migration 3: could not begin transaction";
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(groups)");
        bool hasArchived = false;
        while (check.next()) {
            if (check.value(1).toString() == "is_archived") {
                hasArchived = true;
                break;
            }
        }

        if (!hasArchived && !q.exec("ALTER TABLE groups ADD COLUMN is_archived INTEGER DEFAULT 0")) {
            qCritical() << "Migration 3: add is_archived failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 3: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(3)) {
            return false;
        }
        qInfo() << "Schema migration 3 applied successfully.";
    }

    if (current < 4) {
        qInfo() << "Applying schema migration 4: people characteristics (spiritual_gifts, relational_style, interest_tags)";
        if (!m_db.transaction()) {
            qCritical() << "Migration 4: could not begin transaction";
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(people)");
        bool hasGifts = false, hasStyle = false, hasTags = false;
        while (check.next()) {
            QString colName = check.value(1).toString();
            if (colName == "spiritual_gifts") hasGifts = true;
            else if (colName == "relational_style") hasStyle = true;
            else if (colName == "interest_tags") hasTags = true;
        }

        if (!hasGifts && !q.exec("ALTER TABLE people ADD COLUMN spiritual_gifts TEXT")) {
            qCritical() << "Migration 4: add spiritual_gifts failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!hasStyle && !q.exec("ALTER TABLE people ADD COLUMN relational_style TEXT")) {
            qCritical() << "Migration 4: add relational_style failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!hasTags && !q.exec("ALTER TABLE people ADD COLUMN interest_tags TEXT")) {
            qCritical() << "Migration 4: add interest_tags failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 4: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(4)) {
            return false;
        }
        qInfo() << "Schema migration 4 applied successfully.";
    }

    // --- Schema Migration 5: Add gender to people ---
    int v5 = readSchemaVersion();
    if (v5 < 5) {
        if (!m_db.transaction()) {
            qCritical() << "Migration 5: failed to start transaction";
            return false;
        }
        bool hasGender = false;
        QSqlQuery migQ("PRAGMA table_info(people);", m_db);
        while (migQ.next()) {
            if (migQ.value(1).toString() == "gender") hasGender = true;
        }

        if (!hasGender && !migQ.exec("ALTER TABLE people ADD COLUMN gender TEXT DEFAULT 'Unspecified'")) {
            qCritical() << "Migration 5: add gender failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 5: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(5)) {
            return false;
        }
        qInfo() << "Schema migration 5 applied successfully.";
    }

    // --- Schema Migration 6: Expanded group metadata (meeting schedule, location, contact, capacity, notes) ---
    int v6 = readSchemaVersion();
    if (v6 < 6) {
        if (!m_db.transaction()) {
            qCritical() << "Migration 6: failed to start transaction";
            return false;
        }

        QSqlQuery check("PRAGMA table_info(groups);", m_db);
        bool hasDay = false, hasTime = false, hasLoc = false, hasContact = false, hasCap = false, hasNotes = false;
        while (check.next()) {
            QString colName = check.value(1).toString();
            if (colName == "meeting_day") hasDay = true;
            else if (colName == "meeting_time") hasTime = true;
            else if (colName == "location") hasLoc = true;
            else if (colName == "contact_person_id") hasContact = true;
            else if (colName == "max_capacity") hasCap = true;
            else if (colName == "notes") hasNotes = true;
        }

        QSqlQuery migQ(m_db);
        auto addCol = [&](bool present, const QString& sql) {
            if (present) return true;
            if (!migQ.exec(sql)) {
                qCritical() << "Migration 6:" << sql << "failed:" << migQ.lastError().text();
                return false;
            }
            return true;
        };
        if (!addCol(hasDay, "ALTER TABLE groups ADD COLUMN meeting_day TEXT;") ||
            !addCol(hasTime, "ALTER TABLE groups ADD COLUMN meeting_time TEXT;") ||
            !addCol(hasLoc, "ALTER TABLE groups ADD COLUMN location TEXT;") ||
            !addCol(hasContact, "ALTER TABLE groups ADD COLUMN contact_person_id INTEGER REFERENCES people(id);") ||
            !addCol(hasCap, "ALTER TABLE groups ADD COLUMN max_capacity INTEGER DEFAULT 0;") ||
            !addCol(hasNotes, "ALTER TABLE groups ADD COLUMN notes TEXT;")) {
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 6: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(6)) {
            return false;
        }
        qInfo() << "Schema migration 6 applied successfully.";
    }

    return true;
}

void DatabaseManager::seedInitialData() {
    QSqlQuery query;

    // Insert sample households
    query.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (1, 'The Doe Family', '123 Grace Lane', 'Metropolis', '12345')");
    query.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (2, 'The Smith Family', '456 Faith Way', 'Metropolis', '12345')");
    query.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (3, 'The Johnson Family', '789 Hope Blvd', 'Metropolis', '12345')");
    query.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (4, 'Arthur Single Household', '101 Alone Road', 'Metropolis', '12345')");
    query.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (5, 'The Miller Family', '321 Grace Lane', 'Metropolis', '12345')");
    query.exec("INSERT INTO households (id, family_name, address_line1, city, zip_code) VALUES (6, 'The Davis Family', '555 Peace Road', 'Metropolis', '12345')");

    // Insert sample people linked to households.
    // membership_status lifecycle: Visitor -> Regular Attender -> Member -> Inactive.
    // care_flag is reserved for pastoral concerns (At-Risk, Leader, etc.).
    // Tuple order: id, household_id, first, last, phone, email, dob, avatar,
    //              barcode, care_flag, admin_notes, membership_status,
    //              first_visit_date, join_date
    query.prepare("INSERT INTO people (id, household_id, first_name, last_name, phone_number, email, date_of_birth, avatar_path, barcode, care_flag, admin_notes, membership_status, first_visit_date, join_date) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    QList<QList<QVariant>> people = {
        {1, 1, "John",     "Doe",       "555-0199", "john.doe@example.com",     "1985-05-15", "", "MEMBER-101", "Leader",      "",                                "Member",          "",          "2010-09-12"},
        {2, 2, "Jane",     "Smith",     "555-0144", "jane.smith@example.com",   "1990-08-22", "", "MEMBER-102", "",            "Serves in nursery check-in.",     "Member",          "",          "2014-03-30"},
        {3, 3, "Robert",   "Johnson",   "555-0188", "robert.j@example.com",     "1978-11-03", "", "MEMBER-103", "",            "Helps with setup.",               "Member",          "",          "2008-06-21"},
        {4, 1, "Mary",     "Doe",       "555-0198", "mary.doe@example.com",     "1987-06-20", "", "MEMBER-104", "",            "Bible study coordinator.",        "Member",          "",          "2011-01-15"},
        {5, 1, "Timmy",    "Doe",       "555-0197", "timmy.doe@example.com",    "2015-10-10", "", "MEMBER-105", "",            "Attends Sunday school.",          "Regular Attender","",          ""},
        {6, 2, "Alice",    "Smith",     "555-0145", "alice.smith@example.com",  "1992-04-12", "", "MEMBER-106", "",            "Worship singer.",                 "Member",          "",          "2018-11-04"},
        {7, 4, "Arthur",   "Pendelton", "555-9999", "arthur.p@example.com",     "1950-01-01", "", "MEMBER-107", "At-Risk",     "Needs home visitation support.",  "Inactive",        "",          "1995-05-20"},
        {8, 5, "David",    "Miller",    "555-1111", "david.m@example.com",      "1980-03-03", "", "MEMBER-108", "Leader",      "Youth leader volunteer.",         "Member",          "",          "2012-09-09"},
        {9, 5, "Sarah",    "Miller",    "555-1112", "sarah.m@example.com",      "1982-04-04", "", "MEMBER-109", "",            "Worship keyboardist.",            "Member",          "",          "2013-04-14"},
        {10, 5, "Bobby",   "Miller",    "555-1113", "bobby.m@example.com",      "2018-05-05", "", "MEMBER-110", "",            "Attends youth events.",           "Regular Attender","",          ""},
        {11, 6, "James",   "Davis",     "555-2222", "pastor@example.com",       "1975-02-14", "", "MEMBER-111", "Leader",      "Senior pastor of Ekklesia.",      "Member",          "",          "2000-10-01"},
        {12, 6, "Elizabeth","Davis",    "555-2223", "elizabeth@example.com",    "1977-09-09", "", "MEMBER-112", "",            "Coordinates women fellowship.",   "Member",          "",          "2001-10-01"}
    };

    for (const auto& p : people) {
        query.addBindValue(p[0]);
        query.addBindValue(p[1]);
        query.addBindValue(p[2]);
        query.addBindValue(p[3]);
        query.addBindValue(p[4]);
        query.addBindValue(p[5]);
        query.addBindValue(p[6]);
        query.addBindValue(p[7]);
        query.addBindValue(p[8]);
        query.addBindValue(p[9]);
        query.addBindValue(p[10]);
        query.addBindValue(p[11]);
        query.addBindValue(p[12]);
        query.addBindValue(p[13]);
        query.exec();
    }

    // Insert sample groups
    query.prepare("INSERT INTO groups (name, description, category) VALUES (?, ?, ?)");
    QList<QList<QVariant>> groups = {
        {"Sunday Morning Study", "A weekly Bible study group meeting before services.", "Adults"},
        {"Youth Group", "Friday night fellowship and worship for teens.", "Youth"},
        {"Worship Team", "Musicians and singers leading congregation worship.", "Service"}
    };

    for (const auto& g : groups) {
        query.addBindValue(g[0]);
        query.addBindValue(g[1]);
        query.addBindValue(g[2]);
        query.exec();
    }

    // Insert sample accounts
    // Root level accounts
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (1, 'Assets', 'ASSET', NULL)");
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (2, 'Income', 'INCOME', NULL)");
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (3, 'Expenses', 'EXPENSE', NULL)");

    // Sub-accounts
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (4, 'Checking Account', 'ASSET', 1)");
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (5, 'General Fund', 'INCOME', 2)");
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (6, 'Building Fund', 'INCOME', 2)");
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (7, 'Missions', 'INCOME', 2)");
    query.exec("INSERT INTO accounts (id, name, type, parent_id) VALUES (8, 'Administration', 'EXPENSE', 3)");

    // Insert transactions and splits
    // Transaction 1: John Doe donates 150.00 to General Fund
    query.exec("INSERT INTO transactions (id, date, description) VALUES (1, '2026-07-05', 'Offering - John Doe')");
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (1, 4, 150.00, 1)");  // Debit Assets
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (1, 5, -150.00, 1)"); // Credit Income

    // Transaction 2: Jane Smith donates 200.00 to Building Fund
    query.exec("INSERT INTO transactions (id, date, description) VALUES (2, '2026-07-06', 'Offering - Jane Smith')");
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (2, 4, 200.00, 2)");  // Debit Assets
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (2, 6, -200.00, 2)"); // Credit Income

    // Transaction 3: John Doe donates 50.00 to Missions
    query.exec("INSERT INTO transactions (id, date, description) VALUES (3, '2026-07-10', 'Offering - John Doe')");
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (3, 4, 50.00, 1)");   // Debit Assets
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (3, 7, -50.00, 1)");  // Credit Income

    // Transaction 4: Pay electricity bill 120.00
    query.exec("INSERT INTO transactions (id, date, description) VALUES (4, '2026-07-11', 'Electricity Bill')");
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (4, 8, 120.00, NULL)"); // Debit Expense
    query.exec("INSERT INTO splits (transaction_id, account_id, amount, person_id) VALUES (4, 4, -120.00, NULL)"); // Credit Assets

    // Seed Funds
    query.exec("INSERT INTO funds (id, fund_name, is_active) VALUES (1, 'General Tithe', 1)");
    query.exec("INSERT INTO funds (id, fund_name, is_active) VALUES (2, 'Building Fund', 1)");
    query.exec("INSERT INTO funds (id, fund_name, is_active) VALUES (3, 'Missions', 1)");

    // Seed Contributions (amount in cents: e.g. 15000 = $150.00)
    query.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) VALUES (1, 1, 15000, '2026-07-05', 'Cash', NULL)");
    query.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) VALUES (2, 2, 20000, '2026-07-06', 'Check', '1004')");
    query.exec("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) VALUES (1, 3, 5000, '2026-07-10', 'Cash', NULL)");

    // Seed Entity Relationships
    // Spouses
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Person', 4, 'Spouse')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 4, 'Person', 1, 'Spouse')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Person', 9, 'Spouse')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Person', 8, 'Spouse')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 11, 'Person', 12, 'Spouse')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 12, 'Person', 11, 'Spouse')");

    // Parents / Children
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Person', 5, 'Parent')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 4, 'Person', 5, 'Parent')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Person', 10, 'Parent')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Person', 10, 'Parent')");

    // Household relationships
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Household', 1, 'Head of House')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 4, 'Household', 1, 'Member')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 5, 'Household', 1, 'Member')");
    
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 2, 'Household', 2, 'Head of House')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 6, 'Household', 2, 'Member')");
    
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 3, 'Household', 3, 'Head of House')");
    
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Household', 5, 'Head of House')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Household', 5, 'Member')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 10, 'Household', 5, 'Member')");
    
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 11, 'Household', 6, 'Head of House')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 12, 'Household', 6, 'Member')");

    // Group connections
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 1, 'Group', 1, 'Leader')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 2, 'Group', 3, 'Leader')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 6, 'Group', 3, 'Member')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 3, 'Group', 1, 'Member')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 8, 'Group', 2, 'Leader')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 9, 'Group', 3, 'Member')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 11, 'Group', 1, 'Leader')");
    query.exec("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', 12, 'Group', 3, 'Member')");

    // Seed Assets
    query.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Behringer X32 Sound Board', 'X32-998877', '2023-04-10', 'Sanctuary Booth', 'Good', 'ASSET-101')");
    query.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Epson Pro Projector', 'EPS-554433', '2024-09-12', 'Sanctuary Ceiling', 'Excellent', 'ASSET-102')");
    query.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Portable PA System', 'PPA-112233', '2025-06-01', 'Youth Storage Room', 'Fair', 'ASSET-103')");
    query.exec("INSERT INTO assets (name, serial_number, purchase_date, location, condition, barcode) VALUES ('Nursery Check-in iPad', 'IPD-008899', '2024-11-20', 'Nursery Entry', 'Good', 'ASSET-104')");

    // Seed Checkout Logs
    query.exec("INSERT INTO checkout_logs (asset_id, person_id, checkout_date, return_date, notes) VALUES (3, 1, '2026-07-01', '2026-07-03', 'Checked out for Youth beach trip.')");
    query.exec("INSERT INTO checkout_logs (asset_id, person_id, checkout_date, return_date, notes) VALUES (3, 2, '2026-07-10', NULL, 'Checked out for outdoor worship service.')");

    // Seed Vault Documents
    query.exec("INSERT INTO vault_documents (title, meeting_date, category, file_path) VALUES ('Elder Board Q2 Minutes', '2026-05-10', 'Board Minutes', 'elder_minutes_2026_q2.pdf')");
    query.exec("INSERT INTO vault_documents (title, meeting_date, category, file_path) VALUES ('2026 Annual Budget Plan', '2026-01-15', 'Financial Bylaws', 'annual_budget_2026.pdf')");

    // Seed Users
    query.exec("INSERT INTO users (username, role, pin) VALUES ('Admin', 'admin', '1234')");
    query.exec("INSERT INTO users (username, role, pin) VALUES ('Pastor', 'pastor', '5555')");
    query.exec("INSERT INTO users (username, role, pin) VALUES ('Finance', 'finance', '7777')");
    query.exec("INSERT INTO users (username, role, pin) VALUES ('Guest', 'readonly', '0000')");

    // Seed Member Skills
    query.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (1, 'Acoustic Guitar')");
    query.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (2, 'Nursery Care')");
    query.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (6, 'Vocalist')");
    query.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (8, 'Nursery Care')");
    query.exec("INSERT INTO member_skills (person_id, skill_or_role) VALUES (11, 'Preaching')");

    // Seed Service Volunteers
    query.exec("INSERT INTO service_volunteers (service_date, person_id, role) VALUES ('2026-07-19', 1, 'Acoustic Guitar')");
    query.exec("INSERT INTO service_volunteers (service_date, person_id, role) VALUES ('2026-07-19', 11, 'Preacher')");

    // Seed Budgets (Account 8: Administration has $500.00 = 50000 cents budget for 2026)
    query.exec("INSERT INTO account_budgets (account_id, budgeted_amount_cents, fiscal_year) VALUES (8, 50000, 2026)");

    qInfo() << "Seeded database with double-entry, contributions, relationship, asset, vault document, user, skill, volunteer, and budget data.";
}

void DatabaseManager::logAudit(const QString& tableName, int recordId, const QString& fieldName, const QString& oldValue, const QString& newValue, const QString& changedBy) {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("INSERT INTO audit_log (table_name, record_id, field_name, old_value, new_value, changed_by) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(tableName);
    query.addBindValue(recordId);
    query.addBindValue(fieldName);
    query.addBindValue(oldValue);
    query.addBindValue(newValue);
    query.addBindValue(changedBy);
    if (!query.exec()) {
        qWarning() << "Failed to write to audit_log:" << query.lastError().text();
    }
}

void DatabaseManager::logMemberHistory(int personId, const QString& activityType, const QString& details) {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("INSERT INTO member_history (person_id, activity_type, details) VALUES (?, ?, ?)");
    query.addBindValue(personId);
    query.addBindValue(activityType);
    query.addBindValue(details);
    if (!query.exec()) {
        qWarning() << "Failed to write to member_history:" << query.lastError().text();
    }
}

bool DatabaseManager::purgeDemoData() {
    QSqlDatabase db = database();
    if (!db.isOpen()) return false;

    if (!db.transaction()) {
        qWarning() << "Failed to begin purge transaction:" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);

    // Clear child tables first so foreign-key constraints never block a
    // parent delete. Chart-of-accounts (accounts/funds) and login users are
    // intentionally kept: they are structural, not sample ministry data, and
    // the confirmation dialog does not claim to remove them.
    const QStringList tablesToClear = {
        "graph_layout",          // no FK
        "audit_log",             // no FK
        "entity_relationships",  // no FK (polymorphic)
        "member_history",        // -> people
        "care_logs",             // -> people
        "member_skills",         // -> people
        "attendance",            // -> people
        "service_volunteers",    // -> people
        "checkout_logs",         // -> assets, people
        "contributions",         // -> people, funds
        "splits",                // -> transactions, accounts, people
        "account_budgets",       // -> accounts
        "groups",                // -> people (contact_person_id), self parent_id
        "transactions",
        "assets",
        "vault_documents",
        "people",                // -> households
        "households"
    };

    for (const QString& tbl : tablesToClear) {
        QSqlQuery del(db);
        if (!del.exec(QString("DELETE FROM %1").arg(tbl))) {
            qWarning() << "Failed to purge table" << tbl << ":" << del.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Reset auto-increment counters
    q.exec("DELETE FROM sqlite_sequence");

    bool success = db.commit();
    if (success) {
        qInfo() << "Successfully purged all demo/sample data from database.";
    } else {
        db.rollback();
        qWarning() << "Failed to purge demo data:" << db.lastError().text();
    }
    return success;
}

