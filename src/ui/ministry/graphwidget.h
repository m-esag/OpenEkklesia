#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QTimer>
#include <QPointF>
#include <QVector>
#include <QPushButton>
#include <QLabel>
#include <QGraphicsSceneHoverEvent>
#include <QSet>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "thememanager.h"

class NodeItem;
class EdgeItem;

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
    double radius() const { return 20.0 + (m_connectionCount * 1.5); }

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
};

class EdgeItem : public QGraphicsItem {
public:
    EdgeItem(NodeItem* source, NodeItem* target, const QString& relType, bool isBurnoutLink = false);

    NodeItem* sourceNode() const { return m_source; }
    NodeItem* targetNode() const { return m_target; }
    QString relationshipType() const { return m_relType; }
    bool isBurnoutLink() const { return m_isBurnoutLink; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    NodeItem* m_source;
    NodeItem* m_target;
    QString m_relType;
    bool m_isBurnoutLink;
    bool m_isHovered;
};

class GraphWidget : public QGraphicsView {
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
    void showNodeContextMenu(NodeItem* node, const QPoint& globalPos);
    void applyRole(const QString& role);
    void applyTheme(const ThemeTokens& tokens);

    void focusAndSelectNode(NodeItem* node);
    void spotlightIsolatedNodes();
    void spotlightBurnoutNodes();

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

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* m_topBarWidget;

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

    QGraphicsScene* m_scene;
    QTimer* m_timer;
    QList<NodeItem*> m_nodes;
    QList<EdgeItem*> m_edges;

    // Filter Buttons (Toggle Pills)
    QPushButton* m_btnTogglePeople;
    QPushButton* m_btnToggleHouseholds;
    QPushButton* m_btnToggleGroups;
    QPushButton* m_btnToggleBurnout;
    QPushButton* m_btnToggleIsolation;
    QPushButton* m_btnToggleLayout;

    // Filter States
    bool m_filterPeopleActive;
    bool m_filterHouseholdsActive;
    bool m_filterGroupsActive;
    bool m_filterBurnoutActive;
    bool m_filterIsolationActive;
    LayoutMode m_layoutMode;

    // Persistent Banner Badge for Unconnected Members
    QPushButton* m_btnIsolatedBanner;

    // Collapsible Legend Overlay Widget
    QWidget* m_legendOverlay;
    QPushButton* m_btnLegendToggle;

    // Floating Minimap
    QGraphicsView* m_minimapView;
    void updateMinimap();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

    // Pathfinder UI
    QPushButton* m_btnPathfind;
    QPushButton* m_btnClearPath;
    QLabel* m_lblPathStatus;

    // Pathfinder State
    bool m_pathfindActive;
    NodeItem* m_pathStartNode;
    NodeItem* m_pathEndNode;
    QList<NodeItem*> m_pathHighlightedNodes;
    QList<EdgeItem*> m_pathHighlightedEdges;

    // Side Details Panel (Context-Sensitive)
    QWidget* m_detailsPanel;
    QScrollArea* m_detailsScrollArea;
    QVBoxLayout* m_detailsLayout;

    QLabel* m_lblDetailTitle;
    QLabel* m_lblDetailCategory;
    QLabel* m_lblDetailBody;
    QComboBox* m_cmbCareFlag;
    QPlainTextEdit* m_txtAdminNotes;
    QPushButton* m_btnSaveNotes;
    QPushButton* m_btnOpenProfile;
    QListWidget* m_lstRoster;
    NodeItem* m_selectedNodeForDetails;

    // Actions
    QPushButton* m_btnExportPNG;
    QPushButton* m_btnRunAudit;
    QPushButton* m_btnExportJSON;
    QLineEdit* m_graphSearchEdit;
    QString m_userRole;

    // Data Sets
    QSet<int> m_isolatedPersonIds;
    QMap<int, QString> m_burnoutDetailsMap;

    double m_repulsionStrength;
    double m_attractionStrength;
    double m_damping;
    double m_gravity;
    bool m_physicsRunning;

    // Configured thresholds (read from QSettings on graph load).
    int m_burnoutThreshold = 3;
    int m_familyDominancePercent = 50;
};

#endif // GRAPHWIDGET_H
