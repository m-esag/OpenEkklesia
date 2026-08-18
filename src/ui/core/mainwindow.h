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

#include <QtCharts/QChartView>
#include <QDateTime>
#include <QListWidget>
#include <QProgressBar>
#include <functional>

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
    void onAccountsClicked();
    void onLedgerClicked();
    void onSetBudget();
    void onContributionsClicked();
    void onServiceClicked();
    void onGraphClicked();
    void onInventoryClicked();
    void onVaultClicked();
    void onSettingsClicked();
    void onFacilitiesClicked();
    void onAnalyticsClicked();
    void onPlanningClicked();
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
    void updateDashboardStats();
    void refreshAccountsModel();
    void refreshLedgerModel();
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
    QPushButton* m_btnAccounts;
    QPushButton* m_btnLedger;
    QPushButton* m_btnSettings;
    QPushButton* m_btnFacilities;
    QPushButton* m_btnAnalytics;
    QPushButton* m_btnPlanning;

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

    QWidget* m_sidebar;
    QLabel* m_logoLabel;
    bool m_isCompactSidebar;

    QWidget* m_secCongregation;
    QWidget* m_secMinistry;
    QWidget* m_secFinance;
    void toggleCategorySection(QWidget* container, const QString& settingsKey);
    
    QString m_barcodeBuffer;
    QDateTime m_lastKeyPressTime;
    bool m_isDarkMode;
    QString m_userRole;
    QString m_username;

    QTreeWidget* m_groupsTreeWidget;
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
    QSqlQueryModel* m_accountsModel;
    QSqlQueryModel* m_ledgerModel;

    QLabel* m_lblWelcomeHeader;
    QLabel* m_lblTotalMembers;
    QLabel* m_lblTotalVisitors;
    QLabel* m_lblTotalGroups;
    QLabel* m_lblTotalContributions;
    QLabel* m_lblAssetBalance;
    QLabel* m_lblTotalCheckins;

    QChartView* m_chartView;
    QListWidget* m_lstPastoralAlerts;
    QListWidget* m_lstVisitorFollowup;
    QListWidget* m_lstUpcomingServices;
    QLabel* m_lblBudgetProgress;
    QProgressBar* m_progressBudget;

    bool m_hintsEnabled;
    QWidget* m_hintPanel;
    QLabel* m_lblHintText;
    QCheckBox* m_chkShowHints;

    QPushButton* m_activeNavButton;
    void setActiveNav(QPushButton* btn);
};

#endif // MAINWINDOW_H
