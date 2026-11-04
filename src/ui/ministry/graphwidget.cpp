#include "graphwidget.h"
#include "appsettings.h"
#include "iconmanager.h"
#include "synergyengine.h"
#include "ministryarchetypes.h"
#include "../database/databasemanager.h"
#include "matchdialog.h"
#include <QApplication>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QPainter>
#include <QPainterPathStroker>
#include <QGraphicsSceneMouseEvent>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <cmath>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QRandomGenerator>
#include <QQueue>
#include <QMap>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QLineEdit>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QAction>
#include <QWheelEvent>
#include <QDialog>
#include <QFormLayout>
#include <QRadioButton>
#include <QButtonGroup>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QGuiApplication>
#include <QClipboard>
#include <QTextStream>

// Helper to clean up layout items recursively. Widgets are disposed with
// deleteLater() instead of delete: this runs while the layout is still
// installed on a live widget (the Smart Bridge panel is rebuilt on every
// node selection), and immediately deleting styled widgets mid-churn
// corrupted the heap (detected later in QStyleSheetStyle per-object cache
// frees — dozens of SIGABRTs on teardown and a corrupted scene BSP tree
// that rendered the graph blank).
static void clearLayoutRecursively(QLayout* layout) {
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QLayout* sub = item->layout()) {
            clearLayoutRecursively(sub);
            delete sub; // sub-layout is empty now; safe to delete immediately
        }
        if (QWidget* w = item->widget()) {
            w->deleteLater(); // defer destruction to the event loop
        }
        delete item;
    }
}

// Helper to locate GraphWidget from within graphics items
static GraphWidget* findParentGraphWidget(QGraphicsItem* item) {
    if (!item || !item->scene()) return nullptr;
    const auto views = item->scene()->views();
    if (views.isEmpty()) return nullptr;
    for (QGraphicsView* v : views) {
        GraphCanvasView* gcv = qobject_cast<GraphCanvasView*>(v);
        if (gcv) {
            QWidget* p = gcv->parentWidget();
            while (p) {
                GraphWidget* gw = qobject_cast<GraphWidget*>(p);
                if (gw) return gw;
                p = p->parentWidget();
            }
        }
    }
    return nullptr;
}

// --- NodeItem Implementation ---

NodeItem::NodeItem(int id, const QString& name, Type type)
    : velocity(0, 0), isBeingDragged(false),
      m_id(id), m_name(name), m_type(type), m_isWarning(false),
      m_burnoutGroupCount(0), m_isCriticalBurnout(false),
      m_connectionCount(0), m_isHovered(false), m_thresholdLabeling(false),
      m_healthStatus(HealthNormal)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setToolTip(name);
}

QRectF NodeItem::boundingRect() const {
    double r = radius() + 30.0;
    // The hover detail card (paint()) is drawn up to 240px wide and ~90px
    // tall ABOVE the node — well outside the shape square. While it is
    // shown, the bounds must cover it or Qt's dirty-region tracking clips
    // the card to the node square (white patches, clipped text, stale
    // fragments left on the canvas as the mouse moves between nodes).
    // prepareGeometryChange() in the hover handlers keeps the scene index
    // in sync as the bounds grow and shrink.
    if (m_isHovered) {
        const double halfW = qMax(r, 120.0);           // card is 240px wide
        const double top = -(radius() + 90.0);         // cardH(80) + 10 gap
        const double bottom = r + 40.0;                // name-label area
        return QRectF(-halfW, top, halfW * 2.0, bottom - top);
    }
    return QRectF(-r, -r, r * 2.0, r * 2.0 + 40.0);
}

void NodeItem::setWarning(bool warning, const QString& warningMsg) {
    m_isWarning = warning;
    m_warningMsg = warningMsg;
    if (warning) {
        setToolTip(QString("%1\n⚠️ [WARNING: %2]").arg(m_name, warningMsg));
    } else {
        setToolTip(m_name);
    }
}

void NodeItem::setBurnoutDetails(int groupCount, bool isCritical, const QString& rolesDetail) {
    m_burnoutGroupCount = groupCount;
    m_isCriticalBurnout = isCritical;
    m_burnoutRolesDetail = rolesDetail;

    if (groupCount > 0) {
        QString severityStr = isCritical ? "🔥 CRITICAL BURNOUT OVERLOAD" : "⚠️ HIGH BURNOUT WARNING";
        setToolTip(QString("%1\n%2 (%3 groups)\nRoles: %4")
                       .arg(m_name, severityStr).arg(groupCount).arg(rolesDetail));
    }
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    ThemeTokens tok = ThemeManager::instance().currentTokens();
    GraphWidget* gw = findParentGraphWidget(this);
    bool isHygieneMode = (gw && gw->isHygieneActive());
    bool isBridgeMode = (gw && gw->isBridgeActive());
    bool isFamilyMode = (gw && gw->isFamilyActive());

    QColor borderCol;
    QColor bodyColStart;
    QColor bodyColEnd;
    double r = radius();

    if (isBridgeMode && m_isBridgeBroker) {
        borderCol = QColor("#cba6f7"); // Purple Broker Glow
        bodyColStart = tok.isDark ? QColor("#3c2656") : QColor("#f3e8ff");
        bodyColEnd = tok.bgSurface;
    } else if (isFamilyMode && (!m_familyConcentrationMsg.isEmpty() || m_type == Household)) {
        borderCol = tok.accentWarning;
        bodyColStart = tok.isDark ? QColor("#382418") : QColor("#fef3e7");
        bodyColEnd = tok.bgSurface;
    } else if (isHygieneMode && isPersonNode()) {
        if (isHygieneCritical()) {
            borderCol = tok.accentDanger;
            bodyColStart = tok.isDark ? QColor("#4a1a24") : QColor("#ffebee");
            bodyColEnd = tok.bgSurface;
        } else if (isHygieneWarning()) {
            borderCol = tok.accentWarning;
            bodyColStart = tok.isDark ? QColor("#3b2a1a") : QColor("#fffde7");
            bodyColEnd = tok.bgSurface;
        } else {
            borderCol = tok.accentSuccess;
            bodyColStart = tok.isDark ? QColor("#183020") : QColor("#e8f5e9");
            bodyColEnd = tok.bgSurface;
        }
    } else if (m_isCriticalBurnout) {
        borderCol = tok.accentDanger;
        bodyColStart = tok.isDark ? QColor("#4a1a24") : QColor("#fce8e6");
        bodyColEnd = tok.bgSurface;
    } else if (m_burnoutGroupCount > 0 || m_isWarning) {
        borderCol = tok.accentWarning;
        bodyColStart = tok.isDark ? QColor("#3b2a1a") : QColor("#fef3e7");
        bodyColEnd = tok.bgSurface;
    } else if (!m_familyConcentrationMsg.isEmpty()) {
        borderCol = tok.accentWarning;
        bodyColStart = tok.isDark ? QColor("#382418") : QColor("#fef3e7");
        bodyColEnd = tok.bgSurface;
    } else {
        switch (m_type) {
            case Person:
                if (m_isBridgeBroker) {
                    borderCol = QColor("#cba6f7");
                    bodyColStart = tok.isDark ? QColor("#2b1e40") : QColor("#f5f0fa");
                } else if (m_statusColor.isValid() && !m_isCriticalBurnout) {
                    // Status-cluster view: membership status drives the border.
                    borderCol = m_statusColor;
                    bodyColStart = tok.bgSurface;
                } else if (m_careFlag == "Leader" || m_connectionCount >= 4) {
                    borderCol = tok.accentPrimary;
                    bodyColStart = tok.isDark ? QColor("#003847") : QColor("#e0f7fa");
                } else if (m_careFlag == "Active Member" || m_connectionCount >= 2) {
                    borderCol = tok.accentSuccess;
                    bodyColStart = tok.isDark ? QColor("#1e2438") : QColor("#e8f5e9");
                } else {
                    borderCol = tok.borderColor;
                    bodyColStart = tok.isDark ? QColor("#181825") : QColor("#f5f5f5");
                }
                bodyColEnd = tok.bgSurface;
                break;
            case Group:
                if (!m_familyConcentrationMsg.isEmpty()) borderCol = tok.accentWarning;
                else if (m_healthStatus == Understaffed) borderCol = tok.accentWarning;
                else if (m_healthStatus == CriticalShortage) borderCol = tok.accentDanger;
                else if (m_categoryColor.isValid()) borderCol = m_categoryColor;
                else borderCol = tok.accentSuccess;
                bodyColStart = tok.isDark ? QColor("#183020") : QColor("#e8f5e9");
                bodyColEnd = tok.bgSurface;
                break;
            case Household:
                borderCol = tok.accentWarning;
                bodyColStart = tok.isDark ? QColor("#302d20") : QColor("#fffde7");
                bodyColEnd = tok.bgSurface;
                break;
            case IsolatedPerson:
                borderCol = tok.accentDanger;
                bodyColStart = tok.isDark ? QColor("#381e28") : QColor("#ffebee");
                bodyColEnd = tok.bgSurface;
                break;
            case Visitor:
                borderCol = tok.accentPrimary;
                bodyColStart = tok.isDark ? QColor("#152530") : QColor("#e3f2fd");
                bodyColEnd = tok.bgSurface;
                break;
        }
    }

    if (isSelected() || m_isHovered) {
        borderCol = tok.isDark ? QColor("#ffffff") : tok.accentPrimary;
    }

    QRadialGradient grad(0, 0, r + 2);
    grad.setColorAt(0, bodyColStart);
    grad.setColorAt(1, bodyColEnd);

    painter->setBrush(grad);
    int penWidth = (isSelected() || m_isCriticalBurnout || m_isWarning || isBeingDragged || m_isHovered || m_isBridgeBroker) ? 4 : 2;
    painter->setPen(QPen(borderCol, penWidth));

    if (m_type == Group) {
        QPolygonF hex;
        for (int i = 0; i < 6; ++i) {
            double angle = i * M_PI / 3.0;
            hex << QPointF(r * std::cos(angle), r * std::sin(angle));
        }
        painter->drawPolygon(hex);
    } else if (m_type == Household) {
        QPolygonF diamond;
        diamond << QPointF(0, -r * 1.15) << QPointF(r * 1.15, 0) << QPointF(0, r * 1.15) << QPointF(-r * 1.15, 0);
        painter->drawPolygon(diamond);
    } else if (m_type == Visitor || m_type == IsolatedPerson) {
        QRectF sqRect(-r * 0.85, -r * 0.85, r * 1.7, r * 1.7);
        painter->drawRoundedRect(sqRect, 6, 6);
    } else {
        painter->drawEllipse(-r, -r, r * 2.0, r * 2.0);
    }

    QString iconName;
    if (m_type == Group) iconName = "layers";
    else if (m_type == Household) iconName = "users";
    else if (m_type == IsolatedPerson) iconName = "alert-triangle";
    else if (m_type == Visitor) iconName = "user-plus";
    else if (m_isBridgeBroker) iconName = "network";
    else iconName = "user";

    double iconSz = r * 0.9;
    QRectF iconRect(-iconSz / 2.0, -iconSz / 2.0, iconSz, iconSz);
    IconManager::instance().paintIcon(painter, iconName, iconRect, borderCol, 2.0);

    if (m_isPinned) {
        QRectF pinRect(r * 0.2, -r * 0.9, 12, 12);
        IconManager::instance().paintIcon(painter, "pin", pinRect, tok.accentWarning, 2.0);
    }

    if (!m_careFlag.isEmpty()) {
        QColor badgeColor;
        if (m_careFlag == "At-Risk") badgeColor = tok.accentDanger;
        else if (m_careFlag == "Leader") badgeColor = tok.accentPrimary;
        else if (m_careFlag == "New Visitor") badgeColor = tok.accentWarning;
        else if (m_careFlag == "Active Member") badgeColor = tok.accentSuccess;
        else badgeColor = tok.accentPrimary;

        painter->setBrush(badgeColor);
        painter->setPen(QPen(tok.bgBase, 1));
        painter->drawEllipse(r - 12, -r, 12.0, 12.0);
    }

    bool isLeader = (m_careFlag == "Leader");
    bool isMajorNode = (m_type == Group || m_type == Household);
    bool isHighDegree = (m_connectionCount >= 3);
    bool shouldShowLabel = !m_thresholdLabeling || isSelected() || m_isHovered || isLeader || isMajorNode || isHighDegree || m_isBridgeBroker;

    if (shouldShowLabel) {
        QColor textCol = (isSelected() || m_isHovered) ? tok.accentPrimary : tok.textPrimary;
        painter->setPen(textCol);
        painter->setFont(QFont("Segoe UI", (isSelected() || m_isHovered) ? 9 : 8, (isSelected() || m_isHovered) ? QFont::Bold : QFont::Normal));
        painter->drawText(QRectF(-90, r + 4, 180, 20), Qt::AlignCenter, m_name);
    }

    if (m_isHovered && !isSelected()) {
        int cardW = 240;
        int cardH = m_burnoutGroupCount > 0 ? 80 : (m_isBridgeBroker ? 75 : 55);
        QRectF hoverCard(-cardW / 2, -r - cardH - 10, cardW, cardH);

        // Card background follows the theme instead of hardcoded white: on
        // tinted light brands the pure-white card read as a glitch patch on
        // the canvas.
        QColor cardBg = tok.isDark
                            ? QColor(30, 30, 46, 235)
                            : QColor(tok.bgOverlay.red(), tok.bgOverlay.green(), tok.bgOverlay.blue(), 245);
        painter->setBrush(cardBg);
        painter->setPen(QPen(borderCol, 1.5));
        painter->drawRoundedRect(hoverCard, 8, 8);

        painter->setFont(QFont("Segoe UI", 9, QFont::Bold));
        painter->setPen(tok.accentPrimary);
        painter->drawText(hoverCard.adjusted(10, 6, -10, -cardH + 22), Qt::AlignLeft | Qt::AlignVCenter, m_name);

        painter->setFont(QFont("Segoe UI", 8, QFont::Normal));
        painter->setPen(tok.textSecondary);

        if (m_isBridgeBroker) {
            painter->setPen(QColor("#cba6f7"));
            painter->drawText(hoverCard.adjusted(10, 24, -10, -10), Qt::AlignLeft | Qt::TextWordWrap,
                              QString("🌉 Relational Broker (Connects %1 ministries):\n• %2").arg(m_bridgedGroups.size()).arg(m_bridgedGroups.join(", ")));
        } else if (m_burnoutGroupCount > 0) {
            painter->setPen(m_isCriticalBurnout ? tok.accentDanger : tok.accentWarning);
            painter->drawText(hoverCard.adjusted(10, 24, -10, -10), Qt::AlignLeft | Qt::TextWordWrap,
                              QString("🔥 Burnout Risk (%1 groups):\n%2").arg(m_burnoutGroupCount).arg(m_burnoutRolesDetail));
        } else if (!m_familyConcentrationMsg.isEmpty()) {
            painter->setPen(tok.accentWarning);
            painter->drawText(hoverCard.adjusted(10, 24, -10, -10), Qt::AlignLeft | Qt::TextWordWrap, m_familyConcentrationMsg);
        } else {
            painter->drawText(hoverCard.adjusted(10, 24, -10, -10), Qt::AlignLeft | Qt::AlignVCenter,
                              QString("Status: %1  •  Connections: %2").arg(m_careFlag.isEmpty() ? "Normal" : m_careFlag).arg(m_connectionCount));
        }
    }
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        GraphWidget* gw = findParentGraphWidget(this);
        if (gw) {
            gw->showNodeContextMenu(this, event->screenPos());
            event->accept();
            return;
        }
    }
    isBeingDragged = true;
    m_dragStartPos = event->screenPos();
    QGraphicsItem::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseMoveEvent(event);
    GraphWidget* gw = findParentGraphWidget(this);
    if (gw) {
        gw->updateEdgesForNode(this);
    }
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    isBeingDragged = false;
    QGraphicsItem::mouseReleaseEvent(event);

    GraphWidget* gw = findParentGraphWidget(this);
    if (gw) {
        gw->updateEdgesForNode(this);
    }

    if ((event->screenPos() - m_dragStartPos).manhattanLength() < 5) {
        if (gw) gw->handleNodeClick(this);
    }
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    GraphWidget* gw = findParentGraphWidget(this);
    if (gw) {
        if (isPersonNode()) {
            emit gw->navigateToProfile("Person", m_id);
        } else if (m_type == Group) {
            emit gw->navigateToProfile("Group", m_id);
        }
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
}

void NodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    GraphWidget* gw = findParentGraphWidget(this);
    if (gw) {
        gw->showNodeContextMenu(this, event->screenPos());
        event->accept();
    }
}

void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    // The bounds grow while hovered (boundingRect covers the detail card);
    // notify the scene's index BEFORE the state flips so the enlarged area
    // is actually repainted.
    prepareGeometryChange();
    m_isHovered = true;
    QGraphicsItem::hoverEnterEvent(event);
    QGraphicsItem::update();
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    prepareGeometryChange();
    m_isHovered = false;
    QGraphicsItem::hoverLeaveEvent(event);
    QGraphicsItem::update();
}

// --- EdgeItem Implementation ---

EdgeItem::EdgeItem(NodeItem* source, NodeItem* target, const QString& relType, int relId, bool isBurnoutLink)
    : m_source(source), m_target(target), m_relType(relType), m_relId(relId), m_isBurnoutLink(isBurnoutLink),
      m_isHovered(false), m_isSelected(false)
{
    setAcceptHoverEvents(true);
    setToolTip(QString("Relationship: %1\nBetween: %2 ↔ %3").arg(relType, source->name(), target->name()));
}

QRectF EdgeItem::boundingRect() const {
    if (!m_source || !m_target || !m_source->isVisible() || !m_target->isVisible()) return QRectF();
    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();
    return QRectF(p1, p2).normalized().adjusted(-15, -15, 15, 15);
}

void EdgeItem::adjust() {
    prepareGeometryChange();
    update();
}

QPainterPath EdgeItem::shape() const {
    if (!m_source || !m_target || !m_source->isVisible() || !m_target->isVisible()) return QPainterPath();
    QPainterPath p;
    p.moveTo(m_source->pos());
    p.lineTo(m_target->pos());
    QPainterPathStroker stroker;
    stroker.setWidth(14.0);
    return stroker.createStroke(p);
}

void EdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (!m_source || !m_target || !m_source->isVisible() || !m_target->isVisible()) return;

    GraphWidget* gw = findParentGraphWidget(m_source);
    if (gw && !gw->showConnectors()) return;

    painter->setRenderHint(QPainter::Antialiasing);

    ThemeTokens tok = ThemeManager::instance().currentTokens();

    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();

    double scaleFactor = 1.0;
    if (gw) {
        int lvl = gw->connectorSizeLevel();
        if (lvl == 1) scaleFactor = 0.6;      // Thin
        else if (lvl == 3) scaleFactor = 1.8; // Bold
    }

    QColor lineCol;
    double lineWidth = 1.5 * scaleFactor;
    Qt::PenStyle penStyle = Qt::SolidLine;

    if (m_isSelected) {
        lineCol = tok.accentPrimary;
        lineWidth = 4.0 * scaleFactor;
    } else if (m_isHovered) {
        lineCol = tok.isDark ? QColor("#ffffff") : tok.accentPrimary;
        lineWidth = 3.5 * scaleFactor;
    } else if (m_isBurnoutLink) {
        lineCol = tok.accentDanger;
        lineWidth = 2.5 * scaleFactor;
        penStyle = Qt::DashLine;
    } else if (m_relType == "Parent of") {
        lineCol = tok.textMuted;
        lineWidth = 1.2 * scaleFactor;
        penStyle = Qt::DashLine;
    } else if (m_relType.contains("Spouse") || m_relType.contains("Parent")) {
        lineCol = tok.accentWarning;
        lineWidth = 3.0 * scaleFactor;
    } else if (m_relType.contains("Leader")) {
        lineCol = tok.accentPrimary;
        lineWidth = 2.5 * scaleFactor;
    } else {
        lineCol = tok.borderColor;
        lineWidth = 1.5 * scaleFactor;
        penStyle = Qt::DashLine;
    }

    painter->setPen(QPen(lineCol, qMax(0.8, lineWidth), penStyle));
    painter->drawLine(p1, p2);
}

void EdgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    m_isHovered = true;
    QGraphicsItem::hoverEnterEvent(event);
    QGraphicsItem::update();
}

void EdgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    m_isHovered = false;
    QGraphicsItem::hoverLeaveEvent(event);
    QGraphicsItem::update();
}

void EdgeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        GraphWidget* gw = findParentGraphWidget(this);
        if (gw) {
            gw->selectEdgeForDetails(this);
            event->accept();
            return;
        }
    }
    QGraphicsItem::mousePressEvent(event);
}

// --- GhostEdgeItem Implementation (Smart Bridge Recommendations) ---

GhostEdgeItem::GhostEdgeItem(NodeItem* source, NodeItem* target, const QString& matchReason, int matchPct, bool isSolid)
    : m_source(source), m_target(target), m_matchReason(matchReason), m_matchPct(matchPct), m_isSolid(isSolid)
{
    setZValue(-1);
    const QString targetName = target ? target->name() : QString("?");
    setToolTip(QString("💡 Smart Bridge Recommendation: %1\n%2% Match — %3").arg(targetName).arg(matchPct).arg(matchReason));
}

QRectF GhostEdgeItem::boundingRect() const {
    if (!m_source || !m_target || !m_source->isVisible() || !m_target->isVisible()) return QRectF();
    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();
    QPointF mid = (p1 + p2) / 2.0;
    QPointF diff = p2 - p1;
    QPointF normal(-diff.y() * 0.12, diff.x() * 0.12);
    QPointF ctrl = mid + normal;
    QRectF r = QRectF(p1, p2).normalized();
    r = r.united(QRectF(ctrl, ctrl));
    return r.adjusted(-20, -20, 20, 20);
}

void GhostEdgeItem::adjust() {
    prepareGeometryChange();
    update();
}

void GhostEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (!m_source || !m_target) return;

    GraphWidget* gw = findParentGraphWidget(m_source);
    if (gw && !gw->showConnectors()) return;

    painter->setRenderHint(QPainter::Antialiasing);

    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();
    QPointF mid = (p1 + p2) / 2.0;
    QPointF diff = p2 - p1;
    QPointF normal(-diff.y() * 0.12, diff.x() * 0.12);
    QPointF ctrl = mid + normal;

    QPainterPath path;
    path.moveTo(p1);
    path.quadTo(ctrl, p2);

    double scaleFactor = 1.0;
    if (gw) {
        int lvl = gw->connectorSizeLevel();
        if (lvl == 1) scaleFactor = 0.6;      // Thin
        else if (lvl == 3) scaleFactor = 1.8; // Bold
    }

    double width = (m_isSolid ? 2.5 : 2.0) * scaleFactor;
    // Theme-aware ghost color: accentPrimary reads well on both light and dark.
    const QColor ghost = ThemeManager::instance().currentTokens().accentPrimary;
    QPen pen(ghost, qMax(0.8, width), m_isSolid ? Qt::SolidLine : Qt::DashLine);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->drawPath(path);
}

// --- GraphCanvasView Implementation ---

namespace {
// Zoom-out crash guard: unbounded scale() drives the view transform toward 0
// (then mapToScene divides by ~0 -> inf/nan -> paint crash) or toward huge
// values on repeated zoom-in (painter overflow). Clamp uniform scale.
constexpr double kMinZoom = 0.15;
constexpr double kMaxZoom = 4.0;
constexpr double kSceneBound = 1900.0;
constexpr int kMaxGridPoints = 20000;

double currentZoom(const QGraphicsView* v) {
    if (!v) return 1.0;
    const double m11 = v->transform().m11();
    // transform() is scale-only in this widget (scale/reset/fitInView), so
    // m11 is the uniform zoom. Guard against degenerate transforms.
    if (!std::isfinite(m11) || m11 <= 0.0) return 1.0;
    return m11;
}

bool clampedScale(QGraphicsView* v, double factor) {
    if (!v || !std::isfinite(factor) || factor <= 0.0) return false;
    const double cur = currentZoom(v);
    double want = cur * factor;
    if (want < kMinZoom) factor = kMinZoom / cur;
    else if (want > kMaxZoom) factor = kMaxZoom / cur;
    // Already at the bound and pushing further out: swallow, don't scale.
    if (!std::isfinite(factor) || qFuzzyCompare(factor, 1.0)) return false;
    v->scale(factor, factor);
    return true;
}
} // namespace

GraphCanvasView::GraphCanvasView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), m_selectMode(false)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    // FullViewportUpdate repaints the visible canvas cleanly on each frame,
    // eliminating dirty-rect striations / ghost edge trails during physics
    // simulation, dragging, and panning. The physics layout auto-freezes once
    // settled, so this incurs zero idle CPU overhead.
    // Minimal: only dirty regions repaint. With 100+ nodes and the 33 ms
    // physics timer, FullViewportUpdate repainted the entire canvas every
    // tick — the largest CPU/GPU draw while the graph page is open.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Keep zoom centered under the cursor and stable on resize.
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // Touchscreen (detachable 2-in-1) + active pen: taps synthesize mouse by
    // default; grab pinch so two-finger zoom works on the canvas.
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    grabGesture(Qt::PinchGesture);
}

bool GraphCanvasView::event(QEvent* event) {
    if (event && event->type() == QEvent::Gesture) {
        auto* ge = static_cast<QGestureEvent*>(event);
        if (QGesture* g = ge->gesture(Qt::PinchGesture)) {
            if (auto* pinch = static_cast<QPinchGesture*>(g)) {
                if (handlePinchGesture(pinch)) {
                    event->accept();
                    return true;
                }
            }
        }
    }
    return QGraphicsView::event(event);
}

bool GraphCanvasView::handlePinchGesture(QPinchGesture* pinch) {
    if (!pinch) return false;
    const double total = pinch->totalScaleFactor();
    if (!std::isfinite(total) || total <= 0.0) return false;
    if (pinch->state() == Qt::GestureStarted) {
        m_pinchStartZoom = currentZoom(this);
        return true;
    }
    if (pinch->state() == Qt::GestureUpdated) {
        if (!std::isfinite(m_pinchStartZoom) || m_pinchStartZoom <= 0.0)
            m_pinchStartZoom = currentZoom(this);
        double want = m_pinchStartZoom * total;
        if (!std::isfinite(want) || want <= 0.0) return true;
        want = qBound(kMinZoom, want, kMaxZoom);
        const double cur = currentZoom(this);
        if (cur <= 0.0 || qFuzzyCompare(cur, want)) return true;
        clampedScale(this, want / cur);
        return true;
    }
    return false;
}

void GraphCanvasView::setSelectMode(bool selectMode) {
    m_selectMode = selectMode;
    if (selectMode) {
        setDragMode(QGraphicsView::RubberBandDrag);
    } else {
        setDragMode(QGraphicsView::ScrollHandDrag);
    }
}

