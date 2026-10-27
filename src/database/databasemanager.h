#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>
#include <QSqlDatabase>

class DatabaseManager {
public:
    static DatabaseManager& instance();
    bool initialize(const QString& dbPath = QString());
    void close();

    // Replace the current database with a previously exported .db backup.
    // Closes the live connection, swaps the file, and re-initializes. Callers
    // should warn the user to restart afterward so in-memory models reload.
    bool restoreFromBackup(const QString& backupPath);

    // Snapshot the live database to destPath. Runs a WAL checkpoint first so
    // the copy is complete (a raw file copy of a WAL-mode DB misses data still
    // held in the write-ahead log).
    bool exportBackup(const QString& destPath);

    QSqlDatabase database() const;

    // Canonical vault directory: always the DB's own directory + "/vault" so
    // documents share the app data root regardless of how the DB path was
    // resolved. Pure path computation — callers own mkdir/permissions.
    static QString vaultDirPath();

    bool purgeDemoData();

    // Delete audit_log / member_history rows older than the given retention
    // windows. A keepDays value <= 0 means "keep forever" (that table is
    // skipped). Both deletes share one transaction and roll back together on
    // failure; the returned counts are the rows actually removed.
    struct RetentionResult {
        int auditDeleted = 0;
        int historyDeleted = 0;
    };
    RetentionResult pruneHistory(int auditKeepDays, int historyKeepDays);

    static void logAudit(const QString& tableName, int recordId, const QString& fieldName, const QString& oldValue, const QString& newValue, const QString& changedBy = "system");
    static void logMemberHistory(int personId, const QString& activityType, const QString& details);

    // Monotonic counter bumped whenever data that can affect the Connection
    // Graph changes (people, groups, households, relationships). The graph
    // compares it against the value recorded at its last full load to skip
    // redundant rebuilds. Cheap: one integer increment.
    static quint64 dataGeneration();
    static void bumpDataGeneration();

    // Ensure all critical columns and indices exist across people, groups, and entity_relationships
    static void ensureSchemaIntegrity(QSqlDatabase db = QSqlDatabase::database());

    // Ensure all blueprint ministries (canonical and custom) have a Level 0 top-level heading group
    static bool ensureBlueprintHeadings(QSqlDatabase db = QSqlDatabase::database());

    // Create a new blueprint ministry and immediately generate its top-level heading group
    static bool createBlueprint(const QString& categoryKey, const QString& displayName,
                                const QString& primaryArchetype, const QString& secondaryArchetype,
                                const QStringList& requiredGifts, const QString& description,
                                int* outBlueprintId = nullptr, int* outGroupId = nullptr,
                                QSqlDatabase db = QSqlDatabase::database());

    // Update an existing blueprint ministry and synchronize its top-level heading group
    static bool updateBlueprint(int blueprintId, const QString& categoryKey, const QString& displayName,
                                const QString& primaryArchetype, const QString& secondaryArchetype,
                                const QStringList& requiredGifts, const QString& description,
                                QSqlDatabase db = QSqlDatabase::database());

    // Archive or restore a blueprint ministry, its top-level group, and cascade to child sub-teams
    static bool archiveBlueprint(int blueprintId, bool archive,
                                 QSqlDatabase db = QSqlDatabase::database());

    // Highest schema version known to this build. migrateSchema() will run any
    // migrations between the DB's recorded version and this number, in order.
    static constexpr int kLatestSchemaVersion = 26;

private:
    DatabaseManager() = default;
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();
    bool ensureSchemaVersionTable();
    bool migrateSchema();
    int  readSchemaVersion();
    bool recordSchemaVersion(int version);
    void applyPragmas();
    void seedInitialData();
    void seedServiceLibrary();

    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H
