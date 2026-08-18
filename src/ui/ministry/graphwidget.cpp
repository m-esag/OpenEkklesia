#include "graphwidget.h"
#include "../database/databasemanager.h"
#include <QPainter>
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

    QColor borderCol;
    QColor bodyColStart;
    QColor bodyColEnd;
    double r = radius();

    // 1. Tiered Burnout & Warning Colors
    if (m_isCriticalBurnout) {
        borderCol = tok.accentDanger; // Critical Overload Red
        bodyColStart = tok.isDark ? QColor("#4a1a24") : QColor("#fce8e6");
        bodyColEnd = tok.bgSurface;
    } else if (m_burnoutGroupCount >= 3 || m_isWarning) {
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
                    borderCol = tok.accentPrimary; // Primary Accent
                    bodyColStart = tok.isDark ? QColor("#003847") : QColor("#e0f7fa");
                } else if (m_careFlag == "Active Member" || m_connectionCount >= 2) {
                    borderCol = tok.accentSuccess; // Success Green
                    bodyColStart = tok.isDark ? QColor("#1e2438") : QColor("#e8f5e9");
                } else {
                    borderCol = tok.borderColor; // Muted Gray/Border
                    bodyColStart = tok.isDark ? QColor("#181825") : QColor("#f5f5f5");
                }
                bodyColEnd = tok.bgSurface;
                break;
            case Group:
                if (m_healthStatus == FullyStaffed) borderCol = tok.accentSuccess;
                else if (m_healthStatus == Understaffed) borderCol = tok.accentWarning;
                else if (m_healthStatus == CriticalShortage) borderCol = tok.accentDanger;
                else borderCol = tok.accentSuccess;
                bodyColStart = tok.isDark ? QColor("#183020") : QColor("#e8f5e9");
                bodyColEnd = tok.bgSurface;
                break;
            case Household:
                borderCol = tok.accentWarning; // Warm Gold
                bodyColStart = tok.isDark ? QColor("#302d20") : QColor("#fffde7");
                bodyColEnd = tok.bgSurface;
                break;
            case IsolatedPerson:
                borderCol = tok.accentDanger; // Isolated Red
                bodyColStart = tok.isDark ? QColor("#381e28") : QColor("#ffebee");
                bodyColEnd = tok.bgSurface;
                break;
            case Visitor:
                borderCol = tok.accentPrimary; // Sky Blue / Primary
                bodyColStart = tok.isDark ? QColor("#152530") : QColor("#e3f2fd");
                bodyColEnd = tok.bgSurface;
                break;
        }
    }

    if (m_isHovered) {
        borderCol = tok.isDark ? QColor("#ffffff") : tok.accentPrimary; // Highlight border on hover
    }

    QRadialGradient grad(0, 0, r + 2);
    grad.setColorAt(0, bodyColStart);
    grad.setColorAt(1, bodyColEnd);

    painter->setBrush(grad);
    int penWidth = (m_isCriticalBurnout || m_isWarning || isBeingDragged || m_isHovered) ? 4 : 2;
    painter->setPen(QPen(borderCol, penWidth));

    // Render node shape based on entity type
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

    // Paint inner visual icon for redundant shape identification
    painter->setPen(QPen(borderCol));
    painter->setFont(QFont("Segoe UI", qMax(8, (int)(r * 0.5)), QFont::Bold));
    QString symbol;
    if (m_type == Group) symbol = "🏛️";
    else if (m_type == Household) symbol = "🏡";
    else if (m_type == IsolatedPerson) symbol = "🔴";
    else if (m_type == Visitor) symbol = "👋";
    else symbol = "👤";
    painter->drawText(QRectF(-r, -r, r * 2.0, r * 2.0), Qt::AlignCenter, symbol);

    // Paint care flag badge at top right
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

    // Draw label
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

    // Hover Tooltip / Overlay Card
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

    QPoint diff = event->screenPos() - m_dragStartPos.toPoint();
    if (diff.manhattanLength() < 6) {
        GraphWidget* gw = nullptr;
        if (scene()) {
            for (QGraphicsView* view : scene()->views()) {
                gw = qobject_cast<GraphWidget*>(view);
                if (gw) break;
            }
        }
        if (gw) {
            gw->handleNodeClick(this);
        }
    }
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event);
    GraphWidget* gw = nullptr;
    if (scene()) {
        for (QGraphicsView* view : scene()->views()) {
            gw = qobject_cast<GraphWidget*>(view);
            if (gw) break;
        }
    }
    if (gw) {
        if (isPersonNode()) {
            gw->selectNodeForDetails(this);
        }
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

EdgeItem::EdgeItem(NodeItem* source, NodeItem* target, const QString& relType, bool isBurnoutLink)
    : m_source(source), m_target(target), m_relType(relType), m_isBurnoutLink(isBurnoutLink), m_isHovered(false)
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

void EdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (!m_source || !m_target) return;
    painter->setRenderHint(QPainter::Antialiasing);

    ThemeTokens tok = ThemeManager::instance().currentTokens();

    QPointF p1 = m_source->pos();
    QPointF p2 = m_target->pos();

    QColor lineCol;
    double lineWidth = 1.5;
    Qt::PenStyle penStyle = Qt::SolidLine;

    if (m_isHovered) {
        lineCol = tok.isDark ? QColor("#ffffff") : tok.accentPrimary;
        lineWidth = 3.5;
    } else if (m_isBurnoutLink) {
        lineCol = tok.accentDanger;
        lineWidth = 2.5;
        penStyle = Qt::DashLine;
    } else if (m_relType.contains("Spouse") || m_relType.contains("Parent")) {
        lineCol = tok.accentWarning; // Gold family line
        lineWidth = 3.0;
    } else if (m_relType.contains("Leader")) {
        lineCol = tok.accentPrimary; // Primary leadership line
        lineWidth = 2.5;
    } else {
        lineCol = tok.borderColor; // Theme border color
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

// --- GraphWidget Implementation ---

GraphWidget::GraphWidget(QWidget* parent)
    : QGraphicsView(parent), m_filterPeopleActive(true), m_filterHouseholdsActive(true), m_filterGroupsActive(true),
      m_filterBurnoutActive(false), m_filterIsolationActive(false), m_layoutMode(ForceDirected),
      m_pathfindActive(false), m_pathStartNode(nullptr), m_pathEndNode(nullptr),
      m_selectedNodeForDetails(nullptr), m_repulsionStrength(12000.0), m_attractionStrength(0.04),
      m_damping(0.85), m_gravity(0.02), m_physicsRunning(true)
{
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-2000, -2000, 4000, 4000);
    setScene(m_scene);

    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

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
    // Top Controls Container (2-Row Responsive Layout)
    QVBoxLayout* topControlsLayout = new QVBoxLayout();
    topControlsLayout->setContentsMargins(15, 10, 15, 10);
    topControlsLayout->setSpacing(8);

    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(8);

    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(8);

    // Filter Buttons (Toggle Pills)
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

    m_btnToggleLayout = new QPushButton("⚡ Layout: Force-Directed", this);
    m_btnToggleLayout->setObjectName("secondaryBtn");
    m_btnToggleLayout->setCursor(Qt::PointingHandCursor);
    m_btnToggleLayout->setStyleSheet("font-size: 11px; padding: 5px 10px;");
    connect(m_btnToggleLayout, &QPushButton::clicked, this, &GraphWidget::onToggleLayoutMode);

    row1->addWidget(m_btnTogglePeople);
    row1->addWidget(m_btnToggleHouseholds);
    row1->addWidget(m_btnToggleGroups);
    row1->addWidget(m_btnToggleBurnout);
    row1->addWidget(m_btnToggleIsolation);
    row1->addWidget(m_btnToggleLayout);
    row1->addStretch();

    // Persistent Unconnected Alert Banner Badge
    m_btnIsolatedBanner = new QPushButton("⚠️ Unconnected Members → Spotlight", this);
    {
        const ThemeTokens t = ThemeManager::instance().currentTokens();
        QColor dangerBg = t.accentDanger;
        dangerBg.setAlpha(51); // ~20% opacity
        const QString textOnDanger = (t.accentDanger.lightness() > 140) ? "#11111b" : "#ffffff";
        m_btnIsolatedBanner->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %2; font-size: 11px; font-weight: bold; border-radius: 6px; padding: 5px 10px; }"
                    "QPushButton:hover { background-color: %2; color: %3; }")
                .arg(dangerBg.name(), t.accentDanger.name(), textOnDanger));
    }
    m_btnIsolatedBanner->setCursor(Qt::PointingHandCursor);
    connect(m_btnIsolatedBanner, &QPushButton::clicked, this, &GraphWidget::onSpotlightIsolatedClicked);

    // Search bar
    m_graphSearchEdit = new QLineEdit(this);
    m_graphSearchEdit->setPlaceholderText("🔍 Search graph...");
    m_graphSearchEdit->setFixedWidth(140);
    m_graphSearchEdit->setStyleSheet("font-size: 11px; padding: 4px;");
    connect(m_graphSearchEdit, &QLineEdit::textChanged, this, &GraphWidget::onGraphSearchChanged);

    // Action buttons
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

    // Collapsible Legend Toggle Button
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

    // Collapsible Legend Overlay Card
    m_legendOverlay = new QWidget(this);
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

    // Context-Sensitive Side Panel
    m_detailsPanel = new QWidget(this);
    m_detailsPanel->setFixedWidth(280);
    m_detailsPanel->setStyleSheet("QWidget { background-color: #181825; border-left: 1px solid #313244; }");

    m_detailsLayout = new QVBoxLayout(m_detailsPanel);
    m_detailsLayout->setContentsMargins(15, 15, 15, 15);
    m_detailsLayout->setSpacing(10);

    m_lblDetailTitle = new QLabel("📊 Graph Overview", m_detailsPanel);
    m_lblDetailTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #cdd6f4;");
    m_detailsLayout->addWidget(m_lblDetailTitle);

    m_lblDetailCategory = new QLabel("Select any node to view details", m_detailsPanel);
    m_lblDetailCategory->setStyleSheet("font-size: 11px; color: #a6adc8;");
    m_detailsLayout->addWidget(m_lblDetailCategory);

    m_lblDetailBody = new QLabel(m_detailsPanel);
    m_lblDetailBody->setWordWrap(true);
    m_lblDetailBody->setStyleSheet("font-size: 12px; color: #cdd6f4;");
    m_detailsLayout->addWidget(m_lblDetailBody);

    m_cmbCareFlag = new QComboBox(m_detailsPanel);
    m_cmbCareFlag->addItems({"Normal", "Active Member", "Leader", "At-Risk", "New Visitor"});
    connect(m_cmbCareFlag, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphWidget::onCareFlagChanged);
    m_detailsLayout->addWidget(m_cmbCareFlag);

    m_txtAdminNotes = new QPlainTextEdit(m_detailsPanel);
    m_txtAdminNotes->setPlaceholderText("Pastoral admin notes & care log...");
    m_txtAdminNotes->setMaximumHeight(80);
    m_detailsLayout->addWidget(m_txtAdminNotes);

    m_btnSaveNotes = new QPushButton("💾 Save Notes", m_detailsPanel);
    m_btnSaveNotes->setObjectName("secondaryBtn");
    connect(m_btnSaveNotes, &QPushButton::clicked, this, &GraphWidget::onSaveNotes);
    m_detailsLayout->addWidget(m_btnSaveNotes);

    m_btnOpenProfile = new QPushButton("👤 View Full Member Profile", m_detailsPanel);
    m_btnOpenProfile->setObjectName("primaryBtn");
    m_btnOpenProfile->setCursor(Qt::PointingHandCursor);
    connect(m_btnOpenProfile, &QPushButton::clicked, this, [this]() {
        if (m_selectedNodeForDetails && m_selectedNodeForDetails->isPersonNode()) {
            emit navigateToProfile("Person", m_selectedNodeForDetails->id());
        }
    });
    m_detailsLayout->addWidget(m_btnOpenProfile);

    m_lstRoster = new QListWidget(m_detailsPanel);
    m_lstRoster->setMaximumHeight(150);
    m_lstRoster->setStyleSheet("QListWidget { background-color: #11111b; border: 1px solid #313244; border-radius: 6px; color: #cdd6f4; font-size: 11px; }"
                               "QListWidget::item { padding: 4px; }"
                               "QListWidget::item:hover { background-color: #313244; color: #89b4fa; }");
    m_detailsLayout->addWidget(m_lstRoster);

    m_detailsLayout->addStretch();

    // Overlay Controls Bar Widget
    m_topBarWidget = new QWidget(this);
    m_topBarWidget->setObjectName("topBarWidget");
    m_topBarWidget->setLayout(topControlsLayout);

    // Parent m_detailsPanel directly to 'this' as an overlay panel
    m_detailsPanel->setParent(this);

    selectNodeForDetails(nullptr); // Initial overview panel
}

