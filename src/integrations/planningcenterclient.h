#ifndef PLANNINGCENTERCLIENT_H
#define PLANNINGCENTERCLIENT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <functional>

struct PcoImportSummary {
    int peopleCount = 0;
    int householdsCount = 0;
    int groupsCount = 0;
    int servicesCount = 0;
    int fundsCount = 0;        // new funds created by this import (matches are not counted)
    int donationsCount = 0;
    int donationsSkipped = 0;   // already imported (external_id match) or unmatchable
    int designationsMapped = 0; // contributions attributed to a real designation fund
    QStringList errors;
};

// One fund split of a donation. A gift given entirely to one fund has a single
// part; a gift split across funds (Planning Center allows several Designations
// per donation) has one part per fund, each with its own amount.
struct PcoDesignationPart {
    QString designationPcoId;      // Designation resource id (dedup key component)
    QString designation;           // human-readable fund/designation name (may be empty)
    QString designationFundPcoId;  // Fund id from the Designation's own fund relationship
    qint64 amountCents = 0;        // Designation attributes.amount_cents (portion of the gift)
};

// One donation parsed out of a page of the Planning Center Giving feed.
// `designation`/`designationFundPcoId` mirror the first part for convenience;
// `parts` carries every fund split of the gift. Names come from the JSON:API
// `included` array requested with `&include=designations.fund` (the Giving API
// keeps the display name on the Fund resource). They are empty when the API did
// not return the included resources, which is what falls back to
// "Imported (Unmapped)" instead of misattributing a gift.
struct PcoDonationRecord {
    QString externalId;           // "PCO-<donation id>" — dedup key for contributions
    QString personPcoId;          // Person id from relationships.person
    QString designation;          // first part's human-readable name (may be empty)
    QString designationFundPcoId; // Fund id of the first part's designation
    QList<PcoDesignationPart> parts; // one entry per Designation (fund split)
    qint64 amountCents = 0;       // Donation total (sum of all designation amounts)
    QString date;                 // yyyy-MM-dd from attributes.received_at (may be empty)
    QString paymentMethod;
    QString checkNumber;
};

class PlanningCenterClient : public QObject {
    Q_OBJECT

public:
    explicit PlanningCenterClient(QObject *parent = nullptr);
    ~PlanningCenterClient() override = default;

    void testConnection(const QString& appId, const QString& secret,
                        std::function<void(bool success, const QString& message)> callback);

    void fetchPeople(const QString& appId, const QString& secret,
                     std::function<void(bool success, const QJsonArray& peopleData, const QString& error)> callback);

    void fetchGroups(const QString& appId, const QString& secret,
                     std::function<void(bool success, const QJsonArray& groupsData, const QString& error)> callback);

    void fetchServicePlans(const QString& appId, const QString& secret,
                           std::function<void(bool success, const QJsonArray& plansData, const QString& error)> callback);

    void fetchGivingFunds(const QString& appId, const QString& secret,
                          std::function<void(bool success, const QJsonArray& fundsData, const QString& error)> callback);

    void fetchGivingDonations(const QString& appId, const QString& secret,
                              std::function<void(bool success, const QList<PcoDonationRecord>& donations, const QString& error)> callback);

    // Parse one page of the Giving donations feed. `donations` is the page's
    // `data` array and `included` its JSON:API `included` array (Designation and
    // Fund resources requested via &include=designations.fund). Pure function,
    // no I/O, so the payload → record mapping is directly testable.
    static QList<PcoDonationRecord> parseDonationRecords(const QJsonArray& donations,
                                                         const QJsonArray& included);

    // Local SQLite database importer methods
    PcoImportSummary importPeopleData(const QJsonArray& people);
    PcoImportSummary importGroupsData(const QJsonArray& groups);
    PcoImportSummary importServicesData(const QJsonArray& plans);
    PcoImportSummary importGivingData(const QJsonArray& funds, const QList<PcoDonationRecord>& donations);

private:
    void executeGet(const QString& endpoint, const QString& appId, const QString& secret,
                    std::function<void(int httpCode, const QByteArray& response, const QString& error)> callback);
};

#endif // PLANNINGCENTERCLIENT_H
