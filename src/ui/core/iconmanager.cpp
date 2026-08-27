#include "iconmanager.h"
#include "thememanager.h"
#include <QPainterPath>
#include <cmath>

IconManager& IconManager::instance() {
    static IconManager mgr;
    return mgr;
}

void IconManager::clearCache() {
    m_pixmapCache.clear();
    m_iconCache.clear();
}

QPixmap IconManager::getPixmap(const QString& name, const QColor& color, int size) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QColor effectiveCol = color.isValid() ? color : tok.textPrimary;
    QString key = QString("%1_%2_%3").arg(name, effectiveCol.name(QColor::HexArgb), QString::number(size));

    if (m_pixmapCache.contains(key)) {
        return m_pixmapCache.value(key);
    }

    QPixmap px(size, size);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintIcon(&p, name, QRectF(0, 0, size, size), effectiveCol);
    p.end();

    m_pixmapCache.insert(key, px);
    return px;
}

QIcon IconManager::getIcon(const QString& name, const QColor& color, int size) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QColor effectiveCol = color.isValid() ? color : tok.textPrimary;
    QString key = QString("%1_%2_%3").arg(name, effectiveCol.name(QColor::HexArgb), QString::number(size));

    if (m_iconCache.contains(key)) {
        return m_iconCache.value(key);
    }

    QPixmap normalPx = getPixmap(name, effectiveCol, size);
    QIcon icon(normalPx);

    // Provide higher resolution pixmap for Retina/HiDPI
    QPixmap hiDpiPx = getPixmap(name, effectiveCol, size * 2);
    icon.addPixmap(hiDpiPx);

    m_iconCache.insert(key, icon);
    return icon;
}

QIcon IconManager::getPrimaryIcon(const QString& name, int size) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    return getIcon(name, ThemeManager::textOn(tok.accentPrimary), size);
}

QIcon IconManager::getDangerIcon(const QString& name, int size) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    return getIcon(name, ThemeManager::textOn(tok.accentDanger), size);
}

