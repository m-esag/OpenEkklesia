#include "databasemanager.h"
#include "ministryarchetypes.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QSet>
#include <QIODevice>
#include <QDate>

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

    // --- Automated Database Backup Snapshot (schedule + retention from Settings) ---
    QSettings bSet("OpenEkklesia", "CHMS");
    const QString schedule = bSet.value("autoBackupSchedule", "Weekly").toString();
    const int intervalDays = (schedule == "Daily") ? 1 : (schedule == "Monthly") ? 30 : 7;
    const int retention = qMax(1, bSet.value("backupRetentionCount", 8).toInt());

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
    if (!lastBackupDate.isValid() || lastBackupDate.daysTo(QDate::currentDate()) >= intervalDays) {
        QString autoBackupPath = QString("%1/auto_backup_%2.db").arg(backupDir, QDate::currentDate().toString("yyyyMMdd"));
        if (QFile::copy(resolvedPath, autoBackupPath)) {
            qInfo() << "Automated backup created at:" << autoBackupPath;

            // The vault holds the actual documents; a DB-only backup would
            // preserve records pointing at missing files. Copy the vault dir
            // alongside the .db snapshot and prune down to `retention`.
            const QString vaultDir = vaultDirPath();
            if (QDir(vaultDir).exists()) {
                const QString vaultBackupDir = QString("%1/auto_backup_%2_vault")
                                                   .arg(backupDir, QDate::currentDate().toString("yyyyMMdd"));
                QDir().mkpath(vaultBackupDir);
                const auto files = QDir(vaultDir).entryInfoList(QDir::Files);
                bool allCopied = true;
                for (const QFileInfo& fi : files) {
                    const QString dest = vaultBackupDir + "/" + fi.fileName();
                    QFile::remove(dest);
                    if (!QFile::copy(fi.absoluteFilePath(), dest)) allCopied = false;
                }
                if (!allCopied) {
                    qWarning() << "Vault backup incomplete for" << vaultBackupDir;
                }
                // Keep the newest `retention` vault snapshots (names sort by date).
                const auto backupDirs = QDir(backupDir).entryList(
                    QStringList() << "auto_backup_*_vault", QDir::Dirs | QDir::NoDotAndDotDot,
                    QDir::Name | QDir::Reversed);
                for (int i = retention; i < backupDirs.size(); ++i) {
                    QDir(backupDir + "/" + backupDirs[i]).removeRecursively();
                }
            }

            // Prune old database snapshots to the retention count.
            const auto dbBackups = QDir(backupDir).entryList(
                QStringList() << "auto_backup_*.db", QDir::Files,
                QDir::Name | QDir::Reversed);
            for (int i = retention; i < dbBackups.size(); ++i) {
                QFile::remove(backupDir + "/" + dbBackups[i]);
            }

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
    if (m_db.isValid()) {
        const QString connectionName = m_db.connectionName();
        m_db.close();
        m_db = QSqlDatabase(); // release our handle so the connection can be removed
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool DatabaseManager::restoreFromBackup(const QString& backupPath) {
    if (!QFile::exists(backupPath)) {
        qWarning() << "Restore failed: backup file does not exist:" << backupPath;
        return false;
    }

    const QString dbPath = m_db.isValid() ? m_db.databaseName() : QString();
    if (dbPath.isEmpty()) {
        qWarning() << "Restore failed: no active database path to replace";
        return false;
    }

    // Close the live connection WITHOUT removing it from the registry: the app
    // (and tests) may still hold QSqlDatabase/QSqlQuery handles, so
    // QSqlDatabase::removeDatabase() would warn "connection is still in use"
    // and leave a stale connection behind. Closing, swapping the file, and
    // reopening the same connection avoids that entirely.
    m_db.close();

    QFile::remove(dbPath);
    QFile::remove(dbPath + "-wal");
    QFile::remove(dbPath + "-shm");
    if (!QFile::copy(backupPath, dbPath)) {
        qWarning() << "Restore failed: could not copy backup over" << dbPath;
        m_db.open(); // try to bring the original connection back
        return false;
    }
    return m_db.open();
}

bool DatabaseManager::exportBackup(const QString& destPath) {
    const QString dbPath = m_db.isValid() ? m_db.databaseName() : QString();
    if (dbPath.isEmpty()) return false;

    // Force the write-ahead log into the main file so the copy is complete.
    QSqlQuery q(m_db);
    q.exec("PRAGMA wal_checkpoint(TRUNCATE)");

    QFile::remove(destPath);
    if (!QFile::copy(dbPath, destPath)) return false;

    // Documents live outside the DB; export them to a sibling folder so a
    // manual backup is complete on its own. Failure here is a warning, not a
    // failed backup — the DB copy already succeeded.
    const QString vaultDir = vaultDirPath();
    if (QDir(vaultDir).exists()) {
        QString base = destPath;
        if (base.endsWith(".db", Qt::CaseInsensitive)) base.chop(3);
        const QString vaultDest = base + "_vault";
        QDir().mkpath(vaultDest);
        const auto files = QDir(vaultDir).entryInfoList(QDir::Files);
        for (const QFileInfo& fi : files) {
            const QString dest = vaultDest + "/" + fi.fileName();
            QFile::remove(dest);
            if (!QFile::copy(fi.absoluteFilePath(), dest)) {
                qWarning() << "Vault file not exported:" << fi.fileName();
            }
        }
        qInfo() << "Vault documents exported to" << vaultDest;
    }
    return true;
}

QSqlDatabase DatabaseManager::database() const {
    return QSqlDatabase::database(m_db.connectionName());
}

QString DatabaseManager::vaultDirPath() {
    const QString dbPath = instance().m_db.databaseName();
    return QFileInfo(dbPath).absolutePath() + "/vault";
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
        "  required_skills TEXT,"
        "  min_age INTEGER DEFAULT 0,"
        "  max_age INTEGER DEFAULT 0,"
        "  recruitment_status TEXT DEFAULT 'Open',"
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

    // Create giving_batches table
    QString createBatches =
        "CREATE TABLE IF NOT EXISTS giving_batches ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  batch_name TEXT NOT NULL,"
        "  batch_date TEXT NOT NULL,"
        "  status TEXT DEFAULT 'In Progress',"
        "  default_fund_id INTEGER REFERENCES funds(id),"
        "  default_payment_method TEXT DEFAULT 'Check',"
        "  created_by TEXT DEFAULT 'Admin',"
        "  batch_note TEXT DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    if (!query.exec(createBatches)) {
        qCritical() << "Failed to create giving_batches table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_batches_date ON giving_batches(batch_date);");

    // Create contributions table
    QString createContributions = 
        "CREATE TABLE IF NOT EXISTS contributions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  batch_id INTEGER,"
        "  person_id INTEGER,"
        "  fund_id INTEGER NOT NULL,"
        "  amount INTEGER NOT NULL,"
        "  contribution_date TEXT NOT NULL,"
        "  payment_method TEXT,"
        "  check_number TEXT,"
        "  external_id TEXT,"
        "  note TEXT,"
        "  FOREIGN KEY(batch_id) REFERENCES giving_batches(id) ON DELETE SET NULL,"
        "  FOREIGN KEY(person_id) REFERENCES people(id),"
        "  FOREIGN KEY(fund_id) REFERENCES funds(id)"
        ");";
    if (!query.exec(createContributions)) {
        qCritical() << "Failed to create contributions table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_contributions_external_id "
               "ON contributions(external_id) WHERE external_id IS NOT NULL;");
    query.exec("CREATE INDEX IF NOT EXISTS idx_contributions_date ON contributions(contribution_date);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_contributions_person ON contributions(person_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_contributions_batch ON contributions(batch_id);");

    // Create entity_relationships table
    QString createRelationships = 
        "CREATE TABLE IF NOT EXISTS entity_relationships ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source_type TEXT NOT NULL,"
        "  source_id INTEGER NOT NULL,"
        "  target_type TEXT NOT NULL,"
        "  target_id INTEGER NOT NULL,"
        "  relationship_type TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'Active'"
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
        "  file_path TEXT NOT NULL,"
        "  person_id INTEGER,"
        "  group_id INTEGER,"
        "  added_at TEXT DEFAULT (datetime('now')),"
        "  FOREIGN KEY(person_id) REFERENCES people(id),"
        "  FOREIGN KEY(group_id) REFERENCES groups(id)"
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

    // Create pastoral_activities table (the Shepherd's Workbench queue: visits,
    // calls, prayer, care, discipleship, sacraments, outreach — each can carry
    // a follow-up date the Today's Queue surfaces until it is done).
    QString createPastoralActivities =
        "CREATE TABLE IF NOT EXISTS pastoral_activities ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  person_id INTEGER NOT NULL,"
        "  activity_type TEXT NOT NULL,"
        "  activity_date TEXT NOT NULL,"
        "  notes TEXT,"
        "  outcome TEXT,"
        "  follow_up_date TEXT,"
        "  follow_up_done INTEGER DEFAULT 0,"
        "  group_id INTEGER REFERENCES groups(id),"
        "  FOREIGN KEY(person_id) REFERENCES people(id)"
        ");";
    if (!query.exec(createPastoralActivities)) {
        qCritical() << "Failed to create pastoral_activities table:" << query.lastError().text();
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

    // Ensure default users exist if table is empty (e.g. existing/upgraded database)
    QSqlQuery checkUsers("SELECT COUNT(*) FROM users", m_db);
    if (checkUsers.next() && checkUsers.value(0).toInt() == 0) {
        query.exec("INSERT INTO users (username, role, pin) VALUES ('Admin', 'admin', '1234')");
        query.exec("INSERT INTO users (username, role, pin) VALUES ('Pastor', 'pastor', '5555')");
        query.exec("INSERT INTO users (username, role, pin) VALUES ('Finance', 'finance', '7777')");
        query.exec("INSERT INTO users (username, role, pin) VALUES ('Guest', 'readonly', '0000')");
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
    // team_id/owner_id keep tasks linked to groups/people so renames survive;
    // the text columns stay as display fallbacks (and for teams/owners with
    // no group/person match). start_date/end_date anchor tasks to real
    // calendar dates (the legacy start_day/start_month grid is retained for
    // migration only — new rows are driven by the real dates).
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
        "  status TEXT DEFAULT 'In-Progress',"
        "  team_id INTEGER REFERENCES groups(id),"
        "  owner_id INTEGER REFERENCES people(id),"
        "  start_date TEXT,"
        "  end_date TEXT,"
        "  anchor_event_id INTEGER REFERENCES services(id),"
        "  priority TEXT DEFAULT 'Medium',"
        "  category TEXT DEFAULT 'Task',"
        "  progress_pct INTEGER DEFAULT 0,"
        "  notes TEXT DEFAULT ''"
        ");";
    if (!query.exec(createMinistryTasks)) {
        qCritical() << "Failed to create ministry_tasks table:" << query.lastError().text();
        return false;
    }

    // Create ministry_events table (church-wide events, conferences, seasonal programs)
    QString createMinistryEvents =
        "CREATE TABLE IF NOT EXISTS ministry_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT NOT NULL,"
        "  event_type TEXT DEFAULT 'Outreach',"
        "  team_id INTEGER REFERENCES groups(id),"
        "  coordinator_id INTEGER REFERENCES people(id),"
        "  facility_name TEXT DEFAULT '',"
        "  start_datetime TEXT NOT NULL,"
        "  end_datetime TEXT NOT NULL,"
        "  expected_attendance INTEGER DEFAULT 0,"
        "  budget_cents INTEGER DEFAULT 0,"
        "  status TEXT DEFAULT 'Confirmed',"
        "  description TEXT DEFAULT '',"
        "  service_id INTEGER REFERENCES services(id),"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    if (!query.exec(createMinistryEvents)) {
        qCritical() << "Failed to create ministry_events table:" << query.lastError().text();
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
        "  leader TEXT,"
        "  run_sheet TEXT,"
        "  start_time TEXT"
        ");";
    if (!query.exec(createServices)) {
        qCritical() << "Failed to create services table:" << query.lastError().text();
        return false;
    }

    // Service Builder support tables: persistent element library and named
    // run-sheet templates. The library is seeded with the classic elements
    // when empty (see seedServiceLibrary()).
    QString createLib =
        "CREATE TABLE IF NOT EXISTS service_library ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  type TEXT NOT NULL,"
        "  duration_mins INTEGER DEFAULT 0,"
        "  is_custom INTEGER DEFAULT 0"
        ");";
    if (!query.exec(createLib)) {
        qCritical() << "Failed to create service_library table:" << query.lastError().text();
        return false;
    }
    QString createTpl =
        "CREATE TABLE IF NOT EXISTS service_templates ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL UNIQUE,"
        "  run_sheet TEXT NOT NULL"
        ");";
    if (!query.exec(createTpl)) {
        qCritical() << "Failed to create service_templates table:" << query.lastError().text();
        return false;
    }
    seedServiceLibrary();

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

    // --- Schema Migration 7: Giving-import dedupe key + reporting indexes ---
    // external_id stores the upstream donation id (e.g. Planning Center
    // "PCO-<id>") so repeated imports skip already-imported donations instead
    // of duplicating them. The date indexes speed up the year/month giving
    // reports the dashboard now runs against contributions.
    if (current < 7) {
        qInfo() << "Applying schema migration 7: contributions.external_id + giving indexes";
        if (!m_db.transaction()) {
            qCritical() << "Migration 7: failed to start transaction";
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(contributions)");
        bool hasExternalId = false;
        while (check.next()) {
            if (check.value(1).toString() == "external_id") {
                hasExternalId = true;
                break;
            }
        }

        QSqlQuery migQ(m_db);
        if (!hasExternalId &&
            !migQ.exec("ALTER TABLE contributions ADD COLUMN external_id TEXT;")) {
            qCritical() << "Migration 7: add external_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!migQ.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_contributions_external_id "
                       "ON contributions(external_id) WHERE external_id IS NOT NULL;")) {
            qCritical() << "Migration 7: external_id index failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!migQ.exec("CREATE INDEX IF NOT EXISTS idx_contributions_date "
                       "ON contributions(contribution_date);")) {
            qCritical() << "Migration 7: date index failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!migQ.exec("CREATE INDEX IF NOT EXISTS idx_contributions_person "
                       "ON contributions(person_id);")) {
            qCritical() << "Migration 7: person index failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 7: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(7)) {
            return false;
        }
        qInfo() << "Schema migration 7 applied successfully.";
    }

    // --- Schema Migration 8: per-group Ministry Match profile ---
    // Groups can declare what they are looking for — required_skills
    // (comma-separated) plus an optional age band — so "Suggest People"
    // matches against declared criteria instead of only inferring from
    // current members. max_age 0 means "no age band declared".
    if (current < 8) {
        qInfo() << "Applying schema migration 8: groups match profile columns";
        if (!m_db.transaction()) {
            qCritical() << "Migration 8: failed to start transaction";
            return false;
        }

        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(groups)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());

        QSqlQuery migQ(m_db);
        if (!cols.contains("required_skills") &&
            !migQ.exec("ALTER TABLE groups ADD COLUMN required_skills TEXT;")) {
            qCritical() << "Migration 8: add required_skills failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!cols.contains("min_age") &&
            !migQ.exec("ALTER TABLE groups ADD COLUMN min_age INTEGER DEFAULT 0;")) {
            qCritical() << "Migration 8: add min_age failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!cols.contains("max_age") &&
            !migQ.exec("ALTER TABLE groups ADD COLUMN max_age INTEGER DEFAULT 0;")) {
            qCritical() << "Migration 8: add max_age failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 8: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(8)) {
            return false;
        }
        qInfo() << "Schema migration 8 applied successfully.";
    }

    // --- Schema Migration 9: Service Builder Tier 1 ---
    // services.run_sheet holds the JSON run sheet for a dated service;
    // service_library is a persistent (user-editable) element library seeded
    // with the classic elements; service_templates stores named run sheets.
    if (current < 9) {
        qInfo() << "Applying schema migration 9: run_sheet + service library/templates";
        if (!m_db.transaction()) {
            qCritical() << "Migration 9: failed to start transaction";
            return false;
        }

        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(services)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());

        QSqlQuery migQ(m_db);
        if (!cols.contains("run_sheet") &&
            !migQ.exec("ALTER TABLE services ADD COLUMN run_sheet TEXT;")) {
            qCritical() << "Migration 9: add run_sheet failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!migQ.exec("CREATE TABLE IF NOT EXISTS service_library ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  name TEXT NOT NULL,"
                       "  type TEXT NOT NULL,"
                       "  duration_mins INTEGER DEFAULT 0,"
                       "  is_custom INTEGER DEFAULT 0"
                       ");")) {
            qCritical() << "Migration 9: create service_library failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!migQ.exec("CREATE TABLE IF NOT EXISTS service_templates ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  name TEXT NOT NULL UNIQUE,"
                       "  run_sheet TEXT NOT NULL"
                       ");")) {
            qCritical() << "Migration 9: create service_templates failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 9: commit failed";
            m_db.rollback();
            return false;
        }
        seedServiceLibrary(); // no-op when already populated

        if (!recordSchemaVersion(9)) {
            return false;
        }
        qInfo() << "Schema migration 9 applied successfully.";
    }

    // --- Schema Migration 10: Anonymous gifts + relaxed payment columns + note ---
    // The Sunday batch-entry form is intentionally minimal (Date / Person /
    // Fund / Amount / Note), so:
    //   - person_id becomes nullable: anonymous plate offerings no longer
    //     require a fabricated donor, and fund totals reconcile correctly.
    //   - payment_method loses NOT NULL: the entry form no longer writes it;
    //     the Planning Center import still fills it for online/check gifts.
    //   - note is added for free-text context ("in memory of", campaign, etc).
    // SQLite cannot relax NOT NULL in place, so the table is rebuilt via
    // create-new / copy / drop / rename inside one transaction.
    if (current < 10) {
        qInfo() << "Applying schema migration 10: contributions rebuild (nullable person, note column)";
        if (!m_db.transaction()) {
            qCritical() << "Migration 10: failed to start transaction";
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(contributions)");
        bool hasNote = false;
        bool personNullable = false;
        while (check.next()) {
            const QString col = check.value(1).toString();
            const bool notNull = check.value(3).toBool();
            if (col == "note") hasNote = true;
            if (col == "person_id" && !notNull) personNullable = true;
        }

        if (!hasNote || !personNullable) {
            QSqlQuery migQ(m_db);
            if (!migQ.exec(
                    "CREATE TABLE contributions_mig10 ("
                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  person_id INTEGER,"
                    "  fund_id INTEGER NOT NULL,"
                    "  amount INTEGER NOT NULL,"
                    "  contribution_date TEXT NOT NULL,"
                    "  payment_method TEXT,"
                    "  check_number TEXT,"
                    "  external_id TEXT,"
                    "  note TEXT,"
                    "  FOREIGN KEY(person_id) REFERENCES people(id),"
                    "  FOREIGN KEY(fund_id) REFERENCES funds(id)"
                    ");")) {
                qCritical() << "Migration 10: create staging table failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
            if (!migQ.exec(
                    "INSERT INTO contributions_mig10 (id, person_id, fund_id, amount, contribution_date, "
                    "                                  payment_method, check_number, external_id) "
                    "SELECT id, person_id, fund_id, amount, contribution_date, "
                    "       payment_method, check_number, external_id FROM contributions;")) {
                qCritical() << "Migration 10: copy rows failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
            if (!migQ.exec("DROP TABLE contributions;")) {
                qCritical() << "Migration 10: drop old table failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
            if (!migQ.exec("ALTER TABLE contributions_mig10 RENAME TO contributions;")) {
                qCritical() << "Migration 10: rename staging table failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
            // Indexes were dropped with the old table — recreate all three.
            if (!migQ.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_contributions_external_id "
                           "ON contributions(external_id) WHERE external_id IS NOT NULL;") ||
                !migQ.exec("CREATE INDEX IF NOT EXISTS idx_contributions_date "
                           "ON contributions(contribution_date);") ||
                !migQ.exec("CREATE INDEX IF NOT EXISTS idx_contributions_person "
                           "ON contributions(person_id);")) {
                qCritical() << "Migration 10: recreate indexes failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        } else {
            qInfo() << "Migration 10: contributions already in target shape; skipping rebuild.";
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 10: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(10)) {
            return false;
        }
        qInfo() << "Schema migration 10 applied successfully.";
    }

    // --- Schema Migration 11: services.start_time ---
    // Service Builder "Service starts at" field; the run sheet computes each
    // item's start time from it and warns on overrun.
    if (current < 11) {
        qInfo() << "Applying schema migration 11: services.start_time";
        if (!m_db.transaction()) {
            qCritical() << "Migration 11: failed to start transaction";
            return false;
        }

        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(services)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());

        if (!cols.contains("start_time")) {
            QSqlQuery migQ(m_db);
            if (!migQ.exec("ALTER TABLE services ADD COLUMN start_time TEXT;")) {
                qCritical() << "Migration 11: add start_time failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 11: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(11)) {
            return false;
        }
        qInfo() << "Schema migration 11 applied successfully.";
    }

    // --- Schema Migration 12: groups.recruitment_status ---
    // Controls recommendation & assimilation availability:
    // 'Open' (default), 'Elected' (Board of Directors, Deacons, Elders - closed to general recommendation),
    // 'Seasonal' (closed/biannual cohort), 'Closed' (invitation only).
    if (current < 12) {
        qInfo() << "Applying schema migration 12: groups.recruitment_status";
        if (!m_db.transaction()) {
            qCritical() << "Migration 12: failed to start transaction";
            return false;
        }

        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(groups)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());

        if (!cols.contains("recruitment_status")) {
            QSqlQuery migQ(m_db);
            if (!migQ.exec("ALTER TABLE groups ADD COLUMN recruitment_status TEXT DEFAULT 'Open';")) {
                qCritical() << "Migration 12: add recruitment_status failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
            // Automatically flag existing governance bodies (Board, Deacon, Elder, Governance) as 'Elected'
            migQ.exec("UPDATE groups SET recruitment_status = 'Elected' "
                      "WHERE name LIKE '%Board%' OR name LIKE '%Director%' OR name LIKE '%Deacon%' "
                      "   OR name LIKE '%Elder%' OR category LIKE '%Governance%' OR category LIKE '%Board%';");
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 12: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(12)) {
            return false;
        }
        qInfo() << "Schema migration 12 applied successfully.";
    }

    // --- Schema Migration 13: ministry_tasks relational links ---
    // team_id -> groups.id, owner_id -> people.id so planning tasks survive
    // group/person renames and enable team/owner-filtered views. Existing
    // text rows are backfilled best-effort; the text columns stay as display
    // fallbacks. Owner text like "John Doe (Leader)" is cleaned to "John Doe".
    if (current < 13) {
        qInfo() << "Applying schema migration 13: ministry_tasks team_id/owner_id";
        if (!m_db.transaction()) {
            qCritical() << "Migration 13: failed to start transaction";
            return false;
        }

        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(ministry_tasks)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());

        QSqlQuery migQ(m_db);
        if (!cols.contains("team_id") &&
            !migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN team_id INTEGER;")) {
            qCritical() << "Migration 13: add team_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!cols.contains("owner_id") &&
            !migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN owner_id INTEGER;")) {
            qCritical() << "Migration 13: add owner_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        // Clean "(Role)" suffixes from stored owner text first so both the
        // display and the id backfill see plain names.
        migQ.exec("UPDATE ministry_tasks SET owner = "
                  "CASE WHEN instr(owner, ' (') > 0 "
                  "     THEN substr(owner, 1, instr(owner, ' (') - 1) "
                  "     ELSE owner END "
                  "WHERE instr(owner, ' (') > 0;");

        // Best-effort backfill from the text columns.
        migQ.exec("UPDATE ministry_tasks SET team_id = "
                  "(SELECT g.id FROM groups g WHERE g.name = ministry_tasks.team LIMIT 1) "
                  "WHERE team_id IS NULL;");
        migQ.exec("UPDATE ministry_tasks SET owner_id = "
                  "(SELECT p.id FROM people p "
                  " WHERE p.first_name || ' ' || p.last_name = ministry_tasks.owner LIMIT 1) "
                  "WHERE owner_id IS NULL;");

        if (!m_db.commit()) {
            qCritical() << "Migration 13: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(13)) {
            return false;
        }
        qInfo() << "Schema migration 13 applied successfully.";
    }

    // --- Schema Migration 14: ministry_tasks real calendar dates ---
    // start_date/end_date anchor tasks to real dates so dashboard "slipping"
    // queries and Service Builder integration become possible. Existing rows
    // are backfilled from the legacy grid (start_month 1-5 -> Jan-May of the
    // current year, start_day 1-20 -> day offset, duration_days -> span).
    // anchor_event_id optionally links a task to a dated service.
    if (current < 14) {
        qInfo() << "Applying schema migration 14: ministry_tasks real dates";
        if (!m_db.transaction()) {
            qCritical() << "Migration 14: failed to start transaction";
            return false;
        }

        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(ministry_tasks)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());

        QSqlQuery migQ(m_db);
        if (!cols.contains("start_date") &&
            !migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN start_date TEXT;")) {
            qCritical() << "Migration 14: add start_date failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!cols.contains("end_date") &&
            !migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN end_date TEXT;")) {
            qCritical() << "Migration 14: add end_date failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!cols.contains("anchor_event_id") &&
            !migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN anchor_event_id INTEGER;")) {
            qCritical() << "Migration 14: add anchor_event_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        // Best-effort backfill from the legacy abstract grid to real dates.
        migQ.exec(
            "UPDATE ministry_tasks SET "
            "  start_date = date('now', 'start of year', "
            "                     printf('%+d months', MIN(MAX(COALESCE(start_month, 1), 1), 12) - 1), "
            "                     printf('%+d days', MAX(COALESCE(start_day, 1), 1) - 1)), "
            "  end_date = date('now', 'start of year', "
            "                   printf('%+d months', MIN(MAX(COALESCE(start_month, 1), 1), 12) - 1), "
            "                   printf('%+d days', MAX(COALESCE(start_day, 1), 1) - 1 + COALESCE(duration_days, 1))) "
            "WHERE start_date IS NULL;");

        if (!m_db.commit()) {
            qCritical() << "Migration 14: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(14)) {
            return false;
        }
        qInfo() << "Schema migration 14 applied successfully.";
    }

    // --- Schema Migration 15: link vault documents to people & groups ---
    // Membership applications belong to a person, sign-up sheets to a
    // ministry group. The optional links make documents findable from the
    // profile/group instead of by remembered filenames. added_at records when
    // the document was filed.
    if (current < 15) {
        qInfo() << "Applying schema migration 15: vault_documents person/group links";
        if (!m_db.transaction()) {
            qCritical() << "Migration 15: failed to start transaction";
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(vault_documents)");
        bool hasPerson = false, hasGroup = false, hasAddedAt = false;
        while (check.next()) {
            const QString col = check.value(1).toString();
            if (col == "person_id") hasPerson = true;
            else if (col == "group_id") hasGroup = true;
            else if (col == "added_at") hasAddedAt = true;
        }

        QSqlQuery migQ(m_db);
        if (!hasPerson && !migQ.exec("ALTER TABLE vault_documents ADD COLUMN person_id INTEGER REFERENCES people(id);")) {
            qCritical() << "Migration 15: add person_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!hasGroup && !migQ.exec("ALTER TABLE vault_documents ADD COLUMN group_id INTEGER REFERENCES groups(id);")) {
            qCritical() << "Migration 15: add group_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        // SQLite forbids non-constant defaults in ALTER TABLE ADD COLUMN
        // (e.g. DEFAULT (datetime('now'))), so add a plain column and
        // backfill existing rows afterwards.
        if (!hasAddedAt && !migQ.exec("ALTER TABLE vault_documents ADD COLUMN added_at TEXT;")) {
            qCritical() << "Migration 15: add added_at failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!hasAddedAt) {
            migQ.exec("UPDATE vault_documents SET added_at = datetime('now') WHERE added_at IS NULL;");
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 15: commit failed";
            m_db.rollback();
            return false;
        }

        if (!recordSchemaVersion(15)) {
            return false;
        }
        qInfo() << "Schema migration 15 applied successfully.";
    }

    // --- Schema Migration 16: giving_batches & contributions.batch_id ---
    // Organizes Sunday morning and event collections into batches (In Progress vs Committed)
    // with batch metadata (created_by, batch_note) and default batch parameters.
    if (current < 16) {
        qInfo() << "Applying schema migration 16: giving_batches table & contributions.batch_id";
        if (!m_db.transaction()) {
            qCritical() << "Migration 16: failed to start transaction";
            return false;
        }

        QSqlQuery migQ(m_db);
        if (!migQ.exec("CREATE TABLE IF NOT EXISTS giving_batches ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  batch_name TEXT NOT NULL,"
                       "  batch_date TEXT NOT NULL,"
                       "  status TEXT DEFAULT 'In Progress',"
                       "  default_fund_id INTEGER REFERENCES funds(id),"
                       "  default_payment_method TEXT DEFAULT 'Check',"
                       "  created_by TEXT DEFAULT 'Admin',"
                       "  batch_note TEXT DEFAULT '',"
                       "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                       ");")) {
            qCritical() << "Migration 16: create giving_batches failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        QSqlQuery check(m_db);
        check.exec("PRAGMA table_info(contributions)");
        bool hasBatchId = false;
        while (check.next()) {
            if (check.value(1).toString() == "batch_id") hasBatchId = true;
        }

        if (!hasBatchId && !migQ.exec("ALTER TABLE contributions ADD COLUMN batch_id INTEGER REFERENCES giving_batches(id) ON DELETE SET NULL;")) {
            qCritical() << "Migration 16: add batch_id failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        migQ.exec("CREATE INDEX IF NOT EXISTS idx_contributions_batch ON contributions(batch_id);");
        migQ.exec("CREATE INDEX IF NOT EXISTS idx_batches_date ON giving_batches(batch_date);");

        // Seed initial batch for existing contributions if empty
        QSqlQuery checkBatches("SELECT COUNT(*) FROM giving_batches", m_db);
        if (checkBatches.next() && checkBatches.value(0).toInt() == 0) {
            int genFundId = 1;
            QSqlQuery fq("SELECT id FROM funds WHERE fund_name LIKE '%General%' OR fund_name LIKE '%Tithe%' LIMIT 1", m_db);
            if (fq.next()) genFundId = fq.value(0).toInt();

            migQ.prepare("INSERT INTO giving_batches (batch_name, batch_date, status, default_fund_id, default_payment_method, created_by) "
                         "VALUES ('Sunday Morning Service', date('now'), 'In Progress', ?, 'Check', 'Admin')");
            migQ.addBindValue(genFundId);
            if (migQ.exec()) {
                int newBatchId = migQ.lastInsertId().toInt();
                if (newBatchId > 0) {
                    QSqlQuery linkQ(m_db);
                    linkQ.prepare("UPDATE contributions SET batch_id = ? WHERE batch_id IS NULL");
                    linkQ.addBindValue(newBatchId);
                    linkQ.exec();
                }
            }
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 16: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(16)) {
            return false;
        }
        qInfo() << "Schema migration 16 applied successfully.";
    }

    // --- Schema Migration 17: graph_snapshots (connection-health trend) ---
    // Each Audit run records congregation connectivity metrics so the graph
    // and dashboard can show how isolation/connectivity change over time.
    if (current < 17) {
        qInfo() << "Applying schema migration 17: graph_snapshots";
        if (!m_db.transaction()) {
            qCritical() << "Migration 17: failed to start transaction";
            return false;
        }
        QSqlQuery migQ(m_db);
        if (!migQ.exec("CREATE TABLE IF NOT EXISTS graph_snapshots ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  taken_at TEXT DEFAULT (datetime('now', 'localtime')),"
                       "  node_count INTEGER DEFAULT 0,"
                       "  edge_count INTEGER DEFAULT 0,"
                       "  density REAL DEFAULT 0,"
                       "  avg_connections REAL DEFAULT 0,"
                       "  bridge_count INTEGER DEFAULT 0,"
                       "  isolated_count INTEGER DEFAULT 0,"
                       "  missing_phone INTEGER DEFAULT 0,"
                       "  missing_email INTEGER DEFAULT 0"
                       ");")) {
            qCritical() << "Migration 17: create graph_snapshots failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!m_db.commit()) {
            qCritical() << "Migration 17: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(17)) {
            return false;
        }
        qInfo() << "Schema migration 17 applied successfully.";
    }

    // --- Schema Migration 18: entity_relationships.status (group-scoped member status) ---
    if (current < 18) {
        qInfo() << "Applying schema migration 18: entity_relationships.status";
        if (!m_db.transaction()) {
            qCritical() << "Migration 18: failed to start transaction";
            return false;
        }
        QSqlQuery pragma("PRAGMA table_info(entity_relationships);", m_db);
        bool hasStatus = false;
        while (pragma.next()) {
            if (pragma.value(1).toString() == "status") {
                hasStatus = true;
                break;
            }
        }
        if (!hasStatus) {
            QSqlQuery migQ(m_db);
            if (!migQ.exec("ALTER TABLE entity_relationships ADD COLUMN status TEXT NOT NULL DEFAULT 'Active';")) {
                qCritical() << "Migration 18: add status column to entity_relationships failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        if (!m_db.commit()) {
            qCritical() << "Migration 18: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(18)) {
            return false;
        }
        qInfo() << "Schema migration 18 applied successfully.";
    }

    // --- Schema Migration 19: ministry_archetypes & team dynamics ---
    if (current < 19) {
        qInfo() << "Applying schema migration 19: ministry_archetypes, archetypes in people/groups, and connection audit";
        if (!m_db.transaction()) {
            qCritical() << "Migration 19: failed to start transaction";
            return false;
        }

        QSqlQuery migQ(m_db);

        // 1. Create table ministry_archetypes
        if (!migQ.exec("CREATE TABLE IF NOT EXISTS ministry_archetypes ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  category_key TEXT NOT NULL UNIQUE,"
                       "  display_name TEXT NOT NULL,"
                       "  ideal_primary TEXT NOT NULL,"
                       "  ideal_secondary TEXT NOT NULL,"
                       "  weight_organizer INTEGER NOT NULL DEFAULT 0,"
                       "  weight_listener INTEGER NOT NULL DEFAULT 0,"
                       "  weight_doer INTEGER NOT NULL DEFAULT 0,"
                       "  weight_energizer INTEGER NOT NULL DEFAULT 0,"
                       "  weight_strategist INTEGER NOT NULL DEFAULT 0,"
                       "  weight_connector INTEGER NOT NULL DEFAULT 0,"
                       "  required_gifts TEXT,"
                       "  description TEXT,"
                       "  is_custom INTEGER DEFAULT 0"
                       ");")) {
            qCritical() << "Migration 19: create ministry_archetypes failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        // 2. Seed canonical blueprints
        for (const auto& bp : MinistryArchetypeRegistry::canonicalBlueprints()) {
            QSqlQuery insBp(m_db);
            insBp.prepare("INSERT OR IGNORE INTO ministry_archetypes "
                          "(category_key, display_name, ideal_primary, ideal_secondary, "
                          " weight_organizer, weight_listener, weight_doer, weight_energizer, weight_strategist, weight_connector, "
                          " required_gifts, description, is_custom) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0);");
            insBp.addBindValue(bp.categoryKey);
            insBp.addBindValue(bp.displayName);
            insBp.addBindValue(bp.primaryArchetype);
            insBp.addBindValue(bp.secondaryArchetype);
            insBp.addBindValue(static_cast<int>(bp.targetVector.organizer));
            insBp.addBindValue(static_cast<int>(bp.targetVector.listener));
            insBp.addBindValue(static_cast<int>(bp.targetVector.doer));
            insBp.addBindValue(static_cast<int>(bp.targetVector.energizer));
            insBp.addBindValue(static_cast<int>(bp.targetVector.strategist));
            insBp.addBindValue(static_cast<int>(bp.targetVector.connector));
            insBp.addBindValue(bp.requiredGifts.join(", "));
            insBp.addBindValue(bp.description);
            insBp.exec();
        }

        // 3. Add columns to groups: ideal_primary_archetype, ideal_secondary_archetype, dynamic_health_score
        QSqlQuery pragmaGroups("PRAGMA table_info(groups);", m_db);
        QSet<QString> groupCols;
        while (pragmaGroups.next()) groupCols.insert(pragmaGroups.value(1).toString());
        if (!groupCols.contains("ideal_primary_archetype")) {
            migQ.exec("ALTER TABLE groups ADD COLUMN ideal_primary_archetype TEXT;");
        }
        if (!groupCols.contains("ideal_secondary_archetype")) {
            migQ.exec("ALTER TABLE groups ADD COLUMN ideal_secondary_archetype TEXT;");
        }
        if (!groupCols.contains("dynamic_health_score")) {
            migQ.exec("ALTER TABLE groups ADD COLUMN dynamic_health_score REAL DEFAULT 1.0;");
        }

        // 4. Add columns to people: ministry_personality, personality_secondary
        QSqlQuery pragmaPeople("PRAGMA table_info(people);", m_db);
        QSet<QString> peopleCols;
        while (pragmaPeople.next()) peopleCols.insert(pragmaPeople.value(1).toString());
        if (!peopleCols.contains("ministry_personality")) {
            migQ.exec("ALTER TABLE people ADD COLUMN ministry_personality TEXT;");
        }
        if (!peopleCols.contains("personality_secondary")) {
            migQ.exec("ALTER TABLE people ADD COLUMN personality_secondary TEXT;");
        }

        // 5. Add columns to entity_relationships: connection_source, archetype_fit_score, joined_date
        QSqlQuery pragmaRel("PRAGMA table_info(entity_relationships);", m_db);
        QSet<QString> relCols;
        while (pragmaRel.next()) relCols.insert(pragmaRel.value(1).toString());
        if (!relCols.contains("connection_source")) {
            migQ.exec("ALTER TABLE entity_relationships ADD COLUMN connection_source TEXT NOT NULL DEFAULT 'manual';");
        }
        if (!relCols.contains("archetype_fit_score")) {
            migQ.exec("ALTER TABLE entity_relationships ADD COLUMN archetype_fit_score INTEGER NOT NULL DEFAULT 0;");
        }
        if (!relCols.contains("joined_date")) {
            migQ.exec("ALTER TABLE entity_relationships ADD COLUMN joined_date DATE DEFAULT (date('now'));");
        }

        // 6. Ensure the 11 canonical root ministries exist in groups table (Level 0 Blueprints)
        for (const auto& bp : MinistryArchetypeRegistry::canonicalBlueprints()) {
            QSqlQuery checkRoot(m_db);
            checkRoot.prepare("SELECT id FROM groups WHERE (category = ? OR name = ?) AND (parent_id IS NULL OR parent_id = 0) LIMIT 1;");
            checkRoot.addBindValue(bp.displayName);
            checkRoot.addBindValue(bp.displayName);
            if (checkRoot.exec() && !checkRoot.next()) {
                QSqlQuery insRoot(m_db);
                insRoot.prepare("INSERT INTO groups (name, category, description, ideal_primary_archetype, ideal_secondary_archetype, required_skills, recruitment_status) "
                                "VALUES (?, ?, ?, ?, ?, ?, 'Open');");
                insRoot.addBindValue(bp.displayName);
                insRoot.addBindValue(bp.displayName);
                insRoot.addBindValue(bp.description);
                insRoot.addBindValue(bp.primaryArchetype);
                insRoot.addBindValue(bp.secondaryArchetype);
                insRoot.addBindValue(bp.requiredGifts.join(", "));
                insRoot.exec();
            }
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 19: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(19)) {
            return false;
        }
        qInfo() << "Schema migration 19 applied successfully.";
    }

    // --- Schema Migration 20: pastoral_activities.group_id ---
    // The Synergy Engine's group-staleness guard queries
    // pastoral_activities.group_id, but the column was never added to the
    // schema — the subquery failed silently and the >180-day "stale group"
    // disqualifier never worked. Add it for existing databases.
    if (current < 20) {
        qInfo() << "Applying schema migration 20: pastoral_activities.group_id";
        if (!m_db.transaction()) {
            qCritical() << "Migration 20: failed to start transaction";
            return false;
        }
        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(pastoral_activities)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());
        if (!cols.contains("group_id")) {
            QSqlQuery migQ(m_db);
            if (!migQ.exec("ALTER TABLE pastoral_activities ADD COLUMN group_id INTEGER REFERENCES groups(id);")) {
                qCritical() << "Migration 20: add group_id failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        if (!m_db.commit()) {
            qCritical() << "Migration 20: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(20)) {
            return false;
        }
        qInfo() << "Schema migration 20 applied successfully.";
    }

    if (current < 21) {
        // Migration 21: ministry hide/unhide. Top-level ministries stay
        // system-locked against delete/archive, but a church can hide unused
        // ones (or any group) so they leave the tree, graph and Smart Bridge
        // without losing their data or child teams.
        qInfo() << "Applying schema migration 21: groups.is_hidden";
        if (!m_db.transaction()) {
            qCritical() << "Migration 21: failed to start transaction";
            return false;
        }
        QSqlQuery hasCol(m_db);
        hasCol.exec("PRAGMA table_info(groups)");
        QSet<QString> cols;
        while (hasCol.next()) cols.insert(hasCol.value(1).toString());
        if (!cols.contains("is_hidden")) {
            QSqlQuery migQ(m_db);
            if (!migQ.exec("ALTER TABLE groups ADD COLUMN is_hidden INTEGER NOT NULL DEFAULT 0;")) {
                qCritical() << "Migration 21: add is_hidden failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        if (!m_db.commit()) {
            qCritical() << "Migration 21: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(21)) {
            return false;
        }
        qInfo() << "Schema migration 21 applied successfully.";
    }

    // --- Schema Migration 22: ministry planning upgrades & events hub ---
    // Extends ministry_tasks with priority, category, progress_pct, and notes.
    // Creates ministry_events table for church-wide events, conferences,
    // retreats, and outreach programs with coordinator and facility links.
    if (current < 22) {
        qInfo() << "Applying schema migration 22: ministry planning upgrades & events hub";
        if (!m_db.transaction()) {
            qCritical() << "Migration 22: failed to start transaction";
            return false;
        }

        // 1. Upgrade ministry_tasks columns
        QSqlQuery taskInfo(m_db);
        taskInfo.exec("PRAGMA table_info(ministry_tasks)");
        QSet<QString> taskCols;
        while (taskInfo.next()) taskCols.insert(taskInfo.value(1).toString());

        QSqlQuery migQ(m_db);
        if (!taskCols.contains("priority")) {
            if (!migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN priority TEXT DEFAULT 'Medium';")) {
                qCritical() << "Migration 22: add priority failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        if (!taskCols.contains("category")) {
            if (!migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN category TEXT DEFAULT 'Task';")) {
                qCritical() << "Migration 22: add category failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        if (!taskCols.contains("progress_pct")) {
            if (!migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN progress_pct INTEGER DEFAULT 0;")) {
                qCritical() << "Migration 22: add progress_pct failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        if (!taskCols.contains("notes")) {
            if (!migQ.exec("ALTER TABLE ministry_tasks ADD COLUMN notes TEXT DEFAULT '';")) {
                qCritical() << "Migration 22: add notes failed:" << migQ.lastError().text();
                m_db.rollback();
                return false;
            }
        }

        // 2. Create ministry_events table
        QString createEvents =
            "CREATE TABLE IF NOT EXISTS ministry_events ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  title TEXT NOT NULL,"
            "  event_type TEXT DEFAULT 'Outreach',"
            "  team_id INTEGER REFERENCES groups(id),"
            "  coordinator_id INTEGER REFERENCES people(id),"
            "  facility_name TEXT DEFAULT '',"
            "  start_datetime TEXT NOT NULL,"
            "  end_datetime TEXT NOT NULL,"
            "  expected_attendance INTEGER DEFAULT 0,"
            "  budget_cents INTEGER DEFAULT 0,"
            "  status TEXT DEFAULT 'Confirmed',"
            "  description TEXT DEFAULT '',"
            "  service_id INTEGER REFERENCES services(id),"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        if (!migQ.exec(createEvents)) {
            qCritical() << "Migration 22: create ministry_events failed:" << migQ.lastError().text();
            m_db.rollback();
            return false;
        }

        // 3. Seed canonical church events if empty
        QSqlQuery cntEv("SELECT COUNT(*) FROM ministry_events", m_db);
        if (cntEv.next() && cntEv.value(0).toInt() == 0) {
            const int year = QDate::currentDate().year();
            struct SeedEv {
                const char* title;
                const char* type;
                const char* start;
                const char* end;
                int attendance;
                int budget;
                const char* facility;
                const char* desc;
            };
            const SeedEv seedEvents[] = {
                {"Easter Celebration & Community Outreach", "Worship / Service", "04-05 09:00", "04-05 13:00", 450, 250000, "Main Worship Sanctuary", "Full Easter production, choir, and family hospitality reception."},
                {"Youth Summer Discipleship Camp", "Youth / Children", "07-15 08:00", "07-18 17:00", 85, 450000, "Camp Cedar Ridge", "Annual 4-day student retreat focused on spiritual growth, worship, and leadership."},
                {"Annual Leadership & Ministry Vision Summit", "Training", "09-12 09:00", "09-12 16:00", 120, 150000, "Fellowship Hall", "Strategic planning, team alignment, and volunteer appreciation for all ministry leads."},
                {"Thanksgiving Community Food Drive", "Outreach", "11-21 10:00", "11-21 15:00", 250, 100000, "Outdoor Pavilion & Kitchen", "City-wide basket distribution and hot meal outreach for local families in need."},
                {"Christmas Eve Candlelight Service", "Worship / Service", "12-24 17:00", "12-24 19:30", 500, 180000, "Main Worship Sanctuary", "Candlelight communion, carols, nativity story, and welcome for first-time visitors."}
            };
            for (const auto& ev : seedEvents) {
                QSqlQuery ins(m_db);
                ins.prepare("INSERT INTO ministry_events (title, event_type, start_datetime, end_datetime, expected_attendance, budget_cents, facility_name, description) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
                ins.addBindValue(ev.title);
                ins.addBindValue(ev.type);
                ins.addBindValue(QString("%1-%2").arg(year).arg(ev.start));
                ins.addBindValue(QString("%1-%2").arg(year).arg(ev.end));
                ins.addBindValue(ev.attendance);
                ins.addBindValue(ev.budget);
                ins.addBindValue(ev.facility);
                ins.addBindValue(ev.desc);
                ins.exec();
            }
        }

        if (!m_db.commit()) {
            qCritical() << "Migration 22: commit failed";
            m_db.rollback();
            return false;
        }
        if (!recordSchemaVersion(22)) {
            return false;
        }
        qInfo() << "Schema migration 22 applied successfully.";
    }

    return true;
}

void DatabaseManager::seedServiceLibrary() {
    QSqlQuery cnt("SELECT COUNT(*) FROM service_library", m_db);
    if (cnt.next() && cnt.value(0).toInt() > 0) return;

    struct LibItem { const char* name; const char* type; int mins; };
    const LibItem items[] = {
        {"Amazing Grace", "Song", 4},
        {"How Great Thou Art", "Song", 5},
        {"10,000 Reasons", "Song", 4},
        {"Cornerstone", "Song", 5},
        {"Psalm 23", "Scripture", 2},
        {"John 3:16", "Scripture", 2},
        {"Romans 8:28", "Scripture", 2},
        {"Welcome & Announcements", "Media", 1},
        {"Kids Dismissal Slide", "Media", 1},
        {"Sermon Presentation", "Media", 1},
        {"Closing Slideshow", "Media", 1},
        {"Opening Thanksgiving", "Text", 3},
        {"Pastoral Intercession", "Text", 3},
        {"Sunday Sermon", "Text", 30},
        {"Benediction", "Text", 2},
    };
    for (const LibItem& it : items) {
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO service_library (name, type, duration_mins, is_custom) VALUES (?, ?, ?, 0)");
        q.addBindValue(QString::fromUtf8(it.name));
        q.addBindValue(QString::fromUtf8(it.type));
        q.addBindValue(it.mins);
        if (!q.exec()) {
            qWarning() << "Failed to seed service_library item" << it.name << ":" << q.lastError().text();
        }
    }
    qInfo() << "Seeded service element library with" << (sizeof(items) / sizeof(items[0])) << "items.";
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
        "pastoral_activities",   // -> people
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

