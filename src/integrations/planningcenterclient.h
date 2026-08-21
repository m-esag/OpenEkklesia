#ifndef PLANNINGCENTERCLIENT_H
#define PLANNINGCENTERCLIENT_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

struct PcoImportSummary {
    int peopleCount = 0;
    int householdsCount = 0;
    int groupsCount = 0;
    int servicesCount = 0;
    int fundsCount = 0;
    int donationsCount = 0;
    int donationsSkipped = 0;   // already imported (external_id match) or unmatchable
    QStringList errors;
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
                              std::function<void(bool success, const QJsonArray& donationsData, const QString& error)> callback);

    // Local SQLite database importer methods
    PcoImportSummary importPeopleData(const QJsonArray& people);
    PcoImportSummary importGroupsData(const QJsonArray& groups);
    PcoImportSummary importServicesData(const QJsonArray& plans);
    PcoImportSummary importGivingData(const QJsonArray& funds, const QJsonArray& donations);

private:
    void executeGet(const QString& endpoint, const QString& appId, const QString& secret,
                    std::function<void(int httpCode, const QByteArray& response, const QString& error)> callback);
};

#endif // PLANNINGCENTERCLIENT_H