void GraphCanvasView::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    double factor = (event->angleDelta().y() > 0) ? 1.15 : 0.85;
    clampedScale(this, factor);
    event->accept();
}

void GraphCanvasView::mousePressEvent(QMouseEvent* event) {
    QGraphicsView::mousePressEvent(event);
    if (m_graphWidget) m_graphWidget->resumePhysics(); // a drag may follow
    if (!itemAt(event->pos()) && m_graphWidget) {
        m_graphWidget->resetHighlight();
    }
}

void GraphCanvasView::contextMenuEvent(QContextMenuEvent* event) {
    if (itemAt(event->pos())) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }
    if (!m_graphWidget) return;

    QMenu menu(this);
    bool running = m_graphWidget->isPhysicsRunning();
    QAction* actFreeze = menu.addAction(running ? "❄️ Freeze Layout Physics" : "▶️ Unfreeze & Resume Physics");
    QAction* actResetPhysics = menu.addAction("🔄 Unfreeze Physics & Re-center");
    menu.addSeparator();
    QAction* actExportPng = menu.addAction("📸 Export High-Res Canvas (PNG)...");
    QAction* actFit = menu.addAction("🔍 Fit Graph to View");

    QAction* sel = menu.exec(event->globalPos());
    if (!sel) return;

    if (sel == actFreeze) {
        if (running) m_graphWidget->pausePhysics();
        else m_graphWidget->resumePhysics();
    } else if (sel == actResetPhysics) {
        m_graphWidget->resumePhysics();
        m_graphWidget->loadGraph();
    } else if (sel == actExportPng) {
        m_graphWidget->onExportPNG();
    } else if (sel == actFit) {
        if (scene()) {
            const QRectF bounds = scene()->itemsBoundingRect();
            if (bounds.isValid() && !bounds.isNull() && bounds.width() > 0.0 && bounds.height() > 0.0
                && std::isfinite(bounds.width()) && std::isfinite(bounds.height())) {
                fitInView(bounds, Qt::KeepAspectRatio);
                const double z = currentZoom(this);
                if (z < kMinZoom) clampedScale(this, kMinZoom / z);
                else if (z > kMaxZoom) clampedScale(this, kMaxZoom / z);
            }
        }
    }
}

// Keyboard navigation: arrows move the selection to a connected neighbor in
// that direction; Esc clears the selection/highlight.
void GraphCanvasView::keyPressEvent(QKeyEvent* event) {
    if (!m_graphWidget) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        m_graphWidget->resetHighlight();
        m_graphWidget->clearSceneSelection();
        event->accept();
        return;
    }
    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
        QPointF dir;
        if (key == Qt::Key_Left) dir = QPointF(-1, 0);
        else if (key == Qt::Key_Right) dir = QPointF(1, 0);
        else if (key == Qt::Key_Up) dir = QPointF(0, -1);
        else dir = QPointF(0, 1);
        m_graphWidget->selectNeighborInDirection(dir);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void GraphCanvasView::drawBackground(QPainter* painter, const QRectF& rect) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    painter->fillRect(rect, tok.bgBase);

    // Far zoom-out makes `rect` enormous (viewport mapped to scene). Drawing
    // a 50px dot grid for the whole rect would allocate millions of points
    // and hang/crash. Adapt the step so the point count stays bounded, and
    // skip dots entirely past a sane limit.
    if (!rect.isValid() || !std::isfinite(rect.width()) || !std::isfinite(rect.height())
        || rect.width() <= 0.0 || rect.height() <= 0.0) {
        return;
    }
    double gridSize = 50.0;
    const double area = rect.width() * rect.height();
    if (area / (gridSize * gridSize) > kMaxGridPoints) {
        gridSize = std::ceil(std::sqrt(area / kMaxGridPoints));
        if (!std::isfinite(gridSize) || gridSize <= 0.0) return;
    }
    // Past ~400px steps the grid is sub-pixel noise — skip it.
    if (gridSize > 400.0) return;

    painter->setPen(QPen(tok.bgSurface, 1, Qt::DotLine));
    qreal left = std::floor(rect.left() / gridSize) * gridSize;
    qreal top = std::floor(rect.top() / gridSize) * gridSize;

    QVector<QPointF> points;
    points.reserve(kMaxGridPoints);
    for (qreal x = left; x < rect.right(); x += gridSize) {
        for (qreal y = top; y < rect.bottom(); y += gridSize) {
            points.append(QPointF(x, y));
            if (points.size() >= kMaxGridPoints) break;
        }
        if (points.size() >= kMaxGridPoints) break;
    }
    if (!points.isEmpty())
        painter->drawPoints(points.data(), points.size());
}

void GraphCanvasView::drawForeground(QPainter* painter, const QRectF&) {
    Q_UNUSED(painter);
}

void GraphCanvasView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
}

// --- GraphWidget Implementation ---

GraphWidget::GraphWidget(QWidget* parent)
    : QWidget(parent), m_filterPeopleActive(true), m_filterHouseholdsActive(true), m_filterGroupsActive(true),
      m_filterBurnoutActive(false), m_filterIsolationActive(false), m_filterHygieneActive(false),
      m_filterBridgeActive(false), m_filterFamilyActive(false),
      m_isSelectMode(false), m_layoutMode(ForceDirected), m_selectedMinistryParentId(0),
      m_pathfindActive(false), m_pathStartNode(nullptr), m_pathEndNode(nullptr),
      m_selectedNodeForDetails(nullptr), m_selectedEdgeForDetails(nullptr),
      m_repulsionStrength(3000.0), m_attractionStrength(0.005),
      m_damping(0.75), m_gravity(0.005), m_physicsRunning(true)
{
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-2000, -2000, 4000, 4000);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &GraphWidget::onSceneSelectionChanged);

    setupUI();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &GraphWidget::applyTheme);
    applyTheme(ThemeManager::instance().currentTokens());

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &GraphWidget::updatePhysics);
    {
        AppSettings s;
        m_physicsRunning = s.value("graphPhysicsEnabled", true).toBool();
        // Advisory thresholds from Settings → Pastoral & Graph (live, not hardcoded).
        m_burnoutThreshold = qBound(2, s.value("burnoutThreshold", 3).toInt(), 10);
        m_familyDominancePercent = qBound(10, s.value("familyDominancePercent", 40).toInt(), 90);
    }
    m_timer->start(33);

    restoreFilterState(); // working view (toggles + layout) from last session
    loadGraph();
}

GraphWidget::~GraphWidget() {
    if (m_timer) m_timer->stop();
    saveNodePositions(); // positions may be unsaved if the app closed without hideEvent
}

void GraphWidget::styleFilterButton(QPushButton* btn, bool active, const QColor& activeColor) {
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    if (active) {
        const QString textOnActive = ThemeManager::textOn(activeColor).name();
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
            .arg(activeColor.name(), textOnActive));
        // The icon was painted in the accent color at creation; on an accent
        // fill it would vanish. Repaint it in the on-accent color.
        const QIcon cur = btn->icon();
        if (cur.isNull()) return;
        // Re-render from the icon's last known name is not possible, so the
        // caller-supplied accent below is stored per-button via a property
        // set at creation; fall back to textOn for safety.
        const QVariant iconName = btn->property("iconName");
        if (iconName.isValid()) {
            btn->setIcon(IconManager::instance().getIcon(iconName.toString(),
                                                         ThemeManager::textOn(activeColor), 14));
        }
    } else {
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
            .arg(tok.bgSurface.name(), tok.textSecondary.name(), tok.borderColor.name()));
        const QVariant iconName = btn->property("iconName");
        const QVariant iconColor = btn->property("iconColor");
        if (iconName.isValid() && iconColor.isValid()) {
            btn->setIcon(IconManager::instance().getIcon(iconName.toString(),
                                                         QColor(iconColor.toString()), 14));
        }
    }
}

