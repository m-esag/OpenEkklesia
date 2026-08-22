#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
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
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void setupMenuBar();
    void setupModels();
    void populatePages();
    void applyRolePermissions();
    void applyModuleVisibility();
    bool isModuleEnabled(const QString& moduleKey, bool defaultValue = true) const;
    void updateDashboardStats();
    void updatePieChart();

    void ensurePageLoaded(int index, std::function<QWidget*()> creator);
    void switchPage(int index);
    void setSidebarCompact(bool compact);
    QWidget* createDashboardPage();
    QWidget* createTablePage(const QString& title, QTableView* view);

    QStackedWidget* m_stackedWidget;
    
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
    bool m_isCompactSidebar;

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
    QString m_userRole;
    QString m_username;

    GroupsTreeWidget* m_groupsTreeWidget;
    QCheckBox* m_chkShowArchivedGroups;
    QWidget* m_groupDetailCard;
    QLabel* m_lblGroupDetailTitle;
    QLabel* m_lblGroupDetailMeta;
    QTableWidget* m_groupRosterTable;
    QLabel* m_lblGroupChemistryReport;
    int m_selectedGroupId;

    void refreshGroupsTree();
    void refreshGroupDetailPanel(int groupId);
    void showGroupDialog(int groupId = 0, bool isDuplicate = false);

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

    QChartView* m_chartView;
    QListWidget* m_lstPastoralAlerts = nullptr;
    QListWidget* m_lstVisitorFollowup = nullptr;
    QListWidget* m_lstUpcomingServices = nullptr;

    bool m_hintsEnabled;
    QWidget* m_hintPanel;
    QLabel* m_lblHintText;
    QCheckBox* m_chkShowHints;

    QPushButton* m_activeNavButton;
    void setActiveNav(QPushButton* btn);
};

#endif // MAINWINDOW_H
