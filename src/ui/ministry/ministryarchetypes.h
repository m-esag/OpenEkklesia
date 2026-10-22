#ifndef MINISTRYARCHETYPES_H
#define MINISTRYARCHETYPES_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QColor>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <cmath>

// 5 Church-Contextual Ministry Archetypes
enum class ChurchArchetype {
    Catalyst,   // 🔥 Vision, initiation, launch
    Shepherd,   // 🌿 Care, nurture, 1-on-1
    Architect,  // 📐 Systems, checklists, logistics
    Spark,      // ⚡ Energy, recruiting, momentum
    Anchor,     // ⚓ Reliability, execution, backbone
    None
};

// 6 Functional Relational Styles Vector: [Org, Lis, Doe, Ene, Str, Con]
struct RelationalStyleVector {
    double organizer = 0.0;
    double listener = 0.0;
    double doer = 0.0;
    double energizer = 0.0;
    double strategist = 0.0;
    double connector = 0.0;

    RelationalStyleVector() = default;
    RelationalStyleVector(double o, double l, double d, double e, double s, double c)
        : organizer(o), listener(l), doer(d), energizer(e), strategist(s), connector(c) {}

    QVector<double> toList() const {
        return {organizer, listener, doer, energizer, strategist, connector};
    }

    double sum() const {
        return organizer + listener + doer + energizer + strategist + connector;
    }

    RelationalStyleVector normalized() const {
        double s = sum();
        if (s <= 0.0001) return RelationalStyleVector(1.0/6.0, 1.0/6.0, 1.0/6.0, 1.0/6.0, 1.0/6.0, 1.0/6.0);
        return RelationalStyleVector(organizer / s, listener / s, doer / s, energizer / s, strategist / s, connector / s);
    }

    static double euclideanDistance(const RelationalStyleVector& a, const RelationalStyleVector& b) {
        RelationalStyleVector na = a.normalized();
        RelationalStyleVector nb = b.normalized();
        double d = std::pow(na.organizer - nb.organizer, 2) +
                   std::pow(na.listener - nb.listener, 2) +
                   std::pow(na.doer - nb.doer, 2) +
                   std::pow(na.energizer - nb.energizer, 2) +
                   std::pow(na.strategist - nb.strategist, 2) +
                   std::pow(na.connector - nb.connector, 2);
        return std::sqrt(d);
    }
};

// Canonical Blueprint for a Ministry Category
struct MinistryBlueprint {
    int id = 0;
    QString categoryKey;
    QString displayName;
    QString primaryArchetype;
    QString secondaryArchetype;
    RelationalStyleVector targetVector;
    QStringList requiredGifts;
    QString description;
    bool isCustom = false;
    bool isArchived = false;

    MinistryBlueprint() = default;
    MinistryBlueprint(const QString& cat, const QString& name, const QString& pri, const QString& sec,
                      const RelationalStyleVector& vec, const QStringList& gifts, const QString& desc,
                      bool custom = false, bool archived = false, int bpId = 0)
        : id(bpId), categoryKey(cat), displayName(name), primaryArchetype(pri), secondaryArchetype(sec),
          targetVector(vec), requiredGifts(gifts), description(desc), isCustom(custom), isArchived(archived) {}
};

class MinistryArchetypeRegistry {
public:
    static QString archetypeToString(ChurchArchetype a) {
        switch (a) {
            case ChurchArchetype::Catalyst: return "Catalyst";
            case ChurchArchetype::Shepherd: return "Shepherd";
            case ChurchArchetype::Architect: return "Architect";
            case ChurchArchetype::Spark: return "Spark";
            case ChurchArchetype::Anchor: return "Anchor";
            default: return "None";
        }
    }

    static QString archetypeName(ChurchArchetype a) {
        return archetypeToString(a);
    }

    static ChurchArchetype stringToArchetype(const QString& str) {
        QString s = str.trimmed();
        if (s.compare("Catalyst", Qt::CaseInsensitive) == 0 || s.contains("Catalyst")) return ChurchArchetype::Catalyst;
        if (s.compare("Shepherd", Qt::CaseInsensitive) == 0 || s.contains("Shepherd")) return ChurchArchetype::Shepherd;
        if (s.compare("Architect", Qt::CaseInsensitive) == 0 || s.contains("Architect")) return ChurchArchetype::Architect;
        if (s.compare("Spark", Qt::CaseInsensitive) == 0 || s.contains("Spark")) return ChurchArchetype::Spark;
        if (s.compare("Anchor", Qt::CaseInsensitive) == 0 || s.contains("Anchor")) return ChurchArchetype::Anchor;
        return ChurchArchetype::None;
    }

