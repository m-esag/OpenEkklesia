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
    int fundsCount = 0;
    int donationsCount = 0;
    int donationsSkipped = 0;   // already imported (external_id match) or unmatchable
    int designationsMapped = 0; // donations whose designation resolved to a real fund
    QStringList errors;
};

// One donation parsed out of a page of the Planning Center Giving feed.
// `designation` is the human-readable name of the donation's Designation
// resource, resolved from the JSON:API `included` array requested with
// `&include=designations`; it is empty when the donation has no designation or
// when the API did not return the included resource. Keeping the name on the
// record is what lets importGivingData map a gift to the matching local fund
// instead of everything silently landing in "Imported (Unmapped)".
struct PcoDonationRecord {
    QString externalId;           // "PCO-<donation id>" — dedup key for contributions
    QString personPcoId;          // Person id from relationships.person
    QString designation;          // Designation attributes.name (may be empty)
    QString designationFundPcoId; // Fund id from the Designation's own fund relationship
    qint64 amountCents = 0;
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
    // `data` array and `included` its JSON:API `included` array (Designation
    // resources requested via &include=designations). Pure function, no I/O, so
    // the payload → record mapping is directly testable.
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
