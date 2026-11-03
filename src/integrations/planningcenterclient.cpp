#include "planningcenterclient.h"
#include "../database/databasemanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QProcess>
#include <QDate>
#include <QDebug>
#include <memory>

PlanningCenterClient::PlanningCenterClient(QObject *parent)
    : QObject(parent)
{
}

namespace {
bool parsePcoDataArray(const QByteArray& body, QJsonArray& out, QString& errText) {
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        errText = QString("Invalid server response: %1").arg(perr.errorString());
        return false;
    }
    out = doc.object().value("data").toArray();
    return true;
}
} // namespace

void PlanningCenterClient::executeGet(const QString& endpoint, const QString& appId, const QString& secret,
                                      std::function<void(int, const QByteArray&, const QString&)> callback)
{
    const QString url = "https://api.planningcenteronline.com" + endpoint;
    const QString credentials = QString("%1:%2").arg(appId.trimmed(), secret.trimmed());

    QProcess* proc = new QProcess(this);
    QStringList args;
    args << "-s" << "-w" << "\n%{http_code}" << "-u" << credentials << url;

    // The callback must run exactly once per request. A failed start emits only
    // errorOccurred() and never finished(); a crash emits both. Without this
    // guard (and without the error handler) a missing/failed curl left callers
    // waiting forever and the import dialog hung at "Fetching ...".
    auto delivered = std::make_shared<bool>(false);
    auto deliver = [callback, delivered](int httpCode, const QByteArray& body, const QString& err) {
        if (*delivered) return;
        *delivered = true;
        callback(httpCode, body, err);
    };

    connect(proc, &QProcess::errorOccurred, this, [proc, deliver](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return; // finished() reports the rest
        const QString reason = proc->errorString();
        proc->deleteLater();
        deliver(0, QByteArray(), QString("Could not run 'curl': %1").arg(reason));
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, deliver](int exitCode, QProcess::ExitStatus) {
        const QString procError = proc->errorString();
        const QByteArray fullOut = proc->readAllStandardOutput();
        proc->deleteLater();

        if (exitCode != 0) {
            deliver(0, QByteArray(), procError.isEmpty()
                                           ? QString("curl exited with code %1").arg(exitCode)
                                           : procError);
            return;
        }

        const int lastNewline = fullOut.lastIndexOf('\n');
        if (lastNewline < 0) {
            deliver(0, QByteArray(), "Malformed response from server");
            return;
        }

        const QByteArray body = fullOut.left(lastNewline);
        const int httpCode = fullOut.mid(lastNewline + 1).trimmed().toInt();
        deliver(httpCode, body, QString());
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
                // Never report a truncated roster as success: a mid-pagination
                // failure would silently drop everyone on later pages and the
                // importer would treat the partial list as the whole directory.
                callback(false, QJsonArray(), err.isEmpty()
                    ? QString("HTTP %1 while fetching people at offset %2").arg(httpCode).arg(offset)
                    : err);
                fetchPage.reset(); // break the self-reference so the cycle frees
                return;
            }

            QJsonParseError perr;
            QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
            if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
                callback(false, QJsonArray(), QString("Invalid server response: %1").arg(perr.errorString()));
                fetchPage.reset();
                return;
            }
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
        QJsonArray data;
        QString perr;
        if (!parsePcoDataArray(body, data, perr)) {
            callback(false, QJsonArray(), perr);
            return;
        }
        callback(true, data, QString());
    });
}

