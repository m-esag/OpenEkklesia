#include "churchprofile.h"
#include "appsettings.h"
#include <QImage>
#include <QBuffer>
#include <QPixmap>

namespace {
AppSettings appSettings() { return AppSettings(); }
}

QString ChurchProfile::name() {
    // Neutral fallback: this name prints on giving statements, so never
    // default to a branded placeholder. Real name comes from the first-run
    // wizard or Settings -> General.
    return appSettings().value("churchName", "My Church").toString();
}
QString ChurchProfile::subtitle() {
    return appSettings().value("churchSubtitle").toString();
}
QString ChurchProfile::address() {
    return appSettings().value("churchAddress").toString().trimmed();
}
QString ChurchProfile::phone() {
    return appSettings().value("churchPhone").toString().trimmed();
}
QString ChurchProfile::email() {
    return appSettings().value("churchEmail").toString().trimmed();
}
QString ChurchProfile::website() {
    return appSettings().value("churchWebsite").toString().trimmed();
}
QString ChurchProfile::taxId() {
    return appSettings().value("churchTaxId").toString().trimmed();
}
QString ChurchProfile::logoPath() {
    return appSettings().value("churchLogo").toString().trimmed();
}

QString ChurchProfile::renderHtmlHeader(const QString& reportTitle,
                                        const QString& reportSubtitle,
                                        bool includeTaxId) {
    QString logoHtml;
    const QString logo = logoPath();
    if (!logo.isEmpty()) {
        QImage img(logo);
        if (!img.isNull()) {
            QBuffer buf;
            buf.open(QIODevice::WriteOnly);
            img.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buf, "PNG");
            logoHtml = QString("<img src='data:image/png;base64,%1' style='height:56px; float: right;'/>")
                           .arg(QString::fromLatin1(buf.data().toBase64()));
        }
    }

    QStringList contact;
    if (!address().isEmpty()) contact << address();
    if (!phone().isEmpty()) contact << phone();
    if (!email().isEmpty()) contact << email();
    if (!website().isEmpty()) contact << website();

    QString html;
    html += "<div style='border-bottom: 2px solid #1e66f5; padding-bottom: 8px; margin-bottom: 12px;'>";
    html += logoHtml;
    html += "<h1 style='color: #1e66f5; margin: 0;'>" + name().toHtmlEscaped() + "</h1>";
    if (!subtitle().isEmpty()) {
        html += "<p style='margin: 0; color: #666; font-size: 12px;'>" + subtitle().toHtmlEscaped() + "</p>";
    }
    html += "<h3 style='margin: 6px 0 0;'>" + reportTitle.toHtmlEscaped() + "</h3>";
    if (!reportSubtitle.isEmpty()) {
        html += "<p style='margin: 0; color: #666; font-size: 12px;'>" + reportSubtitle.toHtmlEscaped() + "</p>";
    }
    if (!contact.isEmpty()) {
        html += "<p style='margin: 4px 0 0; font-size: 11px; color: #666;'>" + contact.join(" · ").toHtmlEscaped() + "</p>";
    }
    if (includeTaxId && !taxId().isEmpty()) {
        html += "<p style='margin: 0; font-size: 11px; color: #666;'>Tax ID: " + taxId().toHtmlEscaped() + "</p>";
    }
    html += "</div>";
    return html;
}

void ChurchProfile::renderPainterHeader(QPainter& painter, int pageWidth,
                                        const QString& reportTitle,
                                        bool includeTaxId) {
    const int margin = 60;
    int y = margin;

    // Logo top-right.
    const QString logo = logoPath();
    if (!logo.isEmpty()) {
        QPixmap pix(logo);
        if (!pix.isNull()) {
            QPixmap scaled = pix.scaledToHeight(56, Qt::SmoothTransformation);
            painter.drawPixmap(pageWidth - margin - scaled.width(), y, scaled);
        }
    }

    painter.save();
    QFont titleFont = painter.font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(margin, y + 18, name());
    painter.restore();

    y += 30;
    if (!subtitle().isEmpty()) {
        painter.save();
        QFont subFont = painter.font();
        subFont.setPointSize(9);
        painter.setFont(subFont);
        painter.drawText(margin, y, subtitle());
        painter.restore();
        y += 14;
    }

    painter.save();
    QFont repFont = painter.font();
    repFont.setPointSize(13);
    repFont.setBold(true);
    painter.setFont(repFont);
    painter.drawText(margin, y + 6, reportTitle);
    painter.restore();
    y += 22;

    QStringList contact;
    if (!address().isEmpty()) contact << address();
    if (!phone().isEmpty()) contact << phone();
    if (!email().isEmpty()) contact << email();
    if (includeTaxId && !taxId().isEmpty()) contact << "Tax ID: " + taxId();
    if (!contact.isEmpty()) {
        painter.save();
        QFont cFont = painter.font();
        cFont.setPointSize(8);
        painter.setFont(cFont);
        painter.drawText(margin, y, contact.join("  ·  "));
        painter.restore();
        y += 12;
    }

    painter.drawLine(margin, y + 6, pageWidth - margin, y + 6);
}
