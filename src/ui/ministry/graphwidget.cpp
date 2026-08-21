#include "graphwidget.h"
#include "../database/databasemanager.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QGraphicsSceneMouseEvent>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <cmath>
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
    setFlags(ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setToolTip(name);
}

QRectF NodeItem::boundingRect() const {
    double r = radius() + 30.0;
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

    QColor borderCol;
    QColor bodyColStart;
    QColor bodyColEnd;
    double r = radius();

    // Data Hygiene Mode Coloring
    if (isHygieneMode && isPersonNode()) {
        if (isHygieneCritical()) {
            borderCol = tok.accentDanger; // Red for missing phone AND email
            bodyColStart = tok.isDark ? QColor("#4a1a24") : QColor("#ffebee");
            bodyColEnd = tok.bgSurface;
        } else if (isHygieneWarning()) {
            borderCol = tok.accentWarning; // Yellow for missing phone OR email
            bodyColStart = tok.isDark ? QColor("#3b2a1a") : QColor("#fffde7");
            bodyColEnd = tok.bgSurface;
        } else {
            borderCol = tok.accentSuccess; // Green for complete records
            bodyColStart = tok.isDark ? QColor("#183020") : QColor("#e8f5e9");
            bodyColEnd = tok.bgSurface;
        }
    } else if (m_isCriticalBurnout) {
        borderCol = tok.accentDanger; // Critical Overload Red
        bodyColStart = tok.isDark ? QColor("#4a1a24") : QColor("#fce8e6");
        bodyColEnd = tok.bgSurface;
    } else if (m_burnoutGroupCount > 0 || m_isWarning) {
        borderCol = tok.accentWarning; // High Warning Amber/Orange
        bodyColStart = tok.isDark ? QColor("#3b2a1a") : QColor("#fef3e7");
        bodyColEnd = tok.bgSurface;
    } else if (!m_familyConcentrationMsg.isEmpty()) {
        borderCol = tok.accentWarning; // Family concentration warning amber
        bodyColStart = tok.isDark ? QColor("#382418") : QColor("#fef3e7");
        bodyColEnd = tok.bgSurface;
    } else {
        switch (m_type) {
            case Person:
                if (m_careFlag == "Leader" || m_connectionCount >= 4) {
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
                if (m_healthStatus == Understaffed) borderCol = tok.accentWarning;
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

    if (m_isHovered) {
        borderCol = tok.isDark ? QColor("#ffffff") : tok.accentPrimary;
    }

    QRadialGradient grad(0, 0, r + 2);
    grad.setColorAt(0, bodyColStart);
    grad.setColorAt(1, bodyColEnd);

    painter->setBrush(grad);
    int penWidth = (m_isCriticalBurnout || m_isWarning || isBeingDragged || m_isHovered) ? 4 : 2;
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

    painter->setPen(QPen(borderCol));
    painter->setFont(QFont("Segoe UI", qMax(8, static_cast<int>(r * 0.5)), QFont::Bold));
    QString symbol;
    if (m_type == Group) symbol = "🏛️";
    else if (m_type == Household) symbol = "🏡";
    else if (m_type == IsolatedPerson) symbol = "🔴";
    else if (m_type == Visitor) symbol = "👋";
    else symbol = "👤";
    painter->drawText(QRectF(-r, -r, r * 2.0, r * 2.0), Qt::AlignCenter, symbol);

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
    bool shouldShowLabel = !m_thresholdLabeling || m_isHovered || isLeader || isMajorNode || isHighDegree;

    if (shouldShowLabel) {
        QColor textCol = m_isHovered ? tok.accentPrimary : tok.textPrimary;
        painter->setPen(textCol);
        painter->setFont(QFont("Segoe UI", m_isHovered ? 9 : 8, m_isHovered ? QFont::Bold : QFont::Normal));
        painter->drawText(QRectF(-90, r + 4, 180, 20), Qt::AlignCenter, m_name);
    }

    if (m_isHovered) {
        int cardW = 230;
        int cardH = m_burnoutGroupCount > 0 ? 80 : 55;
        QRectF hoverCard(-cardW / 2, -r - cardH - 10, cardW, cardH);

        QColor cardBg = tok.isDark ? QColor(30, 30, 46, 235) : QColor(255, 255, 255, 245);
        painter->setBrush(cardBg);
        painter->setPen(QPen(borderCol, 1.5));
        painter->drawRoundedRect(hoverCard, 8, 8);

        painter->setFont(QFont("Segoe UI", 9, QFont::Bold));
        painter->setPen(tok.accentPrimary);
        painter->drawText(hoverCard.adjusted(10, 6, -10, -cardH + 22), Qt::AlignLeft | Qt::AlignVCenter, m_name);

        painter->setFont(QFont("Segoe UI", 8, QFont::Normal));
        painter->setPen(tok.textSecondary);

        if (m_burnoutGroupCount > 0) {
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
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    isBeingDragged = false;
    QGraphicsItem::mouseReleaseEvent(event);

    if ((event->screenPos() - m_dragStartPos).manhattanLength() < 5) {
        GraphWidget* gw = findParentGraphWidget(this);
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
    m_isHovered = true;
    QGraphicsItem::hoverEnterEvent(event);
    QGraphicsItem::update();
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
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
    if (!m_source || !m_target) return QRectF();
    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();
    return QRectF(p1, p2).normalized().adjusted(-15, -15, 15, 15);
}

QPainterPath EdgeItem::shape() const {
    if (!m_source || !m_target) return QPainterPath();
    QPainterPath p;
    p.moveTo(m_source->pos());
    p.lineTo(m_target->pos());
    QPainterPathStroker stroker;
    stroker.setWidth(14.0); // 14px wide clickable hitbox!
    return stroker.createStroke(p);
}

void EdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (!m_source || !m_target) return;
    painter->setRenderHint(QPainter::Antialiasing);

    ThemeTokens tok = ThemeManager::instance().currentTokens();

    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();

    QColor lineCol;
    double lineWidth = 1.5;
    Qt::PenStyle penStyle = Qt::SolidLine;

    if (m_isSelected) {
        lineCol = tok.accentPrimary;
        lineWidth = 4.0;
    } else if (m_isHovered) {
        lineCol = tok.isDark ? QColor("#ffffff") : tok.accentPrimary;
        lineWidth = 3.5;
    } else if (m_isBurnoutLink) {
        lineCol = tok.accentDanger;
        lineWidth = 2.5;
        penStyle = Qt::DashLine;
    } else if (m_relType == "Parent of") {
        lineCol = tok.textMuted;
        lineWidth = 1.2;
        penStyle = Qt::DashLine;
    } else if (m_relType.contains("Spouse") || m_relType.contains("Parent")) {
        lineCol = tok.accentWarning;
        lineWidth = 3.0;
    } else if (m_relType.contains("Leader")) {
        lineCol = tok.accentPrimary;
        lineWidth = 2.5;
    } else {
        lineCol = tok.borderColor;
        lineWidth = 1.5;
        penStyle = Qt::DashLine;
    }

    painter->setPen(QPen(lineCol, lineWidth, penStyle));
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

// --- GraphCanvasView Implementation ---

GraphCanvasView::GraphCanvasView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void GraphCanvasView::wheelEvent(QWheelEvent* event) {
    double factor = (event->angleDelta().y() > 0) ? 1.15 : 0.85;
    scale(factor, factor);
    event->accept();
}

void GraphCanvasView::mousePressEvent(QMouseEvent* event) {
    QGraphicsView::mousePressEvent(event);
    if (!itemAt(event->pos()) && m_graphWidget) {
        // Sticky selection: canvas click doesn't wipe unsaved notes, but updates highlighting
        m_graphWidget->resetHighlight();
    }
}

void GraphCanvasView::drawBackground(QPainter* painter, const QRectF& rect) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    painter->fillRect(rect, tok.bgBase);

    painter->setPen(QPen(tok.bgSurface, 1, Qt::DotLine));
    int gridSize = 50;
    qreal left = std::floor(rect.left() / gridSize) * gridSize;
    qreal top = std::floor(rect.top() / gridSize) * gridSize;

    QVector<QPointF> points;
    for (qreal x = left; x < rect.right(); x += gridSize) {
        for (qreal y = top; y < rect.bottom(); y += gridSize) {
            points.append(QPointF(x, y));
        }
    }
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
      m_layoutMode(ForceDirected), m_pathfindActive(false), m_pathStartNode(nullptr), m_pathEndNode(nullptr),
      m_selectedNodeForDetails(nullptr), m_selectedEdgeForDetails(nullptr),
      m_repulsionStrength(12000.0), m_attractionStrength(0.04),
      m_damping(0.85), m_gravity(0.02), m_physicsRunning(true)
{
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-2000, -2000, 4000, 4000);

    setupUI();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &GraphWidget::applyTheme);
    applyTheme(ThemeManager::instance().currentTokens());

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &GraphWidget::updatePhysics);
    m_timer->start(33); // ~30 FPS physics

    loadGraph();
}

GraphWidget::~GraphWidget() {}

void GraphWidget::styleFilterButton(QPushButton* btn, bool active, const QColor& activeColor) {
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    const QString textOnActive = (activeColor.lightness() > 140) ? "#11111b" : "#ffffff";
    if (active) {
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
            .arg(activeColor.name(), textOnActive));
    } else {
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
            .arg(tok.bgSurface.name(), tok.textSecondary.name(), tok.borderColor.name()));
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

    m_btnTogglePeople = createToggleBtn("👤 People", true);
    styleFilterButton(m_btnTogglePeople, true, tok.accentPrimary);
    m_btnToggleHouseholds = createToggleBtn("🏡 Households", true);
    styleFilterButton(m_btnToggleHouseholds, true, tok.accentPrimary);
    m_btnToggleGroups = createToggleBtn("🏛️ Groups", true);
    styleFilterButton(m_btnToggleGroups, true, tok.accentPrimary);
    m_btnToggleBurnout = createToggleBtn("⚠️ Burnout Risk", false);
    styleFilterButton(m_btnToggleBurnout, false, tok.accentWarning);
    m_btnToggleIsolation = createToggleBtn("🔴 Isolated Only", false);
    styleFilterButton(m_btnToggleIsolation, false, tok.accentDanger);
    m_btnToggleHygiene = createToggleBtn("🧹 Data Hygiene", false);
    styleFilterButton(m_btnToggleHygiene, false, tok.accentSuccess);

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

    m_btnToggleLayout = new QPushButton("⚡ Layout: Force-Directed", this);
    m_btnToggleLayout->setObjectName("secondaryBtn");
    m_btnToggleLayout->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnToggleLayout, &QPushButton::clicked, this, &GraphWidget::onToggleLayoutMode);

    row1->addWidget(m_btnTogglePeople);
    row1->addWidget(m_btnToggleHouseholds);
    row1->addWidget(m_btnToggleGroups);
    row1->addWidget(m_btnToggleBurnout);
    row1->addWidget(m_btnToggleIsolation);
    row1->addWidget(m_btnToggleHygiene);
    row1->addStretch();
    row1->addWidget(m_btnToggleLayout);

    m_btnIsolatedBanner = new QPushButton("⚠️ 0 Unconnected Members", this);
    m_btnIsolatedBanner->setCursor(Qt::PointingHandCursor);
    connect(m_btnIsolatedBanner, &QPushButton::clicked, this, &GraphWidget::onSpotlightIsolatedClicked);

    m_graphSearchEdit = new QLineEdit(this);
    m_graphSearchEdit->setPlaceholderText("🔍 Search graph...");
    m_graphSearchEdit->setFixedWidth(150);
    m_graphSearchEdit->setStyleSheet("font-size: 11px; padding: 4px;");
    connect(m_graphSearchEdit, &QLineEdit::textChanged, this, &GraphWidget::onGraphSearchChanged);

    m_btnPathfind = new QPushButton("🔍 Pathfinder", this);
    m_btnPathfind->setObjectName("secondaryBtn");
    m_btnPathfind->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnPathfind, &QPushButton::clicked, this, &GraphWidget::onPathfindToggle);

    m_btnRunAudit = new QPushButton("📊 Audit Report", this);
    m_btnRunAudit->setObjectName("secondaryBtn");
    m_btnRunAudit->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnRunAudit, &QPushButton::clicked, this, &GraphWidget::onRunAudit);

    m_btnExportPNG = new QPushButton("📷 Export PNG", this);
    m_btnExportPNG->setObjectName("secondaryBtn");
    m_btnExportPNG->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnExportPNG, &QPushButton::clicked, this, &GraphWidget::onExportPNG);

    m_btnLegendToggle = new QPushButton("❓ Legend", this);
    m_btnLegendToggle->setObjectName("secondaryBtn");
    m_btnLegendToggle->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnLegendToggle, &QPushButton::clicked, this, &GraphWidget::onToggleLegend);

    row2->addWidget(m_graphSearchEdit);
    row2->addWidget(m_btnIsolatedBanner);
    row2->addStretch();
    row2->addWidget(m_btnPathfind);
    row2->addWidget(m_btnRunAudit);
    row2->addWidget(m_btnExportPNG);
    row2->addWidget(m_btnLegendToggle);

    topControlsLayout->addLayout(row1);
    topControlsLayout->addLayout(row2);
    leftLayout->addWidget(m_topBarWidget);

    m_canvasView = new GraphCanvasView(m_scene, leftContainer);
    m_canvasView->setGraphWidget(this);
    leftLayout->addWidget(m_canvasView, 1);
    m_splitter->addWidget(leftContainer);

    // --- Right Inspector Dock (25% width) ---
    m_detailsDock = new QWidget(m_splitter);
    m_detailsDock->setMinimumWidth(260);
    QVBoxLayout* dockLayout = new QVBoxLayout(m_detailsDock);
    dockLayout->setContentsMargins(12, 10, 12, 10);
    dockLayout->setSpacing(10);

    // Dock Header with Title and Collapse Toggle
    QHBoxLayout* dockHeader = new QHBoxLayout();
    QLabel* dockTitle = new QLabel("🏷️ Context Inspector", m_detailsDock);
    dockTitle->setStyleSheet("font-size: 13px; font-weight: bold;");
    m_btnCollapseDock = new QPushButton("⇥ Collapse", m_detailsDock);
    m_btnCollapseDock->setObjectName("secondaryBtn");
    m_btnCollapseDock->setStyleSheet("padding: 2px 6px; font-size: 10px;");
    connect(m_btnCollapseDock, &QPushButton::clicked, this, &GraphWidget::onToggleCollapseDock);
    dockHeader->addWidget(dockTitle);
    dockHeader->addStretch();
    dockHeader->addWidget(m_btnCollapseDock);
    dockLayout->addLayout(dockHeader);

    m_detailsStack = new QStackedWidget(m_detailsDock);

    // Page 0: Person Node Inspector
    m_pagePerson = new QWidget(m_detailsStack);
    QVBoxLayout* pLay = new QVBoxLayout(m_pagePerson);
    pLay->setContentsMargins(0, 0, 0, 0);
    pLay->setSpacing(8);

    m_lblPersonAvatar = new QLabel("👤", m_pagePerson);
    m_lblPersonAvatar->setStyleSheet("font-size: 28px;");
    m_lblPersonName = new QLabel("Person Name", m_pagePerson);
    m_lblPersonName->setStyleSheet("font-size: 15px; font-weight: bold;");
    m_lblPersonStatus = new QLabel("Status: Active Member", m_pagePerson);
    m_lblPersonStatus->setStyleSheet("font-size: 11px; opacity: 0.8;");

    QHBoxLayout* contactRow = new QHBoxLayout();
    m_btnCopyPhone = new QPushButton("📞 Copy Phone", m_pagePerson);
    m_btnCopyPhone->setObjectName("secondaryBtn");
    m_btnCopyPhone->setStyleSheet("font-size: 10px; padding: 3px 6px;");
    m_btnCopyEmail = new QPushButton("✉️ Copy Email", m_pagePerson);
    m_btnCopyEmail->setObjectName("secondaryBtn");
    m_btnCopyEmail->setStyleSheet("font-size: 10px; padding: 3px 6px;");
    connect(m_btnCopyPhone, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails) onCopyContactInfo(m_selectedNodeForDetails, "phone");
    });
    connect(m_btnCopyEmail, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails) onCopyContactInfo(m_selectedNodeForDetails, "email");
    });
    contactRow->addWidget(m_btnCopyPhone);
    contactRow->addWidget(m_btnCopyEmail);

    QLabel* lblCare = new QLabel("Care Flag:", m_pagePerson);
    lblCare->setStyleSheet("font-size: 11px; font-weight: bold;");
    m_cmbCareFlag = new QComboBox(m_pagePerson);
    m_cmbCareFlag->addItems({"Normal", "Active Member", "Leader", "At-Risk", "New Visitor", "Needs Contact"});
    connect(m_cmbCareFlag, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphWidget::onCareFlagChanged);

    m_lblPersonGroups = new QLabel("Connected Groups: (Loading...)", m_pagePerson);
    m_lblPersonGroups->setStyleSheet("font-size: 11px;");
    m_lblPersonGroups->setWordWrap(true);

    m_txtAdminNotes = new QPlainTextEdit(m_pagePerson);
    m_txtAdminNotes->setPlaceholderText("Pastoral admin notes & observations...");
    m_txtAdminNotes->setMaximumHeight(75);

    m_btnSaveNotes = new QPushButton("💾 Save Notes", m_pagePerson);
    m_btnSaveNotes->setObjectName("secondaryBtn");
    connect(m_btnSaveNotes, &QPushButton::clicked, this, &GraphWidget::onSaveNotes);

    m_btnOpenProfile = new QPushButton("👤 View Full Member Dossier", m_pagePerson);
    m_btnOpenProfile->setObjectName("primaryBtn");
    m_btnOpenProfile->setCursor(Qt::PointingHandCursor);
    connect(m_btnOpenProfile, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails && m_selectedNodeForDetails->isPersonNode()) {
            emit navigateToProfile("Person", m_selectedNodeForDetails->id());
        }
    });

    pLay->addWidget(m_lblPersonAvatar);
    pLay->addWidget(m_lblPersonName);
    pLay->addWidget(m_lblPersonStatus);
    pLay->addLayout(contactRow);
    pLay->addWidget(lblCare);
    pLay->addWidget(m_cmbCareFlag);
    pLay->addWidget(m_lblPersonGroups);
    pLay->addWidget(m_txtAdminNotes);
    pLay->addWidget(m_btnSaveNotes);
    pLay->addWidget(m_btnOpenProfile);
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
    m_lblGroupMemberCount = new QLabel("Members (0):", m_pageGroup);
    m_lblGroupMemberCount->setStyleSheet("font-size: 11px; font-weight: bold;");

    m_lstRoster = new QListWidget(m_pageGroup);
    m_lstRoster->setMaximumHeight(140);
    m_lstRoster->setStyleSheet("QListWidget { background-color: #11111b; border: 1px solid #313244; border-radius: 6px; color: #cdd6f4; font-size: 11px; }"
                               "QListWidget::item { padding: 4px; }"
                               "QListWidget::item:hover { background-color: #313244; color: #89b4fa; }");
    connect(m_lstRoster, &QListWidget::itemClicked, this, &GraphWidget::onRosterItemClicked);

    QHBoxLayout* groupBtnRow = new QHBoxLayout();
    m_btnGroupAddMember = new QPushButton("➕ Add Member", m_pageGroup);
    m_btnGroupAddMember->setObjectName("primaryBtn");
    m_btnGroupAddMember->setStyleSheet("font-size: 10px; padding: 4px 6px;");
    m_btnGroupCopyEmails = new QPushButton("✉️ Copy Emails", m_pageGroup);
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

    gLay->addWidget(m_lblGroupName);
    gLay->addWidget(m_lblGroupCategory);
    gLay->addWidget(m_lblGroupHealth);
    gLay->addWidget(m_lblGroupMemberCount);
    gLay->addWidget(m_lstRoster);
    gLay->addLayout(groupBtnRow);
    gLay->addStretch();
    m_detailsStack->addWidget(m_pageGroup);

    // Page 2: Edge / Relationship Inspector
    m_pageEdge = new QWidget(m_detailsStack);
    QVBoxLayout* eLay = new QVBoxLayout(m_pageEdge);
    eLay->setContentsMargins(0, 0, 0, 0);
    eLay->setSpacing(8);

    m_lblEdgeTitle = new QLabel("🔗 Relationship Details", m_pageEdge);
    m_lblEdgeTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #89b4fa;");
    m_lblEdgeBetween = new QLabel("Between: Person ↔ Group", m_pageEdge);
    m_lblEdgeBetween->setStyleSheet("font-size: 12px; font-weight: bold;");
    m_lblEdgeBetween->setWordWrap(true);

    QLabel* lblRole = new QLabel("Assigned Role / Connection:", m_pageEdge);
    lblRole->setStyleSheet("font-size: 11px;");
    m_cmbEdgeRole = new QComboBox(m_pageEdge);
    m_cmbEdgeRole->addItems({"Member", "Leader", "Co-Leader", "Volunteer", "Spouse", "Child", "Parent"});
    connect(m_cmbEdgeRole, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphWidget::onEdgeRoleChanged);

    m_btnSeverEdge = new QPushButton("🗑️ Sever Connection / Remove", m_pageEdge);
    m_btnSeverEdge->setObjectName("dangerBtn");
    m_btnSeverEdge->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnSeverEdge, &QPushButton::clicked, this, &GraphWidget::onSeverSelectedEdge);

    eLay->addWidget(m_lblEdgeTitle);
    eLay->addWidget(m_lblEdgeBetween);
    eLay->addWidget(lblRole);
    eLay->addWidget(m_cmbEdgeRole);
    eLay->addSpacing(10);
    eLay->addWidget(m_btnSeverEdge);
    eLay->addStretch();
    m_detailsStack->addWidget(m_pageEdge);

    // Page 3: Empty / Data Hygiene Action Dashboard
    m_pageEmpty = new QWidget(m_detailsStack);
    QVBoxLayout* empLay = new QVBoxLayout(m_pageEmpty);
    empLay->setContentsMargins(0, 0, 0, 0);
    empLay->setSpacing(8);

    QLabel* empTitle = new QLabel("🧹 Data Hygiene & Quick Focus", m_pageEmpty);
    empTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: #a6e3a1;");
    m_lblHygieneMissingPhone = new QLabel("• 0 records missing phone", m_pageEmpty);
    m_lblHygieneMissingEmail = new QLabel("• 0 records missing email", m_pageEmpty);
    m_lblHygieneIsolated = new QLabel("• 0 isolated / unconnected members", m_pageEmpty);

    m_btnSpotlightIncomplete = new QPushButton("🔍 Spotlight Incomplete Records", m_pageEmpty);
    m_btnSpotlightIncomplete->setObjectName("primaryBtn");
    m_btnSpotlightIncomplete->setStyleSheet("font-size: 11px; padding: 5px 8px;");
    connect(m_btnSpotlightIncomplete, &QPushButton::clicked, this, &GraphWidget::spotlightHygieneNodes);

    empLay->addWidget(empTitle);
    empLay->addWidget(m_lblHygieneMissingPhone);
    empLay->addWidget(m_lblHygieneMissingEmail);
    empLay->addWidget(m_lblHygieneIsolated);
    empLay->addWidget(m_btnSpotlightIncomplete);
    empLay->addStretch();
    m_detailsStack->addWidget(m_pageEmpty);

    dockLayout->addWidget(m_detailsStack, 1);
    m_splitter->addWidget(m_detailsDock);

    m_splitter->setSizes({750, 250});
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, true);

    mainLayout->addWidget(m_splitter);

    // Collapsible Legend Overlay
    m_legendOverlay = new QWidget(m_canvasView);
    m_legendOverlay->setStyleSheet("QWidget#legendOverlay { background-color: rgba(24, 24, 37, 0.95); border: 1px solid #45475a; border-radius: 8px; }");
    m_legendOverlay->setObjectName("legendOverlay");
    m_legendOverlay->hide();

    QVBoxLayout* legLayout = new QVBoxLayout(m_legendOverlay);
    legLayout->setContentsMargins(12, 10, 12, 10);
    legLayout->setSpacing(6);

    QLabel* legTitle = new QLabel("📖 Graph Legend & Symbols", m_legendOverlay);
    legTitle->setStyleSheet("font-size: 12px; font-weight: bold; color: #89b4fa;");
    legLayout->addWidget(legTitle);

    QLabel* legShapes = new QLabel("👤 Member (Circle)   🏛️ Group (Hexagon)\n🏡 Household (Diamond)   🔴 Isolated (Square)", m_legendOverlay);
    legShapes->setStyleSheet("font-size: 11px; color: #cdd6f4;");
    legLayout->addWidget(legShapes);

    QLabel* legLines = new QLabel("── Gold Solid: Family Tie\n── Cyan Solid: Ministry Leader\n- - Gray Dash: General Member\n- - Red Dash: Burnout Risk Link", m_legendOverlay);
    legLines->setStyleSheet("font-size: 10px; color: #a6adc8;");
    legLayout->addWidget(legLines);

    selectNodeForDetails(nullptr);
}

void GraphWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateDetailsPanelForEmpty();
}

void GraphWidget::onToggleCollapseDock() {
    QList<int> sizes = m_splitter->sizes();
    if (sizes.size() >= 2 && sizes[1] > 0) {
        m_splitter->setSizes({width(), 0});
        m_btnCollapseDock->setText("⇥ Expand");
    } else {
        m_splitter->setSizes({width() - 260, 260});
        m_btnCollapseDock->setText("⇥ Collapse");
    }
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
    if (m_layoutMode == ForceDirected) {
        m_layoutMode = Clustered;
        m_btnToggleLayout->setText("🧩 Layout: Clustered");
        pausePhysics();
        applyClusteredLayout();
    } else {
        m_layoutMode = ForceDirected;
        m_btnToggleLayout->setText("⚡ Layout: Force-Directed");
        resumePhysics();
    }
}

void GraphWidget::loadGraph() {
    m_selectedNodeForDetails = nullptr;
    m_selectedEdgeForDetails = nullptr;
    m_pathStartNode = nullptr;
    m_pathEndNode = nullptr;
    m_pathHighlightedNodes.clear();
    m_pathHighlightedEdges.clear();

    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();
    m_isolatedPersonIds.clear();
    m_burnoutDetailsMap.clear();

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);

    updateIsolationQuery();

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

            double angle = QRandomGenerator::global()->bounded(360.0) * M_PI / 180.0;
            double dist = 150.0 + QRandomGenerator::global()->bounded(550.0);
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    }

    static const QList<QColor> kCategoryPalette = {
        QColor("#89b4fa"), QColor("#a6e3a1"), QColor("#f9e2af"), QColor("#f38ba8"),
        QColor("#cba6f7"), QColor("#94e2d5"), QColor("#fab387"), QColor("#74c7ec"),
    };
    if (q.exec("SELECT id, name, category FROM groups")) {
        while (q.next()) {
            int gid = q.value("id").toInt();
            QString name = q.value("name").toString();
            QString category = q.value("category").toString();

            NodeItem* node = new NodeItem(gid, name, NodeItem::Group);
            node->setHealthStatus(NodeItem::FullyStaffed);
            if (!category.isEmpty()) {
                uint h = qHash(category);
                node->setCategoryColor(kCategoryPalette[h % kCategoryPalette.size()]);
            }

            double angle = QRandomGenerator::global()->bounded(360.0) * M_PI / 180.0;
            double dist = 80.0 + QRandomGenerator::global()->bounded(200.0);
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    }

    if (q.exec("SELECT id, name FROM households")) {
        while (q.next()) {
            int hid = q.value("id").toInt();
            QString name = q.value("name").toString();

            NodeItem* node = new NodeItem(hid, name, NodeItem::Household);
            double angle = QRandomGenerator::global()->bounded(360.0) * M_PI / 180.0;
            double dist = 200.0 + QRandomGenerator::global()->bounded(400.0);
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    }

    QMap<int, int> personActiveGroupCount;
    QMap<int, QStringList> personActiveRoles;
    if (q.exec("SELECT source_id, role, target_id FROM entity_relationships WHERE source_type = 'Person' AND target_type = 'Group'")) {
        while (q.next()) {
            int pid = q.value("source_id").toInt();
            QString role = q.value("role").toString();
            personActiveGroupCount[pid]++;
            personActiveRoles[pid].append(role.isEmpty() ? "Member" : role);
        }
    }

    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode()) {
            int count = personActiveGroupCount.value(n->id(), 0);
            if (count >= m_burnoutThreshold) {
                bool isCritical = (count >= m_burnoutThreshold + 1);
                n->setBurnoutDetails(count, isCritical, personActiveRoles.value(n->id()).join(", "));
            }
        }
    }

    if (q.exec("SELECT id, source_type, source_id, target_type, target_id, relationship_type, role FROM entity_relationships")) {
        while (q.next()) {
            int relId = q.value("id").toInt();
            QString sType = q.value("source_type").toString();
            int sId = q.value("source_id").toInt();
            QString tType = q.value("target_type").toString();
            int tId = q.value("target_id").toInt();
            QString rType = q.value("relationship_type").toString();
            QString role = q.value("role").toString();
            if (!role.isEmpty()) rType = role;

            NodeItem* sNode = nullptr;
            NodeItem* tNode = nullptr;

            for (NodeItem* n : m_nodes) {
                if (n->id() == sId) {
                    if ((sType == "Person" && n->isPersonNode()) ||
                        (sType == "Group" && n->nodeType() == NodeItem::Group) ||
                        (sType == "Household" && n->nodeType() == NodeItem::Household)) {
                        sNode = n;
                    }
                }
                if (n->id() == tId) {
                    if ((tType == "Person" && n->isPersonNode()) ||
                        (tType == "Group" && n->nodeType() == NodeItem::Group) ||
                        (tType == "Household" && n->nodeType() == NodeItem::Household)) {
                        tNode = n;
                    }
                }
                if (sNode && tNode) break;
            }

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
            NodeItem* parentNode = nullptr;
            NodeItem* childNode = nullptr;
            for (NodeItem* n : m_nodes) {
                if (n->nodeType() == NodeItem::Group) {
                    if (n->id() == parentId) parentNode = n;
                    if (n->id() == childId) childNode = n;
                }
            }
            if (parentNode && childNode) {
                EdgeItem* edge = new EdgeItem(parentNode, childNode, "Parent of", 0, false);
                m_scene->addItem(edge);
                m_edges.append(edge);
            }
        }
    }

    loadNodePositions();
    onFilterToggled();
    updateDetailsPanelForEmpty();
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
        m_btnIsolatedBanner->setText(QString("⚠️ %1 Unconnected Members").arg(count));
        const ThemeTokens t = ThemeManager::instance().currentTokens();
        QColor dangerBg = t.accentDanger;
        dangerBg.setAlpha(40);
        m_btnIsolatedBanner->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; font-size: 11px; font-weight: bold; border-radius: 6px; padding: 5px 10px; }")
                .arg(dangerBg.name(), t.accentDanger.name()));
    }
}

