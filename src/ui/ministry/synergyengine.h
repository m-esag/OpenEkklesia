#ifndef SYNERGYENGINE_H
#define SYNERGYENGINE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QDate>
#include <QSqlDatabase>
#include "ministryarchetypes.h"

struct CandidateProfile {
    int personId = 0;
    QString name;
    ChurchArchetype primaryArchetype = ChurchArchetype::Anchor;
    ChurchArchetype secondaryArchetype = ChurchArchetype::None;
    QStringList spiritualGifts;
    QString relationalStyle;
    QStringList interestTags;
    int activeGroupCount = 0;
    QString careFlag;
    QString dob;
    QString city;
    QString zipCode;
    int monthsInHighStress = 0;
    QList<QPair<QString, QString>> currentGroupSchedules; // (meeting_day, meeting_time)
};

struct GroupProfile {
    int groupId = 0;
    QString name;
    QString category;
    ChurchArchetype idealPrimary = ChurchArchetype::Shepherd;
    ChurchArchetype idealSecondary = ChurchArchetype::Anchor;
    RelationalStyleVector targetVector;
    QStringList requiredGifts;
    QString requiredSkills;
    int minAge = 0;
    int maxAge = 0;
    QString meetingDay;
    QString meetingTime;
    QString location;
    QString recruitmentStatus = "Open";
    int activeMemberCount = 0;
    QMap<ChurchArchetype, int> archetypeCounts;
    RelationalStyleVector currentAggregateVector;
    ChurchArchetype primaryLeaderArchetype = ChurchArchetype::None;
    QString leaderCareFlag;
    int daysSinceLastActivity = 0;
    bool hasHouseholdFamilyTie = false;
    bool meetsInPersonZipOrCity = false;
};

struct SynergyMatchResult {
    int groupId = 0;
    QString groupName;
    int score = 0; // 0..100
    QString primaryReason;
    QStringList breakdownLines;
    // Advisory model (dev-review decision): only STRUCTURAL group properties
    // (elected/closed/seasonal cohort) hard-block a recommendation. Personal
    // circumstances (workload, schedule, care flags, staleness) are warnings a
    // pastor may consciously override — the engine suggests, the human decides.
    bool hardBlocked = false;
    bool isEligible = true;
    QString hardBlockReason;
    QStringList warnings;
};

class SynergyEngine {
public:
    // Structural gatekeeper (Phase 1): false ONLY for elected/closed/seasonal
    // governance cohorts. Personal circumstances are advisory warnings, not
    // blocks — see collectWarnings().
    static bool isEligible(const CandidateProfile& candidate, const GroupProfile& group, int maxGroupsServing = 3, QString* hardBlockReason = nullptr);

    // Advisory flags (workload, schedule clash, stale group, leader under care,
    // oversaturation, at-risk + understaffed). Empty = no pastor review needed.
    static QStringList collectWarnings(const CandidateProfile& candidate, const GroupProfile& group, int maxGroupsServing = 3);

    // Dynamic Team Chemistry Scoring (Phase 2)
    static SynergyMatchResult evaluate(const CandidateProfile& candidate, const GroupProfile& group, int maxGroupsServing = 3);

    // Helper loaders from SQLite Database
    static CandidateProfile loadCandidate(const QSqlDatabase& db, int personId);
    static GroupProfile loadGroup(const QSqlDatabase& db, int groupId, int candidatePersonId = 0);

    // Bulk loaders for the graph's Smart Bridge scan — 3 queries total for the
    // whole population instead of ~3 queries per candidate.
    static void loadAllCandidates(const QSqlDatabase& db, QMap<int, CandidateProfile>& out);
    static void loadAllGroups(const QSqlDatabase& db, int candidatePersonId, QMap<int, GroupProfile>& out);

    // Core 4 Laws sub-calculations
    static int calculateVoidBoost(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons = nullptr);
    static int calculateClashPenalty(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons = nullptr);
    static double calculateEquilibriumDelta(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons = nullptr);
    static int calculateGiftsAndInterestsBoost(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons = nullptr);
    static int calculateCoLeadershipPairing(const CandidateProfile& candidate, const GroupProfile& group, QStringList* reasons = nullptr);
};

#endif // SYNERGYENGINE_H
