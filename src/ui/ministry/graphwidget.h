#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointF>
#include <QVector>
#include <QPushButton>
#include <QLabel>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QKeyEvent>
#include <QColor>
#include <QSet>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QPinchGesture>
#include "thememanager.h"

class NodeItem;
class EdgeItem;
class GhostEdgeItem;
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

    // Relational Broker / Bridge Node
    void setBridgeBroker(bool isBroker, const QStringList& bridgedGroups) {
        m_isBridgeBroker = isBroker;
        m_bridgedGroups = bridgedGroups;
        QGraphicsItem::update();
    }
    bool isBridgeBroker() const { return m_isBridgeBroker; }
    QStringList bridgedGroups() const { return m_bridgedGroups; }

    // Family Concentration
    void setFamilyConcentration(const QString& msg) { m_familyConcentrationMsg = msg; QGraphicsItem::update(); }
    QString familyConcentration() const { return m_familyConcentrationMsg; }

    void setConnectionCount(int count) { m_connectionCount = count; }
    int connectionCount() const { return m_connectionCount; }
    double radius() const { return (m_type == Group) ? 30.0 + (m_connectionCount * 1.5) : 20.0 + (m_connectionCount * 1.5); }

    void setCategoryColor(const QColor& c) { m_categoryColor = c; QGraphicsItem::update(); }
    QColor categoryColor() const { return m_categoryColor; }
    // Status-cluster view: overrides the person border color while active.
    void setStatusColor(const QColor& c) { m_statusColor = c; QGraphicsItem::update(); }
    QColor statusColor() const { return m_statusColor; }
    void clearStatusColor() { m_statusColor = QColor(); QGraphicsItem::update(); }

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

    bool isPinned() const { return m_isPinned; }
    void setPinned(bool pinned) { m_isPinned = pinned; QGraphicsItem::update(); }

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
    bool m_isBridgeBroker = false;
    QStringList m_bridgedGroups;
    QString m_familyConcentrationMsg;
    int m_connectionCount;
    QString m_careFlag;
    QString m_adminNotes;
    bool m_isHovered;
    bool m_thresholdLabeling;
    bool m_isPinned = false;
    QString m_phone;
    QString m_email;
    QString m_servingRole;
    QString m_lastServedDate;
    QPointF m_dragStartPos;
    HealthStatus m_healthStatus;
    QColor m_categoryColor;
    QColor m_statusColor;
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
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void adjust();

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

class GhostEdgeItem : public QGraphicsItem {
public:
    GhostEdgeItem(NodeItem* source, NodeItem* target, const QString& matchReason, int matchPct, bool isSolid = false);

    NodeItem* sourceNode() const { return m_source; }
    NodeItem* targetNode() const { return m_target; }
    QString matchReason() const { return m_matchReason; }
    int matchPct() const { return m_matchPct; }
    bool isSolid() const { return m_isSolid; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void adjust();

private:
    NodeItem* m_source;
    NodeItem* m_target;
    QString m_matchReason;
    int m_matchPct;
    bool m_isSolid;
};

class GraphCanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphCanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);
    void setGraphWidget(GraphWidget* gw) { m_graphWidget = gw; }
    void setSelectMode(bool selectMode);

protected:
    bool event(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool handlePinchGesture(QPinchGesture* pinch);
    GraphWidget* m_graphWidget = nullptr;
    bool m_selectMode = false;
    double m_pinchStartZoom = 1.0;
};

class GraphWidget : public QWidget {
    Q_OBJECT
    friend class GraphCanvasView;

signals:
    void navigateToProfile(const QString& entityType, int entityId);

public:
    enum LayoutMode { ForceDirected, Clustered, TreeHierarchy };

    explicit GraphWidget(QWidget* parent = nullptr);
    ~GraphWidget();