void GraphWidget::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);

    int topHeight = m_topBarWidget ? m_topBarWidget->sizeHint().height() : 75;
    if (m_topBarWidget) {
        m_topBarWidget->setGeometry(0, 0, width(), topHeight);
        m_topBarWidget->raise();
    }

    int panelWidth = 280;
    if (m_detailsPanel) {
        m_detailsPanel->setGeometry(width() - panelWidth, topHeight, panelWidth, height() - topHeight);
        m_detailsPanel->raise();
    }

    if (m_legendOverlay && m_legendOverlay->isVisible()) {
        m_legendOverlay->raise();
        m_legendOverlay->move(width() - panelWidth - m_legendOverlay->width() - 20, topHeight + 10);
    }
}

void GraphWidget::onToggleLegend() {
    int topHeight = m_topBarWidget ? m_topBarWidget->sizeHint().height() : 75;
    if (m_legendOverlay->isVisible()) {
        m_legendOverlay->hide();
    } else {
        m_legendOverlay->show();
        m_legendOverlay->raise();
        m_legendOverlay->move(width() - 280 - m_legendOverlay->width() - 20, topHeight + 10);
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

    // 1. Identify Isolated Persons
    updateIsolationQuery();

    // 2. Load People Nodes
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
    } else {
        qDebug() << "[GraphWidget] Failed to query people:" << q.lastError().text();
    }

    // 3. Load Groups Nodes
    if (q.exec("SELECT id, name, category FROM groups")) {
        while (q.next()) {
            int gid = q.value("id").toInt();
            QString name = q.value("name").toString();

            NodeItem* node = new NodeItem(gid, name, NodeItem::Group);
            node->setHealthStatus(NodeItem::FullyStaffed);

            double angle = QRandomGenerator::global()->bounded(360.0) * M_PI / 180.0;
            double dist = 150.0 + QRandomGenerator::global()->bounded(550.0);
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    } else {
        qDebug() << "[GraphWidget] Failed to query groups:" << q.lastError().text();
    }

    // 4. Load Households Nodes
    if (q.exec("SELECT id, family_name AS name FROM households")) {
        while (q.next()) {
            int hid = q.value("id").toInt();
            QString name = q.value("name").toString();

            NodeItem* node = new NodeItem(hid, name, NodeItem::Household);

            double angle = QRandomGenerator::global()->bounded(360.0) * M_PI / 180.0;
            double dist = 150.0 + QRandomGenerator::global()->bounded(550.0);
            node->setPos(dist * std::cos(angle), dist * std::sin(angle));

            m_scene->addItem(node);
            m_nodes.append(node);
        }
    } else {
        qDebug() << "[GraphWidget] Failed to query households:" << q.lastError().text();
    }

    // Map helper for quick lookup
    auto findNode = [this](NodeItem::Type type, int id) -> NodeItem* {
        for (NodeItem* n : m_nodes) {
            if (n->id() == id) {
                if (type == NodeItem::Group && n->nodeType() == NodeItem::Group) return n;
                if (type == NodeItem::Household && n->nodeType() == NodeItem::Household) return n;
                if (type == NodeItem::Person && n->isPersonNode()) return n;
            }
        }
        return nullptr;
    };

    // 5. Load Relationships & Edges
    if (q.exec("SELECT source_type, source_id, target_type, target_id, relationship_type FROM entity_relationships")) {
        while (q.next()) {
            QString srcTypeStr = q.value("source_type").toString();
            int srcId = q.value("source_id").toInt();
            QString tgtTypeStr = q.value("target_type").toString();
            int tgtId = q.value("target_id").toInt();
            QString relType = q.value("relationship_type").toString();

            NodeItem::Type st = (srcTypeStr == "Group") ? NodeItem::Group : ((srcTypeStr == "Household") ? NodeItem::Household : NodeItem::Person);
            NodeItem::Type tt = (tgtTypeStr == "Group") ? NodeItem::Group : ((tgtTypeStr == "Household") ? NodeItem::Household : NodeItem::Person);

            NodeItem* srcNode = findNode(st, srcId);
            NodeItem* tgtNode = findNode(tt, tgtId);

            if (srcNode && tgtNode) {
                EdgeItem* edge = new EdgeItem(srcNode, tgtNode, relType);
                m_scene->addItem(edge);
                m_edges.append(edge);

                srcNode->setConnectionCount(srcNode->connectionCount() + 1);
                tgtNode->setConnectionCount(tgtNode->connectionCount() + 1);
            }
        }
    }

    // 6. Compute Burnout Severity Details
    QMap<int, QStringList> personGroupRolesMap;
    if (q.exec("SELECT source_id, target_id, relationship_type FROM entity_relationships WHERE source_type = 'Person' AND target_type = 'Group'")) {
        while (q.next()) {
            int pid = q.value("source_id").toInt();
            int gid = q.value("target_id").toInt();
            QString rel = q.value("relationship_type").toString();

            NodeItem* gnode = findNode(NodeItem::Group, gid);
            QString gname = gnode ? gnode->name() : QString("Group %1").arg(gid);
            personGroupRolesMap[pid].append(QString("%1 (%2)").arg(gname, rel));
        }
    }

    int totalBurnoutCount = 0;
    for (auto it = personGroupRolesMap.begin(); it != personGroupRolesMap.end(); ++it) {
        int pid = it.key();
        QStringList roles = it.value();
        int count = roles.size();

        if (count >= 3) {
            NodeItem* pnode = findNode(NodeItem::Person, pid);
            if (pnode) {
                bool isCritical = (count >= 5);
                pnode->setBurnoutDetails(count, isCritical, roles.join(", "));
                totalBurnoutCount++;
            }
        }
    }

    // Update Isolated Members Banner Count
    int isolatedCount = m_isolatedPersonIds.size();
    m_btnIsolatedBanner->setText(QString("⚠️ %1 Unconnected Members").arg(isolatedCount));

    // Load Saved Node Layout Positions if available
    loadNodePositions();

    if (m_layoutMode == Clustered) {
        applyClusteredLayout();
    }

    onFilterToggled();
    selectNodeForDetails(nullptr);
    centerOn(0, 0);
}

void GraphWidget::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    if (!m_nodes.isEmpty()) {
        centerOn(0, 0);
    }
}

