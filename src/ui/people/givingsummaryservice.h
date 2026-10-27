#pragma once
#include <QString>
#include <QList>
#include <QSqlDatabase>

struct PersonGiftRecent {
    QString date;
    QString fund;
    double amount;
    QString payment;
    QString note;
};

struct PersonGivingSummary {
    double total = 0.0;
    int count = 0;
    QString lastDate = QStringLiteral("—");
    int spouseId = 0;
    double householdTotal = 0.0;
    bool householdTotalAvailable = false;
    QList<PersonGiftRecent> recent;
};

// Phase 1 wave 2 extraction: all SQL for the profile's Giving tab lives here so
// it can be unit-tested headlessly without the PeopleWidget chrome.
class GivingSummaryService {
public:
    static PersonGivingSummary fetch(int personId, const QSqlDatabase& db);
};