    void loadGraph();
    // Reloads only when underlying data changed since the last full load
    // (DatabaseManager::dataGeneration) or the last load is older than the
    // staleness fallback. Makes graph-page re-entry instant; the fallback
    // bounds staleness for writes that never bump the generation.
    void loadGraphIfDirty();
    void pausePhysics();
    void resumePhysics();
    bool physicsRunning() const;
    void setRepulsionStrength(double val);
    void applyTreeHierarchyLayout();

    void highlightConnections(NodeItem* node);
    void resetHighlight();

    void handleNodeClick(NodeItem* node);
    void selectNodeForDetails(NodeItem* node);
    void selectEdgeForDetails(EdgeItem* edge);
    void updateEdgesForNode(NodeItem* node);
    void showNodeContextMenu(NodeItem* node, const QPoint& globalPos);
    void applyRole(const QString& role);
    void applyTheme(const ThemeTokens& tokens);

    void focusAndSelectNode(NodeItem* node);
    void spotlightIsolatedNodes();
    void spotlightBurnoutNodes();
    void spotlightHygieneNodes();
    void spotlightBridgeNodes();
    void spotlightFamilyDynasties();
    void clearSceneSelection();
    void selectNeighborInDirection(const QPointF& dir);
    void resolveNodeOverlaps(); // test hook: separates stacked visible nodes

    bool isHygieneActive() const { return m_filterHygieneActive; }
    bool isBridgeActive() const { return m_filterBridgeActive; }
    bool isFamilyActive() const { return m_filterFamilyActive; }
    bool isPhysicsRunning() const { return m_physicsRunning; }
    QGraphicsScene* scene() const { return m_scene; }
    GraphCanvasView* canvasView() const { return m_canvasView; }

    void refreshHierarchyTree();

    void computeSmartBridge(NodeItem* selectedNode);
    void clearGhostEdges();

    void setConnectorSizeLevel(int level);
    int connectorSizeLevel() const { return m_connectorSizeLevel; }
    void setShowConnectors(bool show);
    bool showConnectors() const { return m_showConnectors; }
    void toggleConnectors();
    void toggleInspectorDock();

public slots:
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onToggleConnectors();
    void onConnectorSizeLevelChanged(int level);

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
    void onToggleSelectMode();
    void onMinistryBranchChanged(int index);
    void onSceneSelectionChanged();
    void onBatchCopyEmails();
    void onBatchCopyPhones();
    void onBatchExportCSV();
    void onBatchAssignGroup();
    void onConnectSmartBridge(int personId, int groupId, const QString& role, int score = 0, const QStringList& warnings = {});
    void onGroupSuggest();
    void onLogContact();
    void onUndoSever();
    void onToggleStatusCluster();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void restyleChrome(const ThemeTokens& tok);
    void styleChromeWidgets(const ThemeTokens& tok);
    void restoreFilterState();
    void applyForceDirectedLayout();
    void applyClusteredLayout();
    void runBFSPathfind();
    void updateIsolationQuery();
    void clearPathfind();
    void saveNodePositions();
    void loadNodePositions();
    void styleFilterButton(QPushButton* btn, bool active, const QColor& activeColor);
    void updateDetailsPanelForEmpty();
    void refreshMinistryBranches();
    void updateFloatingControlsPosition();

    QSplitter* m_splitter;
    GraphCanvasView* m_canvasView;
    QGraphicsScene* m_scene;
    QTimer* m_timer;
    QList<NodeItem*> m_nodes;
    QList<EdgeItem*> m_edges;
    QList<GhostEdgeItem*> m_ghostEdges;

