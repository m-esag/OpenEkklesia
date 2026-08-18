#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>
#include <QSqlDatabase>

class DatabaseManager {
public:
    static DatabaseManager& instance();
    bool initialize(const QString& dbPath = QString());
    void close();

    QSqlDatabase database() const;

    bool purgeDemoData();

    static void logAudit(const QString& tableName, int recordId, const QString& fieldName, const QString& oldValue, const QString& newValue, const QString& changedBy = "system");
    static void logMemberHistory(int personId, const QString& activityType, const QString& details);

    // Highest schema version known to this build. migrateSchema() will run any
    // migrations between the DB's recorded version and this number, in order.
    static constexpr int kLatestSchemaVersion = 6;

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

    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H