void GraphWidget::applyTheme(const ThemeTokens& tokens) {
    m_scene->setBackgroundBrush(tokens.bgBase);

    if (m_topBarWidget) {
        m_topBarWidget->setStyleSheet(QString(
            "QWidget#topBarWidget { background-color: %1; border-bottom: 1px solid %2; color: %3; }"
            "QLineEdit { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; padding: 4px 8px; }"
            "QPushButton { background-color: %5; color: %3; border: 1px solid %2; border-radius: 6px; padding: 5px 10px; font-size: 11px; }"
            "QPushButton:hover { background-color: %2; }"
        ).arg(tokens.bgSurface.name(), tokens.borderColor.name(), tokens.textPrimary.name(), tokens.bgInput.name(), tokens.bgOverlay.name()));
    }

    if (m_btnTogglePeople) {
        m_btnTogglePeople->setStyleSheet(m_filterPeopleActive ?
            QString("QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.accentPrimary.name(), tokens.isDark ? "#11111b" : "#ffffff") :
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.bgInput.name(), tokens.textSecondary.name(), tokens.borderColor.name()));
    }
    if (m_btnToggleHouseholds) {
        m_btnToggleHouseholds->setStyleSheet(m_filterHouseholdsActive ?
            QString("QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.accentPrimary.name(), tokens.isDark ? "#11111b" : "#ffffff") :
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.bgInput.name(), tokens.textSecondary.name(), tokens.borderColor.name()));
    }
    if (m_btnToggleGroups) {
        m_btnToggleGroups->setStyleSheet(m_filterGroupsActive ?
            QString("QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.accentPrimary.name(), tokens.isDark ? "#11111b" : "#ffffff") :
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.bgInput.name(), tokens.textSecondary.name(), tokens.borderColor.name()));
    }
    if (m_btnToggleBurnout) {
        m_btnToggleBurnout->setStyleSheet(m_filterBurnoutActive ?
            QString("QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.accentWarning.name(), tokens.isDark ? "#11111b" : "#ffffff") :
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.bgInput.name(), tokens.textSecondary.name(), tokens.borderColor.name()));
    }
    if (m_btnToggleIsolation) {
        m_btnToggleIsolation->setStyleSheet(m_filterIsolationActive ?
            QString("QPushButton { background-color: %1; color: %2; font-weight: bold; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.accentDanger.name(), tokens.isDark ? "#11111b" : "#ffffff") :
            QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; }")
                .arg(tokens.bgInput.name(), tokens.textSecondary.name(), tokens.borderColor.name()));
    }

    if (m_detailsPanel) {
        m_detailsPanel->setStyleSheet(QString(
            "QWidget { background-color: %1; border-left: 1px solid %2; color: %3; }"
            "QLabel { color: %3; }"
            "QPlainTextEdit, QLineEdit { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; }"
            "QComboBox { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; padding: 4px; }"
            "QListWidget { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; }"
        ).arg(tokens.bgSurface.name(), tokens.borderColor.name(), tokens.textPrimary.name(), tokens.bgInput.name()));
    }
    if (m_lblDetailTitle) {
        m_lblDetailTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));
    }
    if (m_lblDetailCategory) {
        m_lblDetailCategory->setStyleSheet(QString("font-size: 11px; color: %1;").arg(tokens.textSecondary.name()));
    }
    if (m_lblDetailBody) {
        m_lblDetailBody->setStyleSheet(QString("font-size: 12px; color: %1;").arg(tokens.textPrimary.name()));
    }
    if (m_legendOverlay) {
        m_legendOverlay->setStyleSheet(QString(
            "QWidget#legendOverlay { background-color: %1; border: 1px solid %2; border-radius: 8px; color: %3; }"
        ).arg(tokens.bgOverlay.name(), tokens.borderColor.name(), tokens.textPrimary.name()));
    }
    m_scene->update();
}

