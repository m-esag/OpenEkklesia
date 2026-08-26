#ifndef CHURCHPROFILE_H
#define CHURCHPROFILE_H

// Universal church profile + PDF branding header.
//
// Loads the church metadata from QSettings (churchName, churchSubtitle,
// churchAddress, churchPhone, churchEmail, churchWebsite, churchTaxId,
// churchLogo) and renders it onto printed outputs. Team decision (Settings
// brief Q3): wire into donor receipts (with Tax ID), service run sheets
// (without Tax ID), vault/board documents, and the people directory.

#include <QString>
#include <QPainter>

class ChurchProfile {
public:
    static QString name();
    static QString subtitle();
    static QString address();
    static QString phone();
    static QString email();
    static QString website();
    static QString taxId();
    static QString logoPath();

    // HTML banner for QTextDocument/QPdfWriter exports. includeTaxId is off
    // for outputs where the Tax ID does not belong (e.g. service run sheets).
    static QString renderHtmlHeader(const QString& reportTitle,
                                    const QString& reportSubtitle = QString(),
                                    bool includeTaxId = true);

    // Painter header for direct QPainter PDF exports: logo top-right, church
    // name, report title, and a contact/tax line.
    static void renderPainterHeader(QPainter& painter, int pageWidth,
                                    const QString& reportTitle,
                                    bool includeTaxId = true);
};

#endif // CHURCHPROFILE_H
