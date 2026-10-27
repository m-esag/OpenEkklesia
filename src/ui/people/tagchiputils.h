#pragma once
#include <QString>
#include <QStringList>

// Phase 1 Wave 2b helpers: stateless transformations shared by the profile
// tag/style/gift chips, unit-testable without a PeopleWidget.
class TagChipUtils {
public:
    static QStringList splitTags(const QString& raw);
    static QString joinTags(const QStringList& tags);
    static QString displayTag(const QString& raw, bool checked);
};
