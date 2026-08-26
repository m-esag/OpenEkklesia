#include "ministrymatch.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDate>
#include <QSet>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>

QStringList MinistryRecommender::groupSkillProfile(const QSqlDatabase& db, int groupId, int limit) {
    QStringList skills;
    QSqlQuery q(db);
    q.prepare(
        "SELECT s.skill_or_role, COUNT(*) AS c "
        "FROM member_skills s "
        "JOIN entity_relationships r "
        "  ON r.source_type = 'Person' AND r.target_type = 'Group' "
        " AND r.target_id = ? AND r.source_id = s.person_id "
        "GROUP BY s.skill_or_role "
        "ORDER BY c DESC, s.skill_or_role "
        "LIMIT ?");
    q.addBindValue(groupId);
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next()) skills.append(q.value(0).toString());
    }
    return skills;
}

GroupMatchProfile MinistryRecommender::groupMatchProfile(const QSqlDatabase& db, int groupId) {
    GroupMatchProfile profile;
    QSqlQuery q(db);
    q.prepare("SELECT required_skills, min_age, max_age FROM groups WHERE id = ?");
    q.addBindValue(groupId);
    if (q.exec() && q.next()) {
        const QString declared = q.value(0).toString().trimmed();
        if (!declared.isEmpty()) {
            const QStringList raw = declared.split(QRegularExpression("[,\\n;]"), Qt::SkipEmptyParts);
            QSet<QString> seen;
            for (QString s : raw) {
                s = s.trimmed();
                if (s.isEmpty() || seen.contains(s)) continue;
                seen.insert(s);
                profile.requiredSkills.append(s);
            }
            profile.skillsDeclared = true;
        }
        profile.minAge = qMax(0, q.value(1).toInt());
        profile.maxAge = qMax(0, q.value(2).toInt());
        if (profile.maxAge > 0) profile.ageBandSet = true;
        if (profile.ageBandSet && profile.minAge > profile.maxAge) {
            std::swap(profile.minAge, profile.maxAge);
        }
    }
    return profile;
}

QStringList MinistryRecommender::roleSkills(const QString& role) {
    const QString r = role.toLower();
    if (r.contains("preach")) return {"teaching", "preaching", "speaking"};
    if (r.contains("worship")) return {"worship", "leadership", "vocals", "singing"};
    if (r.contains("acoustic")) return {"guitar", "acoustic guitar", "acoustic"};
    if (r.contains("electric")) return {"guitar", "electric guitar", "electric"};
    if (r.contains("vocal") || r.contains("sing")) return {"vocals", "singing", "vocalist"};
    if (r.contains("bass")) return {"bass", "bass guitar"};
    if (r.contains("drum")) return {"drums", "drum", "percussion"};
    if (r.contains("nursery") || r.contains("child")) return {"nursery", "childcare", "children", "kids"};
    if (r.contains("audio") || r.contains("sound")) return {"audio", "sound", "sound engineering"};
    if (r.contains("slide") || r.contains("media") || r.contains("present")) return {"slides", "media", "presentation"};
    if (r.contains("ush")) return {"usher", "greeting", "hospitality", "welcoming"};
    return {};
}

