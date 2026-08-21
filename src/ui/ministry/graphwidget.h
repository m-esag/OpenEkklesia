#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QTimer>
#include <QPointF>
#include <QVector>
#include <QPushButton>
#include <QLabel>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QColor>
#include <QSet>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include "thememanager.h"

class NodeItem;
class EdgeItem;
class GraphWidget;

class NodeItem : public QGraphicsItem {
public:
    enum Type { Person, Group, Household, IsolatedPerson, Visitor };

    NodeItem(int id, const QString& name, Type type);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    Type nodeType() const { return m_type; }
    bool isPersonNode() const { return m_type == Person || m_type == IsolatedPerson || m_type == Visitor; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setWarning(bool warning, const QString& warningMsg);
    bool isWarning() const { return m_isWarning; }
    QString warningMsg() const { return m_warningMsg; }

    // Burnout & Severity
    void setBurnoutDetails(int groupCount, bool isCritical, const QString& rolesDetail);
    int burnoutGroupCount() const { return m_burnoutGroupCount; }
    bool isCriticalBurnout() const { return m_isCriticalBurnout; }
    QString burnoutRolesDetail() const { return m_burnoutRolesDetail; }

    // Family Concentration
    void setFamilyConcentration(const QString& msg) { m_familyConcentrationMsg = msg; }
    QString familyConcentration() const { return m_familyConcentrationMsg; }

    void setConnectionCount(int count) { m_connectionCount = count; }
    int connectionCount() const { return m_connectionCount; }
    double radius() const { return (m_type == Group) ? 30.0 + (m_connectionCount * 1.5) : 20.0 + (m_connectionCount * 1.5); }

    void setCategoryColor(const QColor& c) { m_categoryColor = c; QGraphicsItem::update(); }
    QColor categoryColor() const { return m_categoryColor; }

    QString careFlag() const { return m_careFlag; }
    void setCareFlag(const QString& cf) { m_careFlag = cf; }
    QString adminNotes() const { return m_adminNotes; }
    void setAdminNotes(const QString& notes) { m_adminNotes = notes; }

    void setHovered(bool h) { m_isHovered = h; QGraphicsItem::update(); }
    bool isHovered() const { return m_isHovered; }

    void setThresholdLabeling(bool enabled) { m_thresholdLabeling = enabled; }
    bool thresholdLabeling() const { return m_thresholdLabeling; }

    void setContactInfo(const QString& phone, const QString& email) { m_phone = phone; m_email = email; }
    void setServingInfo(const QString& role, const QString& lastServed) { m_servingRole = role; m_lastServedDate = lastServed; }
    QString phone() const { return m_phone; }
    QString email() const { return m_email; }
    QString servingRole() const { return m_servingRole; }
    QString lastServedDate() const { return m_lastServedDate; }

    bool isHygieneComplete() const { return !m_phone.trimmed().isEmpty() && !m_email.trimmed().isEmpty(); }
    bool isHygieneWarning() const { return (!m_phone.trimmed().isEmpty() && m_email.trimmed().isEmpty()) || (m_phone.trimmed().isEmpty() && !m_email.trimmed().isEmpty()); }
    bool isHygieneCritical() const { return m_phone.trimmed().isEmpty() && m_email.trimmed().isEmpty(); }

    enum HealthStatus { HealthNormal, FullyStaffed, Understaffed, CriticalShortage };
    void setHealthStatus(HealthStatus hs) { m_healthStatus = hs; QGraphicsItem::update(); }
    HealthStatus healthStatus() const { return m_healthStatus; }

    QPointF velocity;
    bool isBeingDragged;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    int m_id;
    QString m_name;
    Type m_type;
    bool m_isWarning;
    QString m_warningMsg;
    int m_burnoutGroupCount;
    bool m_isCriticalBurnout;
    QString m_burnoutRolesDetail;
    QString m_familyConcentrationMsg;
    int m_connectionCount;
    QString m_careFlag;
    QString m_adminNotes;
    bool m_isHovered;
    bool m_thresholdLabeling;
    QString m_phone;
    QString m_email;
    QString m_servingRole;
    QString m_lastServedDate;
    QPointF m_dragStartPos;
    HealthStatus m_healthStatus;
    QColor m_categoryColor;
};

class EdgeItem : public QGraphicsItem {
public:
    EdgeItem(NodeItem* source, NodeItem* target, const QString& relType, int relId = 0, bool isBurnoutLink = false);