void GraphWidget::updateDetailsPanelForEmpty() {
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

        int idx = m_cmbCareFlag->findText(node->careFlag());
        if (idx >= 0) m_cmbCareFlag->setCurrentIndex(idx);

        QStringList groups;
        QSqlQuery q;
        q.prepare("SELECT g.name, er.role FROM entity_relationships er JOIN groups g ON er.target_id = g.id WHERE er.source_id = ? AND er.source_type = 'Person'");
        q.addBindValue(node->id());
        if (q.exec()) {
            while (q.next()) {
                QString gName = q.value(0).toString();
                QString role = q.value(1).toString();
                groups.append(QString("%1 (%2)").arg(gName, role.isEmpty() ? "Member" : role));
            }
        }
        m_lblPersonGroups->setText(groups.isEmpty() ? "Connected Groups: None (Unconnected)" : QString("Connected Groups (%1):\n• %2").arg(groups.size()).arg(groups.join("\n• ")));
    } else if (node->nodeType() == NodeItem::Group) {
        m_detailsStack->setCurrentWidget(m_pageGroup);
        m_lblGroupName->setText(node->name());
        m_lblGroupCategory->setText(QString("Health: %1").arg(node->healthStatus() == NodeItem::FullyStaffed ? "Fully Staffed" : "Understaffed"));

        m_lstRoster->clear();
        QSqlQuery q;
        q.prepare("SELECT p.id, p.first_name || ' ' || p.last_name, er.role FROM entity_relationships er JOIN people p ON er.source_id = p.id WHERE er.target_id = ? AND er.source_type = 'Person'");
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
    }
}

