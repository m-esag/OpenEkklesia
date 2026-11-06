#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedLayout>
#include <QSqlTableModel>
#include <QTableView>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QTableWidget>

#include "peoplewidget.h"
#include "contributionswidget.h"
#include "servicebuilderwidget.h"
#include "graphwidget.h"
#include "inventorywidget.h"
#include "vaultwidget.h"
#include "settingswidget.h"
#include "facilitieswidget.h"
#include "pastoralanalyticswidget.h"
#include "planningwidget.h"
#include "shepherdwidget.h"

#include <QtCharts/QChartView>
#include <QDateTime>
#include <QListWidget>
#include <QProgressBar>
#include <functional>

class GroupsTreeWidget; // file-scope in mainwindow.cpp
class TrayManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void toggleTheme();
    void launchKiosk();
    void selectPerson(int personId);
    void selectGroup(int groupId);
    void selectAsset(int assetId);
    bool promptLogin();

public slots:
    void onDashboardClicked();
    void onMembersClicked();
    void onGroupsClicked();
    void onContributionsClicked();
    void onServiceClicked();
    void onGraphClicked();
    void onInventoryClicked();
    void onVaultClicked();
    void onSettingsClicked();
    void onFacilitiesClicked();
    void onAnalyticsClicked();
    void onPlanningClicked();
    void onShepherdClicked();
    void onShowUserManual(const QString& section = QString());
    // Groups-tree context-menu actions (right-click on a group).
    void onEditGroup();
    void onAddGroup(int defaultParentId = 0);
    void onArchiveGroup();
    void onDeleteGroup();
    void onToggleHiddenGroup();
protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void setupMenuBar();
    void setupModels();
    void populatePages();
    void applyRolePermissions();
    void applyModuleVisibility();
    bool isModuleEnabled(const QString& moduleKey, bool defaultValue = true) const;
    // True for read-only roles (guest / read-only): write paths must refuse.
    bool isReadOnlyRole() const;
    void updateDashboardStats();
    void updatePieChart();

    void ensurePageLoaded(int index, std::function<QWidget*()> creator);
    void switchPage(int index);
    QWidget* createDashboardPage();
    QWidget* createTablePage(const QString& title, QTableView* view);

    TrayManager* m_trayManager = nullptr;
    QStackedLayout* m_stackedWidget;
    QPushButton* m_btnToggleSidebar = nullptr; // top-bar panel toggle (hide/reopen sidebar)
    QLabel* m_footerUserChip = nullptr;        // bottom-left signed-in user chip // IgnoreMinStackedLayout — see mainwindow.cpp
    
    QPushButton* m_btnDashboard;
    QPushButton* m_btnMembers;
    QPushButton* m_btnGroups;
    QPushButton* m_btnContributions;
    QPushButton* m_btnService;
    QPushButton* m_btnGraph;
    QPushButton* m_btnInventory;
    QPushButton* m_btnVault;
    QPushButton* m_btnSettings;
    QPushButton* m_btnFacilities;
    QPushButton* m_btnAnalytics;
    QPushButton* m_btnPlanning;
    QPushButton* m_btnKiosk;
    QPushButton* m_btnShepherd;

    // Menu Actions for Module Toggling
    QAction* m_actMembers;
    QAction* m_actGraph;
    QAction* m_actAnalytics;
    QAction* m_actShepherd;
    QAction* m_actGroups;
    QAction* m_actService;
    QAction* m_actFacilities;
    QAction* m_actPlanning;
    QAction* m_actCont;
    QAction* m_actInventory;
    QAction* m_actVault;
    QAction* m_actKiosk = nullptr;
    QAction* m_actUserManual;

    PeopleWidget* m_peopleWidget;
    ContributionsWidget* m_contributionsWidget;
    ServiceBuilderWidget* m_serviceWidget;
    GraphWidget* m_graphWidget;
    InventoryWidget* m_inventoryWidget;
    VaultWidget* m_vaultWidget;
    SettingsWidget* m_settingsWidget;
    FacilitiesWidget* m_facilitiesWidget;
    PastoralAnalyticsWidget* m_analyticsWidget;
    PlanningWidget* m_planningWidget;
    ShepherdWidget* m_shepherdWidget;

    QWidget* m_sidebar;
    QLabel* m_logoLabel;

    QWidget* m_secCongregation;
    QWidget* m_secMinistry;
    QWidget* m_secFinance;
    QPushButton* m_headerCongregation;
    QPushButton* m_headerMinistry;
    QPushButton* m_headerFinance;
    void toggleCategorySection(QWidget* container, const QString& settingsKey);
    
    QString m_barcodeBuffer;
    QDateTime m_lastKeyPressTime;
    bool m_isDarkMode;
    bool m_graphPausedByPageLeave = false; // physics auto-paused when leaving the graph page
    QString m_userRole;
    QString m_username;

    GroupsTreeWidget* m_groupsTreeWidget;
    QCheckBox* m_chkShowArchivedGroups;
    QCheckBox* m_chkShowHiddenGroups;
    QCheckBox* m_chkShowMembersInTree;
    QPushButton* m_btnAddGroup = nullptr;
    QPushButton* m_btnEditGroup;
    QPushButton* m_btnDuplicateGroup;
    QPushButton* m_btnArchiveGroup;
    QPushButton* m_btnDeleteGroup;
    QWidget* m_groupDetailCard;
    QLabel* m_lblGroupDetailTitle;
    QLabel* m_lblGroupDetailMeta;
    QTableWidget* m_groupRosterTable;
    QLabel* m_lblGroupChemistryReport;
    QLabel* m_lblRosterTitle;
    QPushButton* m_btnAddRosterMember;
    QPushButton* m_btnCreateSubTeam;
    QPushButton* m_btnSuggestPeople = nullptr;
    int m_selectedGroupId = 0;

    QTabWidget* m_groupsTabWidget;
    QWidget* m_tabMinistryBlueprints;
    QWidget* m_blueprintsGridContainer = nullptr;
    QCheckBox* m_chkShowArchivedBlueprints = nullptr;
    QPushButton* m_btnDefineBlueprint = nullptr;
    void setupMinistryBlueprintsTab(QWidget* container);
    void refreshMinistryBlueprintsTab();
    void showBlueprintDialog(int blueprintId = 0);

    void refreshGroupsTree();
    void refreshGroupDetailPanel(int groupId);
    void showGroupDialog(int groupId = 0, bool isDuplicate = false, int defaultParentId = 0);
    void refreshSidebarIcons();
    void openSettingsCategory(int categoryIndex);

    QLabel* m_lblWelcomeHeader;
    QLabel* m_lblSubHeader;
    QLabel* m_lblTotalMembers;
    QLabel* m_lblTotalVisitors;
    QLabel* m_lblTotalGroups;
    QLabel* m_lblTotalContributions;
    QLabel* m_lblAssetBalance;
    QLabel* m_lblTotalCheckins;

    struct KpiCardInfo {
        QString id;
        QString title;
        QString icon;
        QString subtext;
        QWidget* widget = nullptr;
        QLabel* valLabel = nullptr;
    };
    QHBoxLayout* m_cardsLayout;
    QMap<QString, KpiCardInfo> m_kpiRegistry;
    QStringList m_activeKpis;
    void rebuildKpiCards();
    void onConfigureKpiCards();
    void onLogQuickPastoralNote(int prefillPersonId = -1);

    QChartView* m_chartView = nullptr;
    QListWidget* m_lstPastoralAlerts = nullptr;
    QListWidget* m_lstVisitorFollowup = nullptr;
    QListWidget* m_lstUpcomingServices = nullptr;
    QPushButton* m_btnQuickNote = nullptr;

    bool m_hintsEnabled = false;
    QWidget* m_hintPanel = nullptr;
    QLabel* m_lblHintText = nullptr;
    QCheckBox* m_chkShowHints = nullptr;

    QPushButton* m_activeNavButton = nullptr;
    void setActiveNav(QPushButton* btn);

    QPushButton* m_btnTopSearch = nullptr;
    QPushButton* m_btnTopTheme = nullptr;
    QPushButton* m_btnTopRole = nullptr;
    void updateTopBarRoleBadge();
};

#endif // MAINWINDOW_H
