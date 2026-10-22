#include "givingstatementgenerator.h"
#include "churchprofile.h"
#include "appdateformatter.h"
#include "appcurrency.h"
#include "databasemanager.h"

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QPdfWriter>
#include <QPainter>
#include <QTextDocument>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QDate>
#include <QFileInfo>
#include <QMarginsF>
#include <QPageSize>

QString GivingStatementGenerator::generateHtmlStatement(int personId, int taxYear, bool includeSpouseJoint) {
    if (personId <= 0) return QString();

    QSqlDatabase db = DatabaseManager::instance().database();

    // 1. Fetch primary person & household data
    QString pFirst, pLast, familyName, hAddress1, hCity, hZip;
    {
        QSqlQuery q(db);
        q.prepare("SELECT p.first_name, p.last_name, p.household_id, "
                  "       h.family_name, h.address_line1, h.city, h.zip_code "
                  "FROM people p "
                  "LEFT JOIN households h ON p.household_id = h.id "
                  "WHERE p.id = ?");
        q.addBindValue(personId);
        if (q.exec() && q.next()) {
            pFirst = q.value(0).toString().trimmed();
            pLast = q.value(1).toString().trimmed();
            familyName = q.value(3).toString().trimmed();
            hAddress1 = q.value(4).toString().trimmed();
            hCity = q.value(5).toString().trimmed();
            hZip = q.value(6).toString().trimmed();
        } else {
            return QString();
        }
    }

    // 2. Check for spouse link
    int spouseId = 0;
    QString sFirst, sLast;
    {
        QSqlQuery sq(db);
        sq.prepare("SELECT p.id, p.first_name, p.last_name "
                   "FROM entity_relationships r "
                   "JOIN people p ON (CASE WHEN r.source_type = 'Person' AND r.source_id = :me THEN r.target_id ELSE r.source_id END) = p.id "
                   "WHERE r.relationship_type = 'Spouse' "
                   "  AND ((r.source_type = 'Person' AND r.source_id = :me) OR (r.target_type = 'Person' AND r.target_id = :me)) "
                   "LIMIT 1");
        sq.bindValue(":me", personId);
        if (sq.exec() && sq.next()) {
            spouseId = sq.value(0).toInt();
            sFirst = sq.value(1).toString().trimmed();
            sLast = sq.value(2).toString().trimmed();
        }
    }

    QList<int> targetPersonIds;
    targetPersonIds.append(personId);

    QString donorDisplayName;
    bool isJoint = false;
    if (includeSpouseJoint && spouseId > 0) {
        isJoint = true;
        targetPersonIds.append(spouseId);
        if (pLast.compare(sLast, Qt::CaseInsensitive) == 0) {
            donorDisplayName = QString("%1 & %2 %3").arg(pFirst, sFirst, pLast);
        } else {
            donorDisplayName = QString("%1 %2 & %3 %4").arg(pFirst, pLast, sFirst, sLast);
        }
    } else {
        donorDisplayName = QString("%1 %2").arg(pFirst, pLast);
    }

    // Format Mailing Address
    QString mailingAddress;
    if (!hAddress1.isEmpty()) {
        mailingAddress = hAddress1;
        if (!hCity.isEmpty() || !hZip.isEmpty()) {
            mailingAddress += QString(", %1 %2").arg(hCity, hZip).trimmed();
        }
    } else {
        mailingAddress = "Address not on file";
    }

    // 3. Query contributions for target person(s) in selected year
    QString idPlaceholders;
    for (int i = 0; i < targetPersonIds.size(); ++i) {
        if (i > 0) idPlaceholders += ", ";
        idPlaceholders += "?";
    }

    const QString startDate = QString("%1-01-01").arg(taxYear);
    const QString endDate = QString("%1-12-31").arg(taxYear);

    QSqlQuery qGifts(db);
    QString giftSql = QString(
        "SELECT c.contribution_date, f.fund_name, "
        "       COALESCE(NULLIF(c.payment_method, ''), 'Standard'), "
        "       c.amount, COALESCE(c.check_number, ''), COALESCE(c.note, '') "
        "FROM contributions c "
        "JOIN funds f ON c.fund_id = f.id "
        "WHERE c.person_id IN (%1) "
        "  AND c.contribution_date >= ? AND c.contribution_date <= ? "
        "ORDER BY c.contribution_date ASC, c.id ASC").arg(idPlaceholders);

    qGifts.prepare(giftSql);
    for (int pid : targetPersonIds) {
        qGifts.addBindValue(pid);
    }
    qGifts.addBindValue(startDate);
    qGifts.addBindValue(endDate);

    qint64 totalPennies = 0;
    int giftCount = 0;
    QString tableRowsHtml;

    if (qGifts.exec()) {
        while (qGifts.next()) {
            QString cDate = qGifts.value(0).toString();
            QString fund = qGifts.value(1).toString();
            QString method = qGifts.value(2).toString();
            qint64 amountPennies = qGifts.value(3).toLongLong();
            QString checkNum = qGifts.value(4).toString().trimmed();
            QString note = qGifts.value(5).toString().trimmed();

            totalPennies += amountPennies;
            giftCount++;

            QString methodCol = method;
            if (!checkNum.isEmpty()) {
                methodCol += QString(" #%1").arg(checkNum);
            }

            double dollars = amountPennies / 100.0;
            QString formattedDollars = QString("%1%2").arg(appCurrencySymbol(), QString::number(dollars, 'f', 2));

            tableRowsHtml += QString(
                "<tr>"
                "<td style='padding: 6px 8px; border-bottom: 1px solid #d0d7de; font-size: 11px;'>%1</td>"
                "<td style='padding: 6px 8px; border-bottom: 1px solid #d0d7de; font-size: 11px;'><b>%2</b></td>"
                "<td style='padding: 6px 8px; border-bottom: 1px solid #d0d7de; font-size: 11px;'>%3</td>"
                "<td style='padding: 6px 8px; border-bottom: 1px solid #d0d7de; font-size: 11px;'>%4</td>"
                "<td style='padding: 6px 8px; border-bottom: 1px solid #d0d7de; font-size: 11px; text-align: right;'><b>%5</b></td>"
                "</tr>\n"
            ).arg(cDate, fund, methodCol, note.isEmpty() ? "—" : note, formattedDollars);
        }
    }

    double totalDollars = totalPennies / 100.0;
    QString formattedTotal = QString("%1%2").arg(appCurrencySymbol(), QString::number(totalDollars, 'f', 2));

    // 4. Build Complete HTML Document
    QString html = "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
                   "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; color: #24292f; margin: 0; padding: 10px; }"
                   "table.gifts { width: 100%; border-collapse: collapse; margin-top: 15px; }"
                   "table.gifts th { background-color: #f6f8fa; color: #24292f; padding: 8px; border-bottom: 2px solid #afb8c1; text-align: left; font-size: 11px; font-weight: bold; }"
                   "table.gifts tr:nth-child(even) { background-color: #fafbfc; }"
                   "</style></head><body>\n";

    // Official letterhead
    html += ChurchProfile::renderHtmlHeader("Donor Contribution Statement",
                                            QString("Tax Year %1").arg(taxYear),
                                            /*includeTaxId=*/true);

    // Donor & Period Summary Card
    html += "<div style='margin-top: 16px; margin-bottom: 16px; padding: 12px; background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 6px;'>"
            "<table style='width: 100%; border-collapse: collapse; border: none; font-size: 12px;'>"
            "<tr>" +
            QString("<td style='padding: 3px 0; border: none;'><b>Donor Name:</b> %1%2</td>").arg(donorDisplayName, isJoint ? " <i>(Joint Household)</i>" : "") +
            QString("<td style='padding: 3px 0; border: none; text-align: right;'><b>Statement Date:</b> %1</td>").arg(AppDateFormatter::formatDate(QDate::currentDate())) +
            "</tr><tr>" +
            QString("<td style='padding: 3px 0; border: none;'><b>Address:</b> %1</td>").arg(mailingAddress) +
            QString("<td style='padding: 3px 0; border: none; text-align: right;'><b>Tax Period:</b> Jan 1, %1 – Dec 31, %1</td>").arg(taxYear) +
            "</tr></table></div>\n";

    // Summary Highlight
    html += "<div style='margin-bottom: 16px; padding: 10px 14px; background-color: #eef9fd; border-left: 4px solid #1e66f5; border-radius: 4px;'>"
            "<table style='width: 100%; border: none; font-size: 13px;'>"
            "<tr>" +
            QString("<td style='border: none;'><b>Total Eligible Contributions Recorded:</b> <span style='font-size: 16px; font-weight: bold; color: #1e66f5;'>%1</span></td>").arg(formattedTotal) +
            QString("<td style='border: none; text-align: right; color: #57606a;'>Number of Gifts: <b>%1</b></td>").arg(giftCount) +
            "</tr></table></div>\n";

    // Itemized Donations Table
    if (giftCount > 0) {
        html += "<table class='gifts'>"
                "<thead><tr>"
                "<th style='width: 15%;'>Date</th>"
                "<th style='width: 28%;'>Designated Fund</th>"
                "<th style='width: 22%;'>Payment Method</th>"
                "<th style='width: 20%;'>Reference / Note</th>"
                "<th style='width: 15%; text-align: right;'>Amount</th>"
                "</tr></thead><tbody>\n";
        html += tableRowsHtml;
        html += QString(
            "</tbody><tfoot><tr style='background-color: #f6f8fa; font-weight: bold;'>"
            "<td colspan='4' style='padding: 8px; text-align: right; border-top: 2px solid #afb8c1; font-size: 12px;'>Total Contributions:</td>"
            "<td style='padding: 8px; text-align: right; border-top: 2px solid #afb8c1; font-size: 13px; color: #1e66f5;'>%1</td>"
            "</tr></tfoot></table>\n"
        ).arg(formattedTotal);
    } else {
        html += QString("<p style='padding: 20px; text-align: center; color: #6e7781; background-color: #f6f8fa; border-radius: 6px;'>"
                        "No contributions recorded on file for %1 in calendar year %2.</p>\n")
                    .arg(donorDisplayName, QString::number(taxYear));
    }

    // Official tax and ecclesiastical disclosure
    html += "<div style='margin-top: 30px; padding-top: 10px; border-top: 1px solid #d0d7de; font-size: 10px; color: #57606a; line-height: 1.4;'>"
            "<p><b>Official Record Notice:</b> This annual contribution statement reflects financial tithes and offerings recorded by OpenEkklesia for the period specified above. Please retain this statement for your personal tax and financial records.</p>"
            "<p><i>Unless otherwise explicitly stated in writing, no goods or services were provided by OpenEkklesia in whole or partial consideration for the contributions listed above, other than intangible religious benefits.</i></p>"
            "</div></body></html>";

    return html;
}

