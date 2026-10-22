#ifndef GIVINGSTATEMENTGENERATOR_H
#define GIVINGSTATEMENTGENERATOR_H

#include <QString>
#include <QWidget>

// Universal Annual Donor Contribution Statement generator.
// Generates professional, church-letterhead statements for tax and record-keeping.
// Supports both single-donor and joint/household donor statements (e.g., "Bobby & Sally Smith").

class GivingStatementGenerator {
public:
    // Generates standalone HTML for an annual contribution statement.
    // Includes ChurchProfile letterhead, donor and household details, itemized
    // gifts table, totals, and standard intangible religious benefit disclosure.
    static QString generateHtmlStatement(int personId, int taxYear, bool includeSpouseJoint = true);

    // Displays an interactive modal dialog allowing the user to select the tax year
    // and whether to generate a joint statement with their spouse (if linked).
    // Prompts for PDF file save location and writes the formatted PDF document.
    // Returns the saved file path, or an empty QString if cancelled.
    static QString exportPdfWithDialog(QWidget* parent, int personId, const QString& defaultDonorName = QString());
};

#endif // GIVINGSTATEMENTGENERATOR_H
