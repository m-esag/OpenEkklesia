#include "planningcenterclient.h"
#include "../database/databasemanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QProcess>
#include <QDate>
#include <QDebug>

PlanningCenterClient::PlanningCenterClient(QObject *parent)
    : QObject(parent)
{
}

void PlanningCenterClient::executeGet(const QString& endpoint, const QString& appId, const QString& secret,
                                      std::function<void(int, const QByteArray&, const QString&)> callback)
{
    QString url = "https://api.planningcenteronline.com" + endpoint;
    QString credentials = QString("%1:%2").arg(appId.trimmed(), secret.trimmed());

    QProcess* proc = new QProcess(this);
    QStringList args;
    args << "-s" << "-w" << "\n%{http_code}" << "-u" << credentials << url;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, callback](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        if (exitCode != 0) {
            callback(0, QByteArray(), proc->errorString());
            return;
        }

        QByteArray fullOut = proc->readAllStandardOutput();
        int lastNewline = fullOut.lastIndexOf('\n');
        if (lastNewline < 0) {
            callback(0, QByteArray(), "Malformed response from server");
            return;
        }

        QByteArray body = fullOut.left(lastNewline);
        int httpCode = fullOut.mid(lastNewline + 1).trimmed().toInt();
        callback(httpCode, body, QString());
    });

    proc->start("curl", args);
}

void PlanningCenterClient::testConnection(const QString& appId, const QString& secret,
                                          std::function<void(bool, const QString&)> callback)
{
    if (appId.trimmed().isEmpty() || secret.trimmed().isEmpty()) {
        callback(false, "App ID and Secret cannot be empty.");
        return;
    }

    executeGet("/people/v2/people?per_page=1", appId, secret, [callback](int httpCode, const QByteArray& body, const QString& err) {
        Q_UNUSED(body);
        if (!err.isEmpty()) {
            callback(false, QString("Network error: %1").arg(err));
        } else if (httpCode == 200) {
            callback(true, "Successfully authenticated with Planning Center API!");
        } else if (httpCode == 401) {
            callback(false, "HTTP 401 Unauthorized: Invalid Application ID or Secret Key.");
        } else if (httpCode == 403) {
            callback(false, "HTTP 403 Forbidden: Account does not have sufficient permissions.");
        } else {
            callback(false, QString("Server returned HTTP %1").arg(httpCode));
        }
    });
}

