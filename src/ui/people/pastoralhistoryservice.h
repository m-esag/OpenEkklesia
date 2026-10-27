#pragma once
#include <QString>
#include <QList>
#include <QSqlDatabase>

struct PastoralHistoryEntry {
    int id = 0;
    QString timestamp;
    QString type;
    QString text;
    bool followUp = false;
    QString sourceTable;
};

class PastoralHistoryService {
public:
    static QList<PastoralHistoryEntry> fetch(int personId, const QSqlDatabase& db);
    static QString badge(const QString& type);
};