void GraphWidget::selectEdgeForDetails(EdgeItem* edge) {
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
    int idx = m_cmbEdgeRole->findText(edge->relationshipType());
    if (idx >= 0) m_cmbEdgeRole->setCurrentIndex(idx);
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

void GraphWidget::onSeverSelectedEdge() {
    if (!m_selectedEdgeForDetails) return;

    QString srcName = m_selectedEdgeForDetails->sourceNode()->name();
    QString tgtName = m_selectedEdgeForDetails->targetNode()->name();

    auto reply = QMessageBox::question(this, "Sever Connection",
                                       QString("Are you sure you want to remove the connection between %1 and %2?").arg(srcName, tgtName),
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    int relId = m_selectedEdgeForDetails->relationshipId();
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    if (relId > 0) {
        q.prepare("DELETE FROM entity_relationships WHERE id = ?");
        q.addBindValue(relId);
        q.exec();
    } else {
        q.prepare("DELETE FROM entity_relationships WHERE source_id = ? AND target_id = ?");
        q.addBindValue(m_selectedEdgeForDetails->sourceNode()->id());
        q.addBindValue(m_selectedEdgeForDetails->targetNode()->id());
        q.exec();
    }

    m_scene->removeItem(m_selectedEdgeForDetails);
    m_edges.removeOne(m_selectedEdgeForDetails);
    delete m_selectedEdgeForDetails;
    m_selectedEdgeForDetails = nullptr;

    updateDetailsPanelForEmpty();
    m_scene->update();
}

void GraphWidget::onEdgeRoleChanged(int) {
    if (!m_selectedEdgeForDetails) return;
    QString newRole = m_cmbEdgeRole->currentText();
    m_selectedEdgeForDetails->setRelationshipType(newRole);

    int relId = m_selectedEdgeForDetails->relationshipId();
    QSqlQuery q;
    if (relId > 0) {
        q.prepare("UPDATE entity_relationships SET role = ? WHERE id = ?");
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
    if (!node) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Assign to Group");
    dlg.resize(320, 160);
    QFormLayout* form = new QFormLayout(&dlg);

    QComboBox* grpCombo = new QComboBox(&dlg);
    QSqlQuery q("SELECT id, name FROM groups ORDER BY name");
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
        insertQ.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, role) VALUES ('Person', ?, 'Group', ?, 'Member of', ?)");
        insertQ.addBindValue(node->id());
        insertQ.addBindValue(gid);
        insertQ.addBindValue(role);
        insertQ.exec();
        dlg.accept();
        loadGraph();
    });

    dlg.exec();
}