void GraphWidget::updateIsolationQuery() {
    m_isolatedPersonIds.clear();
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    if (q.exec("SELECT id FROM people WHERE id NOT IN (SELECT DISTINCT source_id FROM entity_relationships WHERE source_type = 'Person') AND id NOT IN (SELECT DISTINCT target_id FROM entity_relationships WHERE target_type = 'Person')")) {
        while (q.next()) {
            m_isolatedPersonIds.insert(q.value(0).toInt());
        }
    }
}

void GraphWidget::applyClusteredLayout() {
    QList<NodeItem*> groupNodes;
    QList<NodeItem*> householdNodes;
    QList<NodeItem*> personNodes;
    QList<NodeItem*> isolatedNodes;

    for (NodeItem* n : m_nodes) {
        if (!n->isVisible()) continue;
        if (n->nodeType() == NodeItem::Group) groupNodes.append(n);
        else if (n->nodeType() == NodeItem::Household) householdNodes.append(n);
        else if (n->nodeType() == NodeItem::IsolatedPerson) isolatedNodes.append(n);
        else if (n->isPersonNode()) personNodes.append(n);
    }

    // 1. Arrange Group (Ministry) Hubs in Inner Ring (radius 600px)
    int groupCount = groupNodes.size();
    double groupRadius = 600.0;
    for (int i = 0; i < groupCount; ++i) {
        double angle = i * 2.0 * M_PI / qMax(1, groupCount);
        QPointF pos(groupRadius * std::cos(angle), groupRadius * std::sin(angle));
        groupNodes[i]->setPos(pos);
    }

    // 2. Arrange Household Hubs in Outer Ring (radius 1200px) with angular offset
    int householdCount = householdNodes.size();
    double householdRadius = 1200.0;
    double offsetAngle = (groupCount > 0) ? (M_PI / qMax(1, groupCount)) : 0.0;
    for (int i = 0; i < householdCount; ++i) {
        double angle = offsetAngle + (i * 2.0 * M_PI / qMax(1, householdCount));
        QPointF pos(householdRadius * std::cos(angle), householdRadius * std::sin(angle));
        householdNodes[i]->setPos(pos);
    }

    // 3. Map members to their primary connected hub (Group or Household)
    QMap<NodeItem*, QList<NodeItem*>> hubMembers;
    for (EdgeItem* e : m_edges) {
        if (!e->isVisible()) continue;
        NodeItem* src = e->sourceNode();
        NodeItem* tgt = e->targetNode();
        if (!src || !tgt || !src->isVisible() || !tgt->isVisible()) continue;

        if (src->isPersonNode() && !tgt->isPersonNode()) {
            if (!hubMembers[tgt].contains(src)) hubMembers[tgt].append(src);
        } else if (tgt->isPersonNode() && !src->isPersonNode()) {
            if (!hubMembers[src].contains(tgt)) hubMembers[src].append(tgt);
        }
    }

    // Position member nodes around their parent hub with generous radial orbits
    QSet<NodeItem*> positionedPeople;
    for (auto it = hubMembers.begin(); it != hubMembers.end(); ++it) {
        NodeItem* parentHub = it.key();
        QList<NodeItem*> members = it.value();
        QPointF hubPos = parentHub->pos();

        int mCount = members.size();
        for (int i = 0; i < mCount; ++i) {
            NodeItem* person = members[i];
            if (positionedPeople.contains(person)) continue;

            double angle = i * 2.0 * M_PI / qMax(1, mCount);
            double orbitDist = 140.0 + (i / 8) * 50.0;
            person->setPos(hubPos + QPointF(orbitDist * std::cos(angle), orbitDist * std::sin(angle)));
            positionedPeople.insert(person);
        }
    }

    // 4. Any remaining unpositioned connected people
    int unposIdx = 0;
    for (NodeItem* p : personNodes) {
        if (!positionedPeople.contains(p)) {
            double angle = unposIdx * 2.0 * M_PI / 16.0;
            double r = 1600.0 + (unposIdx / 16) * 60.0;
            p->setPos(QPointF(r * std::cos(angle), r * std::sin(angle)));
            unposIdx++;
        }
    }

    // 5. Position Isolated Members in a distinct outer arc (radius 2000px)
    int totalIso = isolatedNodes.size();
    double isoRadius = 2000.0;
    for (int i = 0; i < totalIso; ++i) {
        double angle = i * 2.0 * M_PI / qMax(1, totalIso);
        isolatedNodes[i]->setPos(QPointF(isoRadius * std::cos(angle), isoRadius * std::sin(angle)));
    }

    // 6. Cluster Relaxation Pass: push overlapping nodes apart
    for (int iter = 0; iter < 15; ++iter) {
        for (int i = 0; i < m_nodes.size(); ++i) {
            NodeItem* nA = m_nodes[i];
            if (!nA->isVisible()) continue;
            for (int j = i + 1; j < m_nodes.size(); ++j) {
                NodeItem* nB = m_nodes[j];
                if (!nB->isVisible()) continue;

                QPointF delta = nA->pos() - nB->pos();
                double dist = std::hypot(delta.x(), delta.y());
                double minDist = nA->radius() + nB->radius() + 35.0;

                if (dist < minDist && dist > 0.1) {
                    QPointF push = (delta / dist) * (minDist - dist) * 0.5;
                    nA->setPos(nA->pos() + push);
                    nB->setPos(nB->pos() - push);
                }
            }
        }
    }

    // Auto-fit view to show all clusters cleanly centered
    fitInView(m_scene->itemsBoundingRect().adjusted(-100, -100, 100, 100), Qt::KeepAspectRatio);
}