QList<MatchResult> MinistryRecommender::rankByRole(const QSqlDatabase& db, const QString& role) {
    QList<MatchResult> out;
    const QStringList roleSkillsList = MinistryRecommender::roleSkills(role);
    if (roleSkillsList.isEmpty()) return out;

    QSet<QString> wanted;
    for (const QString& s : roleSkillsList) wanted.insert(s.toLower());

    // Team load (groups served in).
    QMap<int, int> groupCount;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT source_id, COUNT(*) FROM entity_relationships "
                   "WHERE source_type = 'Person' AND target_type = 'Group' GROUP BY source_id")) {
            while (q.next()) groupCount.insert(q.value(0).toInt(), q.value(1).toInt());
        }
    }

    // Recent service frequency (last 30 days).
    QMap<int, int> recentServes;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT person_id, COUNT(*) FROM service_volunteers "
                   "WHERE service_date >= date('now', '-30 days') GROUP BY person_id")) {
            while (q.next()) recentServes.insert(q.value(0).toInt(), q.value(1).toInt());
        }
    }

    // Engagement (90-day attendance).
    QMap<int, int> attendance90;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT person_id, COUNT(*) FROM attendance "
                   "WHERE attended = 1 AND timestamp >= datetime('now', '-90 days') "
                   "GROUP BY person_id")) {
            while (q.next()) attendance90.insert(q.value(0).toInt(), q.value(1).toInt());
        }
    }

    // Person skills.
    QMap<int, QStringList> personSkills;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT person_id, skill_or_role FROM member_skills")) {
            while (q.next()) personSkills[q.value(0).toInt()].append(q.value(1).toString());
        }
    }

    QSqlQuery q(db);
    q.prepare("SELECT id, first_name, last_name FROM people ORDER BY last_name, first_name");
    if (!q.exec()) return out;

    while (q.next()) {
        const int pid = q.value(0).toInt();
        MatchResult r;
        r.personId = pid;
        r.name = QString("%1 %2").arg(q.value(1).toString(), q.value(2).toString());

        QStringList matched;
        for (const QString& s : personSkills.value(pid)) {
            if (wanted.contains(s.toLower())) matched << s;
        }

        int raw = 0;
        if (!matched.isEmpty()) {
            raw += 40; // skill match dominates
            r.reasons << "Skills: " + matched.join(", ");
        }
        const int load = groupCount.value(pid, 0);
        if (load > 0) {
            raw -= qMin(15, load * 5);
            r.reasons << QString("%1 team(s)").arg(load);
        }
        const int serves = recentServes.value(pid, 0);
        if (serves > 0) {
            raw -= qMin(15, serves * 5);
            r.reasons << QString("Served %1x (30d)").arg(serves);
        }
        const int att = attendance90.value(pid, 0);
        if (att >= 3) {
            raw += 10;
            r.reasons << QString("Attended %1x (90d)").arg(att);
        }

        r.score = qBound(0, qRound(raw * 100.0 / 50.0), 100);
        out.append(r);
    }

    std::sort(out.begin(), out.end(), [](const MatchResult& a, const MatchResult& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.name < b.name;
    });
    return out;
}