void PlanningCenterClient::fetchPeople(const QString& appId, const QString& secret,
                                       std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    auto allPeople = std::make_shared<QJsonArray>();

    auto fetchPage = std::make_shared<std::function<void(int)>>();
    *fetchPage = [this, appId, secret, allPeople, fetchPage, callback](int offset) mutable {
        QString endpoint = QString("/people/v2/people?per_page=100&offset=%1").arg(offset);
        executeGet(endpoint, appId, secret, [this, appId, secret, allPeople, fetchPage, callback, offset](int httpCode, const QByteArray& body, const QString& err) mutable {
            if (!err.isEmpty() || httpCode != 200) {
                if (allPeople->size() > 0) {
                    callback(true, *allPeople, QString());
                } else {
                    callback(false, QJsonArray(), err.isEmpty() ? QString("HTTP %1").arg(httpCode) : err);
                }
                fetchPage.reset(); // break the self-reference so the cycle frees
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(body);
            QJsonObject obj = doc.object();
            QJsonArray data = obj.value("data").toArray();

            for (const QJsonValue& v : data) {
                allPeople->append(v);
            }

            // Check if there are more records to paginate
            QJsonObject links = obj.value("links").toObject();
            if (data.size() == 100 && links.contains("next")) {
                (*fetchPage)(offset + 100);
            } else {
                callback(true, *allPeople, QString());
                fetchPage.reset(); // break the self-reference so the cycle frees
            }
        });
    };

    (*fetchPage)(0);
}

void PlanningCenterClient::fetchGroups(const QString& appId, const QString& secret,
                                       std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    executeGet("/groups/v2/groups?per_page=100", appId, secret, [callback](int httpCode, const QByteArray& body, const QString& err) {
        if (!err.isEmpty() || httpCode != 200) {
            callback(false, QJsonArray(), err.isEmpty() ? QString("HTTP %1").arg(httpCode) : err);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonObject obj = doc.object();
        QJsonArray data = obj.value("data").toArray();
        callback(true, data, QString());
    });
}

void PlanningCenterClient::fetchServicePlans(const QString& appId, const QString& secret,
                                             std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    executeGet("/services/v2/service_types?per_page=50", appId, secret, [callback](int httpCode, const QByteArray& body, const QString& err) {
        if (!err.isEmpty() || httpCode != 200) {
            callback(false, QJsonArray(), err.isEmpty() ? QString("HTTP %1").arg(httpCode) : err);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonObject obj = doc.object();
        QJsonArray data = obj.value("data").toArray();
        callback(true, data, QString());
    });
}

PcoImportSummary PlanningCenterClient::importPeopleData(const QJsonArray& people) {
    PcoImportSummary summary;
    QSqlDatabase db = DatabaseManager::instance().database();

    // Ensure default household exists for standalone members
    int defaultHouseholdId = 1;
    QSqlQuery qH(db);
    if (!qH.exec("SELECT id FROM households WHERE id = 1") || !qH.next()) {
        QSqlQuery insH(db);
        insH.exec("INSERT OR IGNORE INTO households (id, family_name, address_line1, city, zip_code) "
                  "VALUES (1, 'General / Unassigned', '', '', '')");
    }

    QSqlQuery q(db);
    for (const QJsonValue& val : people) {
        QJsonObject personObj = val.toObject();
        QJsonObject attrs = personObj.value("attributes").toObject();

        QString firstName = attrs.value("first_name").toString().trimmed();
        QString lastName = attrs.value("last_name").toString().trimmed();
        if (firstName.isEmpty() && lastName.isEmpty()) continue;

        QString gender = attrs.value("gender").toString();
        QString birthdate = attrs.value("birthdate").toString();
        QString membership = attrs.value("membership").toString("Member");
        if (membership.isEmpty()) membership = "Member";

        // Household assignment: the People endpoint does not carry household
        // relationships, so we deliberately do NOT auto-create "{Surname}
        // Family" households — that would wrongly merge unrelated people who
        // happen to share a last name. Everyone lands in the shared
        // "General / Unassigned" household until real household sync exists.
        int householdId = defaultHouseholdId;

        QString pcoId = personObj.value("id").toString();
        QString barcode = !pcoId.isEmpty() ? QString("PCO-%1").arg(pcoId) : QString();

        q.prepare("SELECT id FROM people WHERE first_name = ? AND last_name = ?");
        q.addBindValue(firstName);
        q.addBindValue(lastName);

        if (q.exec() && q.next()) {
            int existingId = q.value(0).toInt();
            QSqlQuery u(db);
            u.prepare("UPDATE people SET membership_status = :status, household_id = COALESCE(household_id, :hid), barcode = COALESCE(barcode, :bc) WHERE id = :id");
            u.bindValue(":status", membership);
            u.bindValue(":hid", householdId);
            u.bindValue(":bc", barcode);
            u.bindValue(":id", existingId);
            if (u.exec()) {
                summary.peopleCount++;
            } else {
                summary.errors.append(QString("Person update error (%1 %2): %3").arg(firstName, lastName, u.lastError().text()));
            }
        } else {
            QSqlQuery ins(db);
            ins.prepare("INSERT INTO people (household_id, first_name, last_name, gender, date_of_birth, membership_status, barcode) "
                        "VALUES (:hid, :fname, :lname, :gender, :dob, :status, :bc)");
            ins.bindValue(":hid", householdId);
            ins.bindValue(":fname", firstName);
            ins.bindValue(":lname", lastName);
            ins.bindValue(":gender", gender.isEmpty() ? "Unspecified" : gender);
            ins.bindValue(":dob", birthdate);
            ins.bindValue(":status", membership);
            ins.bindValue(":bc", barcode);
            if (ins.exec()) {
                summary.peopleCount++;
            } else {
                summary.errors.append(QString("Person insert error (%1 %2): %3").arg(firstName, lastName, ins.lastError().text()));
            }
        }
    }
    return summary;
}

PcoImportSummary PlanningCenterClient::importGroupsData(const QJsonArray& groups) {
    PcoImportSummary summary;
    QSqlDatabase db = DatabaseManager::instance().database();

    QSqlQuery q(db);
    for (const QJsonValue& val : groups) {
        QJsonObject grpObj = val.toObject();
        QJsonObject attrs = grpObj.value("attributes").toObject();

        QString name = attrs.value("name").toString().trimmed();
        if (name.isEmpty()) continue;

        QString description = attrs.value("description").toString();
        QString schedule = attrs.value("schedule").toString();

        q.prepare("SELECT id FROM groups WHERE name = ?");
        q.addBindValue(name);
        if (q.exec() && q.next()) {
            summary.groupsCount++;
            continue;
        }

        QSqlQuery ins(db);
        ins.prepare("INSERT INTO groups (name, description, meeting_day) "
                    "VALUES (:name, :desc, :schedule)");
        ins.bindValue(":name", name);
        ins.bindValue(":desc", description);
        ins.bindValue(":schedule", schedule.isEmpty() ? "Sunday" : schedule);
        if (ins.exec()) {
            summary.groupsCount++;
        } else {
            summary.errors.append(QString("Group error (%1): %2").arg(name, ins.lastError().text()));
        }
    }
    return summary;
}

void PlanningCenterClient::fetchGivingFunds(const QString& appId, const QString& secret,
                                            std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    executeGet("/giving/v2/funds?per_page=100", appId, secret, [callback](int httpCode, const QByteArray& body, const QString& err) {
        if (!err.isEmpty() || httpCode != 200) {
            callback(false, QJsonArray(), err.isEmpty() ? QString("HTTP %1").arg(httpCode) : err);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonObject obj = doc.object();
        QJsonArray data = obj.value("data").toArray();
        callback(true, data, QString());
    });
}

void PlanningCenterClient::fetchGivingDonations(const QString& appId, const QString& secret,
                                                std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    auto allDonations = std::make_shared<QJsonArray>();

    auto fetchPage = std::make_shared<std::function<void(int)>>();
    *fetchPage = [this, appId, secret, allDonations, fetchPage, callback](int offset) {
        QString endpoint = QString("/giving/v2/donations?per_page=100&offset=%1").arg(offset);
        executeGet(endpoint, appId, secret, [this, appId, secret, allDonations, fetchPage, callback, offset](int httpCode, const QByteArray& body, const QString& err) {
            if (!err.isEmpty() || httpCode != 200) {
                if (allDonations->size() > 0) {
                    callback(true, *allDonations, QString());
                } else {
                    QString errMsg = err.isEmpty() ? QString("HTTP %1 response from Giving API").arg(httpCode) : err;
                    callback(false, QJsonArray(), errMsg);
                }
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(body);
            QJsonObject obj = doc.object();
            QJsonArray data = obj.value("data").toArray();

            for (const QJsonValue& v : data) {
                allDonations->append(v);
            }

            // Check if there are more records to paginate
            QJsonObject links = obj.value("links").toObject();
            if (data.size() == 100 && links.contains("next")) {
                (*fetchPage)(offset + 100);
            } else {
                callback(true, *allDonations, QString());
            }
        });
    };

    (*fetchPage)(0);
}

PcoImportSummary PlanningCenterClient::importServicesData(const QJsonArray& plans) {
    PcoImportSummary summary;
    QSqlDatabase db = DatabaseManager::instance().database();

    for (const QJsonValue& val : plans) {
        QJsonObject planObj = val.toObject();
        QJsonObject attrs = planObj.value("attributes").toObject();
        QString name = attrs.value("name").toString().trimmed();
        if (name.isEmpty()) continue;

        QSqlQuery ins(db);
        ins.prepare("INSERT OR REPLACE INTO services (date, title, leader) VALUES (:date, :title, :leader)");
        ins.bindValue(":date", QDate::currentDate().toString("yyyy-MM-dd"));
        ins.bindValue(":title", name);
        ins.bindValue(":leader", "Planning Center Sync");
        if (ins.exec()) {
            summary.servicesCount++;
        } else {
            summary.errors.append(QString("Service error (%1): %2").arg(name, ins.lastError().text()));
        }
    }
    return summary;
}

PcoImportSummary PlanningCenterClient::importGivingData(const QJsonArray& funds, const QJsonArray& donations) {
    PcoImportSummary summary;
    QSqlDatabase db = DatabaseManager::instance().database();

    // 1. Import Funds & Map PCO Fund ID -> Local Fund ID
    QMap<QString, int> pcoFundIdToLocalId;
    for (const QJsonValue& fVal : funds) {
        QJsonObject fObj = fVal.toObject();
        QString pcoFundId = fObj.value("id").toString();
        QJsonObject attrs = fObj.value("attributes").toObject();
        QString fundName = attrs.value("name").toString().trimmed();
        if (fundName.isEmpty()) continue;

        QSqlQuery q(db);
        q.prepare("SELECT id FROM funds WHERE fund_name = ?");
        q.addBindValue(fundName);
        if (q.exec() && q.next()) {
            pcoFundIdToLocalId[pcoFundId] = q.value(0).toInt();
            summary.fundsCount++;
        } else {
            QSqlQuery ins(db);
            ins.prepare("INSERT INTO funds (fund_name, is_active) VALUES (?, 1)");
            ins.addBindValue(fundName);
            if (ins.exec()) {
                pcoFundIdToLocalId[pcoFundId] = ins.lastInsertId().toInt();
                summary.fundsCount++;
            } else {
                summary.errors.append(QString("Fund error (%1): %2").arg(fundName, ins.lastError().text()));
            }
        }
    }

    // Default Fund ID fallback
    int defaultFundId = 1;
    QSqlQuery qDef("SELECT id FROM funds ORDER BY id ASC LIMIT 1");
    if (qDef.next()) {
        defaultFundId = qDef.value(0).toInt();
    }

    // Default Person ID fallback
    int defaultPersonId = 1;
    QSqlQuery qPerson("SELECT id FROM people ORDER BY id ASC LIMIT 1");
    if (qPerson.next()) {
        defaultPersonId = qPerson.value(0).toInt();
    }

    // 2. Build map of PCO Person ID -> Local Person ID (via barcode "PCO-<id>")
    QMap<QString, int> pcoPersonIdToLocalId;
    QSqlQuery qPcoPeople("SELECT id, barcode FROM people WHERE barcode LIKE 'PCO-%'");
    while (qPcoPeople.next()) {
        int locId = qPcoPeople.value(0).toInt();
        QString bc = qPcoPeople.value(1).toString();
        QString pcoId = bc.mid(4); // strip "PCO-"
        pcoPersonIdToLocalId[pcoId] = locId;
    }

    // 3. Import Donations & Contributions
    for (const QJsonValue& dVal : donations) {
        QJsonObject dObj = dVal.toObject();
        QJsonObject attrs = dObj.value("attributes").toObject();
        QJsonObject relationships = dObj.value("relationships").toObject();

        // Match donor Person ID
        int personId = defaultPersonId;
        QJsonObject personRel = relationships.value("person").toObject().value("data").toObject();
        QString pcoPersonId = personRel.value("id").toString();
        if (!pcoPersonId.isEmpty() && pcoPersonIdToLocalId.contains(pcoPersonId)) {
            personId = pcoPersonIdToLocalId.value(pcoPersonId);
        }

        // Match Fund ID from designations relationship if present
        int fundId = defaultFundId;
        QJsonObject desigRel = relationships.value("designations").toObject().value("data").toObject();
        QString desigFundId = desigRel.value("id").toString();
        if (pcoFundIdToLocalId.contains(desigFundId)) {
            fundId = pcoFundIdToLocalId.value(desigFundId);
        }

        int amountCents = attrs.value("amount_cents").toInt(0);
        if (amountCents <= 0) {
            double amountDollars = attrs.value("amount").toDouble(0.0);
            amountCents = static_cast<int>(amountDollars * 100.0);
        }
        if (amountCents <= 0) continue;

        QString receivedAt = attrs.value("received_at").toString();
        QString date = receivedAt.left(10);
        if (date.isEmpty()) date = QDate::currentDate().toString("yyyy-MM-dd");

        QString paymentMethod = attrs.value("payment_method").toString("Online");
        if (paymentMethod.isEmpty()) paymentMethod = "Online";
        QString checkNumber = attrs.value("check_number").toString();

        QSqlQuery ins(db);
        ins.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number) "
                    "VALUES (:pid, :fid, :amount, :date, :method, :chk)");
        ins.bindValue(":pid", personId);
        ins.bindValue(":fid", fundId);
        ins.bindValue(":amount", amountCents);
        ins.bindValue(":date", date);
        ins.bindValue(":method", paymentMethod);
        ins.bindValue(":chk", checkNumber);

        if (ins.exec()) {
            summary.donationsCount++;
        } else {
            summary.errors.append(QString("Donation import error: %1").arg(ins.lastError().text()));
        }
    }

    return summary;
}