    // Top Controls
    QWidget* m_topBarWidget;
    QComboBox* m_cmbMinistryBranch;
    QPushButton* m_btnToggleHierarchyTree;
    QPushButton* m_btnToggleSelectMode;
    QPushButton* m_btnTogglePeople;
    QPushButton* m_btnToggleHouseholds;
    QPushButton* m_btnToggleGroups;
    QPushButton* m_btnToggleBurnout;
    QPushButton* m_btnToggleIsolation;
    QPushButton* m_btnToggleHygiene;
    QPushButton* m_btnToggleBridgeNodes;
    QPushButton* m_btnToggleFamilyDynasty;
    QPushButton* m_btnToggleLayout;
    QPushButton* m_btnToggleFreeze;
    QPushButton* m_btnToggleStatusCluster;
    QPushButton* m_btnIsolatedBanner;
    QPushButton* m_btnLegendToggle;
    QLineEdit* m_graphSearchEdit;
    QPushButton* m_btnPathfind;
    QPushButton* m_btnClearPath;
    QLabel* m_lblPathStatus;
    QPushButton* m_btnRunAudit;
    QPushButton* m_btnExportPNG;
    QPushButton* m_btnExportJSON;

    // Zoom controls live in the floating bar (bottom-right of the canvas);
    // the top-bar trio was removed to avoid duplicate controls.

    // Floating Glass Canvas Zoom Overlay (Bottom-Right)
    QWidget* m_floatingZoomBar;

    // Floating Glass Canvas View Options Overlay (Bottom-Left)
    QWidget* m_floatingViewOptionsBar;
    QPushButton* m_btnToggleInspectorDock;
    QPushButton* m_btnConnThin;
    QPushButton* m_btnConnNormal;
    QPushButton* m_btnConnBold;
    QPushButton* m_btnToggleConnectors;
    QPushButton* m_btnFloatingExpandDock;

    // View Options State
    int m_connectorSizeLevel = 2; // 1 = Thin (0.6x), 2 = Normal (1.0x), 3 = Bold (1.8x)
    bool m_showConnectors = true;

    // Filter States
    bool m_filterPeopleActive;
    bool m_filterHouseholdsActive;
    bool m_filterGroupsActive;
    bool m_filterBurnoutActive;
    bool m_filterIsolationActive;
    bool m_filterHygieneActive;
    bool m_filterBridgeActive;
    bool m_filterFamilyActive;
    bool m_isSelectMode;
    bool m_statusClustering = false; // recolor person nodes by membership status
    int m_settleTicks = 0;           // consecutive low-movement ticks for auto-freeze
    double m_lastMaxDisplacement = 1.0;
    LayoutMode m_layoutMode;
    int m_selectedMinistryParentId;

    // Collapsible Legend & Minimap
    QWidget* m_legendOverlay;

    // Pathfinder State
    bool m_pathfindActive;
    NodeItem* m_pathStartNode;
    NodeItem* m_pathEndNode;
    QList<NodeItem*> m_pathHighlightedNodes;
    QList<EdgeItem*> m_pathHighlightedEdges;

    // Right Contextual Inspector Dock (Dual-Panel)
    QWidget* m_detailsDock;
    QPushButton* m_btnCollapseDock;
    QTabWidget* m_dockTabs;
    QScrollArea* m_detailsScrollArea;
    QStackedWidget* m_detailsStack;

    // Tab 2: Hierarchy Tree Explorer
    QWidget* m_hierarchyTab;
    QLineEdit* m_txtHierarchySearch;
    QTreeWidget* m_hierarchyTreeWidget;
    QPushButton* m_btnExpandAllHierarchy;
    QPushButton* m_btnCollapseAllHierarchy;

    // Page 0: Person Details
    QWidget* m_pagePerson;
    QLabel* m_lblPersonAvatar;
    QLabel* m_lblPersonName;
    QLabel* m_lblPersonStatus;
    QLabel* m_lblPersonStats;        // age · membership · household
    QLabel* m_lblPersonAttendance;   // 90-day attendance pulse
    QLabel* m_lblPersonCareRecent;   // recent pastoral activities
    QLabel* m_lblPersonContact;
    QPushButton* m_btnCopyPhone;
    QPushButton* m_btnCopyEmail;
    QComboBox* m_cmbCareFlag;
    QLabel* m_lblPersonGroups;
    QLabel* m_lblPersonBrokerBadge;
    QPlainTextEdit* m_txtAdminNotes;
    QPushButton* m_btnSaveNotes;
    QPushButton* m_btnOpenProfile;
    QWidget* m_smartBridgeSection;
    QVBoxLayout* m_smartBridgeLayout;

