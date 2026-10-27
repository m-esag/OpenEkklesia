#pragma once
#include <QString>
#include <QStringList>

// Phase 1 decomposition: stateless helpers extracted from PeopleWidget so they
// can be unit-tested without constructing the widget.
class PersonCardUtils {
public:
    static QStringList parseCsvLine(const QString& line);
    static QString generateVCard(const QString& firstName, const QString& lastName, const QString& phone,
                                 const QString& email, const QString& bday, const QString& status,
                                 const QString& careFlag, const QString& notes);
};