    NodeItem* sourceNode() const { return m_source; }
    NodeItem* targetNode() const { return m_target; }
    QString relationshipType() const { return m_relType; }
    void setRelationshipType(const QString& rt) { m_relType = rt; update(); }
    int relationshipId() const { return m_relId; }
    void setRelationshipId(int id) { m_relId = id; }
    bool isBurnoutLink() const { return m_isBurnoutLink; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override; // 12px wide hitbox buffer for easy clicking!
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setSelectedState(bool sel) { m_isSelected = sel; update(); }
    bool isSelectedState() const { return m_isSelected; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    NodeItem* m_source;
    NodeItem* m_target;
    QString m_relType;
    int m_relId;
    bool m_isBurnoutLink;
    bool m_isHovered;
    bool m_isSelected;
};

class GraphCanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphCanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);
    void setGraphWidget(GraphWidget* gw) { m_graphWidget = gw; }

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    GraphWidget* m_graphWidget = nullptr;
};

class GraphWidget : public QWidget {
    Q_OBJECT

signals:
    void navigateToProfile(const QString& entityType, int entityId);

public:
    enum LayoutMode { ForceDirected, Clustered };

    explicit GraphWidget(QWidget* parent = nullptr);
    ~GraphWidget();

    void loadGraph();
    void pausePhysics();
    void resumePhysics();
    void setRepulsionStrength(double val);

    void highlightConnections(NodeItem* node);
    void resetHighlight();

    void handleNodeClick(NodeItem* node);
    void selectNodeForDetails(NodeItem* node);
    void selectEdgeForDetails(EdgeItem* edge);
    void showNodeContextMenu(NodeItem* node, const QPoint& globalPos);
    void applyRole(const QString& role);
    void applyTheme(const ThemeTokens& tokens);

    void focusAndSelectNode(NodeItem* node);
    void spotlightIsolatedNodes();
    void spotlightBurnoutNodes();
    void spotlightHygieneNodes();

    bool isHygieneActive() const { return m_filterHygieneActive; }
    QGraphicsScene* scene() const { return m_scene; }
    GraphCanvasView* canvasView() const { return m_canvasView; }

private slots:
    void updatePhysics();
    void onFilterToggled();
    void onPathfindToggle();
    void onClearPathfind();
    void onSaveNotes();
    void onCareFlagChanged(int index);
    void onExportPNG();
    void onRunAudit();
    void onGraphSearchChanged(const QString& text);
    void onExportJSON();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onToggleLegend();
    void onToggleLayoutMode();
    void onSpotlightIsolatedClicked();
    void onToggleDataHygiene();
    void onToggleCollapseDock();
    void onSeverSelectedEdge();
    void onEdgeRoleChanged(int index);
    void onRosterItemClicked(QListWidgetItem* item);
    void onCopyContactInfo(NodeItem* node, const QString& type);
    void onQuickAssignGroup(NodeItem* node);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    void applyForceDirectedLayout();
    void applyClusteredLayout();
    void runBFSPathfind();
    void updateIsolationQuery();
    void clearPathfind();
    void saveNodePositions();
    void loadNodePositions();
    void styleFilterButton(QPushButton* btn, bool active, const QColor& activeColor);
    void updateDetailsPanelForEmpty();

    QSplitter* m_splitter;
    GraphCanvasView* m_canvasView;
    QGraphicsScene* m_scene;
    QTimer* m_timer;
    QList<NodeItem*> m_nodes;
    QList<EdgeItem*> m_edges;