void PlanningCenterClient::fetchServicePlans(const QString& appId, const QString& secret,
                                             std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    // A service type ("Sunday Morning") is not a plan: the old code returned the
    // service_types list and the importer dated every type with "today", so one
    // import collapsed every service into a single bogus row. Fetch each type's
    // plans instead so the importer receives real plan records with real dates.
    auto types = std::make_shared<QJsonArray>();
    auto plans = std::make_shared<QJsonArray>();
    auto index = std::make_shared<int>(0);
    auto failed = std::make_shared<bool>(false);
    auto fetchPage = std::make_shared<std::function<void()>>();

    // mutable: the inner executeGet callback below captures fetchPage from
    // this closure; without mutable this closure's member is const, and the
    // inner lambda inherits a const shared_ptr whose reset() is ill-formed
    // (GCC rejects it; MSVC does not).
    *fetchPage = [this, appId, secret, types, plans, index, failed, fetchPage, callback]() mutable {
        if (*failed) return;
        if (*index >= types->size()) {
            callback(true, *plans, QString());
            return;
        }
        const QJsonObject typeObj = types->at(*index).toObject();
        const QString typeId = typeObj.value("id").toString();
        const QString typeName = typeObj.value("attributes").toObject().value("name").toString();
        const QString endpoint = QString("/services/v2/service_types/%1/plans?per_page=50").arg(typeId);
        executeGet(endpoint, appId, secret,
                   [plans, index, failed, fetchPage, callback, typeName]
                   (int httpCode, const QByteArray& body, const QString& err) mutable {
            if (!err.isEmpty() || httpCode != 200) {
                *failed = true;
                fetchPage.reset();
                callback(false, QJsonArray(), err.isEmpty()
                    ? QString("HTTP %1 while fetching plans for service type '%2'").arg(httpCode).arg(typeName)
                    : QString("%1 (service type '%2')").arg(err, typeName));
                return;
            }
            QJsonArray data;
            QString perr;
            if (!parsePcoDataArray(body, data, perr)) {
                *failed = true;
                fetchPage.reset();
                callback(false, QJsonArray(), perr);
                return;
            }
            for (const QJsonValue& v : data) {
                plans->append(v);
            }
            ++(*index);
            (*fetchPage)();
        });
    };

    executeGet("/services/v2/service_types?per_page=50", appId, secret,
               [this, appId, secret, types, fetchPage, callback, failed]
               (int httpCode, const QByteArray& body, const QString& err) {
        if (!err.isEmpty() || httpCode != 200) {
            callback(false, QJsonArray(), err.isEmpty()
                ? QString("HTTP %1 while listing service types").arg(httpCode)
                : err);
            return;
        }
        QString perr;
        if (!parsePcoDataArray(body, *types, perr)) {
            callback(false, QJsonArray(), perr);
            return;
        }
        if (types->isEmpty()) {
            callback(true, QJsonArray(), QString());
            return;
        }
        (*fetchPage)();
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
        QJsonArray data;
        QString perr;
        if (!parsePcoDataArray(body, data, perr)) {
            callback(false, QJsonArray(), perr);
            return;
        }
        callback(true, data, QString());
    });
}