void GraphWidget::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(4);

    // --- Left Canvas Container (75% width) ---
    QWidget* leftContainer = new QWidget(m_splitter);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Top Controls Bar
    m_topBarWidget = new QWidget(leftContainer);
    m_topBarWidget->setObjectName("topBarWidget");
    QVBoxLayout* topControlsLayout = new QVBoxLayout(m_topBarWidget);
    topControlsLayout->setContentsMargins(15, 10, 15, 10);
    topControlsLayout->setSpacing(8);

    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(8);
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(8);

    auto createToggleBtn = [this](const QString& text, bool defaultActive) {
        QPushButton* btn = new QPushButton(text, this);
        btn->setCheckable(true);
        btn->setChecked(defaultActive);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    const ThemeTokens tok = ThemeManager::instance().currentTokens();

    m_cmbMinistryBranch = new QComboBox(this);
    m_cmbMinistryBranch->addItem("🌐 All Ministries & Groups", 0);
    m_cmbMinistryBranch->setMinimumWidth(160);
    m_cmbMinistryBranch->setStyleSheet("font-size: 11px; padding: 4px;");
    connect(m_cmbMinistryBranch, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphWidget::onMinistryBranchChanged);

    m_btnToggleHierarchyTree = new QPushButton(" Ministry Tree", this);
    m_btnToggleHierarchyTree->setIcon(IconManager::instance().getIcon("folder-tree", tok.accentPrimary, 16));
    m_btnToggleHierarchyTree->setObjectName("secondaryBtn");
    m_btnToggleHierarchyTree->setStyleSheet("font-size: 11px; padding: 5px 8px; font-weight: bold;");
    m_btnToggleHierarchyTree->setToolTip("View full ministry hierarchy tree table with sub-groups and involved members");
    connect(m_btnToggleHierarchyTree, &QPushButton::clicked, this, [this]() {
        if (m_detailsDock->isHidden()) m_detailsDock->show();
        if (m_dockTabs) m_dockTabs->setCurrentWidget(m_hierarchyTab);
    });

    m_btnToggleSelectMode = new QPushButton(" Pan Canvas", this);
    m_btnToggleSelectMode->setIcon(IconManager::instance().getIcon("move", QColor(), 14));
    m_btnToggleSelectMode->setCheckable(true);
    m_btnToggleSelectMode->setChecked(false);
    m_btnToggleSelectMode->setObjectName("secondaryBtn");
    m_btnToggleSelectMode->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnToggleSelectMode, &QPushButton::clicked, this, &GraphWidget::onToggleSelectMode);

    m_btnTogglePeople = createToggleBtn("People", true);
    m_btnTogglePeople->setIcon(IconManager::instance().getIcon("user", tok.accentPrimary, 14));
    m_btnTogglePeople->setProperty("iconName", "user");
    m_btnTogglePeople->setProperty("iconColor", tok.accentPrimary.name());
    styleFilterButton(m_btnTogglePeople, true, tok.accentPrimary);
    m_btnToggleHouseholds = createToggleBtn("Households", true);
    m_btnToggleHouseholds->setIcon(IconManager::instance().getIcon("users", tok.accentPrimary, 14));
    m_btnToggleHouseholds->setProperty("iconName", "users");
    m_btnToggleHouseholds->setProperty("iconColor", tok.accentPrimary.name());
    styleFilterButton(m_btnToggleHouseholds, true, tok.accentPrimary);
    m_btnToggleGroups = createToggleBtn("Groups", true);
    m_btnToggleGroups->setIcon(IconManager::instance().getIcon("layers", tok.accentPrimary, 14));
    m_btnToggleGroups->setProperty("iconName", "layers");
    m_btnToggleGroups->setProperty("iconColor", tok.accentSuccess.name());
    styleFilterButton(m_btnToggleGroups, true, tok.accentPrimary);
    m_btnToggleBurnout = createToggleBtn("Burnout", false);
    m_btnToggleBurnout->setIcon(IconManager::instance().getIcon("alert-triangle", tok.accentWarning, 14));
    m_btnToggleBurnout->setProperty("iconName", "alert-triangle");
    m_btnToggleBurnout->setProperty("iconColor", tok.accentWarning.name());
    styleFilterButton(m_btnToggleBurnout, false, tok.accentWarning);
    m_btnToggleIsolation = createToggleBtn("Isolated", false);
    m_btnToggleIsolation->setIcon(IconManager::instance().getIcon("alert-triangle", tok.accentDanger, 14));
    m_btnToggleIsolation->setProperty("iconName", "alert-triangle");
    m_btnToggleIsolation->setProperty("iconColor", tok.accentDanger.name());
    styleFilterButton(m_btnToggleIsolation, false, tok.accentDanger);
    m_btnToggleHygiene = createToggleBtn("Hygiene", false);
    m_btnToggleHygiene->setIcon(IconManager::instance().getIcon("sparkles", tok.accentSuccess, 14));
    styleFilterButton(m_btnToggleHygiene, false, tok.accentSuccess);
    m_btnToggleBridgeNodes = createToggleBtn("Bridges", false);
    m_btnToggleBridgeNodes->setIcon(IconManager::instance().getIcon("network", QColor("#cba6f7"), 14));
    styleFilterButton(m_btnToggleBridgeNodes, false, QColor("#cba6f7"));
    m_btnToggleFamilyDynasty = createToggleBtn("Dynasties", false);
    m_btnToggleFamilyDynasty->setIcon(IconManager::instance().getIcon("users", tok.accentWarning, 14));
    styleFilterButton(m_btnToggleFamilyDynasty, false, tok.accentWarning);

    connect(m_btnTogglePeople, &QPushButton::clicked, this, [this]() {
        m_filterPeopleActive = m_btnTogglePeople->isChecked();
        styleFilterButton(m_btnTogglePeople, m_filterPeopleActive, ThemeManager::instance().currentTokens().accentPrimary);
        onFilterToggled();
    });
    connect(m_btnToggleHouseholds, &QPushButton::clicked, this, [this]() {
        m_filterHouseholdsActive = m_btnToggleHouseholds->isChecked();
        styleFilterButton(m_btnToggleHouseholds, m_filterHouseholdsActive, ThemeManager::instance().currentTokens().accentPrimary);
        onFilterToggled();
    });
    connect(m_btnToggleGroups, &QPushButton::clicked, this, [this]() {
        m_filterGroupsActive = m_btnToggleGroups->isChecked();
        styleFilterButton(m_btnToggleGroups, m_filterGroupsActive, ThemeManager::instance().currentTokens().accentPrimary);
        onFilterToggled();
    });
    connect(m_btnToggleBurnout, &QPushButton::clicked, this, [this]() {
        m_filterBurnoutActive = m_btnToggleBurnout->isChecked();
        styleFilterButton(m_btnToggleBurnout, m_filterBurnoutActive, ThemeManager::instance().currentTokens().accentWarning);
        onFilterToggled();
    });
    connect(m_btnToggleIsolation, &QPushButton::clicked, this, [this]() {
        m_filterIsolationActive = m_btnToggleIsolation->isChecked();
        styleFilterButton(m_btnToggleIsolation, m_filterIsolationActive, ThemeManager::instance().currentTokens().accentDanger);
        onFilterToggled();
    });
    connect(m_btnToggleHygiene, &QPushButton::clicked, this, &GraphWidget::onToggleDataHygiene);
    connect(m_btnToggleBridgeNodes, &QPushButton::clicked, this, [this]() {
        m_filterBridgeActive = m_btnToggleBridgeNodes->isChecked();
        styleFilterButton(m_btnToggleBridgeNodes, m_filterBridgeActive, QColor("#cba6f7"));
        if (m_filterBridgeActive) spotlightBridgeNodes();
        else resetHighlight();
        m_scene->update();
    });
    connect(m_btnToggleFamilyDynasty, &QPushButton::clicked, this, [this]() {
        m_filterFamilyActive = m_btnToggleFamilyDynasty->isChecked();
        styleFilterButton(m_btnToggleFamilyDynasty, m_filterFamilyActive, ThemeManager::instance().currentTokens().accentWarning);
        if (m_filterFamilyActive) spotlightFamilyDynasties();
        else resetHighlight();
        m_scene->update();
    });

    row1->addWidget(m_cmbMinistryBranch);
    row1->addWidget(m_btnToggleHierarchyTree);
    row1->addWidget(m_btnToggleSelectMode);
    row1->addWidget(m_btnTogglePeople);
    row1->addWidget(m_btnToggleHouseholds);
    row1->addWidget(m_btnToggleGroups);
    row1->addWidget(m_btnToggleBurnout);
    row1->addWidget(m_btnToggleIsolation);
    row1->addWidget(m_btnToggleHygiene);
    row1->addWidget(m_btnToggleBridgeNodes);
    row1->addWidget(m_btnToggleFamilyDynasty);
    row1->addStretch();

    m_btnToggleLayout = new QPushButton(" Force-Directed", this);
    m_btnToggleLayout->setIcon(IconManager::instance().getIcon("activity", QColor(), 14));
    m_btnToggleLayout->setObjectName("secondaryBtn");
    m_btnToggleLayout->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnToggleLayout, &QPushButton::clicked, this, &GraphWidget::onToggleLayoutMode);

    m_btnToggleFreeze = new QPushButton(" Freeze Layout", this);
    m_btnToggleFreeze->setIcon(IconManager::instance().getIcon("pause", QColor(), 14));
    m_btnToggleFreeze->setObjectName("secondaryBtn");
    m_btnToggleFreeze->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    m_btnToggleFreeze->setToolTip("Freeze / Unfreeze the dynamic force-directed physics layout");
    connect(m_btnToggleFreeze, &QPushButton::clicked, this, [this]() {
        if (m_physicsRunning) pausePhysics();
        else resumePhysics();
    });

    m_btnToggleStatusCluster = new QPushButton(" Cluster by Status", this);
    m_btnToggleStatusCluster->setIcon(IconManager::instance().getIcon("tag", QColor(), 14));
    m_btnToggleStatusCluster->setObjectName("secondaryBtn");
    m_btnToggleStatusCluster->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    m_btnToggleStatusCluster->setToolTip("Recolor people by membership status (Member / Attender / Visitor)");
    connect(m_btnToggleStatusCluster, &QPushButton::clicked, this, &GraphWidget::onToggleStatusCluster);

    m_btnIsolatedBanner = new QPushButton(" 0 Unconnected Members", this);
    m_btnIsolatedBanner->setIcon(IconManager::instance().getIcon("alert-triangle", tok.accentWarning, 14));
    m_btnIsolatedBanner->setCursor(Qt::PointingHandCursor);
    connect(m_btnIsolatedBanner, &QPushButton::clicked, this, &GraphWidget::onSpotlightIsolatedClicked);

    m_graphSearchEdit = new QLineEdit(this);
    m_graphSearchEdit->setPlaceholderText("Search graph...");
    m_graphSearchEdit->setFixedWidth(130);
    m_graphSearchEdit->setStyleSheet("font-size: 11px; padding: 4px;");
    connect(m_graphSearchEdit, &QLineEdit::textChanged, this, &GraphWidget::onGraphSearchChanged);

    m_btnPathfind = new QPushButton(" Pathfinder", this);
    m_btnPathfind->setIcon(IconManager::instance().getIcon("search", QColor(), 14));
    m_btnPathfind->setObjectName("secondaryBtn");
    m_btnPathfind->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnPathfind, &QPushButton::clicked, this, &GraphWidget::onPathfindToggle);

    m_btnRunAudit = new QPushButton(" Audit Report", this);
    m_btnRunAudit->setIcon(IconManager::instance().getIcon("bar-chart-2", QColor(), 14));
    m_btnRunAudit->setObjectName("secondaryBtn");
    m_btnRunAudit->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnRunAudit, &QPushButton::clicked, this, &GraphWidget::onRunAudit);

    m_btnExportPNG = new QPushButton(" Export PNG", this);
    m_btnExportPNG->setIcon(IconManager::instance().getIcon("camera", QColor(), 14));
    m_btnExportPNG->setObjectName("secondaryBtn");
    m_btnExportPNG->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnExportPNG, &QPushButton::clicked, this, &GraphWidget::onExportPNG);

    m_btnLegendToggle = new QPushButton(" Legend", this);
    m_btnLegendToggle->setIcon(IconManager::instance().getIcon("help-circle", QColor(), 14));
    m_btnLegendToggle->setObjectName("secondaryBtn");
    m_btnLegendToggle->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnLegendToggle, &QPushButton::clicked, this, &GraphWidget::onToggleLegend);

    row2->addWidget(m_graphSearchEdit);
    row2->addWidget(m_btnIsolatedBanner);
    row2->addStretch();
    row2->addWidget(m_btnToggleLayout);
    row2->addWidget(m_btnToggleFreeze);
    row2->addWidget(m_btnToggleStatusCluster);
    row2->addWidget(m_btnPathfind);
    row2->addWidget(m_btnRunAudit);
    row2->addWidget(m_btnExportPNG);
    row2->addWidget(m_btnLegendToggle);

    topControlsLayout->addLayout(row1);
    topControlsLayout->addLayout(row2);
    leftLayout->addWidget(m_topBarWidget);

    m_canvasView = new GraphCanvasView(m_scene, leftContainer);
    m_canvasView->setGraphWidget(this);
    m_canvasView->setFocusPolicy(Qt::StrongFocus); // arrow-key navigation
    leftLayout->addWidget(m_canvasView, 1);
    m_splitter->addWidget(leftContainer);

    // Floating Zoom Bar Overlay on Canvas Bottom Right
    m_floatingZoomBar = new QWidget(m_canvasView);
    m_floatingZoomBar->setObjectName("floatingZoomBar");
    m_floatingZoomBar->setStyleSheet("background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; padding: 2px;");
    QHBoxLayout* fzLay = new QHBoxLayout(m_floatingZoomBar);
    fzLay->setContentsMargins(4, 4, 4, 4);
    fzLay->setSpacing(4);

    QPushButton* fzIn = new QPushButton(m_floatingZoomBar);
    fzIn->setIcon(IconManager::instance().getIcon("zoom-in", QColor(), 14));
    fzIn->setToolTip("Zoom In");
    fzIn->setStyleSheet("background: transparent; border: none; padding: 3px 6px;");
    QPushButton* fzOut = new QPushButton(m_floatingZoomBar);
    fzOut->setIcon(IconManager::instance().getIcon("zoom-out", QColor(), 14));
    fzOut->setToolTip("Zoom Out");
    fzOut->setStyleSheet("background: transparent; border: none; padding: 3px 6px;");
    QPushButton* fzReset = new QPushButton(m_floatingZoomBar);
    fzReset->setIcon(IconManager::instance().getIcon("maximize-2", QColor(), 14));
    fzReset->setToolTip("Fit Network");
    fzReset->setStyleSheet("background: transparent; border: none; padding: 3px 6px;");

    connect(fzIn, &QPushButton::clicked, this, &GraphWidget::onZoomIn);
    connect(fzOut, &QPushButton::clicked, this, &GraphWidget::onZoomOut);
    connect(fzReset, &QPushButton::clicked, this, &GraphWidget::onZoomReset);

    fzLay->addWidget(fzIn);
    fzLay->addWidget(fzOut);
    fzLay->addWidget(fzReset);

    // Floating View Options Bar Overlay on Canvas Bottom Left
    m_floatingViewOptionsBar = new QWidget(m_canvasView);
    m_floatingViewOptionsBar->setObjectName("floatingViewOptionsBar");
    QHBoxLayout* fvoLay = new QHBoxLayout(m_floatingViewOptionsBar);
    fvoLay->setContentsMargins(6, 4, 6, 4);
    fvoLay->setSpacing(5);

    m_btnToggleInspectorDock = new QPushButton(" Inspector (On)", m_floatingViewOptionsBar);
    m_btnToggleInspectorDock->setIcon(IconManager::instance().getIcon("sidebar", QColor(), 14));
    m_btnToggleInspectorDock->setCheckable(true);
    m_btnToggleInspectorDock->setChecked(true);
    m_btnToggleInspectorDock->setToolTip("Toggle Context Inspector & Hierarchy Panel (Expand / Collapse)");
    m_btnToggleInspectorDock->setStyleSheet("font-size: 11px; font-weight: bold; padding: 3px 8px; border-radius: 5px;");
    connect(m_btnToggleInspectorDock, &QPushButton::clicked, this, &GraphWidget::onToggleCollapseDock);

    QFrame* sep1 = new QFrame(m_floatingViewOptionsBar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);

    QLabel* lblSize = new QLabel("📐 Links:", m_floatingViewOptionsBar);
    lblSize->setStyleSheet("font-size: 11px; font-weight: bold;");

    m_btnConnThin = new QPushButton("Thin", m_floatingViewOptionsBar);
    m_btnConnThin->setCheckable(true);
    m_btnConnThin->setToolTip("Connector Size: Thin (0.6x) — ideal for dense graphs");

    m_btnConnNormal = new QPushButton("Normal", m_floatingViewOptionsBar);
    m_btnConnNormal->setCheckable(true);
    m_btnConnNormal->setChecked(true);
    m_btnConnNormal->setToolTip("Connector Size: Standard (1.0x)");

    m_btnConnBold = new QPushButton("Bold", m_floatingViewOptionsBar);
    m_btnConnBold->setCheckable(true);
    m_btnConnBold->setToolTip("Connector Size: Bold (1.8x) — high prominence");

    connect(m_btnConnThin, &QPushButton::clicked, this, [this]() { onConnectorSizeLevelChanged(1); });
    connect(m_btnConnNormal, &QPushButton::clicked, this, [this]() { onConnectorSizeLevelChanged(2); });
    connect(m_btnConnBold, &QPushButton::clicked, this, [this]() { onConnectorSizeLevelChanged(3); });

    QFrame* sep2 = new QFrame(m_floatingViewOptionsBar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);

    m_btnToggleConnectors = new QPushButton(" Links On", m_floatingViewOptionsBar);
    m_btnToggleConnectors->setIcon(IconManager::instance().getIcon("link", QColor(), 14));
    m_btnToggleConnectors->setCheckable(true);
    m_btnToggleConnectors->setChecked(true);
    m_btnToggleConnectors->setToolTip("Toggle All Relational Connector Lines (Hide for clean Tree View & Cluster Layout)");
    m_btnToggleConnectors->setStyleSheet("font-size: 11px; font-weight: bold; padding: 3px 8px; border-radius: 5px;");
    connect(m_btnToggleConnectors, &QPushButton::clicked, this, &GraphWidget::onToggleConnectors);

    fvoLay->addWidget(m_btnToggleInspectorDock);
    fvoLay->addWidget(sep1);
    fvoLay->addWidget(lblSize);
    fvoLay->addWidget(m_btnConnThin);
    fvoLay->addWidget(m_btnConnNormal);
    fvoLay->addWidget(m_btnConnBold);
    fvoLay->addWidget(sep2);
    fvoLay->addWidget(m_btnToggleConnectors);

    // Floating Expand Tab on Right Edge (visible when dock is collapsed)
    m_btnFloatingExpandDock = new QPushButton(" Open Inspector", m_canvasView);
    m_btnFloatingExpandDock->setIcon(IconManager::instance().getPrimaryIcon("chevron-left", 14));
    m_btnFloatingExpandDock->setObjectName("primaryBtn");
    m_btnFloatingExpandDock->setToolTip("Expand Context Inspector & Hierarchy Panel");
    m_btnFloatingExpandDock->hide();
    connect(m_btnFloatingExpandDock, &QPushButton::clicked, this, &GraphWidget::onToggleCollapseDock);

    // --- Right Inspector Dock (25% width) ---
    m_detailsDock = new QWidget(m_splitter);
    m_detailsDock->setObjectName("detailsDock");
    m_detailsDock->setMinimumWidth(320);
    // Colors come from styleChromeWidgets() (theme tokens) — called at the end
    // of setupUI and on every theme change.
    QVBoxLayout* dockLayout = new QVBoxLayout(m_detailsDock);
    dockLayout->setContentsMargins(10, 10, 10, 10);
    dockLayout->setSpacing(8);

    // Dock Header with Title and Collapse Toggle
    QHBoxLayout* dockHeader = new QHBoxLayout();
    QLabel* dockTitle = new QLabel("Context Inspector", m_detailsDock);
    dockTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: palette(highlight);");
    m_btnCollapseDock = new QPushButton(" Collapse", m_detailsDock);
    m_btnCollapseDock->setIcon(IconManager::instance().getIcon("chevron-right", QColor(), 14));
    m_btnCollapseDock->setObjectName("secondaryBtn");
    m_btnCollapseDock->setStyleSheet("padding: 3px 8px; font-size: 10px;");
    connect(m_btnCollapseDock, &QPushButton::clicked, this, &GraphWidget::onToggleCollapseDock);
    dockHeader->addWidget(dockTitle);
    dockHeader->addStretch();
    dockHeader->addWidget(m_btnCollapseDock);
    dockLayout->addLayout(dockHeader);

    m_dockTabs = new QTabWidget(m_detailsDock);
    m_dockTabs->setStyleSheet("QTabWidget::pane { border: none; background: transparent; } QTabBar::tab { font-size: 11px; padding: 6px 12px; }");

    // Tab 1: Node Context Details
    m_detailsStack = new QStackedWidget();

    m_detailsScrollArea = new QScrollArea(m_dockTabs);
    m_detailsScrollArea->setWidgetResizable(true);
    m_detailsScrollArea->setFrameShape(QFrame::NoFrame);
    m_detailsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailsScrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; } QScrollBar:vertical { width: 6px; background: transparent; } QScrollBar::handle:vertical { background: #313244; border-radius: 3px; }");
    m_detailsScrollArea->setWidget(m_detailsStack);
    m_dockTabs->addTab(m_detailsScrollArea, "Inspector");
    m_dockTabs->setTabIcon(0, IconManager::instance().getIcon("sidebar", tok.accentPrimary, 14));

    // Tab 2: Hierarchy Tree Explorer
    m_hierarchyTab = new QWidget(m_dockTabs);
    QVBoxLayout* hLay = new QVBoxLayout(m_hierarchyTab);
    hLay->setContentsMargins(0, 6, 0, 0);
    hLay->setSpacing(6);

    QHBoxLayout* hControls = new QHBoxLayout();
    m_txtHierarchySearch = new QLineEdit(m_hierarchyTab);
    m_txtHierarchySearch->setPlaceholderText("Filter ministry tree...");
    m_txtHierarchySearch->setClearButtonEnabled(true);
    m_btnExpandAllHierarchy = new QPushButton(m_hierarchyTab);
    m_btnExpandAllHierarchy->setIcon(IconManager::instance().getIcon("plus", QColor(), 14));
    m_btnExpandAllHierarchy->setToolTip("Expand All");
    m_btnExpandAllHierarchy->setStyleSheet("padding: 3px 6px; font-size: 11px;");
    m_btnCollapseAllHierarchy = new QPushButton(m_hierarchyTab);
    m_btnCollapseAllHierarchy->setIcon(IconManager::instance().getIcon("minus", QColor(), 14));
    m_btnCollapseAllHierarchy->setToolTip("Collapse All");
    m_btnCollapseAllHierarchy->setStyleSheet("padding: 3px 6px; font-size: 11px;");

    hControls->addWidget(m_txtHierarchySearch, 1);
    hControls->addWidget(m_btnExpandAllHierarchy);
    hControls->addWidget(m_btnCollapseAllHierarchy);
    hLay->addLayout(hControls);

    m_hierarchyTreeWidget = new QTreeWidget(m_hierarchyTab);
    m_hierarchyTreeWidget->setHeaderLabels({"Entity / Member", "Role / Type", "Status"});
    m_hierarchyTreeWidget->setAlternatingRowColors(true);
    m_hierarchyTreeWidget->setAnimated(true);
    m_hierarchyTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_hierarchyTreeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_hierarchyTreeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hLay->addWidget(m_hierarchyTreeWidget, 1);

    connect(m_btnExpandAllHierarchy, &QPushButton::clicked, m_hierarchyTreeWidget, &QTreeWidget::expandAll);
    connect(m_btnCollapseAllHierarchy, &QPushButton::clicked, m_hierarchyTreeWidget, &QTreeWidget::collapseAll);
    connect(m_txtHierarchySearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        QString term = text.trimmed().toLower();
        std::function<bool(QTreeWidgetItem*)> filterItem = [&](QTreeWidgetItem* item) -> bool {
            bool matches = item->text(0).toLower().contains(term) || item->text(1).toLower().contains(term);
            bool childMatches = false;
            for (int i = 0; i < item->childCount(); ++i) {
                if (filterItem(item->child(i))) childMatches = true;
            }
            bool show = matches || childMatches || term.isEmpty();
            item->setHidden(!show);
            if (childMatches && !term.isEmpty()) item->setExpanded(true);
            return show;
        };
        for (int i = 0; i < m_hierarchyTreeWidget->topLevelItemCount(); ++i) {
            filterItem(m_hierarchyTreeWidget->topLevelItem(i));
        }
    });

    connect(m_hierarchyTreeWidget, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item) return;
        QString type = item->data(0, Qt::UserRole + 1).toString();
        int id = item->data(0, Qt::UserRole).toInt();
        if (id <= 0) return;

        for (NodeItem* n : m_nodes) {
            if (type == "Person" && n->isPersonNode() && n->id() == id) {
                focusAndSelectNode(n);
                selectNodeForDetails(n);
                break;
            } else if (type == "Group" && n->nodeType() == NodeItem::Group && n->id() == id) {
                focusAndSelectNode(n);
                selectNodeForDetails(n);
                break;
            }
        }
    });

    m_hierarchyTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hierarchyTreeWidget, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_hierarchyTreeWidget->itemAt(pos);
        if (!item) return;
        QString type = item->data(0, Qt::UserRole + 1).toString();
        int id = item->data(0, Qt::UserRole).toInt();
        if (id <= 0) return;

        QMenu menu(this);
        if (type == "Person") {
            QAction* actProfile = menu.addAction(IconManager::instance().getIcon("user", QColor(), 14), "View Full Profile");
            QAction* actPhone = menu.addAction(IconManager::instance().getIcon("phone", QColor(), 14), "Copy Phone Number");
            QAction* actEmail = menu.addAction(IconManager::instance().getIcon("mail", QColor(), 14), "Copy Email Address");
            menu.addSeparator();
            QAction* actFocus = menu.addAction(IconManager::instance().getIcon("crosshair", QColor(), 14), "Focus on Canvas");

            QAction* sel = menu.exec(m_hierarchyTreeWidget->viewport()->mapToGlobal(pos));
            if (!sel) return;

            if (sel == actProfile) {
                emit navigateToProfile("Person", id);
            } else if (sel == actPhone) {
                QString phone = item->data(0, Qt::UserRole + 3).toString();
                if (!phone.isEmpty()) QGuiApplication::clipboard()->setText(phone);
            } else if (sel == actEmail) {
                QString email = item->data(0, Qt::UserRole + 4).toString();
                if (!email.isEmpty()) QGuiApplication::clipboard()->setText(email);
            } else if (sel == actFocus) {
                for (NodeItem* n : m_nodes) {
                    if (n->isPersonNode() && n->id() == id) {
                        focusAndSelectNode(n);
                        break;
                    }
                }
            }
        } else if (type == "Group") {
            QAction* actFocus = menu.addAction(IconManager::instance().getIcon("crosshair", QColor(), 14), "Focus on Canvas");
            QAction* actMinistries = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "View in Ministries Tab");

            QAction* sel = menu.exec(m_hierarchyTreeWidget->viewport()->mapToGlobal(pos));
            if (!sel) return;

            if (sel == actFocus) {
                for (NodeItem* n : m_nodes) {
                    if (n->nodeType() == NodeItem::Group && n->id() == id) {
                        focusAndSelectNode(n);
                        break;
                    }
                }
            } else if (sel == actMinistries) {
                emit navigateToProfile("Group", id);
            }
        }
    });

    m_dockTabs->addTab(m_hierarchyTab, "Ministry Tree");
    m_dockTabs->setTabIcon(1, IconManager::instance().getIcon("folder-tree", tok.accentPrimary, 14));
    dockLayout->addWidget(m_dockTabs, 1);

    // Page 0: Person Node Inspector
    m_pagePerson = new QWidget(m_detailsStack);
    QVBoxLayout* pLay = new QVBoxLayout(m_pagePerson);
    pLay->setContentsMargins(0, 0, 0, 0);
    pLay->setSpacing(10);

    m_lblPersonAvatar = new QLabel("👤", m_pagePerson);
    m_lblPersonAvatar->setStyleSheet("font-size: 28px;");
    m_lblPersonName = new QLabel("Person Name", m_pagePerson);
    m_lblPersonName->setStyleSheet("font-size: 16px; font-weight: bold; color: palette(text);");
    m_lblPersonStatus = new QLabel("Status: Active Member", m_pagePerson);
    m_lblPersonStatus->setStyleSheet("font-size: 12px; color: palette(highlight); font-weight: 500;");

    m_lblPersonBrokerBadge = new QLabel("🌉 Relational Broker", m_pagePerson);
    m_lblPersonBrokerBadge->setStyleSheet("background-color: #3c2656; color: #cba6f7; border: 1px solid #cba6f7; border-radius: 4px; padding: 4px 8px; font-size: 11px; font-weight: bold;");
    m_lblPersonBrokerBadge->hide();

    QHBoxLayout* contactRow = new QHBoxLayout();
    m_btnCopyPhone = new QPushButton(" Copy Phone", m_pagePerson);
    m_btnCopyPhone->setIcon(IconManager::instance().getIcon("phone", QColor(), 14));
    m_btnCopyPhone->setObjectName("secondaryBtn");
    m_btnCopyPhone->setStyleSheet("font-size: 11px; padding: 4px 8px; font-weight: bold;");
    m_btnCopyEmail = new QPushButton(" Copy Email", m_pagePerson);
    m_btnCopyEmail->setIcon(IconManager::instance().getIcon("mail", QColor(), 14));
    m_btnCopyEmail->setObjectName("secondaryBtn");
    m_btnCopyEmail->setStyleSheet("font-size: 11px; padding: 4px 8px; font-weight: bold;");
    connect(m_btnCopyPhone, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails) onCopyContactInfo(m_selectedNodeForDetails, "phone");
    });
    connect(m_btnCopyEmail, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails) onCopyContactInfo(m_selectedNodeForDetails, "email");
    });
    contactRow->addWidget(m_btnCopyPhone);
    contactRow->addWidget(m_btnCopyEmail);

    QLabel* lblCare = new QLabel("Care Flag:", m_pagePerson);
    lblCare->setStyleSheet("font-size: 12px; font-weight: bold; color: palette(text);");
    m_cmbCareFlag = new QComboBox(m_pagePerson);
    m_cmbCareFlag->setObjectName("careFlagCombo");
    // Canonical pastoral care flags (same vocabulary as PeopleWidget) so a flag
    // set here reads back correctly everywhere else.
    m_cmbCareFlag->addItems({"Normal", "Needs Contact", "At-Risk", "Follow-up", "Grief Support", "Leader"});
    m_cmbCareFlag->setStyleSheet("font-size: 11px; padding: 5px;");
    connect(m_cmbCareFlag, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphWidget::onCareFlagChanged);

    m_lblPersonGroups = new QLabel("Connected Groups: (Loading...)", m_pagePerson);
    m_lblPersonGroups->setWordWrap(true);

    m_smartBridgeSection = new QWidget(m_pagePerson);
    m_smartBridgeSection->setObjectName("smartBridgeSection");
    m_smartBridgeSection->hide();
    m_smartBridgeLayout = new QVBoxLayout(m_smartBridgeSection);
    m_smartBridgeLayout->setContentsMargins(10, 10, 10, 10);
    m_smartBridgeLayout->setSpacing(8);

    m_txtAdminNotes = new QPlainTextEdit(m_pagePerson);
    m_txtAdminNotes->setPlaceholderText("Pastoral admin notes & observations...");
    m_txtAdminNotes->setMaximumHeight(75);

    m_btnSaveNotes = new QPushButton(" Save Notes", m_pagePerson);
    m_btnSaveNotes->setIcon(IconManager::instance().getIcon("save", QColor(), 14));
    m_btnSaveNotes->setObjectName("secondaryBtn");
    m_btnSaveNotes->setStyleSheet("font-size: 11px; padding: 5px; font-weight: bold;");
    connect(m_btnSaveNotes, &QPushButton::clicked, this, &GraphWidget::onSaveNotes);

    m_btnOpenProfile = new QPushButton(" View Full Member Dossier", m_pagePerson);
    m_btnOpenProfile->setIcon(IconManager::instance().getPrimaryIcon("user", 14));
    m_btnOpenProfile->setObjectName("primaryBtn");
    m_btnOpenProfile->setCursor(Qt::PointingHandCursor);
    m_btnOpenProfile->setStyleSheet("font-size: 11px; padding: 7px; font-weight: bold;");
    connect(m_btnOpenProfile, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails && m_selectedNodeForDetails->isPersonNode()) {
            emit navigateToProfile("Person", m_selectedNodeForDetails->id());
        }
    });

    pLay->addWidget(m_lblPersonAvatar);
    pLay->addWidget(m_lblPersonName);
    pLay->addWidget(m_lblPersonStatus);

    // Upgraded context strip: age/membership/household, attendance pulse and
    // recent pastoral care, so a pastor sees the whole person at a glance.
    m_lblPersonStats = new QLabel(m_pagePerson);
    m_lblPersonStats->setObjectName("personStatsLabel");
    m_lblPersonStats->setWordWrap(true);
    m_lblPersonStats->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    pLay->addWidget(m_lblPersonStats);

    m_lblPersonAttendance = new QLabel(m_pagePerson);
    m_lblPersonAttendance->setObjectName("personAttendanceLabel");
    m_lblPersonAttendance->setWordWrap(true);
    m_lblPersonAttendance->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    pLay->addWidget(m_lblPersonAttendance);

    m_lblPersonCareRecent = new QLabel(m_pagePerson);
    m_lblPersonCareRecent->setObjectName("personCareRecentLabel");
    m_lblPersonCareRecent->setWordWrap(true);
    m_lblPersonCareRecent->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    pLay->addWidget(m_lblPersonCareRecent);

    pLay->addWidget(m_lblPersonBrokerBadge);
    pLay->addLayout(contactRow);
    pLay->addWidget(lblCare);
    pLay->addWidget(m_cmbCareFlag);
    pLay->addWidget(m_lblPersonGroups);
    pLay->addWidget(m_smartBridgeSection);
    pLay->addWidget(m_txtAdminNotes);
    pLay->addWidget(m_btnSaveNotes);
    pLay->addWidget(m_btnOpenProfile);

    // Reconnect workflow: log a contact attempt -> pastoral_activities + flag.
    m_btnLogContact = new QPushButton(" Log Contact Attempt", m_pagePerson);
    m_btnLogContact->setIcon(IconManager::instance().getIcon("phone-call", QColor(), 14));
    m_btnLogContact->setObjectName("secondaryBtn");
    m_btnLogContact->setStyleSheet("font-size: 11px; padding: 5px;");
    m_btnLogContact->setToolTip("Records a contact attempt (pastoral log) and marks the person 'Needs Contact'");
    connect(m_btnLogContact, &QPushButton::clicked, this, &GraphWidget::onLogContact);
    pLay->addWidget(m_btnLogContact);

    m_lblLogContactStatus = new QLabel(m_pagePerson);
    m_lblLogContactStatus->setWordWrap(true);
    m_lblLogContactStatus->hide();
    pLay->addWidget(m_lblLogContactStatus);

    pLay->addStretch();
    m_detailsStack->addWidget(m_pagePerson);

    // Page 1: Group Node Inspector
    m_pageGroup = new QWidget(m_detailsStack);
    QVBoxLayout* gLay = new QVBoxLayout(m_pageGroup);
    gLay->setContentsMargins(0, 0, 0, 0);
    gLay->setSpacing(8);

    m_lblGroupName = new QLabel("Group Name", m_pageGroup);
    m_lblGroupName->setStyleSheet("font-size: 15px; font-weight: bold;");
    m_lblGroupCategory = new QLabel("Category: Ministry", m_pageGroup);
    m_lblGroupCategory->setStyleSheet("font-size: 11px; opacity: 0.8;");
    m_lblGroupHealth = new QLabel("Health: Fully Staffed", m_pageGroup);
    m_lblGroupHealth->setStyleSheet("font-size: 11px; font-weight: bold;");

    m_lblGroupFamilyWarning = new QLabel("⚠️ High Family Concentration", m_pageGroup);
    m_lblGroupFamilyWarning->setStyleSheet("background-color: #382418; color: #fab387; border: 1px solid #fab387; border-radius: 4px; padding: 4px 6px; font-size: 10px;");
    m_lblGroupFamilyWarning->setWordWrap(true);
    m_lblGroupFamilyWarning->hide();

    m_lblGroupMemberCount = new QLabel("Members (0):", m_pageGroup);
    m_lblGroupMemberCount->setStyleSheet("font-size: 11px; font-weight: bold;");

    m_lstRoster = new QListWidget(m_pageGroup);
    m_lstRoster->setMaximumHeight(140);
    connect(m_lstRoster, &QListWidget::itemClicked, this, &GraphWidget::onRosterItemClicked);

    QHBoxLayout* groupBtnRow = new QHBoxLayout();
    m_btnGroupAddMember = new QPushButton(" Add Member", m_pageGroup);
    m_btnGroupAddMember->setIcon(IconManager::instance().getPrimaryIcon("user-plus", 14));
    m_btnGroupAddMember->setObjectName("primaryBtn");
    m_btnGroupAddMember->setStyleSheet("font-size: 10px; padding: 4px 6px;");
    m_btnGroupCopyEmails = new QPushButton(" Copy Emails", m_pageGroup);
    m_btnGroupCopyEmails->setIcon(IconManager::instance().getIcon("mail", QColor(), 14));
    m_btnGroupCopyEmails->setObjectName("secondaryBtn");
    m_btnGroupCopyEmails->setStyleSheet("font-size: 10px; padding: 4px 6px;");
    connect(m_btnGroupAddMember, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails) onQuickAssignGroup(m_selectedNodeForDetails);
    });
    connect(m_btnGroupCopyEmails, &QPushButton::clicked, this, [this]() {
        if (m_lstRoster) {
            QStringList emails;
            QSqlQuery q;
            q.prepare("SELECT email FROM people WHERE id IN (SELECT source_id FROM entity_relationships WHERE target_id = ? AND source_type = 'Person') AND email != ''");
            if (m_selectedNodeForDetails) q.addBindValue(m_selectedNodeForDetails->id());
            if (q.exec()) {
                while (q.next()) emails << q.value(0).toString();
            }
            if (!emails.isEmpty()) {
                QGuiApplication::clipboard()->setText(emails.join(", "));
                QMessageBox::information(this, "Copied", QString("Copied %1 email addresses to clipboard.").arg(emails.size()));
            }
        }
    });
    groupBtnRow->addWidget(m_btnGroupAddMember);
    groupBtnRow->addWidget(m_btnGroupCopyEmails);

    // Suggest People: launch the Ministry Match recommender for this group
    // (declared skills/age profile, ranked, one-click add).
    m_btnGroupSuggest = new QPushButton(" Suggest People", m_pageGroup);
    m_btnGroupSuggest->setIcon(IconManager::instance().getIcon("sparkles", QColor(), 14));
    m_btnGroupSuggest->setObjectName("accentBtnCyan");
    m_btnGroupSuggest->setStyleSheet("font-size: 10px; padding: 4px 6px;");
    m_btnGroupSuggest->setToolTip("Find people who fit this ministry and add them in one click");
    connect(m_btnGroupSuggest, &QPushButton::clicked, this, &GraphWidget::onGroupSuggest);
    groupBtnRow->addWidget(m_btnGroupSuggest);

    // Prep tasks: ministry_tasks linked to this group (real dates), with a
    // slipping flag when past due and not completed.
    m_lblGroupTasks = new QLabel("📋 Prep tasks: none", m_pageGroup);
    m_lblGroupTasks->setObjectName("groupPrepTasksLabel");
    m_lblGroupTasks->setWordWrap(true);

    gLay->addWidget(m_lblGroupName);
    gLay->addWidget(m_lblGroupCategory);

    // Recruitment badge + capacity/meeting meta + archetype composition.
    m_lblGroupRecruit = new QLabel(m_pageGroup);
    m_lblGroupRecruit->setObjectName("groupRecruitLabel");
    m_lblGroupRecruit->setWordWrap(true);
    m_lblGroupRecruit->hide();
    gLay->addWidget(m_lblGroupRecruit);

    m_lblGroupMeta = new QLabel(m_pageGroup);
    m_lblGroupMeta->setObjectName("groupMetaLabel");
    m_lblGroupMeta->setWordWrap(true);
    m_lblGroupMeta->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    gLay->addWidget(m_lblGroupMeta);

    m_lblGroupComposition = new QLabel(m_pageGroup);
    m_lblGroupComposition->setObjectName("groupCompositionLabel");
    m_lblGroupComposition->setWordWrap(true);
    m_lblGroupComposition->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    gLay->addWidget(m_lblGroupComposition);

    gLay->addWidget(m_lblGroupHealth);
    gLay->addWidget(m_lblGroupFamilyWarning);
    gLay->addWidget(m_lblGroupMemberCount);
    gLay->addWidget(m_lstRoster);
    gLay->addLayout(groupBtnRow);
    gLay->addWidget(m_lblGroupTasks);
    gLay->addStretch();
    m_detailsStack->addWidget(m_pageGroup);

    // Page 2: Edge / Relationship Inspector
    m_pageEdge = new QWidget(m_detailsStack);
    QVBoxLayout* eLay = new QVBoxLayout(m_pageEdge);
    eLay->setContentsMargins(0, 0, 0, 0);
    eLay->setSpacing(8);

    m_lblEdgeTitle = new QLabel("Relationship Details", m_pageEdge);
    m_lblEdgeTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: palette(highlight);");
    m_lblEdgeBetween = new QLabel("Between: Person ↔ Group", m_pageEdge);
    m_lblEdgeBetween->setStyleSheet("font-size: 12px; font-weight: bold;");
    m_lblEdgeBetween->setWordWrap(true);

    QLabel* lblRole = new QLabel("Assigned Role / Connection:", m_pageEdge);
    lblRole->setStyleSheet("font-size: 11px;");
    m_cmbEdgeRole = new QComboBox(m_pageEdge);
    m_cmbEdgeRole->setObjectName("edgeRoleCombo");
    m_cmbEdgeRole->addItems({"Member", "Leader", "Co-Leader", "Volunteer", "Spouse", "Child", "Parent"});
    connect(m_cmbEdgeRole, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphWidget::onEdgeRoleChanged);

    m_btnSeverEdge = new QPushButton(" Sever Connection / Remove", m_pageEdge);
    m_btnSeverEdge->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnSeverEdge->setObjectName("dangerBtn");
    m_btnSeverEdge->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnSeverEdge, &QPushButton::clicked, this, &GraphWidget::onSeverSelectedEdge);

    eLay->addWidget(m_lblEdgeTitle);
    eLay->addWidget(m_lblEdgeBetween);
    eLay->addWidget(lblRole);
    eLay->addWidget(m_cmbEdgeRole);

    // Provenance + jump-to-endpoint navigation.
    m_lblEdgeSource = new QLabel(m_pageEdge);
    m_lblEdgeSource->setObjectName("edgeSourceLabel");
    m_lblEdgeSource->setWordWrap(true);
    m_lblEdgeSource->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    eLay->addWidget(m_lblEdgeSource);

    QHBoxLayout* edgeNav = new QHBoxLayout();
    m_btnEdgeOpenLeft = new QPushButton(" Open Source", m_pageEdge);
    m_btnEdgeOpenLeft->setIcon(IconManager::instance().getIcon("arrow-left", QColor(), 14));
    m_btnEdgeOpenLeft->setObjectName("secondaryBtn");
    m_btnEdgeOpenLeft->setStyleSheet("font-size: 10px; padding: 4px 6px;");
    edgeNav->addWidget(m_btnEdgeOpenLeft);
    m_btnEdgeOpenRight = new QPushButton(" Open Target", m_pageEdge);
    m_btnEdgeOpenRight->setIcon(IconManager::instance().getIcon("arrow-right", QColor(), 14));
    m_btnEdgeOpenRight->setObjectName("secondaryBtn");
    m_btnEdgeOpenRight->setStyleSheet("font-size: 10px; padding: 4px 6px;");
    edgeNav->addWidget(m_btnEdgeOpenRight);
    edgeNav->addStretch();
    eLay->addLayout(edgeNav);

    eLay->addSpacing(10);
    eLay->addWidget(m_btnSeverEdge);
    eLay->addStretch();
    m_detailsStack->addWidget(m_pageEdge);

    // Page 3: Empty / Data Hygiene Action Dashboard
    m_pageEmpty = new QWidget(m_detailsStack);
    QVBoxLayout* empLay = new QVBoxLayout(m_pageEmpty);
    empLay->setContentsMargins(0, 0, 0, 0);
    empLay->setSpacing(8);

    QLabel* empTitle = new QLabel("Data Hygiene & Quick Focus", m_pageEmpty);
    empTitle->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tok.accentSuccess.name()));
    m_lblHygieneMissingPhone = new QLabel("• 0 records missing phone", m_pageEmpty);
    m_lblHygieneMissingEmail = new QLabel("• 0 records missing email", m_pageEmpty);
    m_lblHygieneIsolated = new QLabel("• 0 isolated / unconnected members", m_pageEmpty);
    m_lblHygieneBridgeCount = new QLabel("• 0 bridge connectors identified", m_pageEmpty);

    m_btnSpotlightIncomplete = new QPushButton(" Spotlight Incomplete Records", m_pageEmpty);
    m_btnSpotlightIncomplete->setIcon(IconManager::instance().getPrimaryIcon("search", 14));
    m_btnSpotlightIncomplete->setObjectName("primaryBtn");
    m_btnSpotlightIncomplete->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnSpotlightIncomplete, &QPushButton::clicked, this, &GraphWidget::spotlightHygieneNodes);

    // Undo the most recent sever (this session only).
    m_btnUndoSever = new QPushButton(" Undo Last Sever", m_pageEmpty);
    m_btnUndoSever->setIcon(IconManager::instance().getIcon("rotate-ccw", QColor(), 14));
    m_btnUndoSever->setObjectName("secondaryBtn");
    m_btnUndoSever->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    m_btnUndoSever->setEnabled(false);
    connect(m_btnUndoSever, &QPushButton::clicked, this, &GraphWidget::onUndoSever);

    empLay->addWidget(empTitle);
    empLay->addWidget(m_lblHygieneMissingPhone);
    empLay->addWidget(m_lblHygieneMissingEmail);
    empLay->addWidget(m_lblHygieneIsolated);
    empLay->addWidget(m_lblHygieneBridgeCount);
    empLay->addWidget(m_btnSpotlightIncomplete);
    empLay->addWidget(m_btnUndoSever);
    empLay->addStretch();
    m_detailsStack->addWidget(m_pageEmpty);

    // Page 4: Batch Selection Dashboard (Rubber-Band Multi-Select)
    m_pageBatch = new QWidget(m_detailsStack);
    QVBoxLayout* batchLay = new QVBoxLayout(m_pageBatch);
    batchLay->setContentsMargins(0, 0, 0, 0);
    batchLay->setSpacing(8);

    QLabel* batchTitle = new QLabel("Batch Selection Hub", m_pageBatch);
    batchTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: palette(highlight);");
    m_lblBatchSummary = new QLabel("Selected 0 Entities", m_pageBatch);
    m_lblBatchSummary->setStyleSheet("font-size: 12px;");

    m_lstBatchMembers = new QListWidget(m_pageBatch);
    m_lstBatchMembers->setMaximumHeight(120);

    m_btnBatchEmails = new QPushButton(" Copy Selected Emails", m_pageBatch);
    m_btnBatchEmails->setIcon(IconManager::instance().getPrimaryIcon("mail", 14));
    m_btnBatchEmails->setObjectName("primaryBtn");
    m_btnBatchEmails->setStyleSheet("font-size: 11px; padding: 5px;");
    connect(m_btnBatchEmails, &QPushButton::clicked, this, &GraphWidget::onBatchCopyEmails);

    m_btnBatchPhones = new QPushButton(" Copy Selected Phones", m_pageBatch);
    m_btnBatchPhones->setIcon(IconManager::instance().getIcon("phone", QColor(), 14));
    m_btnBatchPhones->setObjectName("secondaryBtn");
    m_btnBatchPhones->setStyleSheet("font-size: 11px; padding: 5px;");
    connect(m_btnBatchPhones, &QPushButton::clicked, this, &GraphWidget::onBatchCopyPhones);

    m_btnBatchCSV = new QPushButton(" Export Selected to CSV", m_pageBatch);
    m_btnBatchCSV->setIcon(IconManager::instance().getIcon("file-text", QColor(), 14));
    m_btnBatchCSV->setObjectName("secondaryBtn");
    m_btnBatchCSV->setStyleSheet("font-size: 11px; padding: 5px;");
    connect(m_btnBatchCSV, &QPushButton::clicked, this, &GraphWidget::onBatchExportCSV);

    m_btnBatchAssign = new QPushButton(" Batch Assign to Group...", m_pageBatch);
    m_btnBatchAssign->setIcon(IconManager::instance().getIcon("users", QColor(), 14));
    m_btnBatchAssign->setObjectName("secondaryBtn");
    m_btnBatchAssign->setStyleSheet("font-size: 11px; padding: 5px;");
    connect(m_btnBatchAssign, &QPushButton::clicked, this, &GraphWidget::onBatchAssignGroup);

    batchLay->addWidget(batchTitle);
    batchLay->addWidget(m_lblBatchSummary);
    batchLay->addWidget(m_lstBatchMembers);
    batchLay->addWidget(m_btnBatchEmails);
    batchLay->addWidget(m_btnBatchPhones);
    batchLay->addWidget(m_btnBatchCSV);
    batchLay->addWidget(m_btnBatchAssign);
    batchLay->addStretch();
    m_detailsStack->addWidget(m_pageBatch);

    m_splitter->setSizes({700, 330});
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, true);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QList<int> sizes = m_splitter->sizes();
        bool isCollapsed = (sizes.size() >= 2 && sizes[1] <= 15);
        if (m_btnFloatingExpandDock) m_btnFloatingExpandDock->setVisible(isCollapsed);
        if (m_btnToggleInspectorDock) {
            m_btnToggleInspectorDock->setChecked(!isCollapsed);
            m_btnToggleInspectorDock->setText(isCollapsed ? " Inspector (Off)" : " Inspector (On)");
            m_btnToggleInspectorDock->setIcon(IconManager::instance().getIcon("sidebar", QColor(), 14));
        }
        if (m_btnCollapseDock) {
            m_btnCollapseDock->setText(isCollapsed ? " Expand" : " Collapse");
            m_btnCollapseDock->setIcon(IconManager::instance().getIcon(isCollapsed ? "chevron-left" : "chevron-right", QColor(), 14));
        }
        updateFloatingControlsPosition();
    });

    mainLayout->addWidget(m_splitter);

    // Collapsible Legend Overlay
    m_legendOverlay = new QWidget(m_canvasView);
    m_legendOverlay->setStyleSheet("QWidget#legendOverlay { background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; }");
    m_legendOverlay->setObjectName("legendOverlay");
    m_legendOverlay->hide();

    QVBoxLayout* legLayout = new QVBoxLayout(m_legendOverlay);
    legLayout->setContentsMargins(12, 10, 12, 10);
    legLayout->setSpacing(6);

    QLabel* legTitle = new QLabel("📖 Graph Legend & Symbols", m_legendOverlay);
    legTitle->setObjectName("legTitleLabel");
    legLayout->addWidget(legTitle);

    QLabel* legShapes = new QLabel("👤 Member (Circle)   🏛️ Group (Hexagon)\n🏡 Household (Diamond)   🔴 Isolated (Square)\n🌉 Relational Broker (Purple Aura)", m_legendOverlay);
    legShapes->setObjectName("legShapesLabel");
    legLayout->addWidget(legShapes);

    QLabel* legLines = new QLabel("── Gold Solid: Family Tie\n── Cyan Solid: Ministry Leader\n- - Gray Dash: General Member\n- - Red Dash: Burnout Risk Link\n- - Cyan Dash: Smart Bridge Suggestion", m_legendOverlay);
    legLines->setObjectName("legLinesLabel");
    legLayout->addWidget(legLines);

    selectNodeForDetails(nullptr);

    // Apply token colors now that every chrome widget exists.
    styleChromeWidgets(ThemeManager::instance().currentTokens());
}

void GraphWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateFloatingControlsPosition();
}

void GraphWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateDetailsPanelForEmpty();
    refreshMinistryBranches();
    updateFloatingControlsPosition();
    // Resume physics when the page becomes visible again (see hideEvent).
    if (m_physicsRunning && !m_timer->isActive() && !m_nodes.isEmpty()) {
        m_timer->start(33);
    }
}

void GraphWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Stop the 30 FPS force loop while the graph page is hidden — it would
    // otherwise burn CPU on every other page of the app.
    m_timer->stop();
    // Persist node positions after any dragging this session.
    saveNodePositions();
}

void GraphWidget::updateFloatingControlsPosition() {
    if (m_canvasView) {
        if (m_floatingZoomBar) {
            int x = m_canvasView->width() - m_floatingZoomBar->width() - 20;
            int y = m_canvasView->height() - m_floatingZoomBar->height() - 20;
            if (x > 0 && y > 0) {
                m_floatingZoomBar->move(x, y);
                m_floatingZoomBar->raise();
            }
        }
        if (m_floatingViewOptionsBar) {
            int x = 20;
            int y = m_canvasView->height() - m_floatingViewOptionsBar->height() - 20;
            if (x > 0 && y > 0) {
                m_floatingViewOptionsBar->move(x, y);
                m_floatingViewOptionsBar->raise();
            }
        }
        if (m_btnFloatingExpandDock && m_btnFloatingExpandDock->isVisible()) {
            int x = m_canvasView->width() - m_btnFloatingExpandDock->width() - 15;
            int y = 15;
            if (x > 0 && y > 0) {
                m_btnFloatingExpandDock->move(x, y);
                m_btnFloatingExpandDock->raise();
            }
        }
    }
}

void GraphWidget::onToggleSelectMode() {
    m_isSelectMode = m_btnToggleSelectMode->isChecked();
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    if (m_isSelectMode) {
        m_btnToggleSelectMode->setText(" Box Select");
        m_btnToggleSelectMode->setIcon(IconManager::instance().getIcon("crop", ThemeManager::textOn(tok.accentPrimary), 14));
        m_btnToggleSelectMode->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; padding: 5px 10px; font-size: 11px;")
            .arg(tok.accentPrimary.name(), ThemeManager::textOn(tok.accentPrimary).name()));
        m_canvasView->setSelectMode(true);
    } else {
        m_btnToggleSelectMode->setText(" Pan Canvas");
        m_btnToggleSelectMode->setIcon(IconManager::instance().getIcon("move", QColor(), 14));
        m_btnToggleSelectMode->setStyleSheet("font-size: 11px; padding: 5px 10px;");
        m_canvasView->setSelectMode(false);
    }
}

void GraphWidget::refreshMinistryBranches() {
    m_cmbMinistryBranch->blockSignals(true);
    m_cmbMinistryBranch->clear();
    m_cmbMinistryBranch->addItem("🌐 All Ministries & Groups", 0);

    QSqlQuery q;
    if (q.exec("SELECT id, name FROM groups WHERE (parent_id IS NULL OR parent_id = 0) AND (is_hidden = 0 OR is_hidden IS NULL) ORDER BY name")) {
        while (q.next()) {
            m_cmbMinistryBranch->addItem(QString("🏛️ %1").arg(q.value(1).toString()), q.value(0).toInt());
        }
    }
    m_cmbMinistryBranch->blockSignals(false);
}

void GraphWidget::onMinistryBranchChanged(int) {
    m_selectedMinistryParentId = m_cmbMinistryBranch->currentData().toInt();
    onFilterToggled();
    refreshHierarchyTree();
    if (m_dockTabs) m_dockTabs->setCurrentWidget(m_hierarchyTab);
}

void GraphWidget::onSceneSelectionChanged() {
    QList<QGraphicsItem*> sel = m_scene->selectedItems();
    QList<NodeItem*> selNodes;
    for (QGraphicsItem* it : sel) {
        NodeItem* n = dynamic_cast<NodeItem*>(it);
        if (n) selNodes.append(n);
    }

    if (selNodes.size() > 1) {
        clearGhostEdges();
        m_detailsStack->setCurrentWidget(m_pageBatch);
        m_lstBatchMembers->clear();
        int pCount = 0, gCount = 0;
        for (NodeItem* n : selNodes) {
            if (n->isPersonNode()) {
                ++pCount;
                QListWidgetItem* item = new QListWidgetItem(QString("👤 %1").arg(n->name()), m_lstBatchMembers);
                item->setData(Qt::UserRole, n->id());
            } else if (n->nodeType() == NodeItem::Group) {
                ++gCount;
                QListWidgetItem* item = new QListWidgetItem(QString("🏛️ %1").arg(n->name()), m_lstBatchMembers);
                item->setData(Qt::UserRole, n->id());
            }
        }
        m_lblBatchSummary->setText(QString("Selected %1 Entities:\n• %2 Members\n• %3 Groups").arg(selNodes.size()).arg(pCount).arg(gCount));
    } else if (selNodes.size() == 1) {
        selectNodeForDetails(selNodes.first());
    } else {
        updateDetailsPanelForEmpty();
    }
}

void GraphWidget::onBatchCopyEmails() {
    QStringList emails;
    for (int i = 0; i < m_lstBatchMembers->count(); ++i) {
        int id = m_lstBatchMembers->item(i)->data(Qt::UserRole).toInt();
        for (NodeItem* n : m_nodes) {
            if (n->id() == id && n->isPersonNode() && !n->email().isEmpty()) {
                emails.append(n->email());
                break;
            }
        }
    }
    if (emails.isEmpty()) {
        QMessageBox::information(this, "No Emails", "No email addresses found among the selected members.");
    } else {
        QGuiApplication::clipboard()->setText(emails.join(", "));
        QMessageBox::information(this, "Copied", QString("Copied %1 email addresses to clipboard.").arg(emails.size()));
    }
}

void GraphWidget::onBatchCopyPhones() {
    QStringList phones;
    for (int i = 0; i < m_lstBatchMembers->count(); ++i) {
        int id = m_lstBatchMembers->item(i)->data(Qt::UserRole).toInt();
        for (NodeItem* n : m_nodes) {
            if (n->id() == id && n->isPersonNode() && !n->phone().isEmpty()) {
                phones.append(n->phone());
                break;
            }
        }
    }
    if (phones.isEmpty()) {
        QMessageBox::information(this, "No Phones", "No phone numbers found among the selected members.");
    } else {
        QGuiApplication::clipboard()->setText(phones.join(", "));
        QMessageBox::information(this, "Copied", QString("Copied %1 phone numbers to clipboard.").arg(phones.size()));
    }
}

void GraphWidget::onBatchExportCSV() {
    auto warn = QMessageBox::warning(this, "Data Security Notice",
        "This export contains personal contact details and directory data.\n"
        "Ensure it is stored on an encrypted drive.",
        QMessageBox::Ok | QMessageBox::Cancel);
    if (warn != QMessageBox::Ok) return;
    QString fileName = QFileDialog::getSaveFileName(this, "Export Batch CSV", "selected_network.csv", "CSV Files (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Error", "Could not open file for writing.");
        return;
    }

    // RFC 4180: quote fields containing quotes, commas or line breaks and
    // double any embedded quote so the export stays parseable.
    auto csvField = [](const QString& value) {
        QString escaped = value;
        escaped.replace('"', "\"\"");
        if (escaped.contains(',') || escaped.contains('"') ||
            escaped.contains('\n') || escaped.contains('\r')) {
            return "\"" + escaped + "\"";
        }
        return escaped;
    };

    QTextStream out(&file);
    out << "ID,Name,Type,Phone,Email,CareFlag\n";
    for (int i = 0; i < m_lstBatchMembers->count(); ++i) {
        int id = m_lstBatchMembers->item(i)->data(Qt::UserRole).toInt();
        for (NodeItem* n : m_nodes) {
            if (n->id() == id) {
                QString nType = n->isPersonNode() ? "Person" : "Group";
                out << QString("%1,%2,%3,%4,%5,%6\n")
                           .arg(n->id())
                           .arg(csvField(n->name()))
                           .arg(nType)
                           .arg(csvField(n->phone()))
                           .arg(csvField(n->email()))
                           .arg(csvField(n->careFlag()));
                break;
            }
        }
    }
    file.close();
    DatabaseManager::logAudit("exports", 0, "batch_csv", QString::number(m_lstBatchMembers->count()), fileName, "graph");
    QMessageBox::information(this, "Export Complete", QString("Exported %1 records to CSV.").arg(m_lstBatchMembers->count()));
}

void GraphWidget::onBatchAssignGroup() {
    if (m_readOnly) return;
    QList<int> personIds;
    for (int i = 0; i < m_lstBatchMembers->count(); ++i) {
        int id = m_lstBatchMembers->item(i)->data(Qt::UserRole).toInt();
        for (NodeItem* n : m_nodes) {
            if (n->id() == id && n->isPersonNode()) {
                personIds.append(id);
                break;
            }
        }
    }
    if (personIds.isEmpty()) {
        QMessageBox::warning(this, "No Members", "Please select at least one person node to assign to a group.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Batch Assign to Group");
    dlg.resize(320, 160);
    QFormLayout* form = new QFormLayout(&dlg);

    QComboBox* grpCombo = new QComboBox(&dlg);
    QSqlQuery q("SELECT id, name FROM groups WHERE is_hidden = 0 OR is_hidden IS NULL ORDER BY name");
    while (q.next()) grpCombo->addItem(q.value(1).toString(), q.value(0).toInt());
    form->addRow("Select Group:", grpCombo);

    QComboBox* roleCombo = new QComboBox(&dlg);
    roleCombo->addItems({"Member", "Volunteer", "Leader", "Co-Leader"});
    form->addRow("Role:", roleCombo);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* btnOk = new QPushButton(QString("Assign %1 People").arg(personIds.size()), &dlg);
    btnOk->setObjectName("primaryBtn");
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btns->addStretch();
    btns->addWidget(btnCancel);
    btns->addWidget(btnOk);
    form->addRow(btns);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, [this, &dlg, personIds, grpCombo, roleCombo]() {
        int gid = grpCombo->currentData().toInt();
        QString role = roleCombo->currentText();
        QSqlDatabase db = DatabaseManager::instance().database();
        db.transaction();
        for (int pid : personIds) {
            QSqlQuery insertQ(db);
            insertQ.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, connection_source, joined_date) "
                              "VALUES ('Person', ?, 'Group', ?, ?, 'manual', CURRENT_DATE)");
            insertQ.addBindValue(pid);
            insertQ.addBindValue(gid);
            insertQ.addBindValue(role);
            insertQ.exec();
        }
        db.commit();
        dlg.accept();
        // Defer the reload: this lambda runs inside dlg.exec(), which can be
        // nested in a node's context-menu handler. loadGraph() clears the
        // scene — doing that now would delete the NodeItem whose handler is
        // still on the call stack (use-after-free).
        QTimer::singleShot(0, this, [this]() { loadGraph(); });
        QMessageBox::information(this, "Assigned", QString("Successfully assigned %1 members to %2.").arg(personIds.size()).arg(grpCombo->currentText()));
    });

    dlg.exec();
}

void GraphWidget::clearGhostEdges() {
    for (GhostEdgeItem* ge : m_ghostEdges) {
        m_scene->removeItem(ge);
        delete ge;
    }
    m_ghostEdges.clear();

    if (m_smartBridgeLayout) {
        clearLayoutRecursively(m_smartBridgeLayout);
    }
    if (m_smartBridgeSection) {
        m_smartBridgeSection->hide();
    }
}

void GraphWidget::computeSmartBridge(NodeItem* selectedNode) {
    clearGhostEdges();
    if (!selectedNode) return;

    m_smartBridgeSourceNode = selectedNode;

    struct Candidate {
        NodeItem* targetNode = nullptr;
        int personId = 0;
        int groupId = 0;
        int score = 0;
        QString reason;
        QString archetype;
        bool isSolid = false;
        QStringList warnings; // advisory flags a pastor can override
    };
    QList<Candidate> candidates;

    QSqlDatabase db = QSqlDatabase::database();
    AppSettings s;
    const int minScore = s.value("smartBridgeMinScore", 40).toInt();

    // Bulk-load the whole population once (3 queries total) instead of
    // ~3 queries per candidate.
    if (selectedNode->isPersonNode()) {
        int personId = selectedNode->id();
        CandidateProfile personProf = SynergyEngine::loadCandidate(db, personId);

        QSet<int> currentGroups;
        QSqlQuery qCur;
        qCur.prepare("SELECT target_id FROM entity_relationships WHERE source_type = 'Person' AND source_id = ? AND target_type = 'Group'");
        qCur.addBindValue(personId);
        if (qCur.exec()) {
            while (qCur.next()) currentGroups.insert(qCur.value(0).toInt());
        }

        QMap<int, GroupProfile> allGroups;
        SynergyEngine::loadAllGroups(db, personId, allGroups);

        for (NodeItem* gn : m_nodes) {
            if (gn->nodeType() != NodeItem::Group) continue;
            if (currentGroups.contains(gn->id())) continue;
            auto it = allGroups.constFind(gn->id());
            if (it == allGroups.constEnd()) continue;

            SynergyMatchResult res = SynergyEngine::evaluate(personProf, it.value());
            if (!res.hardBlocked && res.score >= minScore) {
                Candidate c;
                c.targetNode = gn;
                c.personId = personId;
                c.groupId = gn->id();
                c.score = res.score;
                c.reason = res.primaryReason;
                c.archetype = MinistryArchetypeRegistry::archetypeName(it.value().idealPrimary);
                c.isSolid = it.value().hasHouseholdFamilyTie;
                c.warnings = res.warnings;
                candidates.append(c);
            }
        }
    } else if (selectedNode->nodeType() == NodeItem::Group) {
        int groupId = selectedNode->id();
        GroupProfile groupProf = SynergyEngine::loadGroup(db, groupId);

        QSet<int> currentMembers;
        QSqlQuery qCur;
        qCur.prepare("SELECT source_id FROM entity_relationships WHERE target_type = 'Group' AND target_id = ? AND source_type = 'Person'");
        qCur.addBindValue(groupId);
        if (qCur.exec()) {
            while (qCur.next()) currentMembers.insert(qCur.value(0).toInt());
        }

        QMap<int, CandidateProfile> allPeople;
        SynergyEngine::loadAllCandidates(db, allPeople);

        // Batch family-tie detection: 2 queries instead of one per person.
        // 1) households that already have an active member in this group;
        // 2) every person's household.
        QSet<int> groupMemberHouseholds;
        QSqlQuery mhq(db);
        mhq.prepare("SELECT DISTINCT p.household_id FROM entity_relationships er "
                    "JOIN people p ON er.source_id = p.id "
                    "WHERE er.target_id = ? AND er.source_type = 'Person' AND er.target_type = 'Group' "
                    "AND p.household_id IS NOT NULL");
        mhq.addBindValue(groupId);
        if (mhq.exec()) {
            while (mhq.next()) groupMemberHouseholds.insert(mhq.value(0).toInt());
        }
        QHash<int, int> personHouseholds;
        QSqlQuery phq(db);
        if (phq.exec("SELECT id, household_id FROM people WHERE household_id IS NOT NULL")) {
            while (phq.next()) personHouseholds.insert(phq.value(0).toInt(), phq.value(1).toInt());
        }

        for (NodeItem* pn : m_nodes) {
            if (!pn->isPersonNode()) continue;
            if (currentMembers.contains(pn->id())) continue;
            auto it = allPeople.constFind(pn->id());
            if (it == allPeople.constEnd()) continue;

            GroupProfile grpWithCandidate = groupProf;
            if (groupMemberHouseholds.contains(personHouseholds.value(pn->id(), 0))) {
                grpWithCandidate.hasHouseholdFamilyTie = true;
            }

            SynergyMatchResult res = SynergyEngine::evaluate(it.value(), grpWithCandidate);
            if (!res.hardBlocked && res.score >= minScore) {
                Candidate c;
                c.targetNode = pn;
                c.personId = pn->id();
                c.groupId = groupId;
                c.score = res.score;
                c.reason = res.primaryReason;
                c.archetype = MinistryArchetypeRegistry::archetypeName(it.value().primaryArchetype);
                c.isSolid = grpWithCandidate.hasHouseholdFamilyTie;
                c.warnings = res.warnings;
                candidates.append(c);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    if (candidates.isEmpty()) return;

    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    m_smartBridgeSection->show();
    m_smartBridgeSection->setStyleSheet(
        "QWidget#smartBridgeSection { background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; padding: 6px; }"
    );

    QLabel* header = new QLabel("💡 Smart Bridge Recommendations", m_smartBridgeSection);
    header->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1; margin-bottom: 2px;").arg(tok.accentPrimary.name()));
    m_smartBridgeLayout->addWidget(header);

    const int shown = m_smartBridgeShowAll ? qMin(10, candidates.size()) : qMin(3, candidates.size());
    for (int i = 0; i < shown; ++i) {
        const Candidate& c = candidates[i];
        GhostEdgeItem* ge = new GhostEdgeItem(selectedNode, c.targetNode, c.reason, c.score, c.isSolid);
        m_scene->addItem(ge);
        m_ghostEdges.append(ge);

        QWidget* rowWidget = new QWidget(m_smartBridgeSection);
        rowWidget->setStyleSheet(
            QString("QWidget { background-color: %1; border: 1px solid %2; border-radius: 8px; }")
                .arg(tok.bgInput.name(), tok.borderColor.name())
        );
        QVBoxLayout* cardLay = new QVBoxLayout(rowWidget);
        cardLay->setContentsMargins(10, 8, 10, 8);
        cardLay->setSpacing(6);

        QHBoxLayout* topRow = new QHBoxLayout();
        QString prefix = c.targetNode->isPersonNode() ? "👤 " : "🏛️ ";
        QLabel* lblRec = new QLabel(prefix + c.targetNode->name(), rowWidget);
        lblRec->setStyleSheet("font-size: 13px; font-weight: bold; color: palette(text); background: transparent; border: none;");

        QLabel* lblPct = new QLabel(QString("%1% Match").arg(c.score), rowWidget);
        lblPct->setStyleSheet("background-color: palette(highlight); color: palette(highlighted-text); border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: bold;");

        topRow->addWidget(lblRec, 1);
        topRow->addWidget(lblPct);

        QHBoxLayout* bottomRow = new QHBoxLayout();
        QLabel* lblReason = new QLabel(QString("✨ %1").arg(c.reason), rowWidget);
        lblReason->setStyleSheet("font-size: 11px; color: palette(text); background: transparent; border: none;");
        lblReason->setWordWrap(true);

        QPushButton* btnConnect = new QPushButton(" Connect", rowWidget);
        btnConnect->setIcon(IconManager::instance().getPrimaryIcon("zap", 14));
        btnConnect->setCursor(Qt::PointingHandCursor);
        btnConnect->setEnabled(!m_readOnly);
        if (m_readOnly) {
            btnConnect->setToolTip("Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.");
        }
        btnConnect->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; font-weight: bold; font-size: 11px; border-radius: 5px; padding: 4px 10px; border: none; }"
            "QPushButton:hover { background-color: %3; }"
        ).arg(tok.accentPrimary.name(), ThemeManager::textOn(tok.accentPrimary).name(), tok.accentPrimary.darker(115).name()));
        int pid = c.personId;
        int gid = c.groupId;
        int matchScore = c.score;
        QStringList warnList = c.warnings;
        connect(btnConnect, &QPushButton::clicked, this, [this, pid, gid, matchScore, warnList]() {
            onConnectSmartBridge(pid, gid, "Member", matchScore, warnList);
        });

        bottomRow->addWidget(lblReason, 1);
        bottomRow->addWidget(btnConnect);

        cardLay->addLayout(topRow);
        cardLay->addLayout(bottomRow);

        // Advisory chip: pastor review recommended but Connect stays enabled.
        if (!c.warnings.isEmpty()) {
            QLabel* lblWarn = new QLabel("⚠️ " + c.warnings.join("  •  "), rowWidget);
            lblWarn->setStyleSheet(QString("font-size: 10px; font-weight: bold; color: %1; background: transparent; border: none;")
                                       .arg(tok.accentWarning.name()));
            lblWarn->setWordWrap(true);
            cardLay->addWidget(lblWarn);
        }

        m_smartBridgeLayout->addWidget(rowWidget);
    }

    // "Show More" expands the top-3 list to up to 10 recommendations.
    if (candidates.size() > shown) {
        QPushButton* btnMore = new QPushButton(QString("Show More (%1 more)…").arg(candidates.size() - shown), m_smartBridgeSection);
        btnMore->setObjectName("secondaryBtn");
        btnMore->setStyleSheet("font-size: 11px; padding: 4px 8px;");
        connect(btnMore, &QPushButton::clicked, this, [this]() {
            m_smartBridgeShowAll = true;
            if (m_smartBridgeSourceNode) computeSmartBridge(m_smartBridgeSourceNode);
        });
        m_smartBridgeLayout->addWidget(btnMore);
    }
}