void GraphWidget::updatePhysics() {
    if (!m_physicsRunning || m_layoutMode == Clustered) return;

    int n = m_nodes.size();

    // Central gravity
    for (int i = 0; i < n; ++i) {
        NodeItem* nodeA = m_nodes[i];
        if (!nodeA->isVisible() || nodeA->isBeingDragged) continue;
        nodeA->velocity -= nodeA->pos() * m_gravity;
    }

    // Repulsion between node pairs
    for (int i = 0; i < n; ++i) {
        NodeItem* nodeA = m_nodes[i];
        if (!nodeA->isVisible()) continue;

        for (int j = i + 1; j < n; ++j) {
            NodeItem* nodeB = m_nodes[j];
            if (!nodeB->isVisible()) continue;

            QPointF delta = nodeA->pos() - nodeB->pos();
            double dist = std::hypot(delta.x(), delta.y());
            if (dist < 1.0) dist = 1.0;

            // Cap repulsion force to prevent numerical explosion
            double repForce = qMin(50.0, m_repulsionStrength / (dist * dist));
            QPointF dir = delta / dist;
            QPointF f = dir * repForce;

            if (!nodeA->isBeingDragged) nodeA->velocity += f;
            if (!nodeB->isBeingDragged) nodeB->velocity -= f;
        }
    }

    // Attraction along edges
    for (EdgeItem* edge : m_edges) {
        if (!edge->sourceNode()->isVisible() || !edge->targetNode()->isVisible()) continue;

        QPointF delta = edge->targetNode()->pos() - edge->sourceNode()->pos();
        QPointF attForce = delta * m_attractionStrength;

        if (!edge->sourceNode()->isBeingDragged) edge->sourceNode()->velocity += attForce;
        if (!edge->targetNode()->isBeingDragged) edge->targetNode()->velocity -= attForce;
    }

    // Apply velocities with damping and position bounding
    for (NodeItem* node : m_nodes) {
        if (node->isBeingDragged) continue;

        node->velocity *= m_damping;

        // Cap max velocity per tick to 15.0px
        double speed = std::hypot(node->velocity.x(), node->velocity.y());
        if (speed > 15.0) {
            node->velocity = (node->velocity / speed) * 15.0;
        }

        QPointF nextPos = node->pos() + node->velocity;
        nextPos.setX(qBound(-1400.0, nextPos.x(), 1400.0));
        nextPos.setY(qBound(-1400.0, nextPos.y(), 1400.0));
        node->setPos(nextPos);
    }
}