void IconManager::paintIcon(QPainter* painter, const QString& name, const QRectF& targetRect, const QColor& color, double strokeWidth) {
    if (!painter || targetRect.isEmpty()) return;

    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QColor effectiveCol = color.isValid() ? color : tok.textPrimary;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Normalize coordinates to 24x24 box
    painter->translate(targetRect.topLeft());
    double scaleX = targetRect.width() / 24.0;
    double scaleY = targetRect.height() / 24.0;
    painter->scale(scaleX, scaleY);

    QPen pen(effectiveCol, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    drawLucidePath(painter, name.trimmed().toLower());

    painter->restore();
}

void IconManager::drawLucidePath(QPainter* painter, const QString& name) {
    QPainterPath path;

    if (name == "users" || name == "people" || name == "congregation") {
        // Lucide: users
        painter->drawEllipse(QRectF(5, 3, 8, 8)); // Left person head (center 9, 7)
        path.moveTo(3, 21);
        path.cubicTo(3, 17, 6, 14, 9, 14);
        path.cubicTo(12, 14, 15, 17, 15, 21);
        painter->drawPath(path);

        QPainterPath rightHead;
        rightHead.arcMoveTo(QRectF(13, 4, 6, 6), 90);
        rightHead.arcTo(QRectF(13, 4, 6, 6), 90, -180);
        painter->drawPath(rightHead);

        QPainterPath rightBody;
        rightBody.moveTo(15, 14);
        rightBody.cubicTo(18, 14, 21, 16.5, 21, 21);
        painter->drawPath(rightBody);
    }
    else if (name == "user" || name == "person" || name == "profile") {
        // Lucide: user
        painter->drawEllipse(QRectF(7, 3, 10, 10));
        path.moveTo(4, 21);
        path.cubicTo(4, 16, 7.5, 14, 12, 14);
        path.cubicTo(16.5, 14, 20, 16, 20, 21);
        painter->drawPath(path);
    }
    else if (name == "user-plus" || name == "add-member") {
        painter->drawEllipse(QRectF(5, 3, 8, 8));
        path.moveTo(2, 21);
        path.cubicTo(2, 16.5, 5, 14, 9, 14);
        path.cubicTo(13, 14, 16, 16.5, 16, 21);
        painter->drawPath(path);

        painter->drawLine(QPointF(19, 8), QPointF(19, 14));
        painter->drawLine(QPointF(16, 11), QPointF(22, 11));
    }
    else if (name == "user-check" || name == "attended") {
        painter->drawEllipse(QRectF(5, 3, 8, 8));
        path.moveTo(2, 21);
        path.cubicTo(2, 16.5, 5, 14, 9, 14);
        path.cubicTo(13, 14, 16, 16.5, 16, 21);
        painter->drawPath(path);

        path.clear();
        path.moveTo(16, 11);
        path.lineTo(18, 13);
        path.lineTo(22, 9);
        painter->drawPath(path);
    }
    else if (name == "network" || name == "graph" || name == "connections" || name == "share-2") {
        // Lucide: network
        painter->drawRect(QRectF(9, 2, 6, 6));
        painter->drawRect(QRectF(2, 16, 6, 6));
        painter->drawRect(QRectF(16, 16, 6, 6));

        painter->drawLine(QPointF(12, 8), QPointF(12, 12));
        painter->drawLine(QPointF(5, 12), QPointF(19, 12));
        painter->drawLine(QPointF(5, 12), QPointF(5, 16));
        painter->drawLine(QPointF(19, 12), QPointF(19, 16));
    }
    else if (name == "heart-handshake" || name == "shepherd" || name == "pastoral" || name == "care") {
        // Lucide: heart-handshake / heart
        path.moveTo(12, 21.35);
        path.cubicTo(3.5, 15, 2, 8.5, 6.5, 4.5);
        path.cubicTo(9.5, 2.5, 12, 5, 12, 5);
        path.cubicTo(12, 5, 14.5, 2.5, 17.5, 4.5);
        path.cubicTo(22, 8.5, 20.5, 15, 12, 21.35);
        painter->drawPath(path);

        painter->drawLine(QPointF(8.5, 12), QPointF(15.5, 12));
    }
    else if (name == "layers" || name == "groups" || name == "ministries") {
        // Lucide: layers
        path.moveTo(12, 2);
        path.lineTo(2, 7);
        path.lineTo(12, 12);
        path.lineTo(22, 7);
        path.closeSubpath();
        painter->drawPath(path);

        QPainterPath p2;
        p2.moveTo(2, 12);
        p2.lineTo(12, 17);
        p2.lineTo(22, 12);
        painter->drawPath(p2);

        QPainterPath p3;
        p3.moveTo(2, 17);
        p3.lineTo(12, 22);
        p3.lineTo(22, 17);
        painter->drawPath(p3);
    }
    else if (name == "list-music" || name == "music" || name == "service" || name == "worship") {
        // Lucide: list-music
        painter->drawLine(QPointF(3, 5), QPointF(13, 5));
        painter->drawLine(QPointF(3, 11), QPointF(10, 11));
        painter->drawLine(QPointF(3, 17), QPointF(10, 17));

        painter->drawEllipse(QRectF(15, 15, 4, 4));
        painter->drawLine(QPointF(19, 17), QPointF(19, 7));
        painter->drawLine(QPointF(19, 7), QPointF(22, 6));
    }
    else if (name == "landmark" || name == "building-2" || name == "facilities" || name == "church") {
        // Lucide: landmark / church
        path.moveTo(2, 6);
        path.lineTo(12, 2);
        path.lineTo(22, 6);
        path.closeSubpath();
        painter->drawPath(path);

        painter->drawLine(QPointF(2, 21), QPointF(22, 21));
        painter->drawLine(QPointF(6, 6), QPointF(6, 21));
        painter->drawLine(QPointF(10, 6), QPointF(10, 21));
        painter->drawLine(QPointF(14, 6), QPointF(14, 21));
        painter->drawLine(QPointF(18, 6), QPointF(18, 21));
    }
    else if (name == "calendar" || name == "planning" || name == "roadmap" || name == "schedule") {
        // Lucide: calendar
        painter->drawRoundedRect(QRectF(3, 4, 18, 17), 3, 3);
        painter->drawLine(QPointF(3, 9), QPointF(21, 9));
        painter->drawLine(QPointF(8, 2), QPointF(8, 5));
        painter->drawLine(QPointF(16, 2), QPointF(16, 5));
    }
    else if (name == "hand-coins" || name == "coins" || name == "giving" || name == "finance" || name == "contributions") {
        // Lucide: circle-dollar-sign / hand-coins
        painter->drawEllipse(QRectF(3, 3, 18, 18));
        painter->drawLine(QPointF(12, 7), QPointF(12, 17));
        path.moveTo(14.5, 9.5);
        path.cubicTo(14.5, 8, 9.5, 8, 9.5, 11);
        path.cubicTo(9.5, 13.5, 14.5, 12.5, 14.5, 15);
        path.cubicTo(14.5, 17.5, 9.5, 17.5, 9.5, 15.5);
        painter->drawPath(path);
    }
    else if (name == "package" || name == "boxes" || name == "inventory" || name == "assets") {
        // Lucide: package
        path.moveTo(12, 2);
        path.lineTo(21, 7);
        path.lineTo(21, 17);
        path.lineTo(12, 22);
        path.lineTo(3, 17);
        path.lineTo(3, 7);
        path.closeSubpath();
        painter->drawPath(path);

        painter->drawLine(QPointF(3, 7), QPointF(12, 12));
        painter->drawLine(QPointF(21, 7), QPointF(12, 12));
        painter->drawLine(QPointF(12, 12), QPointF(12, 22));
    }
    else if (name == "shield-check" || name == "vault" || name == "governance" || name == "security") {
        // Lucide: shield-check
        path.moveTo(12, 2);
        path.lineTo(20, 5);
        path.lineTo(20, 11);
        path.cubicTo(20, 17, 16.5, 20.5, 12, 22);
        path.cubicTo(7.5, 20.5, 4, 17, 4, 11);
        path.lineTo(4, 5);
        path.closeSubpath();
        painter->drawPath(path);

        QPainterPath chk;
        chk.moveTo(9, 11.5);
        chk.lineTo(11, 13.5);
        chk.lineTo(15, 9.5);
        painter->drawPath(chk);
    }
    else if (name == "bar-chart-3" || name == "analytics" || name == "chart") {
        // Lucide: bar-chart-3
        painter->drawLine(QPointF(3, 20), QPointF(21, 20));
        painter->drawLine(QPointF(7, 20), QPointF(7, 10));
        painter->drawLine(QPointF(12, 20), QPointF(12, 4));
        painter->drawLine(QPointF(17, 20), QPointF(17, 14));
    }
    else if (name == "settings" || name == "sliders") {
        // Lucide: settings
        painter->drawEllipse(QRectF(9, 9, 6, 6));
        path.moveTo(12, 2); path.lineTo(12, 5);
        path.moveTo(12, 19); path.lineTo(12, 22);
        path.moveTo(2, 12); path.lineTo(5, 12);
        path.moveTo(19, 12); path.lineTo(22, 12);
        path.moveTo(4.93, 4.93); path.lineTo(7.05, 7.05);
        path.moveTo(16.95, 16.95); path.lineTo(19.07, 19.07);
        path.moveTo(4.93, 19.07); path.lineTo(7.05, 16.95);
        path.moveTo(16.95, 7.05); path.lineTo(19.07, 4.93);
        painter->drawPath(path);
    }
    else if (name == "book-open" || name == "book" || name == "bible") {
        // Lucide: book-open (two open pages)
        path.moveTo(2, 3);
        path.cubicTo(2, 3, 5, 2, 12, 5);
        path.cubicTo(19, 2, 22, 3, 22, 3);
        path.lineTo(22, 19);
        path.cubicTo(22, 19, 19, 18, 12, 21);
        path.cubicTo(5, 18, 2, 19, 2, 19);
        path.closeSubpath();
        painter->drawPath(path);
        painter->drawLine(QPointF(12, 5), QPointF(12, 21));
    }
    else if (name == "arrow-up") {
        painter->drawLine(QPointF(12, 19), QPointF(12, 5));
        path.moveTo(5, 12);
        path.lineTo(12, 5);
        path.lineTo(19, 12);
        painter->drawPath(path);
    }
    else if (name == "arrow-down") {
        painter->drawLine(QPointF(12, 5), QPointF(12, 19));
        path.moveTo(5, 12);
        path.lineTo(12, 19);
        path.lineTo(19, 12);
        painter->drawPath(path);
    }
    else if (name == "arrow-left") {
        painter->drawLine(QPointF(19, 12), QPointF(5, 12));
        path.moveTo(12, 19);
        path.lineTo(5, 12);
        path.lineTo(12, 5);
        painter->drawPath(path);
    }
    else if (name == "arrow-right") {
        painter->drawLine(QPointF(5, 12), QPointF(19, 12));
        path.moveTo(12, 5);
        path.lineTo(19, 12);
        path.lineTo(12, 19);
        painter->drawPath(path);
    }
    else if (name == "home") {
        path.moveTo(3, 9);
        path.lineTo(12, 2);
        path.lineTo(21, 9);
        path.lineTo(21, 20);
        path.cubicTo(21, 21, 20, 22, 19, 22);
        path.lineTo(5, 22);
        path.cubicTo(4, 22, 3, 21, 3, 20);
        path.closeSubpath();
        painter->drawPath(path);
        painter->drawRect(QRectF(9, 14, 6, 8));
    }
    else if (name == "printer") {
        painter->drawRoundedRect(QRectF(6, 9, 12, 8), 1, 1);
        path.moveTo(6, 9);
        path.lineTo(6, 4);
        path.lineTo(18, 4);
        path.lineTo(18, 9);
        painter->drawPath(path);

        QPainterPath paper;
        paper.moveTo(6, 14);
        paper.lineTo(6, 20);
        paper.lineTo(18, 20);
        paper.lineTo(18, 14);
        painter->drawPath(paper);

        painter->drawLine(QPointF(6, 11), QPointF(18, 11));
    }
    else if (name == "zoom-in") {
        painter->drawEllipse(QRectF(3, 3, 14, 14));
        painter->drawLine(QPointF(16.5, 16.5), QPointF(21, 21));
        painter->drawLine(QPointF(10, 7), QPointF(10, 13));
        painter->drawLine(QPointF(7, 10), QPointF(13, 10));
    }
    else if (name == "zoom-out") {
        painter->drawEllipse(QRectF(3, 3, 14, 14));
        painter->drawLine(QPointF(16.5, 16.5), QPointF(21, 21));
        painter->drawLine(QPointF(7, 10), QPointF(13, 10));
    }
    else if (name == "compass" || name == "rocket") {
        painter->drawEllipse(QRectF(2, 2, 20, 20));
        path.moveTo(16.24, 7.76);
        path.lineTo(14.12, 14.12);
        path.lineTo(7.76, 16.24);
        path.lineTo(9.88, 9.88);
        path.closeSubpath();
        painter->drawPath(path);
    }
    else if (name == "command") {
        path.moveTo(18, 9);
        path.cubicTo(19.66, 9, 21, 7.66, 21, 6);
        path.cubicTo(21, 4.34, 19.66, 3, 18, 3);
        path.cubicTo(16.34, 3, 15, 4.34, 15, 6);
        path.lineTo(15, 18);
        path.cubicTo(15, 19.66, 16.34, 21, 18, 21);
        path.cubicTo(19.66, 21, 21, 19.66, 21, 18);
        path.cubicTo(21, 16.34, 19.66, 15, 18, 15);
        path.lineTo(6, 15);
        path.cubicTo(4.34, 15, 3, 16.34, 3, 18);
        path.cubicTo(3, 19.66, 4.34, 21, 6, 21);
        path.cubicTo(7.66, 21, 9, 19.66, 9, 18);
        path.lineTo(9, 6);
        path.cubicTo(9, 4.34, 7.66, 3, 6, 3);
        path.cubicTo(4.34, 3, 3, 4.34, 3, 6);
        path.cubicTo(3, 7.66, 4.34, 9, 6, 9);
        path.lineTo(18, 9);
        painter->drawPath(path);
    }
    else if (name == "help-circle" || name == "help" || name == "info") {
        painter->drawEllipse(QRectF(2, 2, 20, 20));
        path.moveTo(9.09, 9);
        path.cubicTo(9.09, 7.34, 10.34, 6, 12, 6);
        path.cubicTo(13.66, 6, 15, 7.34, 15, 9);
        path.cubicTo(15, 11, 12, 12, 12, 14);
        painter->drawPath(path);
        painter->drawPoint(QPointF(12, 17));
    }
    else if (name == "x" || name == "close") {
        painter->drawLine(QPointF(18, 6), QPointF(6, 18));
        painter->drawLine(QPointF(6, 6), QPointF(18, 18));
    }
    else if (name == "search") {
        painter->drawEllipse(QRectF(3, 3, 14, 14));
        painter->drawLine(QPointF(16.5, 16.5), QPointF(21, 21));
    }
    else if (name == "plus" || name == "add") {
        painter->drawLine(QPointF(12, 5), QPointF(12, 19));
        painter->drawLine(QPointF(5, 12), QPointF(19, 12));
    }
    else if (name == "pencil" || name == "edit") {
        path.moveTo(17, 3);
        path.lineTo(21, 7);
        path.lineTo(7.5, 20.5);
        path.lineTo(3, 21);
        path.lineTo(3.5, 16.5);
        path.closeSubpath();
        painter->drawPath(path);
    }
    else if (name == "trash-2" || name == "delete" || name == "remove") {
        painter->drawLine(QPointF(3, 6), QPointF(21, 6));
        path.moveTo(8, 6);
        path.lineTo(8, 3);
        path.lineTo(16, 3);
        path.lineTo(16, 6);
        painter->drawPath(path);

        QPainterPath bin;
        bin.moveTo(19, 6);
        bin.lineTo(18, 20);
        bin.lineTo(6, 20);
        bin.lineTo(5, 6);
        painter->drawPath(bin);

        painter->drawLine(QPointF(10, 10), QPointF(10, 16));
        painter->drawLine(QPointF(14, 10), QPointF(14, 16));
    }
    else if (name == "check" || name == "done") {
        path.moveTo(4, 12);
        path.lineTo(9, 17);
        path.lineTo(20, 6);
        painter->drawPath(path);
    }
    else if (name == "eye" || name == "open" || name == "view") {
        path.moveTo(2, 12);
        path.cubicTo(6, 5, 18, 5, 22, 12);
        path.cubicTo(18, 19, 6, 19, 2, 12);
        painter->drawPath(path);
        painter->drawEllipse(QRectF(9, 9, 6, 6));
    }
    else if (name == "download" || name == "export") {
        path.moveTo(21, 15);
        path.lineTo(21, 19);
        path.cubicTo(21, 20, 20, 21, 19, 21);
        path.lineTo(5, 21);
        path.cubicTo(4, 21, 3, 20, 3, 19);
        path.lineTo(3, 15);
        painter->drawPath(path);

        painter->drawLine(QPointF(12, 3), QPointF(12, 15));
        painter->drawLine(QPointF(7, 10), QPointF(12, 15));
        painter->drawLine(QPointF(17, 10), QPointF(12, 15));
    }
    else if (name == "upload" || name == "import") {
        path.moveTo(21, 15);
        path.lineTo(21, 19);
        path.cubicTo(21, 20, 20, 21, 19, 21);
        path.lineTo(5, 21);
        path.cubicTo(4, 21, 3, 20, 3, 19);
        path.lineTo(3, 15);
        painter->drawPath(path);

        painter->drawLine(QPointF(12, 15), QPointF(12, 3));
        painter->drawLine(QPointF(7, 8), QPointF(12, 3));
        painter->drawLine(QPointF(17, 8), QPointF(12, 3));
    }
    else if (name == "copy") {
        painter->drawRoundedRect(QRectF(8, 8, 13, 13), 2, 2);
        path.moveTo(5, 15);
        path.lineTo(3, 15);
        path.lineTo(3, 3);
        path.lineTo(15, 3);
        path.lineTo(15, 5);
        painter->drawPath(path);
    }
    else if (name == "phone") {
        path.moveTo(22, 16.92);
        path.cubicTo(22, 19.5, 19.5, 22, 16.92, 22);
        path.cubicTo(8.5, 22, 2, 15.5, 2, 7.08);
        path.cubicTo(2, 4.5, 4.5, 2, 7.08, 2);
        path.lineTo(10.5, 6);
        path.lineTo(8.5, 8.5);
        path.cubicTo(10, 11.5, 12.5, 14, 15.5, 15.5);
        path.lineTo(18, 13.5);
        path.closeSubpath();
        painter->drawPath(path);
    }
    else if (name == "mail" || name == "email") {
        painter->drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        path.moveTo(3, 7);
        path.lineTo(12, 13);
        path.lineTo(21, 7);
        painter->drawPath(path);
    }
    else if (name == "flag") {
        painter->drawLine(QPointF(4, 22), QPointF(4, 2));
        path.moveTo(4, 3);
        path.cubicTo(9, 1, 14, 6, 20, 4);
        path.lineTo(20, 13);
        path.cubicTo(14, 15, 9, 10, 4, 12);
        painter->drawPath(path);
    }
    else if (name == "snowflake" || name == "freeze") {
        painter->drawLine(QPointF(12, 2), QPointF(12, 22));
        painter->drawLine(QPointF(3.34, 7), QPointF(20.66, 17));
        painter->drawLine(QPointF(3.34, 17), QPointF(20.66, 7));

        path.moveTo(9, 4); path.lineTo(12, 2); path.lineTo(15, 4);
        path.moveTo(9, 20); path.lineTo(12, 22); path.lineTo(15, 20);
        painter->drawPath(path);
    }
    else if (name == "play" || name == "resume") {
        path.moveTo(6, 4);
        path.lineTo(20, 12);
        path.lineTo(6, 20);
        path.closeSubpath();
        painter->drawPath(path);
    }
    else if (name == "refresh-cw" || name == "recenter" || name == "reset") {
        path.arcMoveTo(QRectF(3, 3, 18, 18), 30);
        path.arcTo(QRectF(3, 3, 18, 18), 30, 290);
        painter->drawPath(path);

        QPainterPath a1;
        a1.moveTo(21, 3); a1.lineTo(21, 9); a1.lineTo(15, 9);
        painter->drawPath(a1);
    }
    else if (name == "pin") {
        path.moveTo(12, 17);
        path.lineTo(12, 22);
        painter->drawPath(path);

        QPainterPath pinBody;
        pinBody.moveTo(5, 9);
        pinBody.lineTo(19, 9);
        pinBody.lineTo(17, 17);
        pinBody.lineTo(7, 17);
        pinBody.closeSubpath();
        painter->drawPath(pinBody);

        painter->drawLine(QPointF(9, 3), QPointF(15, 3));
        painter->drawLine(QPointF(12, 3), QPointF(12, 9));
    }
    else if (name == "folder-tree" || name == "tree") {
        painter->drawRoundedRect(QRectF(3, 3, 7, 5), 1, 1);
        painter->drawRoundedRect(QRectF(13, 10, 8, 5), 1, 1);
        painter->drawRoundedRect(QRectF(13, 17, 8, 5), 1, 1);

        painter->drawLine(QPointF(6.5, 8), QPointF(6.5, 20));
        painter->drawLine(QPointF(6.5, 12.5), QPointF(13, 12.5));
        painter->drawLine(QPointF(6.5, 19.5), QPointF(13, 19.5));
    }
    else if (name == "tag") {
        path.moveTo(12, 2);
        path.lineTo(22, 12);
        path.lineTo(17, 17);
        path.lineTo(7, 7);
        path.lineTo(7, 2);
        path.closeSubpath();
        painter->drawPath(path);
        painter->drawEllipse(QRectF(8.5, 3.5, 2, 2));
    }
    else if (name == "lock") {
        painter->drawRoundedRect(QRectF(4, 11, 16, 11), 2, 2);
        path.arcMoveTo(QRectF(7, 4, 10, 14), 0);
        path.arcTo(QRectF(7, 4, 10, 14), 0, 180);
        painter->drawPath(path);
    }
    else if (name == "unlock") {
        painter->drawRoundedRect(QRectF(4, 11, 16, 11), 2, 2);
        path.arcMoveTo(QRectF(7, 2, 10, 14), 0);
        path.arcTo(QRectF(7, 2, 10, 14), 0, 180);
        painter->drawPath(path);
    }
    else if (name == "sparkles" || name == "zap") {
        path.moveTo(13, 2);
        path.lineTo(3, 14);
        path.lineTo(12, 14);
        path.lineTo(11, 22);
        path.lineTo(21, 10);
        path.lineTo(12, 10);
        path.closeSubpath();
        painter->drawPath(path);
    }
    else if (name == "archive") {
        painter->drawRoundedRect(QRectF(3, 3, 18, 5), 1, 1);
        path.moveTo(4, 8);
        path.lineTo(4, 20);
        path.cubicTo(4, 21, 5, 21, 6, 21);
        path.lineTo(18, 21);
        path.cubicTo(19, 21, 20, 21, 20, 20);
        path.lineTo(20, 8);
        painter->drawPath(path);

        painter->drawLine(QPointF(10, 12), QPointF(14, 12));
    }
    else if (name == "alert-triangle" || name == "warning" || name == "burnout") {
        path.moveTo(12, 2);
        path.lineTo(22, 20);
        path.lineTo(2, 20);
        path.closeSubpath();
        painter->drawPath(path);

        painter->drawLine(QPointF(12, 8), QPointF(12, 13));
        painter->drawPoint(QPointF(12, 17));
    }
    else {
        // Default clean circle with dot if unrecognized
        painter->drawEllipse(QRectF(4, 4, 16, 16));
        painter->drawPoint(QPointF(12, 12));
    }
}