void GraphWidget::onConnectSmartBridge(int personId, int groupId, const QString& role, int score, const QStringList& warnings) {
    if (m_readOnly) return;
    // Dedup guard: never create a second relationship for the same pair.
    QSqlQuery exists;
    exists.prepare("SELECT 1 FROM entity_relationships "
                   "WHERE source_type = 'Person' AND source_id = ? "
                   "AND target_type = 'Group' AND target_id = ? AND relationship_type = ?");
    exists.addBindValue(personId);
    exists.addBindValue(groupId);
    exists.addBindValue(role);
    if (exists.exec() && exists.next()) {
        QMessageBox::information(this, "Already Connected", "This person is already in the group.");
        return;
    }

    QSqlQuery q;
    q.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, connection_source, archetype_fit_score, joined_date) "
              "VALUES ('Person', ?, 'Group', ?, ?, 'smart_bridge', ?, CURRENT_DATE)");
    q.addBindValue(personId);
    q.addBindValue(groupId);
    q.addBindValue(role);
    q.addBindValue(score);
    q.exec();

    // Override audit: connecting despite advisory warnings is a deliberate
    // pastoral decision — log it so the trail shows who overrode what.
    if (!warnings.isEmpty()) {
        DatabaseManager::logAudit("entity_relationships", 0, "smart_bridge_override",
                                  warnings.join(" | "), QString("person=%1 group=%2 score=%3").arg(personId).arg(groupId).arg(score),
                                  "pastor");
    }

    loadGraph();
    for (NodeItem* n : m_nodes) {
        if (n->id() == personId && n->isPersonNode()) {
            selectNodeForDetails(n);
            break;
        }
    }
    QMessageBox::information(this, "Connected", QString("Member successfully connected via Smart Bridge (Archetype Fit: %1%).").arg(score));
}

void GraphWidget::onToggleCollapseDock() {
    QList<int> sizes = m_splitter->sizes();
    bool isCollapsed = (sizes.size() >= 2 && sizes[1] <= 15);
    if (!isCollapsed) {
        m_splitter->setSizes({width(), 0});
        if (m_btnCollapseDock) {
            m_btnCollapseDock->setText(" Expand");
            m_btnCollapseDock->setIcon(IconManager::instance().getIcon("chevron-left", QColor(), 14));
        }
        if (m_btnFloatingExpandDock) m_btnFloatingExpandDock->show();
        if (m_btnToggleInspectorDock) {
            m_btnToggleInspectorDock->setChecked(false);
            m_btnToggleInspectorDock->setText(" Inspector (Off)");
            m_btnToggleInspectorDock->setIcon(IconManager::instance().getIcon("sidebar", QColor(), 14));
        }
    } else {
        int targetDockWidth = 340;
        int leftWidth = qMax(300, width() - targetDockWidth);
        m_splitter->setSizes({leftWidth, targetDockWidth});
        if (m_btnCollapseDock) {
            m_btnCollapseDock->setText(" Collapse");
            m_btnCollapseDock->setIcon(IconManager::instance().getIcon("chevron-right", QColor(), 14));
        }
        if (m_btnFloatingExpandDock) m_btnFloatingExpandDock->hide();
        if (m_btnToggleInspectorDock) {
            m_btnToggleInspectorDock->setChecked(true);
            m_btnToggleInspectorDock->setText(" Inspector (On)");
            m_btnToggleInspectorDock->setIcon(IconManager::instance().getIcon("sidebar", QColor(), 14));
        }
    }
    updateFloatingControlsPosition();
}

void GraphWidget::toggleInspectorDock() {
    onToggleCollapseDock();
}

void GraphWidget::setConnectorSizeLevel(int level) {
    m_connectorSizeLevel = qBound(1, level, 3);
    if (m_btnConnThin) m_btnConnThin->setChecked(m_connectorSizeLevel == 1);
    if (m_btnConnNormal) m_btnConnNormal->setChecked(m_connectorSizeLevel == 2);
    if (m_btnConnBold) m_btnConnBold->setChecked(m_connectorSizeLevel == 3);

    ThemeTokens tok = ThemeManager::instance().currentTokens();
    auto styleBtn = [&](QPushButton* btn, bool active) {
        if (!btn) return;
        if (active) {
            btn->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; font-size: 10px; padding: 3px 6px; border-radius: 4px; border: none;")
                .arg(tok.accentPrimary.name(), ThemeManager::textOn(tok.accentPrimary).name()));
        } else {
            btn->setStyleSheet(QString("background-color: %1; color: %2; font-size: 10px; padding: 3px 6px; border-radius: 4px; border: 1px solid %3;").arg(tok.bgSurface.name(), tok.textSecondary.name(), tok.borderColor.name()));
        }
    };
    styleBtn(m_btnConnThin, m_connectorSizeLevel == 1);
    styleBtn(m_btnConnNormal, m_connectorSizeLevel == 2);
    styleBtn(m_btnConnBold, m_connectorSizeLevel == 3);

    if (m_scene) m_scene->update();
}

void GraphWidget::onConnectorSizeLevelChanged(int level) {
    setConnectorSizeLevel(level);
}

void GraphWidget::setShowConnectors(bool show) {
    m_showConnectors = show;
    if (m_btnToggleConnectors) {
        m_btnToggleConnectors->setChecked(m_showConnectors);
        ThemeTokens tok = ThemeManager::instance().currentTokens();
        if (m_showConnectors) {
            m_btnToggleConnectors->setText(" Links On");
            m_btnToggleConnectors->setIcon(IconManager::instance().getIcon("link", tok.textPrimary, 14));
            m_btnToggleConnectors->setStyleSheet(QString("background-color: %1; color: %2; font-size: 11px; font-weight: bold; padding: 3px 8px; border-radius: 5px; border: 1px solid %3;").arg(tok.bgSurface.name(), tok.textPrimary.name(), tok.borderColor.name()));
        } else {
            m_btnToggleConnectors->setText(" Links Off");
            m_btnToggleConnectors->setIcon(IconManager::instance().getIcon("link-2", ThemeManager::textOn(tok.accentWarning), 14));
            m_btnToggleConnectors->setStyleSheet(QString("background-color: %1; color: %2; font-size: 11px; font-weight: bold; padding: 3px 8px; border-radius: 5px; border: none;").arg(tok.accentWarning.name(), ThemeManager::textOn(tok.accentWarning).name()));
        }
    }
    if (m_scene) m_scene->update();
}

void GraphWidget::onToggleConnectors() {
    setShowConnectors(!m_showConnectors);
}

void GraphWidget::toggleConnectors() {
    onToggleConnectors();
}

void GraphWidget::onToggleDataHygiene() {
    m_filterHygieneActive = m_btnToggleHygiene->isChecked();
    styleFilterButton(m_btnToggleHygiene, m_filterHygieneActive, ThemeManager::instance().currentTokens().accentSuccess);
    m_scene->update();
}

void GraphWidget::onToggleLegend() {
    if (m_legendOverlay->isVisible()) {
        m_legendOverlay->hide();
    } else {
        m_legendOverlay->show();
        m_legendOverlay->raise();
        m_legendOverlay->move(m_canvasView->width() - m_legendOverlay->width() - 20, 10);
    }
}

void GraphWidget::onToggleLayoutMode() {
    const ThemeTokens tokLay = ThemeManager::instance().currentTokens();
    if (m_selectedMinistryParentId > 0) {
        if (m_layoutMode == ForceDirected) {
            m_layoutMode = TreeHierarchy;
            m_btnToggleLayout->setText(" Tree Org-Chart");
            m_btnToggleLayout->setIcon(IconManager::instance().getIcon("folder-tree", ThemeManager::textOn(tokLay.accentSuccess), 14));
            m_btnToggleLayout->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; font-size: 11px; padding: 5px 8px; border-radius: 6px;")
                .arg(tokLay.accentSuccess.name(), ThemeManager::textOn(tokLay.accentSuccess).name()));
            pausePhysics();
            applyTreeHierarchyLayout();
        } else if (m_layoutMode == TreeHierarchy) {
            m_layoutMode = Clustered;
            m_btnToggleLayout->setText(" Clustered");
            m_btnToggleLayout->setIcon(IconManager::instance().getIcon("grid", QColor(), 14));
            m_btnToggleLayout->setStyleSheet("font-size: 11px; padding: 5px 8px;");
            pausePhysics();
            applyClusteredLayout();
        } else {
            m_layoutMode = ForceDirected;
            m_btnToggleLayout->setText(" Force-Directed");
            m_btnToggleLayout->setIcon(IconManager::instance().getIcon("activity", QColor(), 14));
            m_btnToggleLayout->setStyleSheet("font-size: 11px; padding: 5px 8px;");
            resumePhysics();
        }
    } else {
        if (m_layoutMode == ForceDirected) {
            m_layoutMode = Clustered;
            m_btnToggleLayout->setText(" Clustered");
            m_btnToggleLayout->setIcon(IconManager::instance().getIcon("grid", QColor(), 14));
            pausePhysics();
            applyClusteredLayout();
        } else {
            m_layoutMode = ForceDirected;
            m_btnToggleLayout->setText(" Force-Directed");
            m_btnToggleLayout->setIcon(IconManager::instance().getIcon("activity", QColor(), 14));
            resumePhysics();
        }
    }
}

void GraphWidget::loadGraph() {
    m_selectedNodeForDetails = nullptr;
    m_selectedEdgeForDetails = nullptr;
    m_pathStartNode = nullptr;
    m_pathEndNode = nullptr;
    m_pathHighlightedNodes.clear();
    m_pathHighlightedEdges.clear();

    // Re-read advisory thresholds so Settings changes apply without restart.
    {
        AppSettings s;
        m_burnoutThreshold = qBound(2, s.value("burnoutThreshold", 3).toInt(), 10);
        m_familyDominancePercent = qBound(10, s.value("familyDominancePercent", 40).toInt(), 90);
    }

    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();
    m_ghostEdges.clear();
    m_isolatedPersonIds.clear();
    m_bridgeNodeIds.clear();
    m_bridgeGroupsMap.clear();
    m_personStatusMap.clear();
    m_groupFamilyConcentrationMap.clear();
    m_burnoutDetailsMap.clear();

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);

    updateIsolationQuery();

    // 1. Groups placed centrally in an inner ring
    static const QList<QColor> kCategoryPalette = {
        QColor("#89b4fa"), QColor("#a6e3a1"), QColor("#f9e2af"), QColor("#f38ba8"),
        QColor("#cba6f7"), QColor("#94e2d5"), QColor("#fab387"), QColor("#74c7ec"),
    };
    int groupIdx = 0;
    if (q.exec("SELECT id, name, category FROM groups WHERE is_hidden = 0 OR is_hidden IS NULL")) {
        while (q.next()) {
            int gid = q.value("id").toInt();
            QString name = q.value("name").toString();
            QString category = q.value("category").toString();

            NodeItem* node = new NodeItem(gid, name, NodeItem::Group);
            node->setHealthStatus(NodeItem::FullyStaffed);
            if (!category.isEmpty()) {
                const uint h = static_cast<uint>(qHash(category));
                node->setCategoryColor(kCategoryPalette[h % kCategoryPalette.size()]);
            }

            double angle = (2.0 * M_PI / qMax(1, 8)) * groupIdx;
            double dist = 220.0;
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));
            groupIdx++;

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    }

    // 2. Households placed in a secondary ring
    int hhIdx = 0;
    if (q.exec("SELECT id, family_name FROM households")) {
        while (q.next()) {
            int hid = q.value("id").toInt();
            QString name = q.value("family_name").toString();

            NodeItem* node = new NodeItem(hid, name, NodeItem::Household);
            double angle = (2.0 * M_PI / qMax(1, 6)) * hhIdx + (M_PI / 6.0);
            double dist = 380.0;
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));
            hhIdx++;

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    }

    // 3. People placed in an organized distribution
    int personIdx = 0;
    if (q.exec("SELECT id, first_name || ' ' || last_name AS full_name, membership_status, care_flag, admin_notes, phone_number AS phone, email FROM people")) {
        while (q.next()) {
            int pid = q.value("id").toInt();
            QString name = q.value("full_name").toString();
            QString status = q.value("membership_status").toString();
            QString cf = q.value("care_flag").toString();
            QString notes = q.value("admin_notes").toString();
            QString phone = q.value("phone").toString();
            QString email = q.value("email").toString();

            NodeItem::Type ntype = NodeItem::Person;
            if (m_isolatedPersonIds.contains(pid)) {
                ntype = NodeItem::IsolatedPerson;
            } else if (status == "Visitor") {
                ntype = NodeItem::Visitor;
            }

            NodeItem* node = new NodeItem(pid, name, ntype);
            node->setCareFlag(cf);
            node->setAdminNotes(notes);
            node->setContactInfo(phone, email);
            m_personStatusMap[pid] = status;

            double angle = (2.0 * M_PI / qMax(1, 35)) * personIdx;
            double dist = 180.0 + (personIdx % 5) * 55.0;
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));
            personIdx++;

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    }

    // 1. Group Count, Roles, and Relational Broker / Bridge Detection
    QMap<int, int> personActiveGroupCount;
    QMap<int, QStringList> personActiveRoles;
    QMap<int, QSet<int>> personConnectedGroupIds;
    QMap<int, QStringList> personConnectedGroupNames;
    QMap<int, int> groupMemberCount; // total people per group (for the family scan below)

    if (q.exec("SELECT er.source_id, er.relationship_type, er.target_id, g.name "
               "FROM entity_relationships er JOIN groups g ON er.target_id = g.id "
               "WHERE er.source_type = 'Person' AND er.target_type = 'Group'")) {
        while (q.next()) {
            int pid = q.value(0).toInt();
            QString role = q.value(1).toString();
            int gid = q.value(2).toInt();
            QString gName = q.value(3).toString();

            personActiveGroupCount[pid]++;
            personActiveRoles[pid].append(role.isEmpty() ? "Member" : role);
            personConnectedGroupIds[pid].insert(gid);
            groupMemberCount[gid]++;
            if (!personConnectedGroupNames[pid].contains(gName)) {
                personConnectedGroupNames[pid].append(gName);
            }
        }
    }

    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode()) {
            int count = personActiveGroupCount.value(n->id(), 0);
            if (count >= m_burnoutThreshold) {
                bool isCritical = (count >= m_burnoutThreshold + 1);
                n->setBurnoutDetails(count, isCritical, personActiveRoles.value(n->id()).join(", "));
            }
            if (personConnectedGroupIds.value(n->id()).size() >= 2) {
                m_bridgeNodeIds.insert(n->id());
                m_bridgeGroupsMap[n->id()] = personConnectedGroupNames.value(n->id());
                n->setBridgeBroker(true, personConnectedGroupNames.value(n->id()));
            }
        }
    }

    // 2. Family Dynasty & Concentration Scan per Group
    if (q.exec("SELECT g.id, g.name, p.household_id, h.family_name, COUNT(*) "
               "FROM entity_relationships er "
               "JOIN people p ON er.source_id = p.id "
               "JOIN households h ON p.household_id = h.id "
               "JOIN groups g ON er.target_id = g.id "
               "WHERE er.source_type = 'Person' AND er.target_type = 'Group' AND p.household_id IS NOT NULL "
               "GROUP BY g.id, p.household_id HAVING COUNT(*) >= 2")) {
        while (q.next()) {
            int gid = q.value(0).toInt();
            QString gName = q.value(1).toString();
            QString hName = q.value(3).toString();
            int familyCount = q.value(4).toInt();

            int totalGroupMembers = groupMemberCount.value(gid, 0);
            if (totalGroupMembers <= 0) totalGroupMembers = familyCount;

            int pct = (familyCount * 100) / qMax(1, totalGroupMembers);
            if (familyCount >= 3 || pct >= m_familyDominancePercent) {
                QString warningMsg = QString("⚠️ High Family Concentration: %1 Family holds %2% of group roles (%3 members).")
                                         .arg(hName).arg(pct).arg(familyCount);
                m_groupFamilyConcentrationMap[gid] = warningMsg;

                for (NodeItem* gn : m_nodes) {
                    if (gn->nodeType() == NodeItem::Group && gn->id() == gid) {
                        gn->setFamilyConcentration(warningMsg);
                        break;
                    }
                }
            }
        }
    }

    // 3. Edges creation — O(1) node lookup via type-indexed hashes (was an
    //    O(edges x nodes) scan on every load).
    QHash<int, NodeItem*> personNodesById;
    QHash<int, NodeItem*> groupNodesById;
    QHash<int, NodeItem*> householdNodesById;
    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode()) personNodesById.insert(n->id(), n);
        else if (n->nodeType() == NodeItem::Group) groupNodesById.insert(n->id(), n);
        else if (n->nodeType() == NodeItem::Household) householdNodesById.insert(n->id(), n);
    }

    auto resolveNode = [&](const QString& type, int id) -> NodeItem* {
        if (type == "Person") return personNodesById.value(id, nullptr);
        if (type == "Group") return groupNodesById.value(id, nullptr);
        if (type == "Household") return householdNodesById.value(id, nullptr);
        return nullptr;
    };

    if (q.exec("SELECT id, source_type, source_id, target_type, target_id, relationship_type FROM entity_relationships")) {
        while (q.next()) {
            int relId = q.value("id").toInt();
            QString sType = q.value("source_type").toString();
            int sId = q.value("source_id").toInt();
            QString tType = q.value("target_type").toString();
            int tId = q.value("target_id").toInt();
            QString rType = q.value("relationship_type").toString();

            NodeItem* sNode = resolveNode(sType, sId);
            NodeItem* tNode = resolveNode(tType, tId);

            if (sNode && tNode) {
                bool isBurnout = (sNode->burnoutGroupCount() >= m_burnoutThreshold || tNode->burnoutGroupCount() >= m_burnoutThreshold);
                EdgeItem* edge = new EdgeItem(sNode, tNode, rType, relId, isBurnout);
                m_scene->addItem(edge);
                m_edges.append(edge);

                sNode->setConnectionCount(sNode->connectionCount() + 1);
                tNode->setConnectionCount(tNode->connectionCount() + 1);
            }
        }
    }

    if (q.exec("SELECT id, parent_id FROM groups WHERE parent_id IS NOT NULL AND parent_id > 0")) {
        while (q.next()) {
            int childId = q.value(0).toInt();
            int parentId = q.value(1).toInt();
            NodeItem* parentNode = groupNodesById.value(parentId, nullptr);
            NodeItem* childNode = groupNodesById.value(childId, nullptr);
            if (parentNode && childNode) {
                EdgeItem* edge = new EdgeItem(parentNode, childNode, "Parent of", 0, false);
                m_scene->addItem(edge);
                m_edges.append(edge);
            }
        }
    }

    loadNodePositions();
    onFilterToggled();
    refreshHierarchyTree();
    updateDetailsPanelForEmpty();
    m_loadedGeneration = DatabaseManager::dataGeneration();
    m_sinceLoad.restart();
    resumePhysics(); // fresh graph starts moving until it settles (auto-freeze)
}

void GraphWidget::loadGraphIfDirty() {
    const quint64 gen = DatabaseManager::dataGeneration();
    const bool clean = !m_nodes.isEmpty()
                       && m_loadedGeneration == gen
                       && m_sinceLoad.isValid()
                       && m_sinceLoad.elapsed() < 5 * 60 * 1000; // staleness fallback
    if (clean) return;
    loadGraph();
}

bool GraphWidget::physicsRunning() const {
    return m_physicsRunning && m_timer && m_timer->isActive();
}

void GraphWidget::updateIsolationQuery() {
    m_isolatedPersonIds.clear();
    QSqlQuery q;
    if (q.exec("SELECT id FROM people WHERE id NOT IN (SELECT DISTINCT source_id FROM entity_relationships WHERE source_type = 'Person') AND id NOT IN (SELECT DISTINCT target_id FROM entity_relationships WHERE target_type = 'Person')")) {
        while (q.next()) {
            m_isolatedPersonIds.insert(q.value(0).toInt());
        }
    }

    if (m_btnIsolatedBanner) {
        int count = m_isolatedPersonIds.size();
        m_btnIsolatedBanner->setText(QString(" %1 Unconnected Members").arg(count));
        const ThemeTokens t = ThemeManager::instance().currentTokens();
        m_btnIsolatedBanner->setIcon(IconManager::instance().getIcon("alert-triangle", t.accentDanger, 14));
        const QColor onDanger = ThemeManager::textOn(t.accentDanger);
        m_btnIsolatedBanner->setStyleSheet(
            QString("QPushButton { background-color: rgba(%1, %2, %3, 0.4); color: %4; border: 1px solid %5; font-size: 11px; font-weight: bold; border-radius: 6px; padding: 5px 10px; }")
                .arg(t.accentDanger.red()).arg(t.accentDanger.green()).arg(t.accentDanger.blue())
                .arg(onDanger.name(), t.accentDanger.name()));
        m_btnIsolatedBanner->setIcon(IconManager::instance().getIcon("alert-triangle", onDanger, 14));
    }
}

void GraphWidget::updateDetailsPanelForEmpty() {
    clearGhostEdges();
    if (m_detailsStack) {
        m_detailsStack->setCurrentWidget(m_pageEmpty);
    }
    int missingPhone = 0;
    int missingEmail = 0;
    QSqlQuery q("SELECT SUM(CASE WHEN phone_number IS NULL OR phone_number = '' THEN 1 ELSE 0 END), "
                "       SUM(CASE WHEN email IS NULL OR email = '' THEN 1 ELSE 0 END) FROM people");
    if (q.next()) {
        missingPhone = q.value(0).toInt();
        missingEmail = q.value(1).toInt();
    }
    if (m_lblHygieneMissingPhone) m_lblHygieneMissingPhone->setText(QString("• %1 records missing phone number").arg(missingPhone));
    if (m_lblHygieneMissingEmail) m_lblHygieneMissingEmail->setText(QString("• %1 records missing email address").arg(missingEmail));
    if (m_lblHygieneIsolated) m_lblHygieneIsolated->setText(QString("• %1 unconnected / isolated members").arg(m_isolatedPersonIds.size()));
    if (m_lblHygieneBridgeCount) m_lblHygieneBridgeCount->setText(QString("• %1 relational bridge connectors").arg(m_bridgeNodeIds.size()));
}

