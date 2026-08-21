#ifndef MINISTRYMATCH_H
#define MINISTRYMATCH_H

// Ministry Match — prompt-driven person -> group recommendation engine.
//
// Headless (no QWidget dependencies) so the scoring logic is unit-testable
// from tests/tst_openekklesia.cpp. The MatchDialog is a thin UI wrapper.
//
// All criteria are computed from data that already exists in the schema:
//   - age band            -> people.date_of_birth
//   - gender / status     -> people.gender / membership_status
//   - skills              -> member_skills
//   - serving load        -> entity_relationships (Person -> Group)
//   - engagement          -> attendance (last 90 days)
//   - group skill profile -> skills of the group's current members

#include <QString>
#include <QStringList>
#include <QList>
#include <QSqlDatabase>

// Criterion knobs exposed by the dialog.
struct MatchCriteria {
    int minAge = 0;
    int maxAge = 120;
    QString gender;            // empty = any
    bool requireSkill = true;  // require at least one of the group's inferred skills
    int minAttendance90 = 0;   // minimum services attended in the last 90 days
    int maxGroupsServing = 3;  // burnout guard: skip people already serving in more groups
    int maxResults = 25;
};

struct MatchResult {
    int personId = 0;
    QString name;
    int age = -1;              // -1 = unknown birthdate
    QString gender;
    int score = 0;             // 0..100, display-ready match percentage
    QStringList reasons;       // human-readable "why this person"
};

// What a group declares it is looking for (groups.required_skills / min_age /
// max_age), with fallback to what its current members actually do.
struct GroupMatchProfile {
    QStringList requiredSkills;  // declared, possibly empty
    int minAge = 0;              // 0 = unconstrained
    int maxAge = 0;              // 0 = unconstrained (no band declared)
    bool skillsDeclared = false; // true when required_skills is non-empty
    bool ageBandSet = false;     // true when maxAge > 0
};

class MinistryRecommender {
public:
    // Skill profile of a group, inferred from its current members' skills
    // (most common first). Empty when the group has no members yet.
    static QStringList groupSkillProfile(const QSqlDatabase& db, int groupId, int limit = 6);

    // Full match profile: declared skills/age band win over inference.
    static GroupMatchProfile groupMatchProfile(const QSqlDatabase& db, int groupId);

    // Ranked candidates for one group per criteria. People already in the
    // group (any role) are always excluded. Sorted by score descending.
    static QList<MatchResult> recommend(const QSqlDatabase& db, int groupId, const MatchCriteria& criteria);

    // Bulk-add people as Members of the group (dedup-safe). Returns the ids
    // that were actually inserted (not already present).
    static QList<int> addMembers(const QSqlDatabase& db, int groupId, const QList<int>& personIds);
};

#endif // MINISTRYMATCH_H