    static QString archetypeIcon(ChurchArchetype a) {
        switch (a) {
            case ChurchArchetype::Catalyst: return "🔥";
            case ChurchArchetype::Shepherd: return "🌿";
            case ChurchArchetype::Architect: return "📐";
            case ChurchArchetype::Spark: return "⚡";
            case ChurchArchetype::Anchor: return "⚓";
            default: return "👤";
        }
    }

    static QString archetypeIconName(ChurchArchetype a) {
        switch (a) {
            case ChurchArchetype::Catalyst: return "sparkles";
            case ChurchArchetype::Shepherd: return "heart-handshake";
            case ChurchArchetype::Architect: return "layers";
            case ChurchArchetype::Spark: return "zap";
            case ChurchArchetype::Anchor: return "shield-check";
            default: return "user";
        }
    }

    static QColor archetypeColor(ChurchArchetype a) {
        switch (a) {
            case ChurchArchetype::Catalyst: return QColor("#fab387"); // Warm Gold / Orange
            case ChurchArchetype::Shepherd: return QColor("#a6e3a1"); // Green / Nurture
            case ChurchArchetype::Architect: return QColor("#89b4fa"); // Blue / Systems
            case ChurchArchetype::Spark: return QColor("#f9e2af"); // Yellow / High Energy
            case ChurchArchetype::Anchor: return QColor("#cba6f7"); // Purple / Steadfast
            default: return QColor("#cdd6f4");
        }
    }

    // Mathematical Conversion Matrix M: Archetype -> 6 Relational Styles
    static RelationalStyleVector archetypeToStyleVector(ChurchArchetype a) {
        switch (a) {
            case ChurchArchetype::Catalyst:
                return RelationalStyleVector(10, 15, 20, 40, 15, 0);
            case ChurchArchetype::Shepherd:
                return RelationalStyleVector(10, 45, 10, 5, 0, 30);
            case ChurchArchetype::Architect:
                return RelationalStyleVector(40, 10, 35, 0, 15, 0);
            case ChurchArchetype::Spark:
                return RelationalStyleVector(15, 10, 15, 35, 0, 25);
            case ChurchArchetype::Anchor:
                return RelationalStyleVector(25, 25, 40, 0, 10, 0);
            default:
                return RelationalStyleVector(25, 25, 40, 0, 10, 0); // Default to Anchor
        }
    }

    // Blended Person Vector (70% Primary + 30% Secondary)
    static RelationalStyleVector personStyleVector(ChurchArchetype primary, ChurchArchetype secondary) {
        RelationalStyleVector v1 = archetypeToStyleVector(primary);
        if (secondary == ChurchArchetype::None || secondary == primary) {
            return v1;
        }
        RelationalStyleVector v2 = archetypeToStyleVector(secondary);
        return RelationalStyleVector(
            0.70 * v1.organizer + 0.30 * v2.organizer,
            0.70 * v1.listener + 0.30 * v2.listener,
            0.70 * v1.doer + 0.30 * v2.doer,
            0.70 * v1.energizer + 0.30 * v2.energizer,
            0.70 * v1.strategist + 0.30 * v2.strategist,
            0.70 * v1.connector + 0.30 * v2.connector
        );
    }