void GraphWidget::selectNodeForDetails(NodeItem* node) {
    if (m_selectedEdgeForDetails) {
        m_selectedEdgeForDetails->setSelectedState(false);
        m_selectedEdgeForDetails = nullptr;
    }
    m_selectedNodeForDetails = node;
    if (!node) {
        updateDetailsPanelForEmpty();
        return;
    }

    if (node->isPersonNode()) {
        m_detailsStack->setCurrentWidget(m_pagePerson);
        m_lblPersonName->setText(node->name());
        m_lblPersonStatus->setText(QString("Status: %1 (ID: %2)").arg(node->careFlag().isEmpty() ? "Normal" : node->careFlag()).arg(node->id()));
        m_txtAdminNotes->setPlainText(node->adminNotes());

        // Upgraded context: age · membership · household / attendance pulse /
        // recent pastoral care.
        if (m_lblPersonStats) {
            QSqlQuery pq;
            pq.prepare("SELECT p.date_of_birth, p.membership_status, h.family_name FROM people p "
                       "LEFT JOIN households h ON p.household_id = h.id WHERE p.id = ?");
            pq.addBindValue(node->id());
            QStringList stats;
            if (pq.exec() && pq.next()) {
                const QDate dob = QDate::fromString(pq.value(0).toString(), Qt::ISODate);
                if (dob.isValid()) {
                    const QDate today = QDate::currentDate();
                    int age = today.year() - dob.year();
                    if (today.month() < dob.month() || (today.month() == dob.month() && today.day() < dob.day())) age--;
                    stats << QString("Age %1").arg(age);
                }
                const QString status = pq.value(1).toString();
                if (!status.isEmpty()) stats << status;
                const QString family = pq.value(2).toString();
                if (!family.isEmpty()) stats << "🏡 " + family;
            }
            m_lblPersonStats->setText(stats.isEmpty() ? "" : "• " + stats.join(" · "));
        }
        if (m_lblPersonAttendance) {
            QSqlQuery aq;
            aq.prepare("SELECT COUNT(*) FROM attendance WHERE person_id = ? AND attended = 1 "
                       "AND timestamp >= datetime('now', '-90 days')");
            aq.addBindValue(node->id());
            if (aq.exec() && aq.next()) {
                m_lblPersonAttendance->setText(QString("📌 Attended %1 service(s) in the last 90 days").arg(aq.value(0).toInt()));
            }
        }
        if (m_lblPersonCareRecent) {
            QSqlQuery cq;
            cq.prepare("SELECT activity_type, activity_date FROM pastoral_activities "
                       "WHERE person_id = ? ORDER BY activity_date DESC, id DESC LIMIT 3");
            cq.addBindValue(node->id());
            QStringList lines;
            if (cq.exec()) {
                while (cq.next()) lines << QString("%1 (%2)").arg(cq.value(0).toString(), cq.value(1).toString());
            }
            m_lblPersonCareRecent->setText(lines.isEmpty() ? "" : "🕊️ Recent care:\n• " + lines.join("\n• "));
        }

        if (node->isBridgeBroker()) {
            m_lblPersonBrokerBadge->show();
            m_lblPersonBrokerBadge->setText(QString("🌉 Relational Broker (%1 Ministries)").arg(node->bridgedGroups().size()));
        } else {
            m_lblPersonBrokerBadge->hide();
        }

        // Programmatic combo updates must not fire the write-through handler
        // (it would persist the previously selected person's flag).
        const QSignalBlocker careBlocker(m_cmbCareFlag);
        // Trim any legacy flag item added for a previously selected person.
        while (m_cmbCareFlag->count() > 6) m_cmbCareFlag->removeItem(m_cmbCareFlag->count() - 1);
        int idx = m_cmbCareFlag->findText(node->careFlag(), Qt::MatchFixedString);
        if (idx >= 0) {
            m_cmbCareFlag->setCurrentIndex(idx);
        } else if (!node->careFlag().isEmpty()) {
            // Preserve an off-list legacy flag instead of silently showing the
            // previously selected person's flag. Changing it writes the new
            // value, which is what the admin asked for.
            m_cmbCareFlag->addItem(node->careFlag());
            m_cmbCareFlag->setCurrentIndex(m_cmbCareFlag->count() - 1);
        } else {
            m_cmbCareFlag->setCurrentIndex(0);
        }

        QStringList groups;
        QSqlQuery q;
        q.prepare("SELECT g.name, er.relationship_type FROM entity_relationships er JOIN groups g ON er.target_id = g.id WHERE er.source_id = ? AND er.source_type = 'Person'");
        q.addBindValue(node->id());
        if (q.exec()) {
            while (q.next()) {
                QString gName = q.value(0).toString();
                QString role = q.value(1).toString();
                groups.append(QString("%1 (%2)").arg(gName, role.isEmpty() ? "Member" : role));
            }
        }
        m_lblPersonGroups->setText(groups.isEmpty() ? "Connected Groups: None (Unconnected)" : QString("Connected Groups (%1):\n• %2").arg(groups.size()).arg(groups.join("\n• ")));

        computeSmartBridge(node);
    } else if (node->nodeType() == NodeItem::Group) {
        clearGhostEdges();
        m_detailsStack->setCurrentWidget(m_pageGroup);
        m_lblGroupName->setText(node->name());

        // Show the v2 dynamic health score when present (migration 19).
        QString healthTxt = QString("Health: %1").arg(node->healthStatus() == NodeItem::FullyStaffed ? "Fully Staffed" : "Understaffed");
        QSqlQuery hq;
        hq.prepare("SELECT dynamic_health_score FROM groups WHERE id = ?");
        hq.addBindValue(node->id());
        if (hq.exec() && hq.next()) {
            const int score = hq.value(0).toInt();
            if (score > 0) healthTxt += QString(" · %1/100").arg(score);
        }
        m_lblGroupCategory->setText(healthTxt);

        // Upgraded context: recruitment badge, capacity/meeting meta, and the
        // archetype composition of the active roster.
        if (m_lblGroupRecruit && m_lblGroupMeta && m_lblGroupComposition) {
            QSqlQuery gq;
            gq.prepare("SELECT category, max_capacity, meeting_day, meeting_time, location, recruitment_status "
                       "FROM groups WHERE id = ?");
            gq.addBindValue(node->id());
            QString category, recruit;
            int capacity = 0;
            QStringList meta;
            if (gq.exec() && gq.next()) {
                category = gq.value(0).toString();
                capacity = gq.value(1).toInt();
                const QString day = gq.value(2).toString();
                const QString time = gq.value(3).toString();
                const QString loc = gq.value(4).toString();
                recruit = gq.value(5).toString().trimmed();
                if (recruit.isEmpty()) recruit = "Open";
                if (!category.isEmpty()) meta << category;
                if (capacity > 0) meta << QString("Capacity %1").arg(capacity);
                if (!day.isEmpty() || !time.isEmpty()) meta << QString("%1 %2").arg(day, time).trimmed();
                if (!loc.isEmpty()) meta << "📍 " + loc;
            }
            m_lblGroupMeta->setText(meta.isEmpty() ? "" : "• " + meta.join(" · "));

            const ThemeTokens tok = ThemeManager::instance().currentTokens();
            QString badgeTxt, badgeCol, badgeBg;
            if (recruit == "Elected") { badgeTxt = "🗳️ Elected Governance Body"; badgeCol = tok.accentWarning.name(); badgeBg = tok.accentWarning.name(); }
            else if (recruit == "Seasonal") { badgeTxt = "📅 Seasonal Cohort"; badgeCol = tok.accentWarning.name(); badgeBg = tok.accentWarning.name(); }
            else if (recruit == "Closed") { badgeTxt = "🔒 Closed (Invite Only)"; badgeCol = tok.textSecondary.name(); badgeBg = tok.textSecondary.name(); }
            else { badgeTxt = "🟢 Open for Recruitment"; badgeCol = tok.accentSuccess.name(); badgeBg = tok.accentSuccess.name(); }
            m_lblGroupRecruit->setText(badgeTxt);
            m_lblGroupRecruit->setStyleSheet(QString(
                "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: bold; }")
                .arg(badgeBg, tok.isDark ? "#11111b" : "#ffffff", badgeCol));
            m_lblGroupRecruit->show();

            // Archetype composition of the active roster (migration 19 data).
            QMap<ChurchArchetype, int> counts;
            QSqlQuery cq;
            cq.prepare("SELECT p.ministry_personality FROM entity_relationships er "
                       "JOIN people p ON er.source_id = p.id "
                       "WHERE er.target_id = ? AND er.target_type = 'Group' AND er.source_type = 'Person'");
            cq.addBindValue(node->id());
            if (cq.exec()) {
                while (cq.next()) {
                    counts[MinistryArchetypeRegistry::stringToArchetype(cq.value(0).toString())]++;
                }
            }
            QStringList parts;
            for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
                if (it.key() == ChurchArchetype::None) continue;
                parts << QString("%1 ×%2").arg(MinistryArchetypeRegistry::archetypeName(it.key())).arg(it.value());
            }
            m_lblGroupComposition->setText(parts.isEmpty() ? "" : "🧬 Composition: " + parts.join(" · "));
        }

        if (!node->familyConcentration().isEmpty()) {
            m_lblGroupFamilyWarning->show();
            m_lblGroupFamilyWarning->setText(node->familyConcentration());
        } else {
            m_lblGroupFamilyWarning->hide();
        }

        m_lstRoster->clear();
        QSqlQuery q;
        q.prepare("SELECT p.id, p.first_name || ' ' || p.last_name, er.relationship_type FROM entity_relationships er JOIN people p ON er.source_id = p.id WHERE er.target_id = ? AND er.source_type = 'Person'");
        q.addBindValue(node->id());
        int mCount = 0;
        if (q.exec()) {
            while (q.next()) {
                int pid = q.value(0).toInt();
                QString name = q.value(1).toString();
                QString role = q.value(2).toString();
                QListWidgetItem* item = new QListWidgetItem(QString("%1 — %2").arg(name, role.isEmpty() ? "Member" : role), m_lstRoster);
                item->setData(Qt::UserRole, pid);
                ++mCount;
            }
        }
        m_lblGroupMemberCount->setText(QString("Members (%1):").arg(mCount));

        // Prep tasks linked to this group (planning module, real dates).
        if (m_lblGroupTasks) {
            QSqlQuery tq;
            tq.prepare("SELECT title, start_date, end_date, COALESCE(status, 'In-Progress') "
                       "FROM ministry_tasks WHERE team_id = ? ORDER BY start_date");
            tq.addBindValue(node->id());
            QStringList lines;
            if (tq.exec()) {
                const QDate today = QDate::currentDate();
                while (tq.next()) {
                    const QString title = tq.value(0).toString();
                    const QDate start = QDate::fromString(tq.value(1).toString(), Qt::ISODate);
                    const QDate end = QDate::fromString(tq.value(2).toString(), Qt::ISODate);
                    const QString status = tq.value(3).toString();
                    QString line = title;
                    if (start.isValid()) line += QString(" · %1").arg(start.toString("MMM d"));
                    if (end.isValid()) line += QString(" – %1").arg(end.toString("MMM d"));
                    if (status == "Completed") line += " ✅";
                    else if (end.isValid() && end < today) line += " ⚠️ slipping";
                    else if (status == "Planned") line += " 📌";
                    lines << line;
                }
            }
            if (lines.isEmpty()) {
                m_lblGroupTasks->setText("📋 Prep tasks: none");
            } else {
                m_lblGroupTasks->setText("📋 Prep tasks:\n• " + lines.join("\n• "));
            }
        }

        computeSmartBridge(node);
    }
}

void GraphWidget::selectEdgeForDetails(EdgeItem* edge) {
    clearGhostEdges();
    if (m_selectedEdgeForDetails) {
        m_selectedEdgeForDetails->setSelectedState(false);
    }
    m_selectedEdgeForDetails = edge;
    m_selectedNodeForDetails = nullptr;

    if (!edge) {
        updateDetailsPanelForEmpty();
        return;
    }

    edge->setSelectedState(true);
    m_detailsStack->setCurrentWidget(m_pageEdge);

    m_lblEdgeBetween->setText(QString("%1 ↔ %2").arg(edge->sourceNode()->name(), edge->targetNode()->name()));
    // Programmatic selection must not fire the write-through role handler.
    const QSignalBlocker edgeRoleBlocker(m_cmbEdgeRole);
    int idx = m_cmbEdgeRole->findText(edge->relationshipType());
    if (idx >= 0) m_cmbEdgeRole->setCurrentIndex(idx);

    // Provenance: where the connection came from + when it was made.
    if (m_lblEdgeSource) {
        QStringList src;
        if (edge->relationshipId() > 0) {
            QSqlQuery q;
            q.prepare("SELECT connection_source, joined_date FROM entity_relationships WHERE id = ?");
            q.addBindValue(edge->relationshipId());
            if (q.exec() && q.next()) {
                const QString cs = q.value(0).toString();
                if (!cs.isEmpty()) src << QString("Source: %1").arg(cs);
                const QString jd = q.value(1).toString();
                if (!jd.isEmpty()) src << QString("Joined: %1").arg(jd);
            }
        }
        m_lblEdgeSource->setText(src.isEmpty() ? "" : "• " + src.join(" · "));
    }

    // Jump to either endpoint from the relationship card.
    if (m_btnEdgeOpenLeft && m_btnEdgeOpenRight) {
        NodeItem* left = edge->sourceNode();
        NodeItem* right = edge->targetNode();
        m_btnEdgeOpenLeft->setText(QString("◀ %1").arg(left->name()));
        m_btnEdgeOpenRight->setText(QString("%1 ▶").arg(right->name()));
        m_btnEdgeOpenLeft->disconnect();
        m_btnEdgeOpenRight->disconnect();
        connect(m_btnEdgeOpenLeft, &QPushButton::clicked, this, [this, left]() { focusAndSelectNode(left); });
        connect(m_btnEdgeOpenRight, &QPushButton::clicked, this, [this, right]() { focusAndSelectNode(right); });
    }
}

void GraphWidget::onRosterItemClicked(QListWidgetItem* item) {
    if (!item) return;
    int pid = item->data(Qt::UserRole).toInt();
    for (NodeItem* n : m_nodes) {
        if (n->id() == pid && n->isPersonNode()) {
            focusAndSelectNode(n);
            break;
        }
    }
}

void GraphWidget::updateEdgesForNode(NodeItem* node) {
    if (!node) return;
    for (EdgeItem* e : m_edges) {
        if (e && (e->sourceNode() == node || e->targetNode() == node)) {
            e->adjust();
        }
    }
    for (GhostEdgeItem* ge : m_ghostEdges) {
        if (ge && (ge->sourceNode() == node || ge->targetNode() == node)) {
            ge->adjust();
        }
    }
}

void GraphWidget::onSeverSelectedEdge() {
    if (m_readOnly) return;
    if (!m_selectedEdgeForDetails) return;

    // Guard across the confirmation dialog's nested event loop: anything
    // (physics timer, smart-bridge rebuild, a dialog-triggered reload) could
    // clear the scene while the message box is open. Items are not QObjects,
    // so record the edge's slot in m_edges and re-validate after the dialog.
    EdgeItem* edge = m_selectedEdgeForDetails;
    const int edgeSlot = m_edges.indexOf(edge);
    // Capture everything the post-dialog code needs BEFORE the nested event
    // loop — the edge's endpoint nodes can also be deleted while the dialog
    // is open, so no item dereferences are allowed after it.
    const QString relType = edge->relationshipType();
    const int relId = edge->relationshipId();
    auto typeStr = [](NodeItem* n) {
        if (n->isPersonNode()) return QStringLiteral("Person");
        if (n->nodeType() == NodeItem::Group) return QStringLiteral("Group");
        return QStringLiteral("Household");
    };
    const QString srcType = typeStr(edge->sourceNode());
    const QString tgtType = typeStr(edge->targetNode());
    const int srcId = edge->sourceNode()->id();
    const int tgtId = edge->targetNode()->id();
    QString srcName = edge->sourceNode()->name();
    QString tgtName = edge->targetNode()->name();

    auto reply = QMessageBox::question(this, "Sever Connection",
                                       QString("Are you sure you want to remove the connection between %1 and %2?").arg(srcName, tgtName),
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    // Edge destroyed while the dialog was open (slot gone or reused).
    if (edgeSlot < 0 || m_edges.at(edgeSlot) != edge) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    bool removed = false;

    if (relType == "Parent of") {
        // Hierarchy links live in groups.parent_id, not entity_relationships.
        q.prepare("UPDATE groups SET parent_id = NULL WHERE id = ?");
        q.addBindValue(tgtId); // target = child
        removed = q.exec();
    } else if (relId > 0) {
        q.prepare("DELETE FROM entity_relationships WHERE id = ?");
        q.addBindValue(relId);
        removed = q.exec();
    } else {
        // Type-scoped fallback so numeric id collisions can never delete an
        // unrelated relationship (e.g. person 3 <-> group 5 vs group 3 <-> ...).
        q.prepare("DELETE FROM entity_relationships "
                  "WHERE source_type = ? AND source_id = ? "
                  "AND target_type = ? AND target_id = ? AND relationship_type = ?");
        q.addBindValue(srcType);
        q.addBindValue(srcId);
        q.addBindValue(tgtType);
        q.addBindValue(tgtId);
        q.addBindValue(relType);
        removed = q.exec();
    }

    if (!removed) {
        QMessageBox::critical(this, "Database Error", "Failed to sever connection: " + q.lastError().text());
        return;
    }

    // Remember this sever so the admin can undo it this session.
    m_hasUndoSever = true;
    m_undoRelId = relId;
    m_undoRelType = relType;
    m_undoSrcType = srcType;
    m_undoTgtType = tgtType;
    m_undoSrcId = srcId;
    m_undoTgtId = tgtId;
    if (m_btnUndoSever) m_btnUndoSever->setEnabled(true);

    m_scene->removeItem(edge);
    m_edges.removeOne(edge);
    delete edge;
    m_selectedEdgeForDetails = nullptr;

    updateDetailsPanelForEmpty();
    m_scene->update();
}

void GraphWidget::onGroupSuggest() {
    if (m_readOnly) return;
    if (!m_selectedNodeForDetails || m_selectedNodeForDetails->nodeType() != NodeItem::Group) return;
    if (!m_nodes.contains(m_selectedNodeForDetails)) return;
    const int gid = m_selectedNodeForDetails->id();

    MatchDialog dlg(gid, this);
    connect(&dlg, &MatchDialog::openProfile, this, [this](int personId) {
        emit navigateToProfile("Person", personId);
    });
    dlg.exec();
    if (!dlg.addedPersonIds().isEmpty()) {
        loadGraph(); // roster + graph reflect the new members
    }
}

void GraphWidget::onLogContact() {
    if (m_readOnly) return;
    if (!m_selectedNodeForDetails || !m_selectedNodeForDetails->isPersonNode()) return;
    if (!m_nodes.contains(m_selectedNodeForDetails)) return;
    const int pid = m_selectedNodeForDetails->id();

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, follow_up_done) "
              "VALUES (?, 'Contact Attempt', date('now', 'localtime'), 'Logged from Connection Graph', 0)");
    q.addBindValue(pid);
    if (!q.exec()) {
        QMessageBox::critical(this, "Database Error", "Failed to log contact: " + q.lastError().text());
        return;
    }
    // Ensure the person is flagged for follow-up. An existing deliberate flag
    // (At-Risk, Grief Support, Leader, ...) is kept: report what was persisted
    // instead of pretending the flag changed.
    QSqlQuery flag(db);
    flag.prepare("UPDATE people SET care_flag = 'Needs Contact' "
                 "WHERE id = ? AND (care_flag IS NULL OR care_flag = '' OR care_flag = 'Normal')");
    flag.addBindValue(pid);
    const bool flagged = flag.exec() && flag.numRowsAffected() > 0;

    QString persistedFlag;
    if (flagged) {
        persistedFlag = QStringLiteral("Needs Contact");
    } else {
        QSqlQuery read(db);
        read.prepare("SELECT COALESCE(care_flag, '') FROM people WHERE id = ?");
        read.addBindValue(pid);
        if (read.exec() && read.next()) persistedFlag = read.value(0).toString();
    }
    if (!persistedFlag.isEmpty()) m_selectedNodeForDetails->setCareFlag(persistedFlag);
    if (flagged) {
        m_lblLogContactStatus->setText(
            QStringLiteral("✓ Contact attempt logged — flagged 'Needs Contact' for follow-up."));
    } else if (!persistedFlag.isEmpty()) {
        m_lblLogContactStatus->setText(
            QStringLiteral("✓ Contact attempt logged — existing care flag '%1' kept.").arg(persistedFlag));
    } else {
        m_lblLogContactStatus->setText(
            QStringLiteral("✓ Contact attempt logged — care flag unchanged."));
    }
    m_lblLogContactStatus->show();
    m_scene->update();
}

void GraphWidget::onUndoSever() {
    if (m_readOnly) return;
    if (!m_hasUndoSever) return;
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    if (m_undoRelType == "Parent of") {
        q.prepare("UPDATE groups SET parent_id = ? WHERE id = ?");
        q.addBindValue(m_undoSrcId); // source = parent
        q.addBindValue(m_undoTgtId); // target = child
    } else {
        q.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                  "VALUES (?, ?, ?, ?, ?)");
        q.addBindValue(m_undoSrcType);
        q.addBindValue(m_undoSrcId);
        q.addBindValue(m_undoTgtType);
        q.addBindValue(m_undoTgtId);
        q.addBindValue(m_undoRelType);
    }
    if (q.exec()) {
        m_hasUndoSever = false;
        if (m_btnUndoSever) m_btnUndoSever->setEnabled(false);
        loadGraph();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to undo: " + q.lastError().text());
    }
}

void GraphWidget::onToggleStatusCluster() {
    m_statusClustering = !m_statusClustering;
    if (m_statusClustering) {
        QColor cMember("#a6e3a1"), cAttender("#89b4fa"), cVisitor("#74c7ec"), cOther("#f9e2af");
        for (NodeItem* n : m_nodes) {
            if (!n->isPersonNode()) continue;
            const QString status = m_personStatusMap.value(n->id(), QString());
            if (status == "Member") n->setStatusColor(cMember);
            else if (status == "Regular Attender" || status == "Attender") n->setStatusColor(cAttender);
            else if (status == "Visitor") n->setStatusColor(cVisitor);
            else n->setStatusColor(cOther);
        }
    } else {
        for (NodeItem* n : m_nodes) n->clearStatusColor();
    }
    m_scene->update();
}

void GraphWidget::onEdgeRoleChanged(int) {
    if (m_readOnly) return;
    if (!m_selectedEdgeForDetails) return;
    QString newRole = m_cmbEdgeRole->currentText();
    m_selectedEdgeForDetails->setRelationshipType(newRole);

    int relId = m_selectedEdgeForDetails->relationshipId();
    QSqlQuery q;
    if (relId > 0) {
        q.prepare("UPDATE entity_relationships SET relationship_type = ? WHERE id = ?");
        q.addBindValue(newRole);
        q.addBindValue(relId);
        q.exec();
    }
}

void GraphWidget::onCopyContactInfo(NodeItem* node, const QString& type) {
    if (!node) return;
    if (type == "phone") {
        if (node->phone().isEmpty()) {
            QMessageBox::information(this, "No Phone", "This member has no phone number on record.");
        } else {
            QGuiApplication::clipboard()->setText(node->phone());
            QMessageBox::information(this, "Copied", QString("Phone number copied: %1").arg(node->phone()));
        }
    } else if (type == "email") {
        if (node->email().isEmpty()) {
            QMessageBox::information(this, "No Email", "This member has no email address on record.");
        } else {
            QGuiApplication::clipboard()->setText(node->email());
            QMessageBox::information(this, "Copied", QString("Email copied: %1").arg(node->email()));
        }
    }
}

void GraphWidget::onQuickAssignGroup(NodeItem* node) {
    if (m_readOnly) return;
    if (!node) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Assign to Group");
    dlg.resize(320, 160);
    QFormLayout* form = new QFormLayout(&dlg);

    QComboBox* grpCombo = new QComboBox(&dlg);
    QSqlQuery q("SELECT id, name FROM groups WHERE is_hidden = 0 OR is_hidden IS NULL ORDER BY name");
    while (q.next()) {
        grpCombo->addItem(q.value(1).toString(), q.value(0).toInt());
    }
    form->addRow("Select Group:", grpCombo);

    QComboBox* roleCombo = new QComboBox(&dlg);
    roleCombo->addItems({"Member", "Leader", "Volunteer", "Co-Leader"});
    form->addRow("Role:", roleCombo);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* btnOk = new QPushButton("Assign", &dlg);
    btnOk->setObjectName("primaryBtn");
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btns->addStretch();
    btns->addWidget(btnCancel);
    btns->addWidget(btnOk);
    form->addRow(btns);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, [this, &dlg, node, grpCombo, roleCombo]() {
        int gid = grpCombo->currentData().toInt();
        QString role = roleCombo->currentText();
        QSqlQuery insertQ;
        insertQ.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, connection_source, joined_date) "
                              "VALUES ('Person', ?, 'Group', ?, ?, 'manual', CURRENT_DATE)");
        insertQ.addBindValue(node->id());
        insertQ.addBindValue(gid);
        insertQ.addBindValue(role);
        insertQ.exec();
        dlg.accept();
        // Deferred for the same reason as the batch-assign reload above: the
        // dialog may be nested inside a node's context-menu handler, and
        // loadGraph()'s scene->clear() would free that node mid-handler.
        QTimer::singleShot(0, this, [this]() { loadGraph(); });
    });

    dlg.exec();
}

void GraphWidget::showNodeContextMenu(NodeItem* node, const QPoint& globalPos) {
    if (!node) return;
    QMenu m(this);

    // The menu runs a nested event loop; the node can be deleted underneath
    // us (e.g. a dialog-triggered loadGraph() clears the scene). Items are
    // not QObjects so QPointer can't guard them — record the node's slot in
    // m_nodes and re-validate after exec().
    const int nodeSlot = m_nodes.indexOf(node);

    const bool isReadOnly = m_readOnly;
    const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

    if (node->isPersonNode()) {
        QAction* aProfile = m.addAction(IconManager::instance().getIcon("user", QColor(), 14), "View Full Profile");
        QAction* aPhone = m.addAction(IconManager::instance().getIcon("phone", QColor(), 14), "Copy Phone Number");
        QAction* aEmail = m.addAction(IconManager::instance().getIcon("mail", QColor(), 14), "Copy Email Address");
        m.addSeparator();
        QAction* aPin = m.addAction(IconManager::instance().getIcon(node->isPinned() ? "unlock" : "pin", QColor(), 14),
                                    node->isPinned() ? "Unpin / Unfreeze Node" : "Pin / Freeze Node in Place");
        m.addSeparator();
        QAction* aAssign = m.addAction(IconManager::instance().getIcon("user-plus", QColor(), 14), "Assign to Group...");
        QMenu* mCare = m.addMenu(IconManager::instance().getIcon("flag", QColor(), 14), "Set Care Flag");
        QAction* cNone = mCare->addAction("None / Normal");
        // Same canonical flags as the People list / person inspector combo.
        mCare->addAction("Needs Contact");
        mCare->addAction("At-Risk");
        mCare->addAction("Follow-up");
        mCare->addAction("Grief Support");
        mCare->addAction("Leader");

        if (isReadOnly) {
            aAssign->setEnabled(false); aAssign->setToolTip(roTooltip);
            mCare->setEnabled(false); mCare->setToolTip(roTooltip);
        }

        QAction* sel = m.exec(globalPos);
        // Re-validate: bail if the scene was rebuilt while the menu was open.
        if (nodeSlot < 0 || m_nodes.at(nodeSlot) != node) return;
        if (sel == aProfile) {
            emit navigateToProfile("Person", node->id());
        } else if (sel == aPhone) {
            onCopyContactInfo(node, "phone");
        } else if (sel == aEmail) {
            onCopyContactInfo(node, "email");
        } else if (sel == aPin) {
            node->setPinned(!node->isPinned());
            m_scene->update();
        } else if (sel == aAssign && !isReadOnly) {
            onQuickAssignGroup(node);
        } else if (sel && mCare->actions().contains(sel) && !isReadOnly) {
            QString newFlag = (sel == cNone) ? "Normal" : sel->text();
            node->setCareFlag(newFlag);
            QSqlQuery uq;
            uq.prepare("UPDATE people SET care_flag = ? WHERE id = ?");
            uq.addBindValue(newFlag);
            uq.addBindValue(node->id());
            uq.exec();
            selectNodeForDetails(node);
            m_scene->update();
        }
    } else if (node->nodeType() == NodeItem::Group) {
        QAction* aHighlight = m.addAction(IconManager::instance().getIcon("list", QColor(), 14), "Highlight Roster on Canvas");
        QAction* aPin = m.addAction(IconManager::instance().getIcon(node->isPinned() ? "unlock" : "pin", QColor(), 14),
                                    node->isPinned() ? "Unpin / Unfreeze Node" : "Pin / Freeze Node in Place");
        QAction* aAdd = m.addAction(IconManager::instance().getIcon("user-plus", QColor(), 14), "Add Member to Group...");
        QAction* aEmails = m.addAction(IconManager::instance().getIcon("mail", QColor(), 14), "Copy All Member Emails");
        QAction* aMinistries = m.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "View in Ministries Tab");

        if (isReadOnly) {
            aAdd->setEnabled(false); aAdd->setToolTip(roTooltip);
        }

        QAction* sel = m.exec(globalPos);
        // Re-validate: bail if the scene was rebuilt while the menu was open.
        if (nodeSlot < 0 || m_nodes.at(nodeSlot) != node) return;
        if (sel == aHighlight) {
            highlightConnections(node);
        } else if (sel == aPin) {
            node->setPinned(!node->isPinned());
            m_scene->update();
        } else if (sel == aAdd && !isReadOnly) {
            onQuickAssignGroup(node);
        } else if (sel == aEmails) {
            m_btnGroupCopyEmails->click();
        } else if (sel == aMinistries) {
            emit navigateToProfile("Group", node->id());
        }
    }
}

