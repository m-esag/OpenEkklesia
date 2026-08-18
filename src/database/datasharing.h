#ifndef DATASHARING_H
#define DATASHARING_H

#include <QJsonObject>
#include <QStringList>

class QSqlDatabase;

// Selective data export/import ("split sharing") so two machines (e.g. church
// office and pastor) can exchange a subset of the database via a portable file
// (USB drive, etc.) rather than the whole database.
namespace DataSharing {

// Human-readable scope identifiers, in display order.
QStringList availableScopes();

// The table list for a scope, ordered parent-before-child for import.
QStringList tablesForScope(const QString& scope);

// Serialize every row/column of the given tables into a JSON document.
QJsonObject exportTables(const QSqlDatabase& db, const QStringList& tables);

// Import a previously exported document (INSERT OR REPLACE by id). Runs inside
// a transaction with foreign-key checks deferred so parent/child ordering can
// never cause a partial import.
bool importDocument(QSqlDatabase& db, const QJsonObject& doc);

} // namespace DataSharing

#endif // DATASHARING_H