QString GivingStatementGenerator::exportPdfWithDialog(QWidget* parent, int personId, const QString& defaultDonorName) {
    if (personId <= 0) {
        QMessageBox::warning(parent, "No Selection", "Please select a valid person or contributor profile first.");
        return QString();
    }

    QSqlDatabase db = DatabaseManager::instance().database();

    // Check spouse link
    int spouseId = 0;
    QString spouseName;
    {
        QSqlQuery sq(db);
        sq.prepare("SELECT p.id, p.first_name || ' ' || p.last_name "
                   "FROM entity_relationships r "
                   "JOIN people p ON (CASE WHEN r.source_type = 'Person' AND r.source_id = :me THEN r.target_id ELSE r.source_id END) = p.id "
                   "WHERE r.relationship_type = 'Spouse' "
                   "  AND ((r.source_type = 'Person' AND r.source_id = :me) OR (r.target_type = 'Person' AND r.target_id = :me)) "
                   "LIMIT 1");
        sq.bindValue(":me", personId);
        if (sq.exec() && sq.next()) {
            spouseId = sq.value(0).toInt();
            spouseName = sq.value(1).toString().trimmed();
        }
    }

    // Determine available tax years
    QList<int> availableYears;
    {
        QSqlQuery yq(db);
        yq.prepare("SELECT DISTINCT strftime('%Y', contribution_date) FROM contributions WHERE person_id IN (?, ?) AND contribution_date IS NOT NULL ORDER BY 1 DESC");
        yq.addBindValue(personId);
        yq.addBindValue(spouseId > 0 ? spouseId : personId);
        if (yq.exec()) {
            while (yq.next()) {
                int yr = yq.value(0).toInt();
                if (yr > 1900 && !availableYears.contains(yr)) availableYears.append(yr);
            }
        }
    }

    int currentYear = QDate::currentDate().year();
    if (!availableYears.contains(currentYear)) availableYears.prepend(currentYear);
    if (!availableYears.contains(currentYear - 1)) availableYears.append(currentYear - 1);

    // Interactive Modal Dialog
    QDialog dlg(parent);
    dlg.setWindowTitle("Generate Annual Donor Contribution Statement");
    dlg.resize(440, 240);

    QVBoxLayout* rootLayout = new QVBoxLayout(&dlg);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(14);

    QLabel* titleLbl = new QLabel("Select Tax Year & Statement Options", &dlg);
    titleLbl->setStyleSheet("font-weight: bold; font-size: 14px;");
    rootLayout->addWidget(titleLbl);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(10);

    QString displayPerson = defaultDonorName.isEmpty() ? QString("Member #%1").arg(personId) : defaultDonorName;
    QLabel* lblPerson = new QLabel(displayPerson, &dlg);
    lblPerson->setStyleSheet("font-weight: bold; color: #1e66f5;");
    form->addRow("Contributor:", lblPerson);

    QComboBox* cmbYear = new QComboBox(&dlg);
    for (int y : availableYears) {
        cmbYear->addItem(QString("Tax Year %1").arg(y), y);
    }
    form->addRow("Tax Year:", cmbYear);

    QCheckBox* chkJoint = nullptr;
    if (spouseId > 0) {
        chkJoint = new QCheckBox(QString("Generate Joint Statement with spouse (%1)").arg(spouseName), &dlg);
        chkJoint->setChecked(true);
        form->addRow("Joint Household:", chkJoint);
    }

    rootLayout->addLayout(form);

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    QPushButton* btnGenerate = new QPushButton("Generate Statement PDF...", &dlg);
    btnGenerate->setObjectName("primaryBtn");
    btnGenerate->setDefault(true);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnGenerate);
    rootLayout->addLayout(btnRow);

    QObject::connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(btnGenerate, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) {
        return QString();
    }

    int selectedYear = cmbYear->currentData().toInt();
    bool includeJoint = (chkJoint != nullptr && chkJoint->isChecked());

    QString cleanName = displayPerson.toLower();
    cleanName.replace(QRegularExpression("[^a-z0-9]+"), "_");
    cleanName = cleanName.trimmed();
    if (cleanName.startsWith("_")) cleanName.remove(0, 1);
    if (cleanName.endsWith("_")) cleanName.chop(1);
    if (cleanName.isEmpty()) cleanName = QString("donor_%1").arg(personId);

    QString defaultFile = QString("Giving_Statement_%1_%2.pdf").arg(cleanName, QString::number(selectedYear));
    QString savePath = QFileDialog::getSaveFileName(parent, "Save Donor Contribution Statement (PDF)",
                                                    defaultFile, "PDF Documents (*.pdf)");
    if (savePath.isEmpty()) {
        return QString();
    }

    QString html = generateHtmlStatement(personId, selectedYear, includeJoint);
    if (html.isEmpty()) {
        QMessageBox::warning(parent, "Error", "Could not generate statement content.");
        return QString();
    }

    QPdfWriter writer(savePath);
    writer.setPageSize(QPageSize(QPageSize::Letter));
    writer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&writer);

    QFileInfo fi(savePath);
    if (!fi.exists() || fi.size() == 0) {
        QMessageBox::critical(parent, "Export Failed", "Failed to write PDF to disk. Please check disk space and permissions.");
        return QString();
    }

    QMessageBox::information(parent, "Statement Exported",
                             QString("Donor contribution statement successfully generated and saved to:\n%1").arg(savePath));
    return savePath;
}
