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

    bool purgeDemoData();

    static void logAudit(const QString& tableName, int recordId, const QString& fieldName, const QString& oldValue, const QString& newValue, const QString& changedBy = "system");
    static void logMemberHistory(int personId, const QString& activityType, const QString& details);

    // Highest schema version known to this build. migrateSchema() will run any
    // migrations between the DB's recorded version and this number, in order.
    static constexpr int kLatestSchemaVersion = 9;

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
    void seedInitialData();
    void seedServiceLibrary();

    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H