    // Top Controls
    QWidget* m_topBarWidget;
    QPushButton* m_btnTogglePeople;
    QPushButton* m_btnToggleHouseholds;
    QPushButton* m_btnToggleGroups;
    QPushButton* m_btnToggleBurnout;
    QPushButton* m_btnToggleIsolation;
    QPushButton* m_btnToggleHygiene;
    QPushButton* m_btnToggleLayout;
    QPushButton* m_btnIsolatedBanner;
    QPushButton* m_btnLegendToggle;
    QLineEdit* m_graphSearchEdit;
    QPushButton* m_btnPathfind;
    QPushButton* m_btnClearPath;
    QLabel* m_lblPathStatus;
    QPushButton* m_btnRunAudit;
    QPushButton* m_btnExportPNG;
    QPushButton* m_btnExportJSON;

    // Filter States
    bool m_filterPeopleActive;
    bool m_filterHouseholdsActive;
    bool m_filterGroupsActive;
    bool m_filterBurnoutActive;
    bool m_filterIsolationActive;
    bool m_filterHygieneActive;
    LayoutMode m_layoutMode;

    // Collapsible Legend & Minimap
    QWidget* m_legendOverlay;
    QGraphicsView* m_minimapView;
    void updateMinimap();

    // Pathfinder State
    bool m_pathfindActive;
    NodeItem* m_pathStartNode;
    NodeItem* m_pathEndNode;
    QList<NodeItem*> m_pathHighlightedNodes;
    QList<EdgeItem*> m_pathHighlightedEdges;

    // Right Contextual Inspector Dock (Dual-Panel)
    QWidget* m_detailsDock;
    QPushButton* m_btnCollapseDock;
    QScrollArea* m_detailsScrollArea;
    QStackedWidget* m_detailsStack;

    // Page 0: Person Details
    QWidget* m_pagePerson;
    QLabel* m_lblPersonAvatar;
    QLabel* m_lblPersonName;
    QLabel* m_lblPersonStatus;
    QLabel* m_lblPersonContact;
    QPushButton* m_btnCopyPhone;
    QPushButton* m_btnCopyEmail;
    QComboBox* m_cmbCareFlag;
    QLabel* m_lblPersonGroups;
    QPlainTextEdit* m_txtAdminNotes;
    QPushButton* m_btnSaveNotes;
    QPushButton* m_btnOpenProfile;

    // Page 1: Group Details
    QWidget* m_pageGroup;
    QLabel* m_lblGroupName;
    QLabel* m_lblGroupCategory;
    QLabel* m_lblGroupHealth;
    QLabel* m_lblGroupMemberCount;
    QListWidget* m_lstRoster;
    QPushButton* m_btnGroupAddMember;
    QPushButton* m_btnGroupCopyEmails;

    // Page 2: Edge / Relationship Details
    QWidget* m_pageEdge;
    QLabel* m_lblEdgeTitle;
    QLabel* m_lblEdgeBetween;
    QComboBox* m_cmbEdgeRole;
    QPushButton* m_btnSeverEdge;

    // Page 3: Empty / Data Hygiene Action Dashboard
    QWidget* m_pageEmpty;
    QLabel* m_lblHygieneMissingPhone;
    QLabel* m_lblHygieneMissingEmail;
    QLabel* m_lblHygieneIsolated;
    QPushButton* m_btnSpotlightIncomplete;

    NodeItem* m_selectedNodeForDetails;
    EdgeItem* m_selectedEdgeForDetails;
    QString m_userRole;

    // Data Sets
    QSet<int> m_isolatedPersonIds;
    QMap<int, QString> m_burnoutDetailsMap;

    double m_repulsionStrength;
    double m_attractionStrength;
    double m_damping;
    double m_gravity;
    bool m_physicsRunning;

    int m_burnoutThreshold = 3;
    int m_familyDominancePercent = 50;
};

#endif // GRAPHWIDGET_H