void GraphWidget::showNodeContextMenu(NodeItem* node, const QPoint& globalPos) {
    if (!node) return;
    QMenu m(this);

    if (node->isPersonNode()) {
        QAction* aProfile = m.addAction("👤 View Full Profile");
        QAction* aPhone = m.addAction("📞 Copy Phone Number");
        QAction* aEmail = m.addAction("✉️ Copy Email Address");
        m.addSeparator();
        QAction* aAssign = m.addAction("➕ Assign to Group...");
        QMenu* mCare = m.addMenu("🚩 Set Care Flag");
        QAction* cNone = mCare->addAction("None / Normal");
        QAction* cActive = mCare->addAction("Active Member");
        QAction* cLeader = mCare->addAction("Leader");
        QAction* cRisk = mCare->addAction("At-Risk");
        QAction* cContact = mCare->addAction("Needs Contact");

        QAction* sel = m.exec(globalPos);
        if (sel == aProfile) {
            emit navigateToProfile("Person", node->id());
        } else if (sel == aPhone) {
            onCopyContactInfo(node, "phone");
        } else if (sel == aEmail) {
            onCopyContactInfo(node, "email");
        } else if (sel == aAssign) {
            onQuickAssignGroup(node);
        } else if (sel && mCare->actions().contains(sel)) {
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
        QAction* aHighlight = m.addAction("📋 Highlight Roster on Canvas");
        QAction* aAdd = m.addAction("➕ Add Member to Group...");
        QAction* aEmails = m.addAction("✉️ Copy All Member Emails");
        QAction* aMinistries = m.addAction("✏️ View in Ministries Tab");

        QAction* sel = m.exec(globalPos);
        if (sel == aHighlight) {
            highlightConnections(node);
        } else if (sel == aAdd) {
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
    if (!node) return;

    node->setHovered(true);
    for (EdgeItem* edge : m_edges) {
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

void GraphWidget::onSpotlightIsolatedClicked() {
    m_btnToggleIsolation->setChecked(true);
    m_filterIsolationActive = true;
    styleFilterButton(m_btnToggleIsolation, true, ThemeManager::instance().currentTokens().accentDanger);
    onFilterToggled();
    spotlightIsolatedNodes();
}

void GraphWidget::onFilterToggled() {
    for (NodeItem* n : m_nodes) {
        bool visible = true;
        if (n->isPersonNode() && !m_filterPeopleActive) visible = false;
        if (n->nodeType() == NodeItem::Household && !m_filterHouseholdsActive) visible = false;
        if (n->nodeType() == NodeItem::Group && !m_filterGroupsActive) visible = false;
        if (m_filterBurnoutActive && n->burnoutGroupCount() < m_burnoutThreshold) visible = false;
        if (m_filterIsolationActive && !m_isolatedPersonIds.contains(n->id())) visible = false;
        n->setVisible(visible);
    }
    for (EdgeItem* e : m_edges) {
        e->setVisible(e->sourceNode()->isVisible() && e->targetNode()->isVisible());
    }
}

void GraphWidget::onGraphSearchChanged(const QString& text) {
    if (text.trimmed().isEmpty()) {
        resetHighlight();
        return;
    }
    resetHighlight();
    for (NodeItem* n : m_nodes) {
        if (n->name().contains(text, Qt::CaseInsensitive)) {
            n->setHovered(true);
            m_canvasView->centerOn(n);
        }
    }
}

void GraphWidget::onPathfindToggle() {
    m_pathfindActive = !m_pathfindActive;
    if (m_pathfindActive) {
        m_btnPathfind->setStyleSheet("background-color: #89b4fa; color: #11111b; font-weight: bold; padding: 5px 10px; font-size: 11px;");
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
        NodeItem* step = m_pathEndNode;
        while (step) {
            step->setHovered(true);
            step = parentMap.value(step, nullptr);
        }
    }
}

void GraphWidget::onSaveNotes() {
    if (!m_selectedNodeForDetails) return;
    QString notes = m_txtAdminNotes->toPlainText().trimmed();
    m_selectedNodeForDetails->setAdminNotes(notes);

    QSqlQuery q;
    if (m_selectedNodeForDetails->isPersonNode()) {
        q.prepare("UPDATE people SET admin_notes = ? WHERE id = ?");
    } else if (m_selectedNodeForDetails->nodeType() == NodeItem::Group) {
        q.prepare("UPDATE groups SET description = ? WHERE id = ?");
    }
    q.addBindValue(notes);
    q.addBindValue(m_selectedNodeForDetails->id());
    q.exec();

    QMessageBox::information(this, "Notes Saved", "Pastoral care notes saved successfully.");
}

void GraphWidget::onCareFlagChanged(int) {
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

void GraphWidget::onExportJSON() {}

void GraphWidget::onRunAudit() {
    QDialog dlg(this);
    dlg.setWindowTitle("📊 Congregation Connection & Health Audit");
    dlg.resize(480, 360);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    int totalPeople = 0;
    int totalGroups = 0;
    int isolatedCount = m_isolatedPersonIds.size();
    int burnoutCount = 0;
    for (NodeItem* n : m_nodes) {
        if (n->isPersonNode()) ++totalPeople;
        if (n->nodeType() == NodeItem::Group) ++totalGroups;
        if (n->burnoutGroupCount() >= m_burnoutThreshold) ++burnoutCount;
    }

    QLabel* title = new QLabel("Congregation Relational Diagnostics", &dlg);
    title->setStyleSheet("font-size: 14px; font-weight: bold;");
    lay->addWidget(title);

    QLabel* stats = new QLabel(QString("• Total Tracked Members: %1\n"
                                      "• Active Ministry Groups: %2\n"
                                      "• Unconnected / Isolated Members: %3 (%4%)\n"
                                      "• Volunteers in Burnout Danger (>=%5 groups): %6\n")
                                   .arg(totalPeople)
                                   .arg(totalGroups)
                                   .arg(isolatedCount)
                                   .arg(totalPeople > 0 ? (isolatedCount * 100 / totalPeople) : 0)
                                   .arg(m_burnoutThreshold)
                                   .arg(burnoutCount), &dlg);
    stats->setStyleSheet("font-size: 12px; line-height: 1.5;");
    lay->addWidget(stats);

    QPushButton* btnClose = new QPushButton("Close", &dlg);
    lay->addWidget(btnClose);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

void GraphWidget::onZoomIn() {
    m_canvasView->scale(1.2, 1.2);
}

void GraphWidget::onZoomOut() {
    m_canvasView->scale(0.8, 0.8);
}

void GraphWidget::onZoomReset() {
    m_canvasView->resetTransform();
}

void GraphWidget::applyForceDirectedLayout() {
    for (NodeItem* n1 : m_nodes) {
        if (!n1->isVisible() || n1->isBeingDragged) continue;
        QPointF force(0, 0);

        for (NodeItem* n2 : m_nodes) {
            if (n1 == n2 || !n2->isVisible()) continue;
            QPointF diff = n1->pos() - n2->pos();
            double dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());
            if (dist < 1.0) dist = 1.0;
            if (dist < 400.0) {
                double rep = m_repulsionStrength / (dist * dist);
                force += (diff / dist) * rep;
            }
        }

        for (EdgeItem* e : m_edges) {
            if (!e->isVisible()) continue;
            NodeItem* other = nullptr;
            if (e->sourceNode() == n1) other = e->targetNode();
            else if (e->targetNode() == n1) other = e->sourceNode();

            if (other && other->isVisible()) {
                QPointF diff = other->pos() - n1->pos();
                double dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());
                double att = m_attractionStrength * dist;
                force += (diff / dist) * att;
            }
        }

        force += -n1->pos() * m_gravity;
        n1->velocity = (n1->velocity + force) * m_damping;
        n1->setPos(n1->pos() + n1->velocity);
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
}

void GraphWidget::updatePhysics() {
    if (m_physicsRunning && m_layoutMode == ForceDirected) {
        applyForceDirectedLayout();
    }
}

void GraphWidget::pausePhysics() {
    m_physicsRunning = false;
}

void GraphWidget::resumePhysics() {
    m_physicsRunning = true;
}

void GraphWidget::setRepulsionStrength(double val) {
    m_repulsionStrength = val;
}

void GraphWidget::applyRole(const QString& role) {
    m_userRole = role;
}

void GraphWidget::applyTheme(const ThemeTokens&) {
    if (m_scene) m_scene->update();
}

void GraphWidget::saveNodePositions() {
    QSqlDatabase db = DatabaseManager::instance().database();
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
}
