#include "synergyengine.h"
#include <QSqlQuery>
#include <QVariant>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

bool SynergyEngine::isEligible(const CandidateProfile& candidate, const GroupProfile& group, int maxGroupsServing, QString* hardBlockReason) {
    Q_UNUSED(candidate);
    Q_UNUSED(maxGroupsServing);
    // Structural hard blocks only: the cohort itself is not open to
    // recommendations (elected governance bodies, closed/seasonal cohorts).
    // Everything personal is an advisory warning (collectWarnings), never a
    // silent exclusion — a pastor always retains the final call.
    // NOTE: name matching is word-boundary anchored so "Board game club" or
    // "Elder Creek youth" are not mistaken for governance bodies.
    QString status = group.recruitmentStatus.trimmed();
    static const QRegularExpression govRx(
        QString("\\b(board|deacon|elder|elders|director|directors|governance)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (status == "Elected" || status == "Closed" || status == "Seasonal" ||
        group.name.contains(govRx) ||
        group.category.contains("Governance", Qt::CaseInsensitive)) {
        if (hardBlockReason) *hardBlockReason = "Elected governance body or closed cohort";
        return false;
    }
    return true;
}

namespace {
QString normDay(const QString& s) {
    QString d = s.trimmed().toLower();
    if (d.startsWith("sun")) return "sunday";
    if (d.startsWith("mon")) return "monday";
    if (d.startsWith("tue")) return "tuesday";
    if (d.startsWith("wed")) return "wednesday";
    if (d.startsWith("thu") || d.startsWith("thur")) return "thursday";
    if (d.startsWith("fri")) return "friday";
    if (d.startsWith("sat")) return "saturday";
    return d;
}
QString normTime(const QString& s) {
    // Accept "10:00", "10:00 AM", "10:00:00" -> "10:00".
    QString t = s.trimmed().toLower();
    t.remove("am");
    t.remove("pm");
    t = t.trimmed();
    const QStringList parts = t.split(':');
    if (parts.size() >= 2) {
        bool hOk = false, mOk = false;
        const int h = parts.at(0).toInt(&hOk);
        const int m = parts.at(1).left(2).toInt(&mOk);
        if (hOk && mOk) return QString("%1:%2").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0'));
    }
    return t;
}
} // namespace

QStringList SynergyEngine::collectWarnings(const CandidateProfile& candidate, const GroupProfile& group, int maxGroupsServing) {
    QStringList warnings;
    const int threshold = qMax(1, maxGroupsServing);

    // 1. Serving workload advisory (was a hard block; now pastor-reviewed).
    if (candidate.activeGroupCount >= threshold) {
        warnings << QString("⚠️ Workload Advisory: already serving in %1 groups — review schedule before assigning")
                        .arg(candidate.activeGroupCount);
    }

    // 2. Schedule Conflict advisory (normalized day/time compare).
    if (!group.meetingDay.isEmpty() && !group.meetingTime.isEmpty()) {
        const QString gDay = normDay(group.meetingDay);
        const QString gTime = normTime(group.meetingTime);
        for (const auto& sched : candidate.currentGroupSchedules) {
            if (normDay(sched.first) == gDay && normTime(sched.second) == gTime) {
                warnings << QString("⚠️ Schedule conflict on %1 at %2").arg(group.meetingDay, group.meetingTime);
                break;
            }
        }
    }

    // 3. Stale / Inactive Group (>180 days no activity). A group with no logged
    // activity date at all (daysSinceLastActivity <= 0) is new/untracked, not stale.
    if (group.daysSinceLastActivity > 180) {
        warnings << "⚠️ Inactive/stagnant group (no logged activity in 180+ days)";
    }

    // 4. Leader Care Flag advisory.
    if (group.leaderCareFlag == "At-Risk" || group.leaderCareFlag == "Needs Contact") {
        warnings << "⚠️ Group leader is currently under pastoral care";
    }

    // 5. Oversaturation advisory (was a hard block; pastor-reviewed).
    int catalystCount = group.archetypeCounts.value(ChurchArchetype::Catalyst, 0);
    if (catalystCount >= 3 && candidate.primaryArchetype == ChurchArchetype::Catalyst) {
        warnings << "⚠️ Team is oversaturated with visionaries (≥3 Catalysts)";
    }
    int sparkCount = group.archetypeCounts.value(ChurchArchetype::Spark, 0);
    if (sparkCount >= 4 && candidate.primaryArchetype == ChurchArchetype::Spark) {
        warnings << "⚠️ Team is oversaturated with energy (≥4 Sparks)";
    }

    // 6. Pastoral care safety advisory: At-Risk candidate + understaffed team.
    if ((candidate.careFlag == "At-Risk" || candidate.careFlag == "Needs Contact") && group.activeMemberCount < 3) {
        warnings << "🛡️ Pastoral Note: candidate is in active care; team is understaffed — review before assigning";
    }

    return warnings;
}

int SynergyEngine::calculateVoidBoost(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons) {
    int boost = 0;
    int anchorCount = group.archetypeCounts.value(ChurchArchetype::Anchor, 0);
    int archCount = group.archetypeCounts.value(ChurchArchetype::Architect, 0);
    int shepherdCount = group.archetypeCounts.value(ChurchArchetype::Shepherd, 0);

    // CRITICAL: Group needs execution / backbone (Anchor or Architect)
    if ((anchorCount + archCount) == 0 && (candidate.primaryArchetype == ChurchArchetype::Anchor || candidate.primaryArchetype == ChurchArchetype::Architect)) {
        boost += 30;
        if (reasons) reasons->append("🔥 Critical Void: Team lacks execution backbone (Anchor/Architect)");
    }
    // CRITICAL: Group needs pastoral / relational nurture
    else if (shepherdCount == 0 && candidate.primaryArchetype == ChurchArchetype::Shepherd) {
        boost += 25;
        if (reasons) reasons->append("🌿 Missing Piece: Team needs relational care (Shepherd)");
    }
    // Matching Blueprint Ideal Primary
    else if (candidate.primaryArchetype == group.idealPrimary) {
        boost += 20;
        if (reasons) reasons->append(QString("🎯 Blueprint Match: Primary archetype (%1)").arg(MinistryArchetypeRegistry::archetypeToString(group.idealPrimary)));
    }
    // Matching Blueprint Ideal Secondary
    else if (candidate.primaryArchetype == group.idealSecondary || candidate.secondaryArchetype == group.idealSecondary) {
        boost += 10;
        if (reasons) reasons->append(QString("✨ Complementary Fit: Secondary archetype (%1)").arg(MinistryArchetypeRegistry::archetypeToString(group.idealSecondary)));
    }

    return boost;
}

int SynergyEngine::calculateClashPenalty(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons) {
    int penalty = 0;
    int memberCount = std::max(1, group.activeMemberCount);
    
    // Check if adding candidate causes >30% Strategists or >50% Energizers
    RelationalStyleVector candVec = MinistryArchetypeRegistry::personStyleVector(candidate.primaryArchetype, candidate.secondaryArchetype);
    
    if (candVec.strategist >= 10.0) {
        int existingStrategists = group.archetypeCounts.value(ChurchArchetype::Architect, 0); // Architect carries strategist traits
        if (existingStrategists >= 2) {
            penalty -= 20;
            if (reasons) reasons->append("⚠️ Clash Risk: High density of strategic planners (>30%)");
        }
    }

    if (candVec.energizer >= 30.0) {
        int existingEnergizers = group.archetypeCounts.value(ChurchArchetype::Catalyst, 0) + group.archetypeCounts.value(ChurchArchetype::Spark, 0);
        if (existingEnergizers >= (memberCount / 2) && memberCount >= 4) {
            penalty -= 15;
            if (reasons) reasons->append("⚠️ Friction Risk: High energy density lacking execution anchor");
        }
    }

    return penalty;
}

double SynergyEngine::calculateEquilibriumDelta(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons) {
    RelationalStyleVector target = group.targetVector.normalized();
    RelationalStyleVector current = group.currentAggregateVector.normalized();

    double distBefore = RelationalStyleVector::euclideanDistance(current, target);

    // Simulate candidate joining. currentAggregateVector holds the member MEAN
    // (loadGroup/loadAllGroups divide the sum by activeMemberCount), so the new
    // team mean is (mean * n + candidate) / (n + 1) — not (mean + candidate)/n+1.
    RelationalStyleVector candVec = MinistryArchetypeRegistry::personStyleVector(candidate.primaryArchetype, candidate.secondaryArchetype);
    const double n = static_cast<double>(std::max(0, group.activeMemberCount));
    RelationalStyleVector simulated;
    if (n <= 0.0) {
        simulated = candVec; // empty team: the candidate alone defines the mix
    } else {
        simulated = RelationalStyleVector(
            (group.currentAggregateVector.organizer * n + candVec.organizer) / (n + 1.0),
            (group.currentAggregateVector.listener * n + candVec.listener) / (n + 1.0),
            (group.currentAggregateVector.doer * n + candVec.doer) / (n + 1.0),
            (group.currentAggregateVector.energizer * n + candVec.energizer) / (n + 1.0),
            (group.currentAggregateVector.strategist * n + candVec.strategist) / (n + 1.0),
            (group.currentAggregateVector.connector * n + candVec.connector) / (n + 1.0)
        );
    }
    double distAfter = RelationalStyleVector::euclideanDistance(simulated, target);

    double delta = distBefore - distAfter; // positive means moving closer to ideal
    if (delta > 0.05 && reasons) {
        reasons->append(QString("⚖️ Equilibrium: Moves team %1% closer to optimal synergy").arg(static_cast<int>(delta * 100)));
    }
    return delta;
}

int SynergyEngine::calculateGiftsAndInterestsBoost(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons) {
    int giftScore = 0;
    int interestScore = 0;

    // 1. Spiritual Gifts Match (+10 pts per match, max +20 pts)
    for (const QString& gift : candidate.spiritualGifts) {
        if (gift.isEmpty()) continue;
        for (const QString& req : group.requiredGifts) {
            if (gift.compare(req.trimmed(), Qt::CaseInsensitive) == 0) {
                giftScore += 10;
                if (reasons) reasons->append(QString("🎁 Spiritual Gift: %1 (+10 pts)").arg(gift));
                break;
            }
        }
        if (giftScore >= 20) break;
    }
    if (giftScore == 0 && !group.requiredSkills.isEmpty()) {
        QString skillsLower = group.requiredSkills.toLower();
        for (const QString& gift : candidate.spiritualGifts) {
            if (skillsLower.contains(gift.toLower())) {
                giftScore += 10;
                if (reasons) reasons->append(QString("🎁 Ministry Skill: %1 (+10 pts)").arg(gift));
                break;
            }
        }
    }
    giftScore = std::min(20, giftScore);

    // 2. Interest Tags Match (+10 exact, +5 parent domain, max +15 pts)
    QString gKeywords = (group.name + " " + group.category + " " + group.requiredSkills).toLower();
    for (const QString& tag : candidate.interestTags) {
        if (tag.isEmpty()) continue;
        if (gKeywords.contains(tag.toLower())) {
            interestScore += 10;
            if (reasons) reasons->append(QString("🏷️ Shared Interest: %1 (+10 pts)").arg(tag));
            break;
        }
    }
    interestScore = std::min(15, interestScore);

    return giftScore + interestScore;
}

int SynergyEngine::calculateCoLeadershipPairing(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons) {
    int score = 0;
    if (group.primaryLeaderArchetype == ChurchArchetype::None) return 0;

    ChurchArchetype leader = group.primaryLeaderArchetype;
    ChurchArchetype cand = candidate.primaryArchetype;

    // Catalyst + Architect = Vision + Execution
    if ((leader == ChurchArchetype::Catalyst && cand == ChurchArchetype::Architect) ||
        (leader == ChurchArchetype::Architect && cand == ChurchArchetype::Catalyst)) {
        score += 15;
        if (reasons) reasons->append("⚡ Co-Leadership Synergy: Catalyst Vision + Architect Execution (+15 pts)");
    }
    // Shepherd + Spark = Care + Growth
    else if ((leader == ChurchArchetype::Shepherd && cand == ChurchArchetype::Spark) ||
             (leader == ChurchArchetype::Spark && cand == ChurchArchetype::Shepherd)) {
        score += 15;
        if (reasons) reasons->append("🌿 Co-Leadership Synergy: Shepherd Care + Spark Energy (+15 pts)");
    }
    // Spark + Anchor = Energy + Reliability
    else if ((leader == ChurchArchetype::Spark && cand == ChurchArchetype::Anchor) ||
             (leader == ChurchArchetype::Anchor && cand == ChurchArchetype::Spark)) {
        score += 15;
        if (reasons) reasons->append("⚓ Co-Leadership Synergy: Spark Energy + Anchor Reliability (+15 pts)");
    }

    return score;
}

SynergyMatchResult SynergyEngine::evaluate(const CandidateProfile& candidate, const GroupProfile& group, int maxGroupsServing) {
    SynergyMatchResult result;
    result.groupId = group.groupId;
    result.groupName = group.name;

    // Phase 1: Structural hard gate (governance / closed cohorts only).
    QString hardBlockReason;
    if (!isEligible(candidate, group, maxGroupsServing, &hardBlockReason)) {
        result.hardBlocked = true;
        result.isEligible = false;
        result.hardBlockReason = hardBlockReason;
        result.score = 0;
        return result;
    }

    // Phase 1b: Advisory warnings for personal circumstances (pastor override).
    result.warnings = collectWarnings(candidate, group, maxGroupsServing);

    // Phase 2: Zero-Base Scoring
    int total = 0;
    QStringList details;

    // A. Void Law & Archetype Fit
    int voidBoost = calculateVoidBoost(candidate, group, &details);
    total += voidBoost;

    // B. Clash Law Penalty
    int clashPenalty = calculateClashPenalty(candidate, group, &details);
    total += clashPenalty;

    // C. Equilibrium Delta
    double eqDelta = calculateEquilibriumDelta(candidate, group, &details);
    total += static_cast<int>(eqDelta * 50.0);

    // D. Spiritual Gifts & Interests
    int giftsInterests = calculateGiftsAndInterestsBoost(candidate, group, &details);
    total += giftsInterests;

    // E. Co-Leadership Pairing
    int pairBoost = calculateCoLeadershipPairing(candidate, group, &details);
    total += pairBoost;

    // F. Household Family Tie (+15 pts)
    if (group.hasHouseholdFamilyTie) {
        total += 15;
        details.append("🏡 Family Connection: Household member active in group (+15 pts)");
    }

    // G. Geographic / Host Location Proximity (+10 pts)
    if (group.meetsInPersonZipOrCity) {
        total += 10;
        details.append("📍 Geographic Match: Meets near member's neighborhood (+10 pts)");
    }

    // H. Sabbatical & High-Stress Temporal Guardrail
    if (candidate.monthsInHighStress > 18) {
        if (group.category.contains("Infantil", Qt::CaseInsensitive) || group.category.contains("Eventos", Qt::CaseInsensitive)) {
            total -= 30;
            details.append("🛡️ Sabbatical Guardrail: Rest period from high-stress serving (-30 pts)");
        } else if (group.category.contains("Vida", Qt::CaseInsensitive) || group.category.contains("Oracion", Qt::CaseInsensitive)) {
            total += 15;
            details.append("🕊️ Sabbatical Routing: Restorative small group placement (+15 pts)");
        }
    }

    // I. Isolation Magnetism
    if (candidate.activeGroupCount == 0) {
        if (group.idealPrimary == ChurchArchetype::Shepherd || group.idealPrimary == ChurchArchetype::Spark) {
            total += 20;
            details.append("🧲 Isolation Magnetism: High nurture/connector environment (+20 pts)");
        }
    }

    // J. Pastoral Care Routing
    if (candidate.careFlag == "At-Risk" || candidate.careFlag == "Needs Contact") {
        if (group.activeMemberCount < 10) {
            total += 15;
            details.append("❤️ Pastoral Care: Safe, small supportive circle (+15 pts)");
        }
    }

    result.score = std::clamp(total, 15, 98);
    result.breakdownLines = details;

    if (!details.isEmpty()) {
        result.primaryReason = details.first();
    } else {
        result.primaryReason = "General ministry alignment";
    }

    return result;
}

CandidateProfile SynergyEngine::loadCandidate(const QSqlDatabase& db, int personId) {
    CandidateProfile p;
    p.personId = personId;

    QSqlQuery q(db);
    q.prepare("SELECT p.first_name || ' ' || p.last_name, p.ministry_personality, p.personality_secondary, "
              "       p.spiritual_gifts, p.relational_style, p.interest_tags, p.care_flag, p.date_of_birth, "
              "       h.city, h.zip_code "
              "FROM people p LEFT JOIN households h ON p.household_id = h.id WHERE p.id = ?");
    q.addBindValue(personId);
    if (q.exec() && q.next()) {
        p.name = q.value(0).toString();
        p.primaryArchetype = MinistryArchetypeRegistry::stringToArchetype(q.value(1).toString());
        p.secondaryArchetype = MinistryArchetypeRegistry::stringToArchetype(q.value(2).toString());
        
        QString giftsStr = q.value(3).toString();
        if (!giftsStr.isEmpty()) p.spiritualGifts = giftsStr.split(QRegularExpression("[,;]+"), Qt::SkipEmptyParts);
        
        p.relationalStyle = q.value(4).toString();
        
        QString tagsStr = q.value(5).toString();
        if (!tagsStr.isEmpty()) p.interestTags = tagsStr.split(QRegularExpression("[,;]+"), Qt::SkipEmptyParts);
        
        p.careFlag = q.value(6).toString();
        p.dob = q.value(7).toString();
        p.city = q.value(8).toString().trimmed();
        p.zipCode = q.value(9).toString().trimmed();
    }

    // Active Groups & Schedules
    QSqlQuery sq(db);
    sq.prepare("SELECT g.meeting_day, g.meeting_time FROM entity_relationships er "
               "JOIN groups g ON er.target_id = g.id "
               "WHERE er.source_id = ? AND er.source_type = 'Person' AND er.target_type = 'Group' AND er.status = 'Active'");
    sq.addBindValue(personId);
    if (sq.exec()) {
        while (sq.next()) {
            p.activeGroupCount++;
            p.currentGroupSchedules.append({sq.value(0).toString(), sq.value(1).toString()});
        }
    }

    return p;
}

GroupProfile SynergyEngine::loadGroup(const QSqlDatabase& db, int groupId, int candidatePersonId) {
    GroupProfile g;
    g.groupId = groupId;

    QSqlQuery q(db);
    q.prepare("SELECT g.name, g.category, g.ideal_primary_archetype, g.ideal_secondary_archetype, "
              "       g.required_skills, g.min_age, g.max_age, g.meeting_day, g.meeting_time, g.location, "
              "       g.recruitment_status, p.care_flag, "
              "       (SELECT MAX(pa.activity_date) FROM pastoral_activities pa WHERE pa.group_id = g.id) AS last_act "
              "FROM groups g "
              "LEFT JOIN people p ON g.contact_person_id = p.id "
              "WHERE g.id = ?");
    q.addBindValue(groupId);
    if (q.exec() && q.next()) {
        g.name = q.value(0).toString();
        g.category = q.value(1).toString();
        
        QString pri = q.value(2).toString();
        QString sec = q.value(3).toString();
        
        // If ideal archetypes are not explicitly set, retrieve from canonical blueprint
        MinistryBlueprint bp = MinistryArchetypeRegistry::getBlueprint(g.category);
        g.idealPrimary = pri.isEmpty() ? MinistryArchetypeRegistry::stringToArchetype(bp.primaryArchetype) : MinistryArchetypeRegistry::stringToArchetype(pri);
        g.idealSecondary = sec.isEmpty() ? MinistryArchetypeRegistry::stringToArchetype(bp.secondaryArchetype) : MinistryArchetypeRegistry::stringToArchetype(sec);
        g.targetVector = bp.targetVector;
        g.requiredGifts = bp.requiredGifts;

        g.requiredSkills = q.value(4).toString();
        g.minAge = q.value(5).toInt();
        g.maxAge = q.value(6).toInt();
        g.meetingDay = q.value(7).toString();
        g.meetingTime = q.value(8).toString();
        g.location = q.value(9).toString();
        g.recruitmentStatus = q.value(10).toString().trimmed();
        if (g.recruitmentStatus.isEmpty()) g.recruitmentStatus = "Open";
        g.leaderCareFlag = q.value(11).toString();

        QString lastAct = q.value(12).toString();
        if (!lastAct.isEmpty()) {
            g.daysSinceLastActivity = QDate::fromString(lastAct, "yyyy-MM-dd").daysTo(QDate::currentDate());
        }
    }

    // Active roster archetypes & leader inspection
    QSqlQuery mq(db);
    mq.prepare("SELECT p.ministry_personality, p.personality_secondary, er.relationship_type "
               "FROM entity_relationships er JOIN people p ON er.source_id = p.id "
               "WHERE er.target_id = ? AND er.target_type = 'Group' AND er.source_type = 'Person' AND er.status = 'Active'");
    mq.addBindValue(groupId);
    if (mq.exec()) {
        RelationalStyleVector sumVec(0, 0, 0, 0, 0, 0);
        while (mq.next()) {
            g.activeMemberCount++;
            ChurchArchetype a1 = MinistryArchetypeRegistry::stringToArchetype(mq.value(0).toString());
            ChurchArchetype a2 = MinistryArchetypeRegistry::stringToArchetype(mq.value(1).toString());
            QString role = mq.value(2).toString();

            if (role == "Leader" || role == "Co-Leader") {
                g.primaryLeaderArchetype = a1;
            }
            g.archetypeCounts[a1]++;

            RelationalStyleVector mv = MinistryArchetypeRegistry::personStyleVector(a1, a2);
            sumVec.organizer += mv.organizer;
            sumVec.listener += mv.listener;
            sumVec.doer += mv.doer;
            sumVec.energizer += mv.energizer;
            sumVec.strategist += mv.strategist;
            sumVec.connector += mv.connector;
        }
        if (g.activeMemberCount > 0) {
            g.currentAggregateVector = RelationalStyleVector(
                sumVec.organizer / g.activeMemberCount,
                sumVec.listener / g.activeMemberCount,
                sumVec.doer / g.activeMemberCount,
                sumVec.energizer / g.activeMemberCount,
                sumVec.strategist / g.activeMemberCount,
                sumVec.connector / g.activeMemberCount
            );
        }
    }

    // Check household family tie for candidate
    if (candidatePersonId > 0) {
        QSqlQuery fq(db);
        fq.prepare("SELECT COUNT(*) FROM entity_relationships er "
                   "WHERE er.target_id = ? AND er.source_type = 'Person' AND er.target_type = 'Group' "
                   "  AND er.source_id IN (SELECT id FROM people WHERE household_id = (SELECT household_id FROM people WHERE id = ?))");
        fq.addBindValue(groupId);
        fq.addBindValue(candidatePersonId);
        if (fq.exec() && fq.next() && fq.value(0).toInt() > 0) {
            g.hasHouseholdFamilyTie = true;
        }
    }

    return g;
}

// --- Bulk loaders (Smart Bridge population scan) ----------------------------

// Loads every person's profile in 3 queries (vs ~3 queries per person).
void SynergyEngine::loadAllCandidates(const QSqlDatabase& db, QMap<int, CandidateProfile>& out) {
    QSqlQuery q(db);
    q.exec("SELECT p.id, p.first_name || ' ' || p.last_name, p.ministry_personality, p.personality_secondary, "
           "       p.spiritual_gifts, p.relational_style, p.interest_tags, p.care_flag, p.date_of_birth, "
           "       h.city, h.zip_code "
           "FROM people p LEFT JOIN households h ON p.household_id = h.id");
    while (q.next()) {
        CandidateProfile p;
        p.personId = q.value(0).toInt();
        p.name = q.value(1).toString();
        p.primaryArchetype = MinistryArchetypeRegistry::stringToArchetype(q.value(2).toString());
        p.secondaryArchetype = MinistryArchetypeRegistry::stringToArchetype(q.value(3).toString());
        const QString gifts = q.value(4).toString();
        if (!gifts.isEmpty()) p.spiritualGifts = gifts.split(QRegularExpression("[,;]+"), Qt::SkipEmptyParts);
        p.relationalStyle = q.value(5).toString();
        const QString tags = q.value(6).toString();
        if (!tags.isEmpty()) p.interestTags = tags.split(QRegularExpression("[,;]+"), Qt::SkipEmptyParts);
        p.careFlag = q.value(7).toString();
        p.dob = q.value(8).toString();
        p.city = q.value(9).toString().trimmed();
        p.zipCode = q.value(10).toString().trimmed();
        out.insert(p.personId, p);
    }

    // Active group count per person + schedules (one query for everyone).
    QSqlQuery sq(db);
    sq.exec("SELECT er.source_id, g.meeting_day, g.meeting_time "
            "FROM entity_relationships er JOIN groups g ON er.target_id = g.id "
            "WHERE er.source_type = 'Person' AND er.target_type = 'Group' AND er.status = 'Active'");
    while (sq.next()) {
        auto it = out.find(sq.value(0).toInt());
        if (it == out.end()) continue;
        it->activeGroupCount++;
        it->currentGroupSchedules.append({sq.value(1).toString(), sq.value(2).toString()});
    }
}

// Loads every group's profile in 3 queries (vs ~3 queries per group).
// The family-tie query is candidate-specific and runs once for all groups.
void SynergyEngine::loadAllGroups(const QSqlDatabase& db, int candidatePersonId, QMap<int, GroupProfile>& out) {
    QSqlQuery q(db);
    q.exec("SELECT g.id, g.name, g.category, g.ideal_primary_archetype, g.ideal_secondary_archetype, "
           "       g.required_skills, g.min_age, g.max_age, g.meeting_day, g.meeting_time, g.location, "
           "       g.recruitment_status, p.care_flag, "
           "       (SELECT MAX(pa.activity_date) FROM pastoral_activities pa WHERE pa.group_id = g.id) AS last_act "
           "FROM groups g LEFT JOIN people p ON g.contact_person_id = p.id "
           "WHERE g.is_hidden = 0 OR g.is_hidden IS NULL");
    while (q.next()) {
        GroupProfile g;
        g.groupId = q.value(0).toInt();
        g.name = q.value(1).toString();
        g.category = q.value(2).toString();
        const QString pri = q.value(3).toString();
        const QString sec = q.value(4).toString();
        MinistryBlueprint bp = MinistryArchetypeRegistry::getBlueprint(g.category);
        g.idealPrimary = pri.isEmpty() ? MinistryArchetypeRegistry::stringToArchetype(bp.primaryArchetype)
                                       : MinistryArchetypeRegistry::stringToArchetype(pri);
        g.idealSecondary = sec.isEmpty() ? MinistryArchetypeRegistry::stringToArchetype(bp.secondaryArchetype)
                                         : MinistryArchetypeRegistry::stringToArchetype(sec);
        g.targetVector = bp.targetVector;
        g.requiredGifts = bp.requiredGifts;
        g.requiredSkills = q.value(5).toString();
        g.minAge = q.value(6).toInt();
        g.maxAge = q.value(7).toInt();
        g.meetingDay = q.value(8).toString();
        g.meetingTime = q.value(9).toString();
        g.location = q.value(10).toString();
        g.recruitmentStatus = q.value(11).toString().trimmed();
        if (g.recruitmentStatus.isEmpty()) g.recruitmentStatus = "Open";
        g.leaderCareFlag = q.value(12).toString();
        const QString lastAct = q.value(13).toString();
        if (!lastAct.isEmpty()) {
            g.daysSinceLastActivity = QDate::fromString(lastAct, "yyyy-MM-dd").daysTo(QDate::currentDate());
        }
        out.insert(g.groupId, g);
    }

    // Roster archetypes per group (one query for all groups).
    QSqlQuery mq(db);
    mq.exec("SELECT er.target_id, p.ministry_personality, p.personality_secondary, er.relationship_type "
            "FROM entity_relationships er JOIN people p ON er.source_id = p.id "
            "WHERE er.target_type = 'Group' AND er.source_type = 'Person' AND er.status = 'Active'");
    QMap<int, RelationalStyleVector> sumVecs;
    while (mq.next()) {
        const int gid = mq.value(0).toInt();
        auto it = out.find(gid);
        if (it == out.end()) continue;
        ChurchArchetype a1 = MinistryArchetypeRegistry::stringToArchetype(mq.value(1).toString());
        ChurchArchetype a2 = MinistryArchetypeRegistry::stringToArchetype(mq.value(2).toString());
        const QString role = mq.value(3).toString();
        it->activeMemberCount++;
        if (role == "Leader" || role == "Co-Leader") it->primaryLeaderArchetype = a1;
        it->archetypeCounts[a1]++;
        RelationalStyleVector mv = MinistryArchetypeRegistry::personStyleVector(a1, a2);
        sumVecs[gid].organizer += mv.organizer;
        sumVecs[gid].listener += mv.listener;
        sumVecs[gid].doer += mv.doer;
        sumVecs[gid].energizer += mv.energizer;
        sumVecs[gid].strategist += mv.strategist;
        sumVecs[gid].connector += mv.connector;
    }
    for (auto it = sumVecs.constBegin(); it != sumVecs.constEnd(); ++it) {
        auto gIt = out.find(it.key());
        if (gIt == out.end() || gIt->activeMemberCount <= 0) continue;
        gIt->currentAggregateVector = RelationalStyleVector(
            it.value().organizer / gIt->activeMemberCount,
            it.value().listener / gIt->activeMemberCount,
            it.value().doer / gIt->activeMemberCount,
            it.value().energizer / gIt->activeMemberCount,
            it.value().strategist / gIt->activeMemberCount,
            it.value().connector / gIt->activeMemberCount);
    }

    // Candidate household family ties across ALL groups (one query).
    if (candidatePersonId > 0) {
        QSqlQuery fq(db);
        fq.prepare("SELECT er.target_id FROM entity_relationships er "
                   "WHERE er.source_type = 'Person' AND er.target_type = 'Group' "
                   "  AND er.source_id IN (SELECT id FROM people "
                   "      WHERE household_id = (SELECT household_id FROM people WHERE id = ?))");
        fq.addBindValue(candidatePersonId);
        if (fq.exec()) {
            while (fq.next()) {
                auto it = out.find(fq.value(0).toInt());
                if (it != out.end()) it->hasHouseholdFamilyTie = true;
            }
        }
    }
}