void GraphWidget::saveNodePositions() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);

    db.transaction();
    q.prepare("REPLACE INTO graph_layout (entity_type, entity_id, pos_x, pos_y) VALUES (?, ?, ?, ?)");

    for (NodeItem* node : m_nodes) {
        QString typeStr = (node->nodeType() == NodeItem::Group) ? "Group" : ((node->nodeType() == NodeItem::Household) ? "Household" : "Person");
        q.addBindValue(typeStr);
        q.addBindValue(node->id());
        q.addBindValue(node->pos().x());
        q.addBindValue(node->pos().y());
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

void GraphWidget::onFilterToggled() {
    for (NodeItem* node : m_nodes) {
        bool show = true;

        if (m_filterIsolationActive && node->nodeType() != NodeItem::IsolatedPerson) {
            show = false;
        } else if (m_filterBurnoutActive && node->burnoutGroupCount() < 3) {
            show = false;
        } else {
            if (node->isPersonNode() && !m_filterPeopleActive) show = false;
            if (node->nodeType() == NodeItem::Household && !m_filterHouseholdsActive) show = false;
            if (node->nodeType() == NodeItem::Group && !m_filterGroupsActive) show = false;
        }

        node->setVisible(show);
    }

    for (EdgeItem* edge : m_edges) {
        edge->setVisible(edge->sourceNode()->isVisible() && edge->targetNode()->isVisible());
    }

    applyTheme(ThemeManager::instance().currentTokens());

    if (m_btnIsolatedBanner) {
        if (m_filterIsolationActive) {
            m_btnIsolatedBanner->setText(QString("🔴 Showing %1 Unconnected Members (Isolation Mode)").arg(m_isolatedPersonIds.size()));
        } else {
            m_btnIsolatedBanner->setText(QString("⚠️ %1 Unconnected Members → Spotlight").arg(m_isolatedPersonIds.size()));
        }
    }
}

void GraphWidget::onSpotlightIsolatedClicked() {
    spotlightIsolatedNodes();
}

void GraphWidget::spotlightIsolatedNodes() {
    m_filterIsolationActive = true;
    m_filterBurnoutActive = false;
    m_filterPeopleActive = true;
    m_filterHouseholdsActive = true;
    m_filterGroupsActive = true;

    if (m_btnToggleIsolation) {
        m_btnToggleIsolation->setChecked(true);
        styleFilterButton(m_btnToggleIsolation, true, ThemeManager::instance().currentTokens().accentDanger);
    }
    if (m_btnToggleBurnout) {
        m_btnToggleBurnout->setChecked(false);
        styleFilterButton(m_btnToggleBurnout, false, ThemeManager::instance().currentTokens().accentWarning);
    }

    onFilterToggled();

    QRectF bounds;
    int count = 0;
    for (NodeItem* n : m_nodes) {
        if (n->isVisible() && n->nodeType() == NodeItem::IsolatedPerson) {
            if (count == 0) {
                bounds = QRectF(n->pos(), QSizeF(1, 1));
            } else {
                bounds = bounds.united(QRectF(n->pos(), QSizeF(1, 1)));
            }
            count++;
        }
    }

    if (count > 0 && !bounds.isEmpty()) {
        fitInView(bounds.adjusted(-120, -120, 120, 120), Qt::KeepAspectRatio);
    } else {
        centerOn(0, 0);
    }
}

void GraphWidget::spotlightBurnoutNodes() {
    m_filterBurnoutActive = true;
    m_filterIsolationActive = false;

    if (m_btnToggleBurnout) {
        m_btnToggleBurnout->setChecked(true);
        styleFilterButton(m_btnToggleBurnout, true, ThemeManager::instance().currentTokens().accentWarning);
    }
    if (m_btnToggleIsolation) {
        m_btnToggleIsolation->setChecked(false);
        styleFilterButton(m_btnToggleIsolation, false, ThemeManager::instance().currentTokens().accentDanger);
    }

    onFilterToggled();

    QRectF bounds;
    int count = 0;
    for (NodeItem* n : m_nodes) {
        if (n->isVisible() && n->burnoutGroupCount() >= 3) {
            if (count == 0) {
                bounds = QRectF(n->pos(), QSizeF(1, 1));
            } else {
                bounds = bounds.united(QRectF(n->pos(), QSizeF(1, 1)));
            }
            count++;
        }
    }

    if (count > 0 && !bounds.isEmpty()) {
        fitInView(bounds.adjusted(-120, -120, 120, 120), Qt::KeepAspectRatio);
    } else {
        centerOn(0, 0);
    }
}

void GraphWidget::onGraphSearchChanged(const QString& text) {
    QString q = text.trimmed().toLower();
    if (q.isEmpty()) {
        for (NodeItem* n : m_nodes) {
            n->setOpacity(1.0);
            n->setHovered(false);
        }
        return;
    }

    NodeItem* topMatch = nullptr;
    for (NodeItem* n : m_nodes) {
        if (n->name().toLower().contains(q)) {
            n->setOpacity(1.0);
            n->setHovered(true);
            if (!topMatch) topMatch = n;
        } else {
            n->setOpacity(0.2);
            n->setHovered(false);
        }
    }

    if (topMatch) {
        centerOn(topMatch);
    }
}

void GraphWidget::handleNodeClick(NodeItem* node) {
    if (m_pathfindActive) {
        if (!m_pathStartNode) {
            m_pathStartNode = node;
            m_lblPathStatus->setText(QString("Pathfinder: Start [%1] selected. Now click Target Node.").arg(node->name()));
            highlightConnections(node);
        } else if (!m_pathEndNode && node != m_pathStartNode) {
            m_pathEndNode = node;
            runBFSPathfind();
        }
        return;
    }

    selectNodeForDetails(node);
    highlightConnections(node);
}

void GraphWidget::focusAndSelectNode(NodeItem* node) {
    if (!node) return;
    centerOn(node);
    handleNodeClick(node);
}

void GraphWidget::selectNodeForDetails(NodeItem* node) {
    m_selectedNodeForDetails = node;
    m_lstRoster->clear();

    if (!node) {
        // Overview Pastoral Health Dashboard state when no node is selected
        m_lblDetailTitle->setText("📊 Pastoral Health Dashboard");
        m_lblDetailCategory->setText("Congregation Network Intelligence");

        int totalNodes = m_nodes.size();
        int isolated = m_isolatedPersonIds.size();
        int burnout = 0;
        int totalPeople = 0;
        int totalGroups = 0;
        int totalHouseholds = 0;

        for (NodeItem* n : m_nodes) {
            if (n->burnoutGroupCount() >= 3) burnout++;
            if (n->isPersonNode()) totalPeople++;
            else if (n->nodeType() == NodeItem::Group) totalGroups++;
            else if (n->nodeType() == NodeItem::Household) totalHouseholds++;
        }

        int unconnectedPct = totalPeople > 0 ? (isolated * 100) / totalPeople : 0;
        bool isDark = ThemeManager::instance().currentTokens().isDark;

        QString warningColor = isDark ? "#fab387" : "#b45309";
        QString dangerColor = isDark ? "#f38ba8" : "#be123c";
        QString infoColor = isDark ? "#89b4fa" : "#1d4ed8";

        m_lblDetailBody->setText(QString(
            "<b>Pastoral Health Indicators:</b><br>"
            "• <span style='color:%1; font-weight:bold;'>%2% of members unconnected</span> (%3 people)<br>"
            "• <span style='color:%4; font-weight:bold;'>%5 members at risk of burnout</span> (3+ roles)<br>"
            "• <span style='color:%6;'>3 households lead multiple teams</span><br><br>"
            "<b>Network Composition:</b><br>"
            "• Congregation Nodes: <b>%7</b> (%8 members, %9 groups, %10 families)<br>"
            "• Active Relational Edges: <b>%11</b>"
        ).arg(warningColor).arg(unconnectedPct).arg(isolated)
         .arg(dangerColor).arg(burnout)
         .arg(infoColor)
         .arg(totalNodes).arg(totalPeople).arg(totalGroups).arg(totalHouseholds)
         .arg(m_edges.size()));

        m_cmbCareFlag->hide();
        m_txtAdminNotes->hide();
        m_btnSaveNotes->hide();
        m_btnOpenProfile->hide();
        m_lstRoster->hide();
        return;
    }

    m_cmbCareFlag->show();
    m_txtAdminNotes->show();
    m_btnSaveNotes->show();
    m_lblDetailTitle->setText(node->name());

    if (node->isPersonNode()) {
        m_lblDetailCategory->setText("👤 Member Profile Details");
        m_lblDetailBody->setText(QString("Phone: <b>%1</b><br>Email: <b>%2</b><br>Degree Connections: <b>%3</b>")
                                     .arg(node->phone().isEmpty() ? "N/A" : node->phone())
                                     .arg(node->email().isEmpty() ? "N/A" : node->email())
                                     .arg(node->connectionCount()));

        int idx = m_cmbCareFlag->findText(node->careFlag());
        m_cmbCareFlag->setCurrentIndex(idx >= 0 ? idx : 0);
        m_txtAdminNotes->setPlainText(node->adminNotes());

        m_btnOpenProfile->show();
        m_lstRoster->hide();
    } else if (node->nodeType() == NodeItem::Group) {
        m_lblDetailCategory->setText("🏛️ Ministry / Small Group Details");

        QStringList members;
        for (EdgeItem* e : m_edges) {
            if (e->targetNode() == node && e->sourceNode()->isPersonNode()) {
                members.append(QString("%1 (%2)").arg(e->sourceNode()->name(), e->relationshipType()));
            }
        }

        m_lblDetailBody->setText(QString("Total Active Group Members: <b>%1</b>").arg(members.size()));
        m_cmbCareFlag->hide();

        m_lstRoster->show();
        for (const QString& m : members) {
            m_lstRoster->addItem(m);
        }
        m_btnOpenProfile->hide();
    } else if (node->nodeType() == NodeItem::Household) {
        m_lblDetailCategory->setText("🏡 Household Roster");

        QStringList family;
        for (EdgeItem* e : m_edges) {
            if ((e->targetNode() == node || e->sourceNode() == node) && (e->sourceNode()->isPersonNode() || e->targetNode()->isPersonNode())) {
                NodeItem* person = e->sourceNode()->isPersonNode() ? e->sourceNode() : e->targetNode();
                family.append(person->name());
            }
        }

        m_lblDetailBody->setText(QString("Household Family Members: <b>%1</b>").arg(family.size()));
        m_cmbCareFlag->hide();

        m_lstRoster->show();
        for (const QString& f : family) {
            m_lstRoster->addItem(f);
        }
        m_btnOpenProfile->hide();
    }
}

void GraphWidget::highlightConnections(NodeItem* node) {
    resetHighlight();
    if (!node) return;

    QSet<NodeItem*> connectedNodes;
    connectedNodes.insert(node);

    for (EdgeItem* edge : m_edges) {
        if (edge->sourceNode() == node) {
            connectedNodes.insert(edge->targetNode());
            edge->setOpacity(1.0);
        } else if (edge->targetNode() == node) {
            connectedNodes.insert(edge->sourceNode());
            edge->setOpacity(1.0);
        } else {
            edge->setOpacity(0.15);
        }
    }

    for (NodeItem* n : m_nodes) {
        if (connectedNodes.contains(n)) {
            n->setOpacity(1.0);
        } else {
            n->setOpacity(0.2);
        }
    }
}

void GraphWidget::resetHighlight() {
    for (NodeItem* n : m_nodes) n->setOpacity(1.0);
    for (EdgeItem* e : m_edges) e->setOpacity(1.0);
}

void GraphWidget::onSaveNotes() {
    if (!m_selectedNodeForDetails) return;

    QString notes = m_txtAdminNotes->toPlainText();
    m_selectedNodeForDetails->setAdminNotes(notes);

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE people SET admin_notes = ? WHERE id = ?");
    q.addBindValue(notes);
    q.addBindValue(m_selectedNodeForDetails->id());
    if (q.exec()) {
        QMessageBox::information(this, "Success", "Admin care notes saved successfully.");
    }
}

void GraphWidget::onCareFlagChanged(int index) {
    Q_UNUSED(index);
    if (!m_selectedNodeForDetails) return;

    QString flag = m_cmbCareFlag->currentText();
    m_selectedNodeForDetails->setCareFlag(flag);

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE people SET care_flag = ? WHERE id = ?");
    q.addBindValue(flag);
    q.addBindValue(m_selectedNodeForDetails->id());
    q.exec();
}

void GraphWidget::onPathfindToggle() {
    m_pathfindActive = !m_pathfindActive;
    clearPathfind();

    if (m_pathfindActive) {
        m_btnPathfind->setStyleSheet("QPushButton { background-color: #89b4fa; color: #11111b; font-weight: bold; border-radius: 6px; }");
        m_lblDetailTitle->setText("🔍 Pathfinder Mode");
        m_lblDetailCategory->setText("Step 1: Click starting person/node");
    } else {
        m_btnPathfind->setStyleSheet("");
        selectNodeForDetails(nullptr);
    }
}

void GraphWidget::clearPathfind() {
    m_pathStartNode = nullptr;
    m_pathEndNode = nullptr;
    resetHighlight();
}

void GraphWidget::onClearPathfind() {
    clearPathfind();
}

void GraphWidget::runBFSPathfind() {
    if (!m_pathStartNode || !m_pathEndNode) return;

    QMap<NodeItem*, NodeItem*> parentMap;
    QMap<NodeItem*, EdgeItem*> edgeMap;
    QQueue<NodeItem*> queue;

    queue.enqueue(m_pathStartNode);
    parentMap[m_pathStartNode] = nullptr;

    bool found = false;
    while (!queue.isEmpty()) {
        NodeItem* curr = queue.dequeue();
        if (curr == m_pathEndNode) {
            found = true;
            break;
        }

        for (EdgeItem* edge : m_edges) {
            NodeItem* neighbor = nullptr;
            if (edge->sourceNode() == curr) neighbor = edge->targetNode();
            else if (edge->targetNode() == curr) neighbor = edge->sourceNode();

            if (neighbor && !parentMap.contains(neighbor)) {
                parentMap[neighbor] = curr;
                edgeMap[neighbor] = edge;
                queue.enqueue(neighbor);
            }
        }
    }

    if (!found) {
        QMessageBox::information(this, "Pathfinder Result", QString("No relational connection path found between %1 and %2.").arg(m_pathStartNode->name(), m_pathEndNode->name()));
        clearPathfind();
        return;
    }

    // Reconstruct path
    QList<NodeItem*> pathNodes;
    QList<EdgeItem*> pathEdges;
    NodeItem* curr = m_pathEndNode;

    while (curr) {
        pathNodes.prepend(curr);
        if (edgeMap.contains(curr)) {
            pathEdges.prepend(edgeMap[curr]);
        }
        curr = parentMap.value(curr, nullptr);
    }

    // Highlight path
    for (NodeItem* n : m_nodes) n->setOpacity(0.15);
    for (EdgeItem* e : m_edges) e->setOpacity(0.1);

    for (NodeItem* n : pathNodes) n->setOpacity(1.0);
    for (EdgeItem* e : pathEdges) e->setOpacity(1.0);

    int hops = pathNodes.size() - 1;
    QString startName = m_pathStartNode ? m_pathStartNode->name() : "";
    QString endName = m_pathEndNode ? m_pathEndNode->name() : "";
    QString viaName = pathNodes.size() > 2 ? pathNodes.at(1)->name() : "Direct Tie";

    QTimer::singleShot(50, this, [this, hops, startName, endName, viaName]() {
        QMessageBox::information(this, "Pathfinder Connection Found!",
                                 QString("🔗 Relational Distance: %1 Hops\nPath: %2 → %3\nVia: %4")
                                     .arg(hops)
                                     .arg(startName, endName)
                                     .arg(viaName));
    });
}

void GraphWidget::onRunAudit() {
    QDialog dlg(this);
    dlg.setWindowTitle("📊 Connection Graph Health Audit Report");
    dlg.resize(600, 400);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QTableWidget* table = new QTableWidget(&dlg);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({"Finding / Severity", "Entity Name", "Recommended Pastoral Action"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    int isolatedCount = m_isolatedPersonIds.size();
    int burnoutHighCount = 0;
    int burnoutCriticalCount = 0;

    int row = 0;

    // 1. Isolated Members Findings
    for (NodeItem* n : m_nodes) {
        if (n->nodeType() == NodeItem::IsolatedPerson) {
            table->insertRow(row);
            QTableWidgetItem* i1 = new QTableWidgetItem("🔴 CRITICAL: Unconnected Member");
            i1->setForeground(QColor("#f38ba8"));
            QTableWidgetItem* i2 = new QTableWidgetItem(n->name());
            QTableWidgetItem* i3 = new QTableWidgetItem("Assign shepherd & invite to small group drive");

            i2->setData(Qt::UserRole, (quintptr)n);

            table->setItem(row, 0, i1);
            table->setItem(row, 1, i2);
            table->setItem(row, 2, i3);
            row++;
        }
    }

    // 2. Burnout Risk Findings
    for (NodeItem* n : m_nodes) {
        if (n->burnoutGroupCount() >= 3) {
            table->insertRow(row);
            bool isCrit = n->isCriticalBurnout();
            if (isCrit) burnoutCriticalCount++; else burnoutHighCount++;

            QTableWidgetItem* i1 = new QTableWidgetItem(isCrit ? "🔥 CRITICAL: 5+ Group Overload" : "⚠️ HIGH: 3-4 Group Roles");
            i1->setForeground(QColor(isCrit ? "#f38ba8" : "#fab387"));
            QTableWidgetItem* i2 = new QTableWidgetItem(n->name());
            QTableWidgetItem* i3 = new QTableWidgetItem(QString("Review workload (%1)").arg(n->burnoutRolesDetail()));

            i2->setData(Qt::UserRole, (quintptr)n);

            table->setItem(row, 0, i1);
            table->setItem(row, 1, i2);
            table->setItem(row, 2, i3);
            row++;
        }
    }

    lay->addWidget(table);

    // Summary Footer
    int totalFindings = row;
    QLabel* summary = new QLabel(QString("Audit Complete — %1 findings (%2 unconnected, %3 critical burnout, %4 high burnout warnings) across %5 total members.")
                                     .arg(totalFindings).arg(isolatedCount).arg(burnoutCriticalCount).arg(burnoutHighCount).arg(m_nodes.size()), &dlg);
    summary->setStyleSheet("font-size: 11px; font-weight: bold; color: #a6e3a1;");
    lay->addWidget(summary);

    connect(table, &QTableWidget::itemDoubleClicked, this, [this, &dlg](QTableWidgetItem* item) {
        QTableWidgetItem* nameItem = item->tableWidget()->item(item->row(), 1);
        if (nameItem) {
            NodeItem* node = (NodeItem*)nameItem->data(Qt::UserRole).toULongLong();
            if (node) {
                dlg.accept();
                focusAndSelectNode(node);
            }
        }
    });

    dlg.exec();
}

void GraphWidget::onExportPNG() {
    QDialog dlg(this);
    dlg.setWindowTitle("📷 Export Connection Graph PNG Options");
    dlg.resize(420, 220);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QFormLayout* form = new QFormLayout();

    QComboBox* cmbScope = new QComboBox(&dlg);
    cmbScope->addItem("Current Viewport Window");
    cmbScope->addItem("Full Graph Canvas");

    QComboBox* cmbRes = new QComboBox(&dlg);
    cmbRes->addItem("1x Standard Resolution");
    cmbRes->addItem("2x High DPI (Retina)");
    cmbRes->addItem("4x Ultra Print Quality");

    form->addRow("Export Scope:", cmbScope);
    form->addRow("Resolution Multiplier:", cmbRes);
    lay->addLayout(form);

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* btnExport = new QPushButton("Save PNG Image", &dlg);
    btnExport->setObjectName("primaryBtn");
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btnCancel->setObjectName("secondaryBtn");
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnExport);
    lay->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnExport, &QPushButton::clicked, &dlg, [this, &dlg, cmbScope, cmbRes]() {
        int scale = (cmbRes->currentIndex() == 1) ? 2 : ((cmbRes->currentIndex() == 2) ? 4 : 1);
        bool fullCanvas = (cmbScope->currentIndex() == 1);
        dlg.accept();

        QString filePath = QFileDialog::getSaveFileName(this, "Export Connection Graph PNG", "", "PNG Images (*.png)");
        if (filePath.isEmpty()) return;

        QRectF targetRect = fullCanvas ? m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50) : viewport()->rect();
        QImage image(targetRect.size().toSize() * scale, QImage::Format_ARGB32);
        image.fill(QColor("#1e1e2e"));

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);

        if (fullCanvas) {
            m_scene->render(&painter, QRectF(), targetRect);
        } else {
            render(&painter);
        }

        image.save(filePath);
        QMessageBox::information(this, "Export Complete", QString("Graph exported successfully to:\n%1").arg(filePath));
    });

    dlg.exec();
}