    // All 11 Canonical Ministry Blueprints
    static QList<MinistryBlueprint> canonicalBlueprints() {
        return {
            {
                "Ujier", "Ujier / Usher & Greeters",
                "Spark", "Anchor",
                RelationalStyleVector(20, 30, 10, 0, 0, 40),
                {"Hospitality", "Helps"},
                "Warm greeting, guest hospitality, sanctuary seating, and door ministry."
            },
            {
                "Multimedia", "Multimedia & AV Tech",
                "Architect", "Anchor",
                RelationalStyleVector(30, 10, 40, 0, 20, 0),
                {"Worship / Music", "Administration"},
                "Audio engineering, video streaming, slides, lighting, and technical execution."
            },
            {
                "Alabanza", "Alabanza / Worship",
                "Catalyst", "Spark",
                RelationalStyleVector(15, 30, 25, 30, 0, 0),
                {"Worship / Music", "Leadership"},
                "Musical leadership, stage presence, congregational worship, and rehearsal coordination."
            },
            {
                "Infantil", "Infantil / Children's Ministry",
                "Shepherd", "Spark",
                RelationalStyleVector(30, 25, 10, 35, 0, 0),
                {"Teaching", "Pastoral Care"},
                "Sunday school teaching, child care, energetic activities, and family check-in."
            },
            {
                "Mantenimiento", "Mantenimiento / Maintenance",
                "Anchor", "Architect",
                RelationalStyleVector(20, 20, 60, 0, 0, 0),
                {"Helps"},
                "Facility upkeep, building repairs, physical setups, and logistical support."
            },
            {
                "Jardineria", "Jardineria / Gardening",
                "Anchor", "Architect",
                RelationalStyleVector(20, 20, 60, 0, 0, 0),
                {"Helps"},
                "Church grounds landscaping, lawn care, plant maintenance, and exterior beauty."
            },
            {
                "Misiones", "Misiones / Missions & Outreach",
                "Spark", "Catalyst",
                RelationalStyleVector(30, 0, 20, 20, 30, 0),
                {"Evangelism", "Leadership"},
                "Community benevolence, local evangelism, regional outreaches, and cross-cultural missions."
            },
            {
                "Grupos Vida", "Grupos Vida / Life Groups",
                "Shepherd", "Catalyst",
                RelationalStyleVector(20, 40, 0, 10, 0, 30),
                {"Pastoral Care", "Teaching"},
                "Relational small groups, host home fellowship, Bible study, and discipleship."
            },
            {
                "Oracion", "Oracion / Prayer & Intercession",
                "Anchor", "Shepherd",
                RelationalStyleVector(10, 60, 20, 0, 10, 0),
                {"Pastoral Care", "Prophecy"},
                "Weekly prayer vigils, intercessory teams, altar ministry, and pastoral prayer support."
            },
            {
                "Eventos", "Eventos / Special Events",
                "Architect", "Spark",
                RelationalStyleVector(30, 0, 20, 20, 30, 0),
                {"Administration", "Hospitality"},
                "Conferences, banquets, seasonal outreaches, logistics, and guest coordination."
            },
            {
                "Other", "Other / Custom Ministry",
                "Shepherd", "Anchor",
                RelationalStyleVector(20, 30, 20, 10, 10, 10),
                {"Helps", "Pastoral Care"},
                "Custom ministry or small group with admin-defined archetype profile."
            }
        };
    }

    static RelationalStyleVector calculateTargetVector(const QString& priStr, const QString& secStr) {
        ChurchArchetype pri = stringToArchetype(priStr);
        ChurchArchetype sec = stringToArchetype(secStr);
        return personStyleVector(pri, sec);
    }

    static QList<MinistryBlueprint> allBlueprints(bool includeArchived = false) {
        QSqlDatabase db = QSqlDatabase::database();
        if (db.isOpen()) {
            QSqlQuery q("SELECT id, category_key, display_name, ideal_primary, ideal_secondary, "
                        "weight_organizer, weight_listener, weight_doer, weight_energizer, weight_strategist, weight_connector, "
                        "required_gifts, description, is_custom, COALESCE(is_archived, 0) "
                        "FROM ministry_archetypes ORDER BY is_custom ASC, id ASC", db);
            if (q.exec() && q.next()) {
                QList<MinistryBlueprint> list;
                do {
                    bool isArch = q.value(14).toInt() == 1;
                    if (!includeArchived && isArch) continue;
                    MinistryBlueprint bp;
                    bp.id = q.value(0).toInt();
                    bp.categoryKey = q.value(1).toString();
                    bp.displayName = q.value(2).toString();
                    bp.primaryArchetype = q.value(3).toString();
                    bp.secondaryArchetype = q.value(4).toString();
                    bp.targetVector = RelationalStyleVector(
                        q.value(5).toDouble(), q.value(6).toDouble(), q.value(7).toDouble(),
                        q.value(8).toDouble(), q.value(9).toDouble(), q.value(10).toDouble()
                    );
                    bp.requiredGifts = q.value(11).toString().split(",", Qt::SkipEmptyParts);
                    for (QString& g : bp.requiredGifts) g = g.trimmed();
                    bp.description = q.value(12).toString();
                    bp.isCustom = q.value(13).toInt() == 1;
                    bp.isArchived = isArch;
                    list.append(bp);
                } while (q.next());
                if (!list.isEmpty()) return list;
            }
        }
        return canonicalBlueprints();
    }

    static MinistryBlueprint getBlueprint(const QString& categoryKey) {
        QString key = categoryKey.trimmed().toLower();
        for (const auto& bp : allBlueprints(true)) {
            if (key.contains(bp.categoryKey.toLower()) || bp.displayName.toLower().contains(key)) {
                return bp;
            }
        }
        return canonicalBlueprints().last(); // Return "Other"
    }
};

#endif // MINISTRYARCHETYPES_H