QList<PcoDonationRecord> PlanningCenterClient::parseDonationRecords(const QJsonArray& donations,
                                                                    const QJsonArray& included)
{
    // Index the JSON:API sideloaded resources by id so each donation's
    // designation id can be resolved. `include=designations.fund` returns both
    // Designation and Fund resources; the Giving API keeps the human-readable
    // name on the Fund, and each Designation carries the amount of the gift that
    // goes to that fund (a gift may be split across several designations).
    QHash<QString, QJsonObject> includedDesignations;
    QHash<QString, QString> includedFundNames;
    for (const QJsonValue& incVal : included) {
        const QJsonObject inc = incVal.toObject();
        const QString type = inc.value("type").toString();
        const QString id = inc.value("id").toString();
        if (id.isEmpty()) continue;
        if (type == "Designation") {
            includedDesignations.insert(id, inc);
        } else if (type == "Fund") {
            const QString name = inc.value("attributes").toObject()
                                     .value("name").toString().trimmed();
            if (!name.isEmpty()) includedFundNames.insert(id, name);
        }
    }

    // A relationship payload is normally a to-one object, but to-many
    // relationships (for example a donation's designations) use an array.
    auto relationshipIds = [](const QJsonValue& relationship) {
        QStringList ids;
        const QJsonValue data = relationship.toObject().value("data");
        if (data.isArray()) {
            for (const QJsonValue& entry : data.toArray()) {
                const QString id = entry.toObject().value("id").toString();
                if (!id.isEmpty()) ids.append(id);
            }
        } else {
            const QString id = data.toObject().value("id").toString();
            if (!id.isEmpty()) ids.append(id);
        }
        return ids;
    };
    auto firstRelationshipId = [&relationshipIds](const QJsonValue& relationship) {
        const QStringList ids = relationshipIds(relationship);
        return ids.isEmpty() ? QString() : ids.first();
    };

    QList<PcoDonationRecord> records;
    for (const QJsonValue& dVal : donations) {
        const QJsonObject dObj = dVal.toObject();
        const QString donationId = dObj.value("id").toString();
        if (donationId.isEmpty()) continue;

        PcoDonationRecord rec;
        rec.externalId = QString("PCO-%1").arg(donationId);

        const QJsonObject relationships = dObj.value("relationships").toObject();
        rec.personPcoId = firstRelationshipId(relationships.value("person"));

        QStringList designationIds = relationshipIds(relationships.value("designations"));
        if (designationIds.isEmpty()) {
            // Accept a to-one `designation` payload too (older/other feeds).
            designationIds = relationshipIds(relationships.value("designation"));
        }

        const QJsonObject attrs = dObj.value("attributes").toObject();
        qint64 amountCents = attrs.value("amount_cents").toInteger(0);
        if (amountCents <= 0) {
            const double amountDollars = attrs.value("amount").toDouble(0.0);
            amountCents = qRound64(amountDollars * 100.0);
        }
        rec.amountCents = amountCents;

        for (const QString& designationId : designationIds) {
            PcoDesignationPart part;
            part.designationPcoId = designationId;
            const auto desigIt = includedDesignations.constFind(designationId);
            if (desigIt != includedDesignations.constEnd()) {
                const QJsonObject desig = desigIt.value();
                const QJsonObject desigAttrs = desig.value("attributes").toObject();
                part.designation = desigAttrs.value("name").toString().trimmed();
                part.designationFundPcoId = firstRelationshipId(
                    desig.value("relationships").toObject().value("fund"));
                // The Designation resource often carries no `name`; fall back to
                // the name of the Fund it points at when that fund was sideloaded.
                if (part.designation.isEmpty() && !part.designationFundPcoId.isEmpty()) {
                    part.designation = includedFundNames.value(part.designationFundPcoId);
                }
                part.amountCents = desigAttrs.value("amount_cents").toInteger(0);
            }
            // Last resort for payloads that inline the fund on the donation.
            if (part.designationFundPcoId.isEmpty()) {
                part.designationFundPcoId = firstRelationshipId(relationships.value("fund"));
            }
            rec.parts.append(part);
        }
        if (rec.parts.isEmpty()) {
            // Gifts without designation details still import; the importer
            // attributes them to "Imported (Unmapped)" rather than dropping them.
            PcoDesignationPart part;
            part.designationFundPcoId = firstRelationshipId(relationships.value("fund"));
            rec.parts.append(part);
        }
        // A single designation owns the whole gift when the API omits per-part
        // amounts; split gifts trust each Designation's own amount.
        if (rec.parts.size() == 1 && rec.parts.first().amountCents <= 0) {
            rec.parts.first().amountCents = rec.amountCents;
        }
        // Convenience mirrors of the first part (kept for callers/tests).
        rec.designation = rec.parts.first().designation;
        rec.designationFundPcoId = rec.parts.first().designationFundPcoId;

        rec.date = attrs.value("received_at").toString().left(10);
        rec.paymentMethod = attrs.value("payment_method").toString();
        rec.checkNumber = attrs.value("check_number").toString();

        records.append(rec);
    }
    return records;
}

