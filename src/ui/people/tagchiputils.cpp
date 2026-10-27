#include "tagchiputils.h"

QStringList TagChipUtils::splitTags(const QString& raw) {
    QStringList out = raw.split(",", Qt::SkipEmptyParts);
    for (int i = 0; i < out.size(); ++i) out[i] = out[i].trimmed();
    return out;
}

QString TagChipUtils::joinTags(const QStringList& tags) {
    return tags.join(", ");
}

QString TagChipUtils::displayTag(const QString& raw, bool checked) {
    return checked ? (QStringLiteral("✓ ") + raw) : raw;
}