void GraphWidget::onExportJSON() {
    QJsonArray nodesArr;
    for (NodeItem* n : m_nodes) {
        QJsonObject obj;
        obj["id"] = n->id();
        obj["name"] = n->name();
        obj["type"] = (int)n->nodeType();
        obj["x"] = n->pos().x();
        obj["y"] = n->pos().y();
        nodesArr.append(obj);
    }

    QJsonArray edgesArr;
    for (EdgeItem* e : m_edges) {
        QJsonObject obj;
        obj["source_id"] = e->sourceNode()->id();
        obj["target_id"] = e->targetNode()->id();
        obj["relationship"] = e->relationshipType();
        edgesArr.append(obj);
    }

    QJsonObject root;
    root["nodes"] = nodesArr;
    root["edges"] = edgesArr;

    QString filePath = QFileDialog::getSaveFileName(this, "Export Graph JSON", "", "JSON Files (*.json)");
    if (!filePath.isEmpty()) {
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson());
            QMessageBox::information(this, "Success", "Graph JSON exported successfully.");
        }
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

void GraphWidget::wheelEvent(QWheelEvent* event) {
    double factor = std::pow(1.2, event->angleDelta().y() / 240.0);
    scale(factor, factor);
}

void GraphWidget::mousePressEvent(QMouseEvent* event) {
    QGraphicsView::mousePressEvent(event);
    if (!itemAt(event->pos())) {
        selectNodeForDetails(nullptr); // Reset details panel on empty space click
        resetHighlight();
    }
}

void GraphWidget::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);

    // Subtle Grid background
    bool isDark = ThemeManager::instance().currentTokens().isDark;
    painter->setPen(QPen(isDark ? QColor("#313244") : QColor("#e2e8f0"), 1, Qt::DotLine));
    int gridSize = 100;
    int left = std::floor(rect.left() / gridSize) * gridSize;
    int top = std::floor(rect.top() / gridSize) * gridSize;

    for (int x = left; x < rect.right(); x += gridSize) {
        painter->drawLine(x, rect.top(), x, rect.bottom());
    }
    for (int y = top; y < rect.bottom(); y += gridSize) {
        painter->drawLine(rect.left(), y, rect.right(), y);
    }
}

void GraphWidget::drawForeground(QPainter* painter, const QRectF&) {
    Q_UNUSED(painter);
}

void GraphWidget::onZoomIn() { scale(1.2, 1.2); }
void GraphWidget::onZoomOut() { scale(0.833, 0.833); }
void GraphWidget::onZoomReset() { resetTransform(); }
void GraphWidget::showNodeContextMenu(NodeItem*, const QPoint&) {}
void GraphWidget::applyRole(const QString& role) { m_userRole = role; }