void PlanningCenterClient::fetchGivingDonations(const QString& appId, const QString& secret,
                                                std::function<void(bool, const QList<PcoDonationRecord>&, const QString&)> callback)
{
    auto allDonations = std::make_shared<QList<PcoDonationRecord>>();
    // Sideloading designations (and the funds that name them) is what lets a
    // gift be attributed to the right fund. If the deployed API rejects the
    // include parameter, fall back once to a plain donations fetch instead of
    // failing the whole sync: the import then uses the "Imported (Unmapped)"
    // fallback, which is the pre-existing behavior.
    auto includeDesignations = std::make_shared<bool>(true);

    auto fetchPage = std::make_shared<std::function<void(int)>>();
    *fetchPage = [this, appId, secret, allDonations, includeDesignations, fetchPage, callback](int offset) mutable {
        QString endpoint = QString("/giving/v2/donations?per_page=100&offset=%1").arg(offset);
        if (*includeDesignations) endpoint += "&include=designations.fund";
        executeGet(endpoint, appId, secret,
                   [allDonations, includeDesignations, fetchPage, callback, offset]
                   (int httpCode, const QByteArray& body, const QString& err) mutable {
            // A 4xx on the first page usually means the API does not accept the
            // include parameter: retry the feed once without it.
            if (*includeDesignations && offset == 0 && err.isEmpty()
                && (httpCode == 400 || httpCode == 422)) {
                *includeDesignations = false;
                allDonations->clear();
                (*fetchPage)(0);
                return;
            }
            if (!err.isEmpty() || httpCode != 200) {
                // Never report a partially paginated giving run as success. The
                // importer must not treat a truncated list as the full ledger.
                fetchPage.reset(); // break the self-reference so the cycle frees
                callback(false, QList<PcoDonationRecord>(), err.isEmpty()
                    ? QString("HTTP %1 response from Giving API while fetching donations at offset %2").arg(httpCode).arg(offset)
                    : err);
                return;
            }

            QJsonParseError dperr;
            QJsonDocument doc = QJsonDocument::fromJson(body, &dperr);
            if (dperr.error != QJsonParseError::NoError || !doc.isObject()) {
                fetchPage.reset();
                callback(false, QList<PcoDonationRecord>(), QString("Invalid server response: %1").arg(dperr.errorString()));
                return;
            }
            const QJsonObject obj = doc.object();
            const QJsonArray data = obj.value("data").toArray();

            allDonations->append(parseDonationRecords(data, obj.value("included").toArray()));

            // Check if there are more records to paginate
            QJsonObject links = obj.value("links").toObject();
            if (data.size() == 100 && links.contains("next")) {
                (*fetchPage)(offset + 100);
            } else {
                fetchPage.reset(); // break the self-reference so the cycle frees
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
        QString name = attrs.value("title").toString().trimmed();
        if (name.isEmpty()) name = attrs.value("name").toString().trimmed();
        if (name.isEmpty()) continue;

        // Use the plan's own date (plans carry `sort_date`, sometimes `date`).
        // The old code stamped every plan with QDate::currentDate(), collapsing
        // the whole Planning Center history into one bogus "today" service and
        // overwriting the row for that date. Never invent a date: a plan without
        // one is skipped and reported.
        QString rawDate = attrs.value("sort_date").toString().trimmed();
        if (rawDate.isEmpty()) rawDate = attrs.value("date").toString().trimmed();
        const QDate planDate = QDate::fromString(rawDate.left(10), "yyyy-MM-dd");
        if (!planDate.isValid()) {
            summary.errors.append(QString("Service plan '%1' skipped: Planning Center returned no valid date.").arg(name));
            continue;
        }
        const QString date = planDate.toString("yyyy-MM-dd");

        // services.date is UNIQUE (one service per date). Never overwrite a
        // different plan that already occupies the date: the old code updated
        // whatever row sat on QDate::currentDate(), silently replacing its title.
        QSqlQuery chk(db);
        chk.prepare("SELECT id, COALESCE(title, '') FROM services WHERE date = ?");
        chk.addBindValue(date);
        if (chk.exec() && chk.next()) {
            const int existingId = chk.value(0).toInt();
            const QString existingTitle = chk.value(1).toString().trimmed();
            if (!existingTitle.isEmpty() && existingTitle.compare(name, Qt::CaseInsensitive) != 0) {
                summary.errors.append(QString("Service plan '%1' for %2 not imported: that date already holds '%3'.")
                                          .arg(name, date, existingTitle));
                continue;
            }
            QSqlQuery upd(db);
            upd.prepare("UPDATE services SET title = ?, leader = CASE WHEN COALESCE(leader, '') = '' THEN ? ELSE leader END WHERE id = ?");
            upd.addBindValue(name);
            upd.addBindValue("Planning Center Sync");
            upd.addBindValue(existingId);
            if (upd.exec()) summary.servicesCount++;
            else summary.errors.append(QString("Service error (%1): %2").arg(name, upd.lastError().text()));
            continue;
        }
        QSqlQuery ins(db);
        ins.prepare("INSERT INTO services (date, title, leader) VALUES (:date, :title, :leader)");
        ins.bindValue(":date", date);
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

PcoImportSummary PlanningCenterClient::importGivingData(const QJsonArray& funds, const QList<PcoDonationRecord>& donations) {
    PcoImportSummary summary;
    QSqlDatabase db = DatabaseManager::instance().database();

    // 1. Local funds indexed by case-folded name. One cache serves both the
    // sideloaded PCO funds and designation mapping, so "Youth" and "youth" from
    // different syncs resolve to the same fund instead of creating duplicates.
    QHash<QString, int> fundIdsByName;
    QSqlQuery fundScan(db);
    if (fundScan.exec("SELECT id, fund_name FROM funds")) {
        while (fundScan.next()) {
            const QString fundName = fundScan.value(1).toString().trimmed();
            if (!fundName.isEmpty()) {
                fundIdsByName.insert(fundName.toCaseFolded(), fundScan.value(0).toInt());
            }
        }
    }
    auto fundIdForName = [&](const QString& rawName) -> int {
        const QString name = rawName.trimmed();
        if (name.isEmpty()) return -1;
        const QString key = name.toCaseFolded();
        const auto existing = fundIdsByName.constFind(key);
        if (existing != fundIdsByName.constEnd()) return existing.value();

        QSqlQuery ins(db);
        ins.prepare("INSERT INTO funds (fund_name, is_active) VALUES (?, 1)");
        ins.addBindValue(name);
        if (!ins.exec()) {
            summary.errors.append(QString("Fund error (%1): %2").arg(name, ins.lastError().text()));
            return -1;
        }
        const int newId = ins.lastInsertId().toInt();
        fundIdsByName.insert(key, newId);
        summary.fundsCount++;
        return newId;
    };

    // 1b. Index the sideloaded PCO funds so a designation whose name is missing
    // can still be resolved through its Fund relationship.
    QMap<QString, int> pcoFundIdToLocalId;
    for (const QJsonValue& fVal : funds) {
        const QJsonObject fObj = fVal.toObject();
        const QString pcoFundId = fObj.value("id").toString();
        const QString fundName = fObj.value("attributes").toObject()
                                     .value("name").toString().trimmed();
        if (fundName.isEmpty()) continue;
        const int localId = fundIdForName(fundName);
        if (localId != -1) pcoFundIdToLocalId.insert(pcoFundId, localId);
    }

    // "Imported (Unmapped)" is the structural fallback: resolve/create it lazily
    // once, so only gifts whose designation cannot be resolved land there.
    int unmappedFundId = -2; // -2 = not resolved yet, -1 = could not resolve
    auto importedUnmappedFund = [&]() -> int {
        if (unmappedFundId != -2) return unmappedFundId;
        // The cache is case-folded and already loaded, so this also reuses a
        // fallback fund whose stored name differs only by casing.
        const auto cached = fundIdsByName.constFind(QStringLiteral("imported (unmapped)"));
        if (cached != fundIdsByName.constEnd()) {
            unmappedFundId = cached.value();
            return unmappedFundId;
        }
        QSqlQuery lookup(db);
        lookup.prepare("SELECT id FROM funds WHERE fund_name = 'Imported (Unmapped)'");
        if (lookup.exec() && lookup.next()) {
            unmappedFundId = lookup.value(0).toInt();
            fundIdsByName.insert(QStringLiteral("imported (unmapped)"), unmappedFundId);
        } else {
            QSqlQuery createUnmapped(db);
            createUnmapped.prepare("INSERT INTO funds (fund_name, is_active) VALUES ('Imported (Unmapped)', 1)");
            if (createUnmapped.exec()) {
                unmappedFundId = createUnmapped.lastInsertId().toInt();
                fundIdsByName.insert(QStringLiteral("imported (unmapped)"), unmappedFundId);
            } else {
                unmappedFundId = -1;
            }
        }
        return unmappedFundId;
    };

    // 2. Build map of PCO Person ID -> Local Person ID (via barcode "PCO-<id>")
    QMap<QString, int> pcoPersonIdToLocalId;
    QSqlQuery qPcoPeople("SELECT id, barcode FROM people WHERE barcode LIKE 'PCO-%'");
    while (qPcoPeople.next()) {
        int locId = qPcoPeople.value(0).toInt();
        QString bc = qPcoPeople.value(1).toString();
        QString pcoId = bc.mid(4); // strip "PCO-"
        pcoPersonIdToLocalId[pcoId] = locId;
    }

    auto alreadyImported = [&](const QString& externalId) -> bool {
        QSqlQuery dup(db);
        dup.prepare("SELECT id FROM contributions WHERE external_id = ?");
        dup.addBindValue(externalId);
        return dup.exec() && dup.next();
    };

    // 3. Import donations. Each donation contributes one row per designation
    // (fund split). Rows are deduplicated on external_id, so re-running the
    // import is safe. Donations whose donor cannot be matched to a local person
    // are SKIPPED and reported — never silently attributed to an arbitrary
    // person, which would corrupt giving records and tax statements.
    for (const PcoDonationRecord& donation : donations) {
        if (donation.externalId.isEmpty() || donation.externalId == "PCO-") {
            summary.donationsSkipped++;
            continue;
        }
        // Validate the amount before any fund side effect: a gift that will not
        // be imported must not create funds or the unmapped fallback.
        if (donation.amountCents <= 0) {
            summary.donationsSkipped++;
            continue;
        }

        // Match donor Person ID before any fund side effect.
        if (donation.personPcoId.isEmpty() || !pcoPersonIdToLocalId.contains(donation.personPcoId)) {
            summary.donationsSkipped++;
            summary.errors.append(QString("Donation %1 skipped: donor not found locally. "
                                          "Import people first, or link the donor's barcode to 'PCO-%2'.")
                                      .arg(donation.externalId.mid(4), donation.personPcoId));
            continue;
        }
        const int personId = pcoPersonIdToLocalId.value(donation.personPcoId);

        QList<PcoDesignationPart> parts = donation.parts;
        if (parts.isEmpty()) {
            PcoDesignationPart part;
            part.designation = donation.designation;
            part.designationFundPcoId = donation.designationFundPcoId;
            part.amountCents = donation.amountCents;
            parts.append(part);
        }

        // Single-designation gifts keep the historical "PCO-<id>" dedup key;
        // split gifts add the designation id so each row is independently
        // idempotent and resumable.
        QStringList partExternalIds;
        partExternalIds.reserve(parts.size());
        for (int i = 0; i < parts.size(); ++i) {
            if (parts.size() == 1) {
                partExternalIds.append(donation.externalId);
            } else {
                const QString suffix = parts[i].designationPcoId.isEmpty()
                                           ? QString::number(i + 1)
                                           : parts[i].designationPcoId;
                partExternalIds.append(QString("%1-%2").arg(donation.externalId, suffix));
            }
        }

        // Bounds-check the split: negative or unusable portions must never be
        // imported as-is, and a split with no usable amounts is skipped instead
        // of guessing an attribution.
        qint64 partsTotal = 0;
        for (const PcoDesignationPart& part : parts) {
            if (part.amountCents > 0) partsTotal += part.amountCents;
        }
        if (parts.size() > 1 && partsTotal <= 0) {
            summary.donationsSkipped++;
            summary.errors.append(QString("Donation %1 skipped: designation amounts are missing or invalid.")
                                      .arg(donation.externalId.mid(4)));
            continue;
        }
        if (parts.size() > 1 && partsTotal > donation.amountCents) {
            summary.errors.append(QString("Donation %1: designation amounts (%2) exceed the donation total (%3); imported per-designation amounts.")
                                      .arg(donation.externalId.mid(4)).arg(partsTotal).arg(donation.amountCents));
        }
        if (parts.size() > 1 && partsTotal > 0 && partsTotal < donation.amountCents) {
            summary.errors.append(QString("Donation %1: designation amounts (%2) total less than the donation total (%3); imported per-designation amounts only — reconcile in Planning Center.")
                                      .arg(donation.externalId.mid(4)).arg(partsTotal).arg(donation.amountCents));
        }

        bool hasNewPart = false;
        for (const QString& externalId : partExternalIds) {
            if (!alreadyImported(externalId)) {
                hasNewPart = true;
                break;
            }
        }
        if (!hasNewPart) {
            summary.donationsSkipped++;
            continue;
        }

        QString date = donation.date;
        if (date.isEmpty()) date = QDate::currentDate().toString("yyyy-MM-dd");
        QString paymentMethod = donation.paymentMethod;
        if (paymentMethod.isEmpty()) paymentMethod = "Online";

        for (int i = 0; i < parts.size(); ++i) {
            const PcoDesignationPart& part = parts[i];
            const QString externalId = partExternalIds[i];
            if (alreadyImported(externalId)) continue;

            qint64 partAmount = qMax<qint64>(0, part.amountCents);
            if (parts.size() == 1 && partAmount <= 0) partAmount = donation.amountCents;
            if (partAmount <= 0) continue; // zero/negative portion: nothing to record

            // Map the designation to a local fund: existing fund whose name
            // matches case-insensitively, else a new fund named after the
            // designation, else the PCO fund relationship, else the fallback.
            int fundId = fundIdForName(part.designation);
            if (fundId != -1) {
                summary.designationsMapped++;
            } else if (!part.designationFundPcoId.isEmpty()
                       && pcoFundIdToLocalId.contains(part.designationFundPcoId)) {
                fundId = pcoFundIdToLocalId.value(part.designationFundPcoId);
                summary.designationsMapped++;
            }
            if (fundId == -1) fundId = importedUnmappedFund();
            if (fundId == -1) {
                summary.errors.append(QString("Donation %1 skipped: could not resolve fund.")
                                          .arg(donation.externalId.mid(4)));
                continue;
            }

            QSqlQuery ins(db);
            ins.prepare("INSERT INTO contributions (person_id, fund_id, amount, contribution_date, payment_method, check_number, external_id) "
                        "VALUES (:pid, :fid, :amount, :date, :method, :chk, :ext)");
            ins.bindValue(":pid", personId);
            ins.bindValue(":fid", fundId);
            ins.bindValue(":amount", partAmount);
            ins.bindValue(":date", date);
            ins.bindValue(":method", paymentMethod);
            ins.bindValue(":chk", donation.checkNumber);
            ins.bindValue(":ext", externalId);

            if (ins.exec()) {
                summary.donationsCount++;
            } else {
                // A unique-constraint hit means another import raced us to this
                // external_id; treat as skipped rather than an error. The
                // native code is driver-dependent, so also match the message.
                const QSqlError err = ins.lastError();
                if (err.nativeErrorCode() == "19"
                    || err.text().contains(QStringLiteral("constraint"), Qt::CaseInsensitive)) {
                    summary.donationsSkipped++;
                } else {
                    summary.errors.append(QString("Donation import error: %1").arg(err.text()));
                }
            }
        }
    }

    return summary;
}