    // Page 1: Group Details
    QWidget* m_pageGroup;
    QLabel* m_lblGroupName;
    QLabel* m_lblGroupCategory;
    QLabel* m_lblGroupRecruit;       // recruitment status badge
    QLabel* m_lblGroupMeta;          // category · capacity · meeting
    QLabel* m_lblGroupComposition;   // archetype composition summary
    QLabel* m_lblGroupHealth;
    QLabel* m_lblGroupFamilyWarning;
    QLabel* m_lblGroupMemberCount;
    QListWidget* m_lstRoster;
    QPushButton* m_btnGroupAddMember;
    QPushButton* m_btnGroupCopyEmails;
    QPushButton* m_btnGroupSuggest;
    QLabel* m_lblGroupTasks;

    // Page 0: Person Details — reconnect workflow
    QPushButton* m_btnLogContact;
    QLabel* m_lblLogContactStatus;

    // Undo-one-sever (in-memory, this session)
    QPushButton* m_btnUndoSever;
    bool m_hasUndoSever = false;
    int m_undoRelId = 0;
    QString m_undoRelType;
    QString m_undoSrcType, m_undoTgtType;
    int m_undoSrcId = 0, m_undoTgtId = 0;

    // Smart Bridge recommendation state (Show More expansion).
    NodeItem* m_smartBridgeSourceNode = nullptr;
    bool m_smartBridgeShowAll = false;

    // Page 2: Edge / Relationship Details
    QWidget* m_pageEdge;
    QLabel* m_lblEdgeTitle;
    QLabel* m_lblEdgeBetween;
    QComboBox* m_cmbEdgeRole;
    QPushButton* m_btnSeverEdge;
    QLabel* m_lblEdgeSource;      // connection_source + joined_date
    QPushButton* m_btnEdgeOpenLeft;
    QPushButton* m_btnEdgeOpenRight;

    // Page 3: Empty / Data Hygiene Action Dashboard
    QWidget* m_pageEmpty;
    QLabel* m_lblHygieneMissingPhone;
    QLabel* m_lblHygieneMissingEmail;
    QLabel* m_lblHygieneIsolated;
    QLabel* m_lblHygieneBridgeCount;
    QPushButton* m_btnSpotlightIncomplete;

    // Page 4: Batch Selection Dashboard (Rubber-Band Multi-Select)
    QWidget* m_pageBatch;
    QLabel* m_lblBatchSummary;
    QListWidget* m_lstBatchMembers;
    QPushButton* m_btnBatchEmails;
    QPushButton* m_btnBatchPhones;
    QPushButton* m_btnBatchCSV;
    QPushButton* m_btnBatchAssign;

    NodeItem* m_selectedNodeForDetails;
    EdgeItem* m_selectedEdgeForDetails;
    QString m_userRole;
    bool m_readOnly = false; // true for readonly/guest: all DB-writing affordances off

    // Data Sets
    QSet<int> m_isolatedPersonIds;
    QSet<int> m_bridgeNodeIds;
    QMap<int, QStringList> m_bridgeGroupsMap;
    QMap<int, QString> m_personStatusMap; // person id -> membership_status (status clustering)
    QMap<int, QString> m_groupFamilyConcentrationMap;
    QMap<int, QString> m_burnoutDetailsMap;

    double m_repulsionStrength;
    double m_attractionStrength;
    double m_damping;
    double m_gravity;
    bool m_physicsRunning;
    quint64 m_loadedGeneration = 0;      // DatabaseManager::dataGeneration() at last full load
    QElapsedTimer m_sinceLoad;           // staleness fallback for unlogged writes

    int m_burnoutThreshold = 3;
    int m_familyDominancePercent = 40;
};

#endif // GRAPHWIDGET_H