QList<MatchResult> MinistryRecommender::recommend(const QSqlDatabase& db, int groupId, const MatchCriteria& criteria) {
    QList<MatchResult> out;

    // Skills the group "needs": declared required_skills win; otherwise infer
    // from the skills of the group's current members.
    const GroupMatchProfile matchProfile = groupMatchProfile(db, groupId);
    QStringList profile;
    if (matchProfile.skillsDeclared) {
        profile = matchProfile.requiredSkills;
    } else {
        profile = groupSkillProfile(db, groupId);
    }
    const QSet<QString> needSkills(profile.begin(), profile.end());

    // People already in the group (any role) are never recommended.
    QSet<int> alreadyIn;
    {
        QSqlQuery q(db);
        q.prepare("SELECT source_id FROM entity_relationships "
                  "WHERE source_type = 'Person' AND target_type = 'Group' AND target_id = ?");
        q.addBindValue(groupId);
        if (q.exec()) {
            while (q.next()) alreadyIn.insert(q.value(0).toInt());
        }
    }

    // Serving load: number of groups each person already serves in.
    QMap<int, int> groupCount;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT source_id, COUNT(*) FROM entity_relationships "
                   "WHERE source_type = 'Person' AND target_type = 'Group' GROUP BY source_id")) {
            while (q.next()) groupCount.insert(q.value(0).toInt(), q.value(1).toInt());
        }
    }

    // Engagement: services attended in the last 90 days.
    QMap<int, int> attendance90;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT person_id, COUNT(*) FROM attendance "
                   "WHERE attended = 1 AND timestamp >= datetime('now', '-90 days') "
                   "GROUP BY person_id")) {
            while (q.next()) attendance90.insert(q.value(0).toInt(), q.value(1).toInt());
        }
    }

    // Person skills, indexed by person id.
    QMap<int, QStringList> personSkills;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT person_id, skill_or_role FROM member_skills")) {
            while (q.next()) personSkills[q.value(0).toInt()].append(q.value(1).toString());
        }
    }

    const QDate today = QDate::currentDate();

    QSqlQuery q(db);
    q.prepare("SELECT id, first_name, last_name, date_of_birth, gender, membership_status FROM people");
    if (!q.exec()) return out;

    while (q.next()) {
        const int pid = q.value("id").toInt();
        if (alreadyIn.contains(pid)) continue;

        MatchResult r;
        r.personId = pid;
        r.name = QString("%1 %2").arg(q.value("first_name").toString(), q.value("last_name").toString());
        r.gender = q.value("gender").toString();

        // Age from birthdate (full years).
        const QString dob = q.value("date_of_birth").toString();
        if (!dob.isEmpty()) {
            const QDate bd = QDate::fromString(dob, Qt::ISODate);
            if (bd.isValid()) {
                int age = today.year() - bd.year();
                if (today.month() < bd.month() || (today.month() == bd.month() && today.day() < bd.day())) {
                    age--;
                }
                r.age = age;
            }
        }
        if (r.age >= 0 && (r.age < criteria.minAge || r.age > criteria.maxAge)) continue;
        if (!criteria.gender.isEmpty() && r.gender != criteria.gender) continue;
        if (attendance90.value(pid, 0) < criteria.minAttendance90) continue;

        int raw = 0;
        QStringList reasons;

        // 1. Age band (weight 35).
        if (r.age >= 0 && r.age >= criteria.minAge && r.age <= criteria.maxAge) {
            raw += 35;
            reasons << QString("Age %1 fits %2–%3 band").arg(r.age).arg(criteria.minAge).arg(criteria.maxAge);
        }

        // 2. Skill match (weight 30). When the group has a skill profile and the
        //    user requires it, people with none of those skills are excluded.
        const QStringList& skills = personSkills.value(pid);
        QStringList matched;
        for (const QString& s : skills) {
            if (needSkills.contains(s)) matched << s;
        }
        if (!matched.isEmpty()) {
            raw += 30;
            reasons << "Has needed skill(s): " + matched.join(", ");
        } else if (criteria.requireSkill && !profile.isEmpty()) {
            continue;
        }

        // 3. Serving load / burnout guard (weight -5 per group, cap -15).
        const int serving = groupCount.value(pid, 0);
        if (serving > criteria.maxGroupsServing) continue;
        if (serving > 0) {
            raw -= qMin(15, serving * 5);
            reasons << QString("Already serving in %1 group(s)").arg(serving);
        }

        // 4. Engagement boost (weight up to +20, no attendance = -10).
        const int att = attendance90.value(pid, 0);
        if (att > 0) {
            raw += qMin(20, att * 3);
            reasons << QString("Attended %1 service(s) in last 90 days").arg(att);
        } else {
            raw -= 10;
            reasons << "No attendance recorded in last 90 days";
        }

        // 5. Membership status (Member +5, prospective lower).
        const QString status = q.value("membership_status").toString();
        if (status == "Member") {
            raw += 5;
        } else if (status == "Visitor" || status == "Attender") {
            raw += 1;
        }

        // Normalize to a display-ready percentage (max achievable raw = 90).
        r.score = qBound(0, qRound(raw * 100.0 / 90.0), 100);
        r.reasons = reasons;
        out.append(r);
    }

    std::sort(out.begin(), out.end(), [](const MatchResult& a, const MatchResult& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.name < b.name;
    });

    if (out.size() > qMax(0, criteria.maxResults)) {
        out.resize(qMax(0, criteria.maxResults));
    }
    return out;
}

QList<int> MinistryRecommender::addMembers(const QSqlDatabase& db, int groupId, const QList<int>& personIds) {
    QList<int> added;
    for (int pid : personIds) {
        QSqlQuery exists(db);
        exists.prepare("SELECT 1 FROM entity_relationships "
                       "WHERE source_type = 'Person' AND source_id = ? "
                       "AND target_type = 'Group' AND target_id = ? AND relationship_type = 'Member'");
        exists.addBindValue(pid);
        exists.addBindValue(groupId);
        if (exists.exec() && exists.next()) continue;

        QSqlQuery ins(db);
        ins.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, connection_source, joined_date) "
                    "VALUES ('Person', ?, 'Group', ?, 'Member', 'ministry_match', CURRENT_DATE)");
        ins.addBindValue(pid);
        ins.addBindValue(groupId);
        if (ins.exec()) added.append(pid);
    }
    return added;
}