void GraphWidget::handleNodeClick(NodeItem* node) {
    if (m_pathfindActive) {
        if (!m_pathStartNode) {
            m_pathStartNode = node;
            m_lblPathStatus->setText(QString("Start: %1. Pick Destination.").arg(node->name()));
            highlightConnections(node);
        } else if (!m_pathEndNode && node != m_pathStartNode) {
            m_pathEndNode = node;
            runBFSPathfind();
        }
    } else {
        highlightConnections(node);
        selectNodeForDetails(node);
    }
}

void GraphWidget::highlightConnections(NodeItem* node) {
    resetHighlight();
    if (!node || !node->isVisible()) return;

    node->setHovered(true);
    for (EdgeItem* edge : m_edges) {
        if (!edge->isVisible() || !edge->sourceNode()->isVisible() || !edge->targetNode()->isVisible()) continue;
        if (edge->sourceNode() == node || edge->targetNode() == node) {
            edge->sourceNode()->setHovered(true);
            edge->targetNode()->setHovered(true);
        }
    }
}

void GraphWidget::resetHighlight() {
    for (NodeItem* n : m_nodes) n->setHovered(false);
    m_scene->update();
}

void GraphWidget::focusAndSelectNode(NodeItem* node) {
    if (!node) return;
    m_canvasView->centerOn(node);
    highlightConnections(node);
    selectNodeForDetails(node);
}

void GraphWidget::spotlightIsolatedNodes() {
    resetHighlight();
    for (NodeItem* n : m_nodes) {
        if (m_isolatedPersonIds.contains(n->id()) && n->isPersonNode()) {
            n->setHovered(true);
        }
    }
}

void GraphWidget::spotlightBurnoutNodes() {
    resetHighlight();
    for (NodeItem* n : m_nodes) {
        if (n->burnoutGroupCount() >= m_burnoutThreshold) {
            n->setHovered(true);
        }
    }
}

void GraphWidget::spotlightHygieneNodes() {
    resetHighlight();
    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode() && (n->isHygieneCritical() || n->isHygieneWarning())) {
            n->setHovered(true);
        }
    }
}

void GraphWidget::spotlightBridgeNodes() {
    resetHighlight();
    for (NodeItem* n : m_nodes) {
        if (n->isBridgeBroker()) {
            n->setHovered(true);
        }
    }
}

void GraphWidget::spotlightFamilyDynasties() {
    resetHighlight();
    for (NodeItem* n : m_nodes) {
        if (!n->familyConcentration().isEmpty() || n->nodeType() == NodeItem::Household) {
            n->setHovered(true);
        }
    }
}

void GraphWidget::onSpotlightIsolatedClicked() {
    m_btnToggleIsolation->setChecked(true);
    m_filterIsolationActive = true;
    styleFilterButton(m_btnToggleIsolation, true, ThemeManager::instance().currentTokens().accentDanger);
    onFilterToggled();
    spotlightIsolatedNodes();
}

void GraphWidget::refreshFilterVisibility() {
    QSet<int> branchGroupIds;
    QSet<int> branchPersonIds;
    if (m_selectedMinistryParentId > 0) {
        branchGroupIds.insert(m_selectedMinistryParentId);
        // Follow the whole subtree, not just direct children (deep nesting is
        // supported via the cycle-safe groups.parent_id hierarchy).
        QSqlQuery q;
        q.prepare(
            "WITH RECURSIVE descendants(id) AS ("
            "  SELECT id FROM groups WHERE parent_id = ? "
            "  UNION ALL "
            "  SELECT g.id FROM groups g JOIN descendants d ON g.parent_id = d.id"
            ") SELECT id FROM descendants");
        q.addBindValue(m_selectedMinistryParentId);
        if (q.exec()) {
            while (q.next()) branchGroupIds.insert(q.value(0).toInt());
        }
        for (EdgeItem* e : m_edges) {
            if (e->sourceNode()->nodeType() == NodeItem::Group && branchGroupIds.contains(e->sourceNode()->id())) {
                if (e->targetNode()->isPersonNode()) branchPersonIds.insert(e->targetNode()->id());
            } else if (e->targetNode()->nodeType() == NodeItem::Group && branchGroupIds.contains(e->targetNode()->id())) {
                if (e->sourceNode()->isPersonNode()) branchPersonIds.insert(e->sourceNode()->id());
            }
        }
    }

    for (NodeItem* n : m_nodes) {
        bool visible = true;
        if (m_selectedMinistryParentId > 0) {
            if (n->nodeType() == NodeItem::Group && !branchGroupIds.contains(n->id())) visible = false;
            else if (n->isPersonNode() && !branchPersonIds.contains(n->id())) visible = false;
            else if (n->nodeType() == NodeItem::Household) visible = false;
        }
        if (n->isPersonNode() && !m_filterPeopleActive) visible = false;
        if (n->nodeType() == NodeItem::Household && !m_filterHouseholdsActive) visible = false;
        if (n->nodeType() == NodeItem::Group && !m_filterGroupsActive) visible = false;
        // Burnout spotlight: hide non-flagged PEOPLE but keep groups and
        // households as context so the at-risk members' connections stay
        // visible instead of floating alone.
        if (m_filterBurnoutActive && n->isPersonNode() && n->burnoutGroupCount() < m_burnoutThreshold) visible = false;
        if (m_filterIsolationActive && n->isPersonNode() && !m_isolatedPersonIds.contains(n->id())) visible = false;
        n->setVisible(visible);
    }
    for (EdgeItem* e : m_edges) {
        bool v = e->sourceNode()->isVisible() && e->targetNode()->isVisible();
        if (m_selectedMinistryParentId > 0) {
            if (e->sourceNode()->nodeType() == NodeItem::Group && !branchGroupIds.contains(e->sourceNode()->id())) v = false;
            if (e->targetNode()->nodeType() == NodeItem::Group && !branchGroupIds.contains(e->targetNode()->id())) v = false;
            if (e->sourceNode()->isPersonNode() && !branchPersonIds.contains(e->sourceNode()->id())) v = false;
            if (e->targetNode()->isPersonNode() && !branchPersonIds.contains(e->targetNode()->id())) v = false;
        }
        e->setVisible(v);
    }

}

void GraphWidget::onFilterToggled() {
    refreshFilterVisibility();

    // Persist the working view so it survives restarts.
    AppSettings s;
    s.setValue("graphFilters/people", m_filterPeopleActive);
    s.setValue("graphFilters/households", m_filterHouseholdsActive);
    s.setValue("graphFilters/groups", m_filterGroupsActive);
    s.setValue("graphFilters/burnout", m_filterBurnoutActive);
    s.setValue("graphFilters/isolation", m_filterIsolationActive);
    s.setValue("graphFilters/hygiene", m_filterHygieneActive);
    s.setValue("graphFilters/bridge", m_filterBridgeActive);
    s.setValue("graphFilters/family", m_filterFamilyActive);
    s.setValue("graphFilters/layout", (int)m_layoutMode);

    resumePhysics(); // visibility changed — let the remaining nodes re-settle
}

void GraphWidget::showAllNodeTypesForTesting() {
    m_filterPeopleActive = true;
    m_filterHouseholdsActive = true;
    m_filterGroupsActive = true;
    m_filterBurnoutActive = false;
    m_filterIsolationActive = false;
    if (m_btnTogglePeople) m_btnTogglePeople->setChecked(true);
    if (m_btnToggleHouseholds) m_btnToggleHouseholds->setChecked(true);
    if (m_btnToggleGroups) m_btnToggleGroups->setChecked(true);
    if (m_btnToggleBurnout) m_btnToggleBurnout->setChecked(false);
    if (m_btnToggleIsolation) m_btnToggleIsolation->setChecked(false);
    refreshFilterVisibility();
}

void GraphWidget::onGraphSearchChanged(const QString& text) {
    resetHighlight();
    if (text.trimmed().isEmpty()) return;

    NodeItem* first = nullptr;
    for (NodeItem* n : m_nodes) {
        if (n->name().contains(text, Qt::CaseInsensitive)) {
            n->setHovered(true);
            if (!first) first = n;
        }
    }
    // Center on the FIRST match so the view doesn't jump per keystroke.
    if (first) m_canvasView->centerOn(first);
}

void GraphWidget::onPathfindToggle() {
    m_pathfindActive = !m_pathfindActive;
    if (m_pathfindActive) {
        const ThemeTokens tokPath = ThemeManager::instance().currentTokens();
        m_btnPathfind->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; padding: 5px 10px; font-size: 11px;")
            .arg(tokPath.accentPrimary.name(), ThemeManager::textOn(tokPath.accentPrimary).name()));
        m_lblPathStatus = new QLabel("Click first person/node", this);
    } else {
        m_btnPathfind->setStyleSheet("font-size: 11px; padding: 5px 10px;");
        clearPathfind();
    }
}

void GraphWidget::clearPathfind() {
    m_pathStartNode = nullptr;
    m_pathEndNode = nullptr;
    m_pathHighlightedNodes.clear();
    m_pathHighlightedEdges.clear();
    resetHighlight();
}

void GraphWidget::onClearPathfind() {
    clearPathfind();
}

void GraphWidget::runBFSPathfind() {
    if (!m_pathStartNode || !m_pathEndNode) return;
    QQueue<NodeItem*> queue;
    QMap<NodeItem*, NodeItem*> parentMap;
    QSet<NodeItem*> visited;

    queue.enqueue(m_pathStartNode);
    visited.insert(m_pathStartNode);
    bool found = false;

    while (!queue.isEmpty()) {
        NodeItem* curr = queue.dequeue();
        if (curr == m_pathEndNode) {
            found = true;
            break;
        }
        for (EdgeItem* e : m_edges) {
            NodeItem* neighbor = nullptr;
            if (e->sourceNode() == curr) neighbor = e->targetNode();
            else if (e->targetNode() == curr) neighbor = e->sourceNode();

            if (neighbor && !visited.contains(neighbor) && neighbor->isVisible()) {
                visited.insert(neighbor);
                parentMap[neighbor] = curr;
                queue.enqueue(neighbor);
            }
        }
    }

    resetHighlight();
    if (found) {
        // Count the chain length (degrees of separation) as we walk back.
        int degrees = 0;
        NodeItem* step = m_pathEndNode;
        while (step) {
            step->setHovered(true);
            step = parentMap.value(step, nullptr);
            if (step) ++degrees;
        }
        if (m_lblPathStatus) {
            m_lblPathStatus->setText(QString("✅ Path found — %1 connection(s) between %2 and %3.")
                                         .arg(degrees)
                                         .arg(m_pathStartNode->name(), m_pathEndNode->name()));
        }
    } else if (m_lblPathStatus) {
        m_lblPathStatus->setText("No path between the selected nodes (with current filters).");
    }
}

void GraphWidget::onSaveNotes() {
    if (m_readOnly) return;
    if (!m_selectedNodeForDetails) return;
    if (!m_nodes.contains(m_selectedNodeForDetails)) return;
    QString notes = m_txtAdminNotes->toPlainText().trimmed();
    m_selectedNodeForDetails->setAdminNotes(notes);

    QSqlQuery q;
    if (m_selectedNodeForDetails->isPersonNode()) {
        q.prepare("UPDATE people SET admin_notes = ? WHERE id = ?");
    } else if (m_selectedNodeForDetails->nodeType() == NodeItem::Group) {
        q.prepare("UPDATE groups SET notes = ? WHERE id = ?");
    } else {
        return; // households carry no free-text notes here
    }
    q.addBindValue(notes);
    q.addBindValue(m_selectedNodeForDetails->id());
    if (!q.exec()) {
        QMessageBox::critical(this, "Database Error", "Failed to save notes: " + q.lastError().text());
        return;
    }
    // Silent success — the dock already shows the updated text.
    m_scene->update();
}

void GraphWidget::onCareFlagChanged(int) {
    if (m_readOnly) return;
    if (!m_selectedNodeForDetails || !m_selectedNodeForDetails->isPersonNode()) return;
    QString newFlag = m_cmbCareFlag->currentText();
    m_selectedNodeForDetails->setCareFlag(newFlag);

    QSqlQuery q;
    q.prepare("UPDATE people SET care_flag = ? WHERE id = ?");
    q.addBindValue(newFlag);
    q.addBindValue(m_selectedNodeForDetails->id());
    q.exec();

    m_scene->update();
}

void GraphWidget::onExportPNG() {
    QString fileName = QFileDialog::getSaveFileName(this, "Export Graph Image", "connections_graph.png", "PNG Images (*.png)");
    if (fileName.isEmpty()) return;

    QImage img(m_scene->sceneRect().size().toSize(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&p);
    img.save(fileName);
    QMessageBox::information(this, "Export Complete", "Graph image exported successfully.");
}

void GraphWidget::onExportJSON() {
    // Dump the current graph (nodes + edges) as JSON for external analysis.
    QJsonArray nodesArr;
    for (NodeItem* n : m_nodes) {
        QJsonObject o;
        o["id"] = n->id();
        o["name"] = n->name();
        o["type"] = n->isPersonNode() ? "Person"
                     : (n->nodeType() == NodeItem::Group ? "Group" : "Household");
        o["x"] = n->pos().x();
        o["y"] = n->pos().y();
        nodesArr.append(o);
    }
    QJsonArray edgesArr;
    for (EdgeItem* e : m_edges) {
        QJsonObject o;
        o["source"] = e->sourceNode()->id();
        o["target"] = e->targetNode()->id();
        o["type"] = e->relationshipType();
        edgesArr.append(o);
    }
    QJsonObject root;
    root["nodes"] = nodesArr;
    root["edges"] = edgesArr;

    QString fileName = QFileDialog::getSaveFileName(this, "Export Graph JSON", "connections_graph.json", "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
        QMessageBox::information(this, "Export Complete",
                                 QString("Exported %1 nodes and %2 edges.").arg(nodesArr.size()).arg(edgesArr.size()));
    } else {
        QMessageBox::critical(this, "File Error", "Could not write JSON: " + file.errorString());
    }
}

void GraphWidget::onRunAudit() {
    QDialog dlg(this);
    dlg.setWindowTitle("📊 Congregation Connection & Health Audit");
    dlg.resize(520, 420);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    int totalPeople = 0;
    int totalGroups = 0;
    int isolatedCount = m_isolatedPersonIds.size();
    int burnoutCount = 0;
    int edgeCount = m_edges.size();
    int bridgeCount = m_bridgeNodeIds.size();
    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode()) ++totalPeople;
        if (n->nodeType() == NodeItem::Group) ++totalGroups;
        if (n->burnoutGroupCount() >= m_burnoutThreshold) ++burnoutCount;
    }

    // Hygiene gaps (phone/email) for the snapshot.
    int missingPhone = 0, missingEmail = 0;
    QSqlQuery hq;
    if (hq.exec("SELECT SUM(CASE WHEN phone_number IS NULL OR phone_number = '' THEN 1 ELSE 0 END), "
                "       SUM(CASE WHEN email IS NULL OR email = '' THEN 1 ELSE 0 END) FROM people")) {
        if (hq.next()) {
            missingPhone = hq.value(0).toInt();
            missingEmail = hq.value(1).toInt();
        }
    }

    // Save a connection-health snapshot for trend comparison.
    const double density = (totalPeople > 1) ? (2.0 * edgeCount) / (totalPeople * (totalPeople - 1)) : 0.0;
    int avgConnections = 0;
    int peopleWithEdges = 0;
    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode() && n->connectionCount() > 0) {
            avgConnections += n->connectionCount();
            ++peopleWithEdges;
        }
    }
    avgConnections = peopleWithEdges > 0 ? (avgConnections / peopleWithEdges) : 0;

    QSqlQuery snap;
    snap.prepare("INSERT INTO graph_snapshots (node_count, edge_count, density, avg_connections, "
                 "bridge_count, isolated_count, missing_phone, missing_email) "
                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    snap.addBindValue(totalPeople);
    snap.addBindValue(edgeCount);
    snap.addBindValue(density);
    snap.addBindValue(avgConnections);
    snap.addBindValue(bridgeCount);
    snap.addBindValue(isolatedCount);
    snap.addBindValue(missingPhone);
    snap.addBindValue(missingEmail);
    snap.exec();

    // Trend vs the previous snapshot (isolation is the headline metric).
    QString trend;
    QSqlQuery prev;
    if (prev.exec("SELECT isolated_count, node_count FROM graph_snapshots "
                  "ORDER BY id DESC LIMIT 1 OFFSET 1")) {
        if (prev.next()) {
            const int prevIsolated = prev.value(0).toInt();
            const int delta = isolatedCount - prevIsolated;
            trend = QString("\n\n📈 vs previous audit: isolation %1 (%2)")
                        .arg(delta <= 0 ? "improved" : "worsened")
                        .arg(delta > 0 ? "+" + QString::number(delta) : QString::number(delta));
        }
    }

    QLabel* title = new QLabel("Congregation Relational Diagnostics", &dlg);
    title->setStyleSheet("font-size: 14px; font-weight: bold;");
    lay->addWidget(title);

    QLabel* stats = new QLabel(QString("• Total Tracked Members: %1\n"
                                      "• Active Ministry Groups: %2\n"
                                      "• Relationships (edges): %3\n"
                                      "• Unconnected / Isolated Members: %4 (%5%)\n"
                                      "• Relational Bridge Connectors: %6\n"
                                      "• Volunteers in Burnout Danger (>=%7 groups): %8\n"
                                      "• Missing phone / email: %9 / %10%11")
                                   .arg(totalPeople)
                                   .arg(totalGroups)
                                   .arg(edgeCount)
                                   .arg(isolatedCount)
                                   .arg(totalPeople > 0 ? (isolatedCount * 100 / totalPeople) : 0)
                                   .arg(bridgeCount)
                                   .arg(m_burnoutThreshold)
                                   .arg(burnoutCount)
                                   .arg(missingPhone)
                                   .arg(missingEmail)
                                   .arg(trend), &dlg);
    stats->setStyleSheet("font-size: 12px; line-height: 1.5;");
    lay->addWidget(stats);

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* btnExport = new QPushButton(" Export CSV", &dlg);
    btnExport->setIcon(IconManager::instance().getIcon("download", QColor(), 14));
    btnExport->setObjectName("secondaryBtn");
    btnRow->addWidget(btnExport);
    btnRow->addStretch();
    QPushButton* btnClose = new QPushButton("Close", &dlg);
    btnClose->setObjectName("secondaryBtn");
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnExport, &QPushButton::clicked, &dlg, [&]() {
        QString fileName = QFileDialog::getSaveFileName(&dlg, "Export Audit CSV", "graph_audit.csv", "CSV Files (*.csv)");
        if (fileName.isEmpty()) return;
        QFile f(fileName);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "metric,value\n"
                << "members," << totalPeople << "\n"
                << "groups," << totalGroups << "\n"
                << "edges," << edgeCount << "\n"
                << "isolated," << isolatedCount << "\n"
                << "bridges," << bridgeCount << "\n"
                << "burnout," << burnoutCount << "\n"
                << "missing_phone," << missingPhone << "\n"
                << "missing_email," << missingEmail << "\n"
                << "density," << QString::number(density, 'f', 6) << "\n"
                << "avg_connections," << avgConnections << "\n";
            f.close();
        }
    });

    dlg.exec();
}

// Moves the selection to the connected neighbor whose position best aligns
// with the pressed arrow direction (falls back to nearest node when nothing
// is selected).
void GraphWidget::selectNeighborInDirection(const QPointF& dir) {
    NodeItem* current = m_selectedNodeForDetails;
    if (!current) {
        for (NodeItem* n : m_nodes) {
            if (n->isSelected()) { current = n; break; }
        }
    }
    if (!current) return;

    NodeItem* best = nullptr;
    double bestDot = -1.0;
    for (EdgeItem* e : m_edges) {
        NodeItem* other = (e->sourceNode() == current) ? e->targetNode()
                        : (e->targetNode() == current) ? e->sourceNode() : nullptr;
        if (!other || !other->isVisible()) continue;
        QPointF rel = other->pos() - current->pos();
        const double len = std::hypot(rel.x(), rel.y());
        if (len < 1.0) continue;
        const double dot = (rel.x() / len) * dir.x() + (rel.y() / len) * dir.y();
        if (dot > bestDot) {
            bestDot = dot;
            best = other;
        }
    }
    if (best) {
        focusAndSelectNode(best);
        m_canvasView->centerOn(best);
    }
}

void GraphWidget::clearSceneSelection() {
    m_scene->clearSelection();
    selectNodeForDetails(nullptr);
    selectEdgeForDetails(nullptr);
    resetHighlight();
}

void GraphWidget::onZoomIn() {
    if (!m_canvasView) return;
    clampedScale(m_canvasView, 1.2);
}

void GraphWidget::onZoomOut() {
    if (!m_canvasView) return;
    clampedScale(m_canvasView, 0.8);
}

void GraphWidget::onZoomReset() {
    if (!m_canvasView || !m_scene) return;
    m_canvasView->resetTransform();
    if (m_nodes.isEmpty()) return;
    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);
    // fitInView with a null/degenerate rect produces a degenerate transform
    // (the next wheel/scale then divides by ~0). Guard it.
    if (!bounds.isValid() || bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0
        || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height())) {
        return;
    }
    m_canvasView->fitInView(bounds, Qt::KeepAspectRatio);
    // fitInView can still land outside the safe band for very large/small
    // graphs (huge sceneRect vs tiny cluster) — clamp back inside.
    const double z = currentZoom(m_canvasView);
    if (z < kMinZoom) clampedScale(m_canvasView, kMinZoom / z);
    else if (z > kMaxZoom) clampedScale(m_canvasView, kMaxZoom / z);
}

void GraphWidget::applyForceDirectedLayout() {
    const double kIdealDistance = 140.0;
    const double kMaxSpeed = 5.0;
    m_lastMaxDisplacement = 0.0;

    for (NodeItem* n1 : m_nodes) {
        if (!n1->isVisible() || n1->isBeingDragged || n1->isPinned()) continue;
        QPointF force(0, 0);

        // Repulsion between all visible nodes (Coulomb with minimum distance buffer)
        for (NodeItem* n2 : m_nodes) {
            if (n1 == n2 || !n2->isVisible()) continue;
            QPointF diff = n1->pos() - n2->pos();
            double dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());
            if (dist < 1.0) {
                diff = QPointF(QRandomGenerator::global()->bounded(4.0) - 2.0,
                               QRandomGenerator::global()->bounded(4.0) - 2.0);
                dist = 2.0;
            }
            if (dist < 450.0) {
                double clampedDist = qMax(45.0, dist);
                double rep = m_repulsionStrength / (clampedDist * clampedDist);
                force += (diff / dist) * rep;
            }
        }

        // Spring attraction along edges (Hooke's Law towards ideal distance)
        for (EdgeItem* e : m_edges) {
            if (!e->isVisible()) continue;
            NodeItem* other = nullptr;
            if (e->sourceNode() == n1) other = e->targetNode();
            else if (e->targetNode() == n1) other = e->sourceNode();

            if (other && other->isVisible()) {
                QPointF diff = other->pos() - n1->pos();
                double dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());
                if (dist > 1.0) {
                    double delta = dist - kIdealDistance;
                    double att = m_attractionStrength * delta;
                    force += (diff / dist) * att;
                }
            }
        }

        // Centering gravity pull
        force += -n1->pos() * m_gravity;

        // Apply velocity with high damping and speed cap
        n1->velocity = (n1->velocity + force) * m_damping;
        double speed = std::sqrt(n1->velocity.x() * n1->velocity.x() + n1->velocity.y() * n1->velocity.y());
        if (speed > kMaxSpeed) {
            n1->velocity = (n1->velocity / speed) * kMaxSpeed;
        }

        if (speed > 0.04) {
            QPointF next = n1->pos() + n1->velocity;
            // Keep nodes inside the scene so itemsBoundingRect can never grow
            // unbounded (which would make fitInView zoom to ~0 and explode
            // the background grid / paint math on the next zoom-out).
            next.setX(qBound(-kSceneBound, next.x(), kSceneBound));
            next.setY(qBound(-kSceneBound, next.y(), kSceneBound));
            n1->setPos(next);
        }
        m_lastMaxDisplacement = qMax(m_lastMaxDisplacement, speed);
    }

    resolveNodeOverlaps();

    for (EdgeItem* e : m_edges) {
        if (e) e->adjust();
    }
    for (GhostEdgeItem* ge : m_ghostEdges) {
        if (ge) ge->adjust();
    }
}

// Hard collision resolution: directly separate any two visible nodes whose
// bounding circles overlap (radii + margin). The force-based repulsion can
// settle with residual stacking; this guarantees no node sits on top of
// another. Dragged nodes are immovable for the pass so the user keeps
// control. It also feeds m_lastMaxDisplacement so auto-freeze never locks
// in an overlapping layout.
void GraphWidget::resolveNodeOverlaps() {
    for (int i = 0; i < m_nodes.size(); ++i) {
        NodeItem* a = m_nodes[i];
        if (!a->isVisible()) continue;
        for (int j = i + 1; j < m_nodes.size(); ++j) {
            NodeItem* b = m_nodes[j];
            if (!b->isVisible()) continue;
            if (a->isBeingDragged && b->isBeingDragged) continue;

            const QPointF delta = b->pos() - a->pos();
            const double dist = std::hypot(delta.x(), delta.y());
            const double minDist = a->radius() + b->radius() + 8.0;
            if (dist >= minDist) continue;

            // Exactly stacked nodes have no direction from the delta; nudge
            // along an arbitrary axis instead of skipping them.
            const QPointF dir = (dist < 0.001) ? QPointF(1.0, 0.0) : delta / dist;
            const double push = (minDist - dist) * 0.5;
            if (!a->isBeingDragged && !a->isPinned()) {
                QPointF next = a->pos() - dir * push;
                next.setX(qBound(-kSceneBound, next.x(), kSceneBound));
                next.setY(qBound(-kSceneBound, next.y(), kSceneBound));
                a->setPos(next);
            }
            if (!b->isBeingDragged && !b->isPinned()) {
                QPointF next = b->pos() + dir * push;
                next.setX(qBound(-kSceneBound, next.x(), kSceneBound));
                next.setY(qBound(-kSceneBound, next.y(), kSceneBound));
                b->setPos(next);
            }
            m_lastMaxDisplacement = qMax(m_lastMaxDisplacement, push);
        }
    }
}

