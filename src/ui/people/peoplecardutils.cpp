#include "peoplecardutils.h"
#include <QChar>

QStringList PersonCardUtils::parseCsvLine(const QString& line) {
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.length(); ++i) {
        QChar c = line.at(i);
        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line.at(i + 1) == '"') {
                // Escaped double quote
                current += '"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.append(current.trimmed());
            current.clear();
        } else {
            current += c;
        }
    }
    fields.append(current.trimmed());
    return fields;
}

QString PersonCardUtils::generateVCard(const QString& firstName, const QString& lastName, const QString& phone,
                                       const QString& email, const QString& bday, const QString& status,
                                       const QString& careFlag, const QString& notes) {
    QString vcf;
    vcf += "BEGIN:VCARD\r\n";
    vcf += "VERSION:3.0\r\n";
    vcf += QString("N:%1;%2;;;\r\n").arg(lastName, firstName);
    vcf += QString("FN:%1 %2\r\n").arg(firstName, lastName).trimmed();
    if (!phone.isEmpty()) {
        vcf += QString("TEL;TYPE=CELL,VOICE:%1\r\n").arg(phone);
    }
    if (!email.isEmpty()) {
        vcf += QString("EMAIL;TYPE=INTERNET,PREF:%1\r\n").arg(email);
    }
    if (!bday.isEmpty() && bday != "1900-01-01") {
        vcf += QString("BDAY:%1\r\n").arg(bday);
    }
    if (!status.isEmpty()) {
        vcf += QString("ROLE:%1\r\n").arg(status);
        vcf += QString("CATEGORIES:OpenEkklesia,%1\r\n").arg(status);
    } else {
        vcf += "CATEGORIES:OpenEkklesia\r\n";
    }
    // Confidentiality: the vCard intentionally carries no care flag and no
    // admin/pastoral notes — a contact export must never leak the care trail.
    Q_UNUSED(careFlag);
    Q_UNUSED(notes);
    vcf += "ORG:OpenEkklesia Congregation\r\n";
    vcf += "END:VCARD\r\n";
    return vcf;
}