void GraphWidget::applyClusteredLayout() {
    QList<NodeItem*> groupNodes;
    for (NodeItem* n : m_nodes) {
        if (n->nodeType() == NodeItem::Group) groupNodes.append(n);
    }
    int gCount = groupNodes.size();
    if (gCount == 0) return;

    for (int i = 0; i < gCount; ++i) {
        double angle = (2.0 * M_PI / gCount) * i;
        double radius = 400.0;
        groupNodes[i]->setPos(radius * std::cos(angle), radius * std::sin(angle));

        int memberIdx = 0;
        for (EdgeItem* e : m_edges) {
            NodeItem* person = nullptr;
            if (e->sourceNode() == groupNodes[i] && e->targetNode()->isPersonNode()) person = e->targetNode();
            else if (e->targetNode() == groupNodes[i] && e->sourceNode()->isPersonNode()) person = e->sourceNode();

            if (person) {
                double subAngle = (2.0 * M_PI / 8.0) * memberIdx;
                double subR = 80.0 + (memberIdx * 10);
                person->setPos(groupNodes[i]->pos().x() + subR * std::cos(subAngle),
                               groupNodes[i]->pos().y() + subR * std::sin(subAngle));
                ++memberIdx;
            }
        }
    }

    // Clustered mode is one-shot (physics paused): run the collision pass
    // repeatedly so no nodes remain stacked when the view settles.
    for (int iter = 0; iter < 40; ++iter) {
        const double before = m_lastMaxDisplacement;
        m_lastMaxDisplacement = 0.0;
        resolveNodeOverlaps();
        if (m_lastMaxDisplacement < 0.05 || qFuzzyCompare(m_lastMaxDisplacement, before)) break;
    }

    for (EdgeItem* e : m_edges) {
        if (e) e->adjust();
    }
    for (GhostEdgeItem* ge : m_ghostEdges) {
        if (ge) ge->adjust();
    }
}

void GraphWidget::updatePhysics() {
    if (!m_physicsRunning || m_layoutMode != ForceDirected) return;
    // Never mutate the scene while a modal dialog (sever confirm, MatchDialog)
    // holds the event loop — that path clears/deletes nodes mid-iteration.
    if (QApplication::activeModalWidget()) return;
    if (m_nodes.isEmpty()) {
        m_timer->stop(); // nothing to simulate — don't idle at 30 FPS
        return;
    }
    applyForceDirectedLayout();

    // Auto-freeze: once the layout has settled, stop the timer entirely so
    // the graph doesn't burn CPU at 30 FPS while nothing moves. Any filter
    // toggle, drag, or layout switch resumes it.
    if (m_lastMaxDisplacement < 0.05) {
        if (++m_settleTicks >= 90) m_timer->stop();
    } else {
        m_settleTicks = 0;
    }
}

void GraphWidget::pausePhysics() {
    m_physicsRunning = false;
    m_timer->stop();
    if (m_btnToggleFreeze) {
        const ThemeTokens tokFrz = ThemeManager::instance().currentTokens();
        m_btnToggleFreeze->setText(" Resume Physics");
        m_btnToggleFreeze->setIcon(IconManager::instance().getIcon("play", ThemeManager::textOn(tokFrz.accentWarning), 14));
        m_btnToggleFreeze->setStyleSheet(QString("background-color: %1; color: %2; font-weight: bold; font-size: 11px; padding: 5px 8px; border-radius: 6px;")
            .arg(tokFrz.accentWarning.name(), ThemeManager::textOn(tokFrz.accentWarning).name()));
    }
}

void GraphWidget::resumePhysics() {
    m_physicsRunning = true;
    m_settleTicks = 0;
    if (m_btnToggleFreeze) {
        m_btnToggleFreeze->setText(" Freeze Layout");
        m_btnToggleFreeze->setIcon(IconManager::instance().getIcon("pause", QColor(), 14));
        m_btnToggleFreeze->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    }
    if (!m_nodes.isEmpty() && !m_timer->isActive()) {
        m_timer->start(33);
    }
}

void GraphWidget::setRepulsionStrength(double val) {
    m_repulsionStrength = val;
}

void GraphWidget::applyRole(const QString& role) {
    m_userRole = role;
    m_readOnly = (role.compare("readonly", Qt::CaseInsensitive) == 0 ||
                  role.compare("guest", Qt::CaseInsensitive) == 0);
    const bool canEdit = !m_readOnly;
    const QString tip = m_readOnly
        ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
        : "";

    // Person inspector: care flag, admin notes and the reconnect workflow all
    // write to the database.
    if (m_cmbCareFlag) {
        m_cmbCareFlag->setEnabled(canEdit);
        m_cmbCareFlag->setToolTip(tip);
    }
    if (m_txtAdminNotes) {
        m_txtAdminNotes->setReadOnly(m_readOnly);
        m_txtAdminNotes->setToolTip(tip);
    }
    if (m_btnSaveNotes) {
        m_btnSaveNotes->setEnabled(canEdit);
        m_btnSaveNotes->setToolTip(tip);
    }
    if (m_btnLogContact) {
        m_btnLogContact->setEnabled(canEdit);
        // Keep the explanatory tooltip for admins; only guests get the
        // read-only explanation.
        m_btnLogContact->setToolTip(m_readOnly
            ? tip
            : QStringLiteral("Records a contact attempt (pastoral log) and flags 'Needs Contact' "
                             "when no other care flag is set"));
    }

    // Edge inspector: role reassignment and severing are writes.
    if (m_cmbEdgeRole) {
        m_cmbEdgeRole->setEnabled(canEdit);
        m_cmbEdgeRole->setToolTip(tip);
    }
    if (m_btnSeverEdge) {
        m_btnSeverEdge->setEnabled(canEdit);
        m_btnSeverEdge->setToolTip(tip);
    }
    if (m_btnUndoSever) {
        m_btnUndoSever->setEnabled(canEdit && m_hasUndoSever);
        m_btnUndoSever->setToolTip(tip);
    }

    // Group inspector: roster assignment and the ministry-match quick-add flow.
    if (m_btnGroupAddMember) {
        m_btnGroupAddMember->setEnabled(canEdit);
        m_btnGroupAddMember->setToolTip(tip);
    }
    if (m_btnGroupSuggest) {
        m_btnGroupSuggest->setEnabled(canEdit);
        m_btnGroupSuggest->setToolTip(tip);
    }

    // Batch operations: assignment writes memberships.
    if (m_btnBatchAssign) {
        m_btnBatchAssign->setEnabled(canEdit);
        m_btnBatchAssign->setToolTip(tip);
    }

    // Smart Bridge "Connect" cards are rebuilt on demand; update any that are
    // already on screen instead of recomputing (which would duplicate cards).
    if (m_smartBridgeSection) {
        for (QPushButton* connectBtn : m_smartBridgeSection->findChildren<QPushButton*>()) {
            if (connectBtn->text().trimmed() == QStringLiteral("Connect")) {
                connectBtn->setEnabled(canEdit);
                connectBtn->setToolTip(tip);
            }
        }
    }
}

void GraphWidget::applyTheme(const ThemeTokens& tok) {
    if (m_scene) {
        m_scene->setBackgroundBrush(tok.bgBase);
        m_scene->update();
    }
    if (m_canvasView) {
        m_canvasView->setBackgroundBrush(tok.bgBase);
    }
    styleChromeWidgets(tok);
    restyleChrome(tok);
}

// Token-based styles for the inspector dock, its lists/editors, and the
// legend labels — previously hardcoded Catppuccin-dark, which became
// unreadable (light text on light background) under light themes.
void GraphWidget::styleChromeWidgets(const ThemeTokens& tok) {
    const QString bg = tok.bgSurface.name();
    const QString bgInput = tok.bgInput.name();
    const QString border = tok.borderColor.name();
    const QString text = tok.textPrimary.name();
    const QString muted = tok.textSecondary.name();
    const QString accent = tok.accentPrimary.name();

    if (m_detailsDock) {
        m_detailsDock->setStyleSheet(QString(
            "QWidget#detailsDock { background-color: %1; border-left: 1px solid %2; color: %3; }"
            "QWidget#detailsDock QLabel { color: %3; }"
            "QLineEdit, QPlainTextEdit, QComboBox { background-color: %4; border: 1px solid %2; border-radius: 6px; color: %3; font-size: 11px; padding: 4px; }"
        ).arg(bg, border, text, bgInput));
    }
    if (m_hierarchyTreeWidget) {
        m_hierarchyTreeWidget->setStyleSheet(QString(
            "QTreeWidget { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; }"
            "QTreeWidget::item { padding: 4px 6px; }"
            "QTreeWidget::item:selected { background-color: %4; color: %5; font-weight: bold; }"
        ).arg(bgInput, border, text, accent, tok.isDark ? QStringLiteral("#11111b") : QStringLiteral("#ffffff")));
    }
    if (m_lblPersonGroups) {
        m_lblPersonGroups->setStyleSheet(QString("font-size: 11px; color: %1; line-height: 1.4;").arg(text));
    }
    if (m_txtAdminNotes) {
        m_txtAdminNotes->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 6px; color: %3; font-size: 11px;").arg(bgInput, border, text));
    }
    if (m_lblLogContactStatus) {
        m_lblLogContactStatus->setStyleSheet(QString("font-size: 10px; color: %1;").arg(muted));
    }
    if (m_lstRoster) {
        m_lstRoster->setStyleSheet(QString(
            "QListWidget { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %3; font-size: 11px; }"
            "QListWidget::item { padding: 4px; }"
            "QListWidget::item:hover { background-color: %4; color: %5; }"
        ).arg(bgInput, border, text, bg, accent));
    }
    if (m_lblGroupTasks) {
        m_lblGroupTasks->setStyleSheet(QString("font-size: 11px; color: %1;").arg(muted));
    }
    if (m_lstBatchMembers) {
        m_lstBatchMembers->setStyleSheet(QString(
            "QListWidget { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %3; font-size: 11px; }"
        ).arg(bgInput, border, text));
    }
    if (m_legendOverlay) {
        if (QLabel* t = m_legendOverlay->findChild<QLabel*>("legTitleLabel")) {
            t->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(accent));
        }
        if (QLabel* s = m_legendOverlay->findChild<QLabel*>("legShapesLabel")) {
            s->setStyleSheet(QString("font-size: 11px; color: %1;").arg(text));
        }
        if (QLabel* l = m_legendOverlay->findChild<QLabel*>("legLinesLabel")) {
            l->setStyleSheet(QString("font-size: 10px; color: %1;").arg(muted));
        }
    }
}

// Restores the admin's working view (filter toggles + layout mode) from
// QSettings — saved by onFilterToggled on every change.
void GraphWidget::restoreFilterState() {
    AppSettings s;
    m_filterPeopleActive = s.value("graphFilters/people", true).toBool();
    m_filterHouseholdsActive = s.value("graphFilters/households", true).toBool();
    m_filterGroupsActive = s.value("graphFilters/groups", true).toBool();
    m_filterBurnoutActive = s.value("graphFilters/burnout", false).toBool();
    m_filterIsolationActive = s.value("graphFilters/isolation", false).toBool();
    m_filterHygieneActive = s.value("graphFilters/hygiene", false).toBool();
    m_filterBridgeActive = s.value("graphFilters/bridge", false).toBool();
    m_filterFamilyActive = s.value("graphFilters/family", false).toBool();
    m_layoutMode = (LayoutMode)s.value("graphFilters/layout", (int)ForceDirected).toInt();

    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    if (m_btnTogglePeople) {
        m_btnTogglePeople->setChecked(m_filterPeopleActive);
        styleFilterButton(m_btnTogglePeople, m_filterPeopleActive, tok.accentPrimary);
    }
    if (m_btnToggleHouseholds) {
        m_btnToggleHouseholds->setChecked(m_filterHouseholdsActive);
        styleFilterButton(m_btnToggleHouseholds, m_filterHouseholdsActive, tok.accentPrimary);
    }
    if (m_btnToggleGroups) {
        m_btnToggleGroups->setChecked(m_filterGroupsActive);
        styleFilterButton(m_btnToggleGroups, m_filterGroupsActive, tok.accentPrimary);
    }
    if (m_btnToggleBurnout) {
        m_btnToggleBurnout->setChecked(m_filterBurnoutActive);
        styleFilterButton(m_btnToggleBurnout, m_filterBurnoutActive, tok.accentWarning);
    }
    if (m_btnToggleIsolation) {
        m_btnToggleIsolation->setChecked(m_filterIsolationActive);
        styleFilterButton(m_btnToggleIsolation, m_filterIsolationActive, tok.accentDanger);
    }
    if (m_btnToggleHygiene) {
        m_btnToggleHygiene->setChecked(m_filterHygieneActive);
        styleFilterButton(m_btnToggleHygiene, m_filterHygieneActive, tok.accentSuccess);
    }
    if (m_btnToggleBridgeNodes) {
        m_btnToggleBridgeNodes->setChecked(m_filterBridgeActive);
        styleFilterButton(m_btnToggleBridgeNodes, m_filterBridgeActive, QColor("#cba6f7"));
    }
    if (m_btnToggleFamilyDynasty) {
        m_btnToggleFamilyDynasty->setChecked(m_filterFamilyActive);
        styleFilterButton(m_btnToggleFamilyDynasty, m_filterFamilyActive, tok.accentWarning);
    }
    if (m_btnToggleLayout) {
        m_btnToggleLayout->setText(m_layoutMode == Clustered ? " Clustered" : " Force-Directed");
        m_btnToggleLayout->setIcon(IconManager::instance().getIcon(m_layoutMode == Clustered ? "grid" : "activity", QColor(), 14));
    }
    onFilterToggled();
}

// Re-applies token-based colors to the chrome (dock, floating controls) so a
// runtime theme switch restyles everything, not just the canvas.
void GraphWidget::restyleChrome(const ThemeTokens& tok) {
    if (m_detailsDock) {
        m_detailsDock->setStyleSheet(QString(
            "QWidget#detailsDock { background-color: %1; border-left: 1px solid %2; color: %3; }"
            "QWidget#detailsDock QLabel { color: %3; }"
            "QLineEdit, QComboBox, QPlainTextEdit, QListWidget, QTableWidget { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; }"
        ).arg(tok.bgSurface.name(), tok.borderColor.name(), tok.textPrimary.name(), tok.bgInput.name()));
    }
    if (m_floatingZoomBar) {
        m_floatingZoomBar->setStyleSheet(QString(
            "QWidget { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
            "QPushButton { background-color: transparent; color: %3; border: none; font-size: 14px; padding: 2px 6px; }"
        ).arg(tok.bgOverlay.name(), tok.borderColor.name(), tok.textPrimary.name()));
    }
    if (m_floatingViewOptionsBar) {
        m_floatingViewOptionsBar->setStyleSheet(QString(
            "QWidget#floatingViewOptionsBar { background-color: %1; border: 1px solid %2; border-radius: 8px; color: %3; }"
            "QLabel { color: %3; }"
        ).arg(tok.bgOverlay.name(), tok.borderColor.name(), tok.textPrimary.name()));
        setConnectorSizeLevel(m_connectorSizeLevel);
        setShowConnectors(m_showConnectors);
    }
    if (m_btnFloatingExpandDock) {
        m_btnFloatingExpandDock->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; font-weight: bold; font-size: 11px; padding: 6px 12px; border-radius: 6px; border: none; }"
            "QPushButton:hover { background-color: %3; }"
        ).arg(tok.accentPrimary.name(), ThemeManager::textOn(tok.accentPrimary).name(), tok.accentPrimary.darker(115).name()));
    }
    if (m_legendOverlay) {
        m_legendOverlay->setStyleSheet(QString(
            "QWidget#legendOverlay { background-color: %1; border: 1px solid %2; border-radius: 8px; color: %3; }"
            "QWidget#legendOverlay QLabel { color: %3; }"
        ).arg(tok.bgOverlay.name(), tok.borderColor.name(), tok.textPrimary.name()));
    }
    if (m_scene) m_scene->update();
}

void GraphWidget::saveNodePositions() {
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen() || m_nodes.isEmpty()) return;
    db.transaction();
    QSqlQuery clearQ(db);
    clearQ.exec("DELETE FROM graph_layout");

    QSqlQuery q(db);
    q.prepare("INSERT INTO graph_layout (entity_type, entity_id, pos_x, pos_y) VALUES (?, ?, ?, ?)");
    for (NodeItem* n : m_nodes) {
        QString typeStr = (n->nodeType() == NodeItem::Group) ? "Group" :
                          (n->nodeType() == NodeItem::Household) ? "Household" : "Person";
        q.addBindValue(typeStr);
        q.addBindValue(n->id());
        q.addBindValue(n->pos().x());
        q.addBindValue(n->pos().y());
        q.exec();
    }
    db.commit();
}

void GraphWidget::loadNodePositions() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    if (q.exec("SELECT entity_type, entity_id, pos_x, pos_y FROM graph_layout")) {
        while (q.next()) {
            QString typeStr = q.value("entity_type").toString();
            int eid = q.value("entity_id").toInt();
            double px = q.value("pos_x").toDouble();
            double py = q.value("pos_y").toDouble();

            for (NodeItem* n : m_nodes) {
                if (n->id() == eid) {
                    if ((typeStr == "Group" && n->nodeType() == NodeItem::Group) ||
                        (typeStr == "Household" && n->nodeType() == NodeItem::Household) ||
                        (typeStr == "Person" && n->isPersonNode())) {
                        n->setPos(px, py);
                    }
                }
            }
        }
    }

    for (EdgeItem* e : m_edges) {
        if (e) e->adjust();
    }
    for (GhostEdgeItem* ge : m_ghostEdges) {
        if (ge) ge->adjust();
    }
}

void GraphWidget::refreshHierarchyTree() {
    if (!m_hierarchyTreeWidget) return;
    m_hierarchyTreeWidget->clear();

    QSqlDatabase db = DatabaseManager::instance().database();

    // Map parent_id -> list of child group rows
    QMap<int, QList<QVariantMap>> childrenMap;
    QList<QVariantMap> rootGroups;

    QSqlQuery qGroups("SELECT id, name, category, parent_id FROM groups WHERE is_hidden = 0 OR is_hidden IS NULL ORDER BY name", db);
    while (qGroups.next()) {
        QVariantMap g;
        g["id"] = qGroups.value(0).toInt();
        g["name"] = qGroups.value(1).toString();
        g["category"] = qGroups.value(2).toString();
        g["parent_id"] = qGroups.value(3).toInt();

        int pId = g["parent_id"].toInt();
        if (pId > 0) childrenMap[pId].append(g);
        else rootGroups.append(g);
    }

    // Map group_id -> list of members
    QMap<int, QList<QVariantMap>> groupMembers;
    QSqlQuery qMem(
        "SELECT er.target_id, p.id, p.first_name || ' ' || p.last_name, er.relationship_type, p.phone_number AS phone, p.email, p.membership_status AS status, p.care_flag "
        "FROM entity_relationships er JOIN people p ON p.id = er.source_id "
        "WHERE er.target_type = 'Group' AND er.source_type = 'Person' "
        "ORDER BY CASE WHEN er.relationship_type IN ('Leader', 'Co-Leader', 'Teacher') THEN 0 ELSE 1 END, p.first_name", db);
    while (qMem.next()) {
        QVariantMap m;
        int gId = qMem.value(0).toInt();
        m["id"] = qMem.value(1).toInt();
        m["name"] = qMem.value(2).toString();
        m["role"] = qMem.value(3).toString();
        m["phone"] = qMem.value(4).toString();
        m["email"] = qMem.value(5).toString();
        m["status"] = qMem.value(6).toString();
        m["care_flag"] = qMem.value(7).toString();
        groupMembers[gId].append(m);
    }

    const ThemeTokens tokTree = ThemeManager::instance().currentTokens();
    std::function<void(QTreeWidgetItem*, const QVariantMap&)> addGroupItem;
    addGroupItem = [&](QTreeWidgetItem* parentItem, const QVariantMap& gData) {
        int gId = gData["id"].toInt();
        QString gName = gData["name"].toString();
        QString cat = gData["category"].toString();

        QTreeWidgetItem* gItem = nullptr;
        if (parentItem) {
            gItem = new QTreeWidgetItem(parentItem);
            gItem->setText(0, gName);
            gItem->setIcon(0, IconManager::instance().getIcon("layers", tokTree.accentPrimary, 16));
            gItem->setForeground(0, QBrush(tokTree.accentPrimary));
            gItem->setFont(0, QFont("Segoe UI", 9, QFont::DemiBold));
        } else {
            gItem = new QTreeWidgetItem(m_hierarchyTreeWidget);
            gItem->setText(0, gName);
            gItem->setIcon(0, IconManager::instance().getIcon("building-2", tokTree.accentSuccess, 16));
            gItem->setFont(0, QFont("Segoe UI", 10, QFont::Bold));
            gItem->setForeground(0, QBrush(tokTree.accentSuccess));
        }
        gItem->setText(1, cat.isEmpty() ? "Ministry Group" : cat);
        int mCount = groupMembers.value(gId).size();
        gItem->setText(2, QString("%1 involved").arg(mCount));
        gItem->setData(0, Qt::UserRole, gId);
        gItem->setData(0, Qt::UserRole + 1, "Group");

        // Add sub-groups first
        if (childrenMap.contains(gId)) {
            for (const auto& childG : childrenMap[gId]) {
                addGroupItem(gItem, childG);
            }
        }

        // Add people involved under this group
        if (groupMembers.contains(gId)) {
            for (const auto& mem : groupMembers[gId]) {
                QTreeWidgetItem* pItem = new QTreeWidgetItem(gItem);
                QString role = mem["role"].toString();
                QString careFlag = mem["care_flag"].toString();
                QString status = mem["status"].toString();

                pItem->setText(0, mem["name"].toString());
                pItem->setIcon(0, IconManager::instance().getIcon("user", tokTree.textPrimary, 14));
                pItem->setFont(0, QFont("Segoe UI", 9));
                pItem->setText(1, role.isEmpty() ? "Member" : role);
                if (role == "Leader" || role == "Co-Leader") {
                    pItem->setForeground(1, QBrush(tokTree.accentWarning));
                    pItem->setFont(1, QFont("Segoe UI", 9, QFont::Bold));
                } else {
                    pItem->setForeground(1, QBrush(tokTree.textSecondary));
                }

                QString statDisplay = status;
                if (!careFlag.isEmpty() && careFlag != "Normal") {
                    statDisplay += QString(" [%1]").arg(careFlag);
                    if (careFlag == "At-Risk") pItem->setForeground(2, QBrush(ThemeManager::instance().currentTokens().accentDanger));
                }
                pItem->setText(2, statDisplay);

                pItem->setData(0, Qt::UserRole, mem["id"].toInt());
                pItem->setData(0, Qt::UserRole + 1, "Person");
                pItem->setData(0, Qt::UserRole + 2, gId);
                pItem->setData(0, Qt::UserRole + 3, mem["phone"].toString());
                pItem->setData(0, Qt::UserRole + 4, mem["email"].toString());
            }
        }

        gItem->setExpanded(true);
    };

    if (m_selectedMinistryParentId > 0) {
        // Find the selected branch root
        QSqlQuery qSpecific(db);
        qSpecific.prepare("SELECT id, name, category, parent_id FROM groups WHERE id = ?");
        qSpecific.addBindValue(m_selectedMinistryParentId);
        if (qSpecific.exec() && qSpecific.next()) {
            QVariantMap g;
            g["id"] = qSpecific.value(0).toInt();
            g["name"] = qSpecific.value(1).toString();
            g["category"] = qSpecific.value(2).toString();
            g["parent_id"] = qSpecific.value(3).toInt();
            addGroupItem(nullptr, g);
        }
    } else {
        for (const auto& rootG : rootGroups) {
            addGroupItem(nullptr, rootG);
        }
    }
}

void GraphWidget::applyTreeHierarchyLayout() {
    if (m_selectedMinistryParentId <= 0) return;

    // 1. Identify Root Group Node
    NodeItem* rootGroupNode = nullptr;
    for (NodeItem* n : m_nodes) {
        if (n->nodeType() == NodeItem::Group && n->id() == m_selectedMinistryParentId) {
            rootGroupNode = n;
            break;
        }
    }
    if (!rootGroupNode) return;

    // 2. Identify Sub-Groups (child groups)
    QList<NodeItem*> subGroupNodes;
    for (NodeItem* n : m_nodes) {
        if (n->nodeType() == NodeItem::Group && n->id() != m_selectedMinistryParentId && n->isVisible()) {
            subGroupNodes.append(n);
        }
    }

    // 3. Map group -> list of visible member nodes
    QMap<int, QList<NodeItem*>> groupMembers;
    QSet<int> placedPersonIds;

    for (EdgeItem* e : m_edges) {
        if (!e->isVisible()) continue;
        NodeItem* gNode = nullptr;
        NodeItem* pNode = nullptr;
        if (e->sourceNode()->nodeType() == NodeItem::Group && e->targetNode()->isPersonNode()) {
            gNode = e->sourceNode();
            pNode = e->targetNode();
        } else if (e->targetNode()->nodeType() == NodeItem::Group && e->sourceNode()->isPersonNode()) {
            gNode = e->targetNode();
            pNode = e->sourceNode();
        }
        if (gNode && pNode && pNode->isVisible()) {
            if (!groupMembers[gNode->id()].contains(pNode)) {
                groupMembers[gNode->id()].append(pNode);
            }
        }
    }

    // 4. Position Root Group at top center
    rootGroupNode->setPos(0, -250);

    // If root group has direct members, place them on the left and right of the root
    QList<NodeItem*> directMembers = groupMembers.value(rootGroupNode->id());
    for (int i = 0; i < directMembers.size(); ++i) {
        double offset = (i % 2 == 0 ? 1 : -1) * (140.0 + (i / 2) * 100.0);
        directMembers[i]->setPos(offset, -250.0 + (i % 2 == 0 ? 30.0 : -30.0));
        placedPersonIds.insert(directMembers[i]->id());
    }

    // 5. Position Sub-Groups horizontally at y = 0
    int subCount = subGroupNodes.size();
    double subSpacing = qMax(240.0, 900.0 / qMax(1, subCount));
    double startX = -((subCount - 1) * subSpacing) / 2.0;

    for (int i = 0; i < subCount; ++i) {
        NodeItem* subG = subGroupNodes[i];
        double gx = startX + i * subSpacing;
        subG->setPos(gx, 0);

        // Position members of this sub-group in vertical columns below it
        QList<NodeItem*> mems = groupMembers.value(subG->id());
        int memIdx = 0;
        for (NodeItem* p : mems) {
            if (placedPersonIds.contains(p->id())) continue;
            double px = gx + ((memIdx % 2 == 0) ? -55.0 : 55.0);
            double py = 120.0 + (memIdx / 2) * 85.0;
            p->setPos(px, py);
            placedPersonIds.insert(p->id());
            ++memIdx;
        }
    }

    for (EdgeItem* e : m_edges) {
        if (e) e->adjust();
    }
    for (GhostEdgeItem* ge : m_ghostEdges) {
        if (ge) ge->adjust();
    }

    // Pause physics so the tree stays locked in place
    pausePhysics();
    if (m_canvasView && m_scene) {
        m_canvasView->fitInView(m_scene->itemsBoundingRect().adjusted(-80, -80, 80, 80), Qt::KeepAspectRatio);
    }
}
