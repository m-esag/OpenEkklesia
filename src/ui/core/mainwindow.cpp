#include "mainwindow.h"
#include "thememanager.h"
#include "transactiondialog.h"
#include "peoplewidget.h"
#include "contributionswidget.h"
#include "servicebuilderwidget.h"
#include "graphwidget.h"
#include "inventorywidget.h"
#include "vaultwidget.h"
#include "commandpalette.h"
#include "kioskwindow.h"
#include "settingswidget.h"
#include "facilitieswidget.h"
#include "pastoralanalyticswidget.h"
#include "planningwidget.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QKeyEvent>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include "../database/databasemanager.h"
#include "../common/appcurrency.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDebug>
#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QChart>



class LoginDialog : public QDialog {
public:
    LoginDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("OpenEkklesia Authentication");
        resize(320, 200);
        // Inherit the application-wide themed stylesheet (ThemeManager) so the
        // sign-in screen matches the active dark/light theme.

        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(20, 20, 20, 20);
        lay->setSpacing(12);

        QLabel* header = new QLabel("🔒 Enter OpenEkklesia Access PIN:", this);
        header->setObjectName("detailHeader");
        lay->addWidget(header);

        m_pinEdit = new QLineEdit(this);
        m_pinEdit->setEchoMode(QLineEdit::Password);
        m_pinEdit->setMaxLength(16);
        m_pinEdit->setFocus();
        lay->addWidget(m_pinEdit);

        QHBoxLayout* buttons = new QHBoxLayout();
        QPushButton* btnGuest = new QPushButton("Guest (Read-Only)", this);
        btnGuest->setObjectName("secondaryBtn");
        connect(btnGuest, &QPushButton::clicked, this, [this]() {
            m_role = "readonly";
            m_username = "Guest";
            accept();
        });

        QPushButton* btnLogin = new QPushButton("Login", this);
        btnLogin->setObjectName("primaryBtn");
        connect(btnLogin, &QPushButton::clicked, this, &LoginDialog::onLogin);

        buttons->addWidget(btnGuest);
        buttons->addWidget(btnLogin);
        lay->addLayout(buttons);
    }

    QString role() const { return m_role; }
    QString username() const { return m_username; }

private:
    void onLogin() {
        QString pin = m_pinEdit->text().trimmed();
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery query(db);
        query.prepare("SELECT username, role FROM users WHERE pin = ?");
        query.addBindValue(pin);
        if (query.exec() && query.next()) {
            m_username = query.value(0).toString();
            m_role = query.value(1).toString();
            accept();
        } else {
            QMessageBox::warning(this, "Invalid PIN", "The access PIN entered is incorrect.");
            m_pinEdit->clear();
            m_pinEdit->setFocus();
        }
    }

    QLineEdit* m_pinEdit;
    QString m_role;
    QString m_username;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_btnSettings(nullptr), m_sidebar(nullptr), m_logoLabel(nullptr), m_isCompactSidebar(false), m_isDarkMode(true), m_hintsEnabled(true), m_activeNavButton(nullptr)
{
    m_userRole = "admin";
    m_username = "Admin";

    setupModels();
    setupUI();
    setupMenuBar();
    qApp->installEventFilter(this);
    setActiveNav(m_btnDashboard);
    updateDashboardStats();

    m_isDarkMode = ThemeManager::instance().currentTokens().isDark;
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](const ThemeTokens& tokens) {
        m_isDarkMode = tokens.isDark;
        updatePieChart();
        refreshGroupDetailPanel(m_selectedGroupId);
    });
    
    // Apply role permissions & module visibility
    applyRolePermissions();
    applyModuleVisibility();

    QShortcut* searchShortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        setWindowOpacity(0.95);
        CommandPaletteDialog palette(this);
        palette.exec();
        setWindowOpacity(1.0);
    });

    QShortcut* zoomInShortcut = new QShortcut(QKeySequence::ZoomIn, this);
    connect(zoomInShortcut, &QShortcut::activated, this, []() {
        ThemeManager::instance().zoomIn();
    });

    QShortcut* zoomOutShortcut = new QShortcut(QKeySequence::ZoomOut, this);
    connect(zoomOutShortcut, &QShortcut::activated, this, []() {
        ThemeManager::instance().zoomOut();
    });

    QShortcut* shortcutDash = new QShortcut(QKeySequence("Ctrl+1"), this);
    connect(shortcutDash, &QShortcut::activated, this, &MainWindow::onDashboardClicked);

    QShortcut* shortcutDashD = new QShortcut(QKeySequence("Ctrl+D"), this);
    connect(shortcutDashD, &QShortcut::activated, this, &MainWindow::onDashboardClicked);

    QShortcut* shortcutMem = new QShortcut(QKeySequence("Ctrl+2"), this);
    connect(shortcutMem, &QShortcut::activated, this, &MainWindow::onMembersClicked);

    QShortcut* shortcutGrp = new QShortcut(QKeySequence("Ctrl+3"), this);
    connect(shortcutGrp, &QShortcut::activated, this, &MainWindow::onGroupsClicked);

    QShortcut* shortcutCont = new QShortcut(QKeySequence("Ctrl+4"), this);
    connect(shortcutCont, &QShortcut::activated, this, &MainWindow::onContributionsClicked);

    QShortcut* shortcutSrv = new QShortcut(QKeySequence("Ctrl+5"), this);
    connect(shortcutSrv, &QShortcut::activated, this, &MainWindow::onServiceClicked);
}

MainWindow::~MainWindow() {}

void MainWindow::setupMenuBar() {
    QMenuBar* mb = menuBar();

    // 1. File / Application Menu
    QMenu* fileMenu = mb->addMenu("&File");
    
    QAction* actSearch = fileMenu->addAction("🔍 Quick Command Search...");
    actSearch->setShortcut(QKeySequence("Ctrl+K"));
    connect(actSearch, &QAction::triggered, this, [this]() {
        CommandPaletteDialog palette(this);
        palette.exec();
    });

    QAction* actKiosk = fileMenu->addAction("🖥️ Nursery Kiosk Portal");
    connect(actKiosk, &QAction::triggered, this, &MainWindow::launchKiosk);

    fileMenu->addSeparator();

    QAction* actSettings = fileMenu->addAction("⚙️ Application Settings");
    connect(actSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    fileMenu->addSeparator();

    QAction* actSwitchUser = fileMenu->addAction("🔑 Switch User / Sign In");
    connect(actSwitchUser, &QAction::triggered, this, &MainWindow::promptLogin);

    fileMenu->addSeparator();

    QAction* actExit = fileMenu->addAction("❌ Exit OpenEkklesia");
    actExit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    // 2. Congregation Menu
    QMenu* congMenu = mb->addMenu("&Congregation");

    QAction* actDash = congMenu->addAction("📊 Dashboard Overview");
    actDash->setShortcut(QKeySequence("Ctrl+1"));
    connect(actDash, &QAction::triggered, this, &MainWindow::onDashboardClicked);

    m_actMembers = congMenu->addAction("👥 Members Directory");
    m_actMembers->setShortcut(QKeySequence("Ctrl+2"));
    connect(m_actMembers, &QAction::triggered, this, &MainWindow::onMembersClicked);

    m_actGraph = congMenu->addAction("🕸️ Connection Graph Map");
    connect(m_actGraph, &QAction::triggered, this, &MainWindow::onGraphClicked);

    m_actAnalytics = congMenu->addAction("📈 Pastoral Analytics");
    connect(m_actAnalytics, &QAction::triggered, this, &MainWindow::onAnalyticsClicked);

    QAction* actShepherd = congMenu->addAction("🛐 Shepherd's Workbench");
    m_actShepherd = actShepherd;
    connect(actShepherd, &QAction::triggered, this, &MainWindow::onShepherdClicked);

    // 3. Ministry & Services Menu
    QMenu* minMenu = mb->addMenu("&Ministry");

    m_actGroups = minMenu->addAction("👨‍👩‍👧 Small Groups & Serving Teams");
    m_actGroups->setShortcut(QKeySequence("Ctrl+3"));
    connect(m_actGroups, &QAction::triggered, this, &MainWindow::onGroupsClicked);

    m_actService = minMenu->addAction("📋 Service Liturgy Builder");
    m_actService->setShortcut(QKeySequence("Ctrl+5"));
    connect(m_actService, &QAction::triggered, this, &MainWindow::onServiceClicked);

    m_actFacilities = minMenu->addAction("🏛️ Facilities Matrix");
    connect(m_actFacilities, &QAction::triggered, this, &MainWindow::onFacilitiesClicked);

    m_actPlanning = minMenu->addAction("📌 Ministry Planning");
    connect(m_actPlanning, &QAction::triggered, this, &MainWindow::onPlanningClicked);

    // 4. Finance & Operations Menu
    QMenu* finMenu = mb->addMenu("&Finance");

    m_actCont = finMenu->addAction("💳 Donation Contributions");
    m_actCont->setShortcut(QKeySequence("Ctrl+4"));
    connect(m_actCont, &QAction::triggered, this, &MainWindow::onContributionsClicked);

    m_actAccounts = finMenu->addAction("📈 Chart of Accounts");
    connect(m_actAccounts, &QAction::triggered, this, &MainWindow::onAccountsClicked);

    m_actLedger = finMenu->addAction("📒 Double-Entry Ledger");
    connect(m_actLedger, &QAction::triggered, this, &MainWindow::onLedgerClicked);

    finMenu->addSeparator();

    m_actInventory = finMenu->addAction("📦 Asset Inventory");
    connect(m_actInventory, &QAction::triggered, this, &MainWindow::onInventoryClicked);

    m_actVault = finMenu->addAction("📁 Document Vault");
    connect(m_actVault, &QAction::triggered, this, &MainWindow::onVaultClicked);

    // 5. View / Theme Menu
    QMenu* viewMenu = mb->addMenu("&View");

    QAction* actTheme = viewMenu->addAction("🌓 Toggle Dark / Light Theme");
    connect(actTheme, &QAction::triggered, this, &MainWindow::toggleTheme);

    // 6. Help Menu
    QMenu* helpMenu = mb->addMenu("&Help");

    QAction* actAbout = helpMenu->addAction("ℹ️ About OpenEkklesia");
    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About OpenEkklesia",
            "<h3>OpenEkklesia ChMS</h3>"
            "<p>Version 1.5.0 — Open-Source Church Management & Pastoral Analytics Suite.</p>"
            "<p>Features double-entry accounting, relational connection graph visualization, small group management, service liturgy builder, asset tracking, and nursery kiosk portal.</p>");
    });
}

void MainWindow::setupUI() {
    resize(1100, 700);
    setWindowTitle("OpenEkklesia - Church Management System");

    // Central widget
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Sidebar Widget
    QWidget* sidebar = new QWidget(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(220);

    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(15, 30, 15, 30);
    sidebarLayout->setSpacing(10);

    QLabel* logoLabel = new QLabel("OpenEkklesia", this);
    logoLabel->setObjectName("logoLabel");
    sidebarLayout->addWidget(logoLabel);
    sidebarLayout->addSpacing(30);

    QSettings settings("OpenEkklesia", "CHMS");

    // 1. CONGREGATION Section
    m_headerCongregation = new QPushButton("▼ CONGREGATION", this);
    m_headerCongregation->setObjectName("sectionHeaderBtn");
    m_headerCongregation->setCursor(Qt::PointingHandCursor);
    
    sidebarLayout->addWidget(m_headerCongregation);

    m_secCongregation = new QWidget(this);
    QVBoxLayout* layCong = new QVBoxLayout(m_secCongregation);
    layCong->setContentsMargins(0, 0, 0, 0);
    layCong->setSpacing(6);

    m_btnDashboard = new QPushButton("📊 Dashboard", this);
    m_btnDashboard->setObjectName("navButton");
    m_btnDashboard->setToolTip("Dashboard: View overall member counts, fund stats, and budget progress.");
    layCong->addWidget(m_btnDashboard);

    m_btnMembers = new QPushButton("👥 Members", this);
    m_btnMembers->setObjectName("navButton");
    m_btnMembers->setToolTip("Members: Search, add, edit member details and view activity history.");
    layCong->addWidget(m_btnMembers);

    m_btnGraph = new QPushButton("🕸️ Connection Graph", this);
    m_btnGraph->setObjectName("navButton");
    m_btnGraph->setToolTip("Connection Graph: View relationships, care flags, and small group clusters.");
    layCong->addWidget(m_btnGraph);

    m_btnAnalytics = new QPushButton("📈 Pastoral Analytics", this);
    m_btnAnalytics->setObjectName("navButton");
    m_btnAnalytics->setToolTip("Pastoral Analytics: View 5-Point Health Radar and Discipleship Pipeline.");
    layCong->addWidget(m_btnAnalytics);

    m_btnShepherd = new QPushButton("🛐 Shepherd's Workbench", this);
    m_btnShepherd->setObjectName("navButton");
    m_btnShepherd->setToolTip("Shepherd's Workbench: Today's follow-up queue, member care timeline, and prayer list.");
    layCong->addWidget(m_btnShepherd);

    sidebarLayout->addWidget(m_secCongregation);

    bool showCong = settings.value("sidebarSecCongregation", true).toBool();
    m_secCongregation->setVisible(showCong);
    m_headerCongregation->setText(showCong ? "▼ CONGREGATION" : "▶ CONGREGATION");

    connect(m_headerCongregation, &QPushButton::clicked, this, [this]() {
        bool nowVis = !m_secCongregation->isVisible();
        m_secCongregation->setVisible(nowVis);
        m_headerCongregation->setText(nowVis ? "▼ CONGREGATION" : "▶ CONGREGATION");
        QSettings s("OpenEkklesia", "CHMS");
        s.setValue("sidebarSecCongregation", nowVis);
    });

    // 2. MINISTRY & SERVICES Section
    m_headerMinistry = new QPushButton("▼ MINISTRY & SERVICES", this);
    m_headerMinistry->setObjectName("sectionHeaderBtn");
    m_headerMinistry->setCursor(Qt::PointingHandCursor);
    
    sidebarLayout->addWidget(m_headerMinistry);

    m_secMinistry = new QWidget(this);
    QVBoxLayout* layMin = new QVBoxLayout(m_secMinistry);
    layMin->setContentsMargins(0, 0, 0, 0);
    layMin->setSpacing(6);

    m_btnGroups = new QPushButton("🏠 Groups", this);
    m_btnGroups->setObjectName("navButton");
    m_btnGroups->setToolTip("Groups: Coordinate small groups, classes, and volunteer teams.");
    layMin->addWidget(m_btnGroups);

    m_btnService = new QPushButton("🗓️ Service Builder", this);
    m_btnService->setObjectName("navButton");
    m_btnService->setToolTip("Service Builder: Plan liturgy, run-sheets, and schedule volunteers.");
    layMin->addWidget(m_btnService);

    m_btnFacilities = new QPushButton("🏛️ Facilities Matrix", this);
    m_btnFacilities->setObjectName("navButton");
    m_btnFacilities->setToolTip("Facilities Matrix: Reserve church spaces and audio/visual equipment.");
    layMin->addWidget(m_btnFacilities);

    m_btnPlanning = new QPushButton("📌 Ministry Planning", this);
    m_btnPlanning->setObjectName("navButton");
    m_btnPlanning->setToolTip("Ministry Planning: View Product Roadmaps and Project Gantt Charts.");
    layMin->addWidget(m_btnPlanning);

    sidebarLayout->addWidget(m_secMinistry);

    bool showMin = settings.value("sidebarSecMinistry", true).toBool();
    m_secMinistry->setVisible(showMin);
    m_headerMinistry->setText(showMin ? "▼ MINISTRY & SERVICES" : "▶ MINISTRY & SERVICES");

    connect(m_headerMinistry, &QPushButton::clicked, this, [this]() {
        bool nowVis = !m_secMinistry->isVisible();
        m_secMinistry->setVisible(nowVis);
        m_headerMinistry->setText(nowVis ? "▼ MINISTRY & SERVICES" : "▶ MINISTRY & SERVICES");
        QSettings s("OpenEkklesia", "CHMS");
        s.setValue("sidebarSecMinistry", nowVis);
    });

    // 3. FINANCE & OPERATIONS Section
    m_headerFinance = new QPushButton("▼ FINANCE & OPERATIONS", this);
    m_headerFinance->setObjectName("sectionHeaderBtn");
    m_headerFinance->setCursor(Qt::PointingHandCursor);
    
    sidebarLayout->addWidget(m_headerFinance);

    m_secFinance = new QWidget(this);
    QVBoxLayout* layFin = new QVBoxLayout(m_secFinance);
    layFin->setContentsMargins(0, 0, 0, 0);
    layFin->setSpacing(6);

    m_btnContributions = new QPushButton("💸 Contributions", this);
    m_btnContributions->setObjectName("navButton");
    m_btnContributions->setToolTip("Contributions: Record donations, select funds, and print tax receipts.");
    layFin->addWidget(m_btnContributions);

    m_btnAccounts = new QPushButton("🧾 Accounts (Chart)", this);
    m_btnAccounts->setObjectName("navButton");
    m_btnAccounts->setToolTip("Accounts: View chart of accounts, balance sheet, and budget targets.");
    layFin->addWidget(m_btnAccounts);

    m_btnLedger = new QPushButton("📖 Double-Entry Ledger", this);
    m_btnLedger->setObjectName("navButton");
    m_btnLedger->setToolTip("Ledger: View double-entry transactions and input splits.");
    layFin->addWidget(m_btnLedger);

    m_btnInventory = new QPushButton("📦 Asset Inventory", this);
    m_btnInventory->setObjectName("navButton");
    m_btnInventory->setToolTip("Asset Inventory: Manage church assets, checkouts, and barcodes.");
    layFin->addWidget(m_btnInventory);

    m_btnVault = new QPushButton("🔒 Document Vault", this);
    m_btnVault->setObjectName("navButton");
    m_btnVault->setToolTip("Document Vault: Securely store board minutes, bylaws, and official PDFs.");
    layFin->addWidget(m_btnVault);

    sidebarLayout->addWidget(m_secFinance);

    bool showFin = settings.value("sidebarSecFinance", true).toBool();
    m_secFinance->setVisible(showFin);
    m_headerFinance->setText(showFin ? "▼ FINANCE & OPERATIONS" : "▶ FINANCE & OPERATIONS");

    connect(m_headerFinance, &QPushButton::clicked, this, [this]() {
        bool nowVis = !m_secFinance->isVisible();
        m_secFinance->setVisible(nowVis);
        m_headerFinance->setText(nowVis ? "▼ FINANCE & OPERATIONS" : "▶ FINANCE & OPERATIONS");
        QSettings s("OpenEkklesia", "CHMS");
        s.setValue("sidebarSecFinance", nowVis);
    });

    m_btnSettings = new QPushButton("⚙️ Settings", this);
    m_btnSettings->setObjectName("navButton");
    m_btnSettings->setToolTip("Settings: Configure church details, responsive layout rules, graph thresholds & backups.");
    sidebarLayout->addWidget(m_btnSettings);

    sidebarLayout->addSpacing(15);

    bool showHintsPref = settings.value("showHelpHints", true).toBool();
    m_hintsEnabled = showHintsPref;

    m_chkShowHints = new QCheckBox("Show Help Hints", this);
    m_chkShowHints->setChecked(showHintsPref);
    connect(m_chkShowHints, &QCheckBox::toggled, this, [this](bool checked) {
        m_hintsEnabled = checked;
        m_hintPanel->setVisible(checked);
        QSettings s("OpenEkklesia", "CHMS");
        s.setValue("showHelpHints", checked);
    });
    sidebarLayout->addWidget(m_chkShowHints);

    m_hintPanel = new QWidget(this);
    m_hintPanel->setObjectName("hintPanel");
    m_hintPanel->setVisible(showHintsPref);
    QVBoxLayout* hintPanelLayout = new QVBoxLayout(m_hintPanel);
    hintPanelLayout->setContentsMargins(10, 10, 10, 10);
    m_lblHintText = new QLabel("Hover over items to see tips.", m_hintPanel);
    m_lblHintText->setWordWrap(true);
    m_lblHintText->setStyleSheet("border: none; background: transparent;");
    hintPanelLayout->addWidget(m_lblHintText);
    sidebarLayout->addWidget(m_hintPanel);

    sidebarLayout->addStretch();

    // Bottom Dock (Kiosk Portal)
    QHBoxLayout* bottomDock = new QHBoxLayout();
    bottomDock->setSpacing(8);

    m_btnKiosk = new QPushButton("🖥️ Kiosk Portal", this);
    m_btnKiosk->setObjectName("navButton");
    m_btnKiosk->setToolTip("Kiosk: Launch full-screen self-checkin nursery portal.");
    connect(m_btnKiosk, &QPushButton::clicked, this, &MainWindow::launchKiosk);

    bottomDock->addWidget(m_btnKiosk);
    sidebarLayout->addLayout(bottomDock);

    QLabel* footerLabel = new QLabel("v1.5.0 (ChMS Suite)", this);
    footerLabel->setObjectName("footerLabel");
    sidebarLayout->addWidget(footerLabel);

    m_sidebar = sidebar;
    m_logoLabel = logoLabel;
    m_isCompactSidebar = false;

    mainLayout->addWidget(sidebar);

    // Stacked widget for pages
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setObjectName("mainContent");
    mainLayout->addWidget(m_stackedWidget);

    populatePages();

    // Connect nav buttons
    connect(m_btnDashboard, &QPushButton::clicked, this, &MainWindow::onDashboardClicked);
    connect(m_btnMembers, &QPushButton::clicked, this, &MainWindow::onMembersClicked);
    connect(m_btnGroups, &QPushButton::clicked, this, &MainWindow::onGroupsClicked);
    connect(m_btnContributions, &QPushButton::clicked, this, &MainWindow::onContributionsClicked);
    connect(m_btnService, &QPushButton::clicked, this, &MainWindow::onServiceClicked);
    connect(m_btnGraph, &QPushButton::clicked, this, &MainWindow::onGraphClicked);
    connect(m_btnInventory, &QPushButton::clicked, this, &MainWindow::onInventoryClicked);
    connect(m_btnVault, &QPushButton::clicked, this, &MainWindow::onVaultClicked);
    connect(m_btnAccounts, &QPushButton::clicked, this, &MainWindow::onAccountsClicked);
    connect(m_btnLedger, &QPushButton::clicked, this, &MainWindow::onLedgerClicked);
    connect(m_btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(m_btnFacilities, &QPushButton::clicked, this, &MainWindow::onFacilitiesClicked);
    connect(m_btnAnalytics, &QPushButton::clicked, this, &MainWindow::onAnalyticsClicked);
    connect(m_btnPlanning, &QPushButton::clicked, this, &MainWindow::onPlanningClicked);
    connect(m_btnShepherd, &QPushButton::clicked, this, &MainWindow::onShepherdClicked);
    connect(m_btnKiosk, &QPushButton::clicked, this, &MainWindow::launchKiosk);
}

void MainWindow::setupModels() {
    QSqlDatabase db = DatabaseManager::instance().database();

    // Initialize widget pointers to nullptr for on-demand lazy instantiation (reduces RAM by ~70%)
    m_peopleWidget = nullptr;
    m_contributionsWidget = nullptr;
    m_serviceWidget = nullptr;
    m_graphWidget = nullptr;
    m_inventoryWidget = nullptr;
    m_vaultWidget = nullptr;
    m_facilitiesWidget = nullptr;
    m_analyticsWidget = nullptr;
    m_planningWidget = nullptr;
    m_shepherdWidget = nullptr;
    m_settingsWidget = nullptr;

    // Accounts
    m_accountsModel = new QSqlQueryModel(this);
    refreshAccountsModel();

    m_ledgerModel = new QSqlQueryModel(this);
    refreshLedgerModel();
}

void MainWindow::ensurePageLoaded(int index, std::function<QWidget*()> creator) {
    QWidget* current = m_stackedWidget->widget(index);
    if (current && current->property("isPlaceholder").toBool()) {
        QWidget* realWidget = creator();
        m_stackedWidget->insertWidget(index, realWidget);
        m_stackedWidget->removeWidget(current);
        current->deleteLater();
    }
}

void MainWindow::switchPage(int index) {
    if (!m_stackedWidget || index < 0 || index >= m_stackedWidget->count()) return;
    if (m_stackedWidget->currentIndex() == index) return;

    // Switch pages instantly. A QGraphicsOpacityEffect fade used to be applied
    // here, but it renders each page to an offscreen buffer and, on switching
    // (especially between heavy modules like the Connection Graph and the
    // members list), leaves ghost layers from the previous page bleeding into
    // the next one ("header bleed-through"). The effect also interfered with
    // QGraphicsView scene rendering. An instant switch has no artifacts.
    QWidget* targetPage = m_stackedWidget->widget(index);
    if (!targetPage) return;
    targetPage->ensurePolished();
    m_stackedWidget->setCurrentIndex(index);
}

void MainWindow::setSidebarCompact(bool compact) {
    if (m_isCompactSidebar == compact || !m_sidebar) return;
    m_isCompactSidebar = compact;

    int startW = m_sidebar->width();
    int endW = compact ? 70 : 250;

    QPropertyAnimation* anim = new QPropertyAnimation(m_sidebar, "maximumWidth", this);
    anim->setDuration(200);
    anim->setStartValue(startW);
    anim->setEndValue(endW);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    m_sidebar->setMinimumWidth(compact ? 70 : 200);

    if (m_logoLabel) {
        m_logoLabel->setText(compact ? "⛪" : "⛪ OpenEkklesia");
    }
}

void MainWindow::populatePages() {
    auto makePlaceholder = [this]() {
        QWidget* w = new QWidget(this);
        w->setProperty("isPlaceholder", true);
        return w;
    };

    // Add pages to Stack (Lazy placeholders installed for unvisited pages)
    m_stackedWidget->addWidget(createDashboardPage()); // 0
    m_stackedWidget->addWidget(makePlaceholder());      // 1: Members
    
    QWidget* groupsPage = new QWidget(this);
    QVBoxLayout* groupsLayout = new QVBoxLayout(groupsPage);
    groupsLayout->setContentsMargins(25, 25, 25, 25);
    groupsLayout->setSpacing(15);

    // Header Row
    QHBoxLayout* grpHeaderLayout = new QHBoxLayout();
    QLabel* grpHeader = new QLabel("Ministries & Groups", this);
    grpHeader->setObjectName("pageHeader");
    grpHeaderLayout->addWidget(grpHeader);
    grpHeaderLayout->addStretch();

    m_chkShowArchivedGroups = new QCheckBox("Show Archived Groups", this);
    m_chkShowArchivedGroups->setStyleSheet("font-size: 12px; font-weight: bold;");
    connect(m_chkShowArchivedGroups, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) {
        refreshGroupsTree();
    });
    grpHeaderLayout->addWidget(m_chkShowArchivedGroups);

    QPushButton* btnAddGroup = new QPushButton("➕ Add Group", this);
    btnAddGroup->setObjectName("primaryBtn");
    connect(btnAddGroup, &QPushButton::clicked, this, [this]() {
        showGroupDialog(0, false);
    });
    grpHeaderLayout->addWidget(btnAddGroup);

    QPushButton* btnEditGroup = new QPushButton("✏️ Edit Group", this);
    btnEditGroup->setObjectName("secondaryBtn");
    connect(btnEditGroup, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_groupsTreeWidget->currentItem();
        if (!item) {
            QMessageBox::warning(this, "No Selection", "Please select a group to edit first.");
            return;
        }
        int groupId = item->data(0, Qt::UserRole).toInt();
        showGroupDialog(groupId, false);
    });
    grpHeaderLayout->addWidget(btnEditGroup);

    QPushButton* btnDuplicateGroup = new QPushButton("📋 Duplicate Group", this);
    btnDuplicateGroup->setObjectName("secondaryBtn");
    connect(btnDuplicateGroup, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_groupsTreeWidget->currentItem();
        if (!item) {
            QMessageBox::warning(this, "No Selection", "Please select a group to duplicate first.");
            return;
        }
        int groupId = item->data(0, Qt::UserRole).toInt();
        showGroupDialog(groupId, true);
    });
    grpHeaderLayout->addWidget(btnDuplicateGroup);

    QPushButton* btnArchiveGroup = new QPushButton("📦 Archive / Unarchive", this);
    btnArchiveGroup->setObjectName("secondaryBtn");
    connect(btnArchiveGroup, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_groupsTreeWidget->currentItem();
        if (!item) {
            QMessageBox::warning(this, "No Selection", "Please select a group first.");
            return;
        }
        int groupId = item->data(0, Qt::UserRole).toInt();
        QString name = item->text(0);
        if (name.startsWith("↳ ")) name = name.mid(2);
        if (name.endsWith(" [ARCHIVED]")) name.chop(11);

        int isArchived = 0;
        QSqlQuery qCheck("SELECT is_archived FROM groups WHERE id = ?");
        qCheck.addBindValue(groupId);
        if (qCheck.exec() && qCheck.next()) {
            isArchived = qCheck.value(0).toInt();
        }

        int targetState = (isArchived == 1) ? 0 : 1;
        QString actionName = (targetState == 1) ? "archive" : "unarchive";

        auto res = QMessageBox::question(this, "Confirm Action", 
                                         QString("Are you sure you want to %1 the group '%2'?").arg(actionName, name),
                                         QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::Yes) {
            QSqlQuery update;
            update.prepare("UPDATE groups SET is_archived = ? WHERE id = ?");
            update.addBindValue(targetState);
            update.addBindValue(groupId);
            if (update.exec()) {
                refreshGroupsTree();
                refreshGroupDetailPanel(groupId);
                if (m_graphWidget) m_graphWidget->loadGraph();
                QMessageBox::information(this, "Success", QString("Group '%1' successfully %2d.").arg(name, actionName));
            } else {
                QMessageBox::critical(this, "Database Error", "Failed to update archive state: " + update.lastError().text());
            }
        }
    });
    grpHeaderLayout->addWidget(btnArchiveGroup);

    QPushButton* btnDeleteGroup = new QPushButton("🗑️ Delete Group", this);
    btnDeleteGroup->setObjectName("dangerBtn");
    connect(btnDeleteGroup, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_groupsTreeWidget->currentItem();
        if (!item) {
            QMessageBox::warning(this, "No Selection", "Please select a group first.");
            return;
        }
        int groupId = item->data(0, Qt::UserRole).toInt();
        QString groupName = item->text(0);
        if (groupName.startsWith("↳ ")) groupName = groupName.mid(2);
        if (groupName.endsWith(" [ARCHIVED]")) groupName.chop(11);
        
        auto res = QMessageBox::question(this, "Confirm Deletion", 
                                         QString("Are you sure you want to delete the group '%1'? This will clear member assignments.").arg(groupName), 
                                         QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::Yes) {
            QSqlDatabase db = QSqlDatabase::database();
            db.transaction();

            int deletedGroupParentId = 0;
            QSqlQuery qParentCheck(db);
            qParentCheck.prepare("SELECT parent_id FROM groups WHERE id = ?");
            qParentCheck.addBindValue(groupId);
            if (qParentCheck.exec() && qParentCheck.next()) {
                deletedGroupParentId = qParentCheck.value(0).toInt();
            }

            QSqlQuery qOrphans(db);
            qOrphans.prepare("UPDATE groups SET parent_id = ? WHERE parent_id = ?");
            qOrphans.addBindValue(deletedGroupParentId > 0 ? QVariant(deletedGroupParentId) : QVariant(QMetaType(QMetaType::Int)));
            qOrphans.addBindValue(groupId);
            qOrphans.exec();

            QSqlQuery query(db);
            query.prepare("DELETE FROM entity_relationships WHERE target_type = 'Group' AND target_id = ?");
            query.addBindValue(groupId);
            query.exec();
            
            QSqlQuery query2(db);
            query2.prepare("DELETE FROM groups WHERE id = ?");
            query2.addBindValue(groupId);
            if (query2.exec()) {
                db.commit();
                m_selectedGroupId = 0;
                refreshGroupsTree();
                refreshGroupDetailPanel(0);
                if (m_graphWidget) m_graphWidget->loadGraph();
                QMessageBox::information(this, "Success", "Group deleted successfully.");
            } else {
                db.rollback();
                QMessageBox::critical(this, "Database Error", "Failed to delete group: " + query2.lastError().text());
            }
        }
    });
    grpHeaderLayout->addWidget(btnDeleteGroup);

    groupsLayout->addLayout(grpHeaderLayout);

    // Master-Detail Splitter View
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);

    // Left Panel: Group Tree List
    m_groupsTreeWidget = new QTreeWidget(this);
    m_groupsTreeWidget->setHeaderLabels({"Name", "Category", "Schedule & Location", "Parent Group", "Members", "Status"});
    m_groupsTreeWidget->setAlternatingRowColors(true);
    m_groupsTreeWidget->setAnimated(true);
    m_groupsTreeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_groupsTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    splitter->addWidget(m_groupsTreeWidget);

    connect(m_groupsTreeWidget, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) {
            refreshGroupDetailPanel(0);
            return;
        }
        int groupId = current->data(0, Qt::UserRole).toInt();
        refreshGroupDetailPanel(groupId);
    });

    // Right Panel: Inline Group Detail & Roster Panel
    m_groupDetailCard = new QWidget(this);
    m_groupDetailCard->setObjectName("card");
    QVBoxLayout* detailLayout = new QVBoxLayout(m_groupDetailCard);
    detailLayout->setContentsMargins(16, 16, 16, 16);
    detailLayout->setSpacing(12);

    m_lblGroupDetailTitle = new QLabel("Select a Ministry or Group", m_groupDetailCard);
    m_lblGroupDetailTitle->setObjectName("detailHeader");
    detailLayout->addWidget(m_lblGroupDetailTitle);

    m_lblGroupDetailMeta = new QLabel("Select a group from the list on the left to view active members, schedule, and pastoral dynamics.", m_groupDetailCard);
    m_lblGroupDetailMeta->setWordWrap(true);
    detailLayout->addWidget(m_lblGroupDetailMeta);

    QHBoxLayout* rosterHeaderRow = new QHBoxLayout();
    QLabel* lblRosterTitle = new QLabel("👥 Active Roster & Roles", m_groupDetailCard);
    lblRosterTitle->setStyleSheet("font-size: 13px; font-weight: bold;");
    rosterHeaderRow->addWidget(lblRosterTitle);
    rosterHeaderRow->addStretch();

    QPushButton* btnAddRosterMember = new QPushButton("➕ Add Member", m_groupDetailCard);
    btnAddRosterMember->setObjectName("primaryBtn");
    btnAddRosterMember->setStyleSheet("padding: 4px 10px; font-size: 11px;");
    connect(btnAddRosterMember, &QPushButton::clicked, this, [this]() {
        if (m_selectedGroupId <= 0) {
            QMessageBox::warning(this, "No Selection", "Please select a group first.");
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle("Add Member to Group");
        dlg.resize(350, 180);
        QFormLayout* form = new QFormLayout(&dlg);
        form->setSpacing(12);

        QComboBox* personCombo = new QComboBox(&dlg);
        QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name || ' (' || membership_status || ')' FROM people ORDER BY first_name");
        while (qPeople.next()) {
            personCombo->addItem(qPeople.value(1).toString(), qPeople.value(0).toInt());
        }

        QComboBox* roleCombo = new QComboBox(&dlg);
        roleCombo->addItems({"Member", "Leader", "Co-Leader"});

        form->addRow("Select Member:", personCombo);
        form->addRow("Assigned Role:", roleCombo);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        QPushButton* btnAdd = buttons->button(QDialogButtonBox::Ok);
        if (btnAdd) btnAdd->setText("Add");
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            int personId = personCombo->currentData().toInt();
            QString role = roleCombo->currentText();
            if (personId > 0) {
                QSqlQuery ins;
                ins.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) VALUES ('Person', ?, 'Group', ?, ?)");
                ins.addBindValue(personId);
                ins.addBindValue(m_selectedGroupId);
                ins.addBindValue(role);
                if (ins.exec()) {
                    refreshGroupDetailPanel(m_selectedGroupId);
                    refreshGroupsTree();
                    if (m_graphWidget) m_graphWidget->loadGraph();
                } else {
                    QMessageBox::critical(this, "Error", "Member is already in this group or database error occurred.");
                }
            }
        }
    });
    rosterHeaderRow->addWidget(btnAddRosterMember);
    detailLayout->addLayout(rosterHeaderRow);

    m_groupRosterTable = new QTableWidget(m_groupDetailCard);
    m_groupRosterTable->setColumnCount(4);
    m_groupRosterTable->setHorizontalHeaderLabels({"Member", "Role", "Status", "Action"});
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_groupRosterTable->verticalHeader()->setVisible(false);
    m_groupRosterTable->setAlternatingRowColors(true);
    detailLayout->addWidget(m_groupRosterTable);

    // Group Dynamics & Chemistry Section
    QLabel* chemistryTitle = new QLabel("🧪 Group Dynamics & Chemistry Analysis", m_groupDetailCard);
    chemistryTitle->setStyleSheet("font-size: 12px; font-weight: bold; margin-top: 4px;");
    detailLayout->addWidget(chemistryTitle);

    m_lblGroupChemistryReport = new QLabel(m_groupDetailCard);
    m_lblGroupChemistryReport->setWordWrap(true);
    detailLayout->addWidget(m_lblGroupChemistryReport);

    QPushButton* btnViewInGraph = new QPushButton("🔍 View in Connection Graph", m_groupDetailCard);
    btnViewInGraph->setObjectName("secondaryBtn");
    connect(btnViewInGraph, &QPushButton::clicked, this, [this]() {
        if (m_selectedGroupId > 0) {
            onGraphClicked();
        } else {
            QMessageBox::warning(this, "No Selection", "Please select a group first.");
        }
    });
    detailLayout->addWidget(btnViewInGraph);

    splitter->addWidget(m_groupDetailCard);
    splitter->setSizes({600, 400});

    groupsLayout->addWidget(splitter);

    refreshGroupsTree();
    refreshGroupDetailPanel(0);

    m_stackedWidget->addWidget(groupsPage); // 2

    m_stackedWidget->addWidget(makePlaceholder()); // 3: Contributions
    m_stackedWidget->addWidget(makePlaceholder()); // 4: Service Builder

    QWidget* accountsPage = new QWidget(this);
    QVBoxLayout* accountsLayout = new QVBoxLayout(accountsPage);
    accountsLayout->setContentsMargins(30, 30, 30, 30);
    accountsLayout->setSpacing(15);

    QHBoxLayout* accHeaderLayout = new QHBoxLayout();
    QLabel* accHeader = new QLabel("Chart of Accounts", this);
    accHeader->setObjectName("pageHeader");
    accHeaderLayout->addWidget(accHeader);
    accHeaderLayout->addStretch();

    QPushButton* btnAddAccount = new QPushButton("➕ Add Account", this);
    btnAddAccount->setObjectName("primaryBtn");
    connect(btnAddAccount, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("Add New Account");
        dlg.resize(300, 200);
        
        QFormLayout* form = new QFormLayout(&dlg);
        form->setSpacing(12);
        
        QLineEdit* nameEdit = new QLineEdit(&dlg);
        QComboBox* typeCombo = new QComboBox(&dlg);
        typeCombo->addItems({"ASSET", "LIABILITY", "EQUITY", "INCOME", "EXPENSE"});
        
        form->addRow("Account Name:", nameEdit);
        form->addRow("Account Type:", typeCombo);
        
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(buttons);
        
        if (dlg.exec() == QDialog::Accepted) {
            QString name = nameEdit->text().trimmed();
            QString type = typeCombo->currentText();
            if (name.isEmpty()) {
                QMessageBox::warning(this, "Validation Error", "Account name cannot be empty.");
                return;
            }
            
            QSqlDatabase db = QSqlDatabase::database();
            QSqlQuery query(db);
            query.prepare("INSERT INTO accounts (name, type) VALUES (?, ?)");
            query.addBindValue(name);
            query.addBindValue(type);
            
            if (query.exec()) {
                refreshAccountsModel();
                QMessageBox::information(this, "Success", "Account created successfully.");
            } else {
                QMessageBox::critical(this, "Database Error", "Failed to create account: " + query.lastError().text());
            }
        }
    });
    accHeaderLayout->addWidget(btnAddAccount);

    QPushButton* btnSetBudget = new QPushButton("🎯 Set Budget Targets", this);
    btnSetBudget->setObjectName("saveBtn");
    connect(btnSetBudget, &QPushButton::clicked, this, &MainWindow::onSetBudget);
    accHeaderLayout->addWidget(btnSetBudget);
    accountsLayout->addLayout(accHeaderLayout);

    QTableView* accountsView = new QTableView(this);
    accountsView->setModel(m_accountsModel);
    accountsView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    accountsLayout->addWidget(accountsView);

    m_stackedWidget->addWidget(accountsPage); // 5

    QWidget* ledgerPage = new QWidget(this);
    QVBoxLayout* ledgerLayout = new QVBoxLayout(ledgerPage);
    ledgerLayout->setContentsMargins(30, 30, 30, 30);
    ledgerLayout->setSpacing(15);

    QHBoxLayout* ledgerHeaderLayout = new QHBoxLayout();
    QLabel* ledgerHeader = new QLabel("Double-Entry Ledger (All Splits)", this);
    ledgerHeader->setObjectName("pageHeader");
    ledgerHeaderLayout->addWidget(ledgerHeader);
    ledgerHeaderLayout->addSpacing(20);

    QPushButton* btnAddTrans = new QPushButton("+ Add Transaction", this);
    btnAddTrans->setObjectName("primaryBtn");
    connect(btnAddTrans, &QPushButton::clicked, this, [this]() {
        TransactionDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            refreshLedgerModel();
            updateDashboardStats();
        }
    });
    ledgerHeaderLayout->addWidget(btnAddTrans);
    ledgerHeaderLayout->addStretch();

    ledgerLayout->addLayout(ledgerHeaderLayout);

    QTableView* ledgerView = new QTableView(this);
    ledgerView->setModel(m_ledgerModel);
    ledgerView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ledgerLayout->addWidget(ledgerView);


    m_stackedWidget->addWidget(ledgerPage);         // 6
    m_stackedWidget->addWidget(makePlaceholder()); // 7: Connection Graph
    m_stackedWidget->addWidget(makePlaceholder()); // 8: Asset Inventory
    m_stackedWidget->addWidget(makePlaceholder()); // 9: Document Vault
    m_stackedWidget->addWidget(makePlaceholder()); // 10: Settings
    m_stackedWidget->addWidget(makePlaceholder()); // 11: Facilities
    m_stackedWidget->addWidget(makePlaceholder()); // 12: Analytics
    m_stackedWidget->addWidget(makePlaceholder()); // 13: Planning
    m_stackedWidget->addWidget(makePlaceholder()); // 14: Shepherd's Workbench
}

QWidget* MainWindow::createDashboardPage() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(25, 25, 25, 25);
    layout->setSpacing(20);

    // --- Header Section: Welcome Banner & Quick Action Buttons ---
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(15);

    QVBoxLayout* headerTextLay = new QVBoxLayout();
    m_lblWelcomeHeader = new QLabel("Welcome to OpenEkklesia", page);
    m_lblWelcomeHeader->setObjectName("pageHeader");
    m_lblWelcomeHeader->setStyleSheet("font-size: 22px; font-weight: bold; margin: 0;");

    m_lblSubHeader = new QLabel(page);
    m_lblSubHeader->setStyleSheet("font-size: 13px; opacity: 0.8;");
    headerTextLay->addWidget(m_lblWelcomeHeader);
    headerTextLay->addWidget(m_lblSubHeader);

    headerRow->addLayout(headerTextLay);
    headerRow->addStretch();

    // Quick Actions Bar
    QPushButton* btnAddPersonQuick = new QPushButton("➕ Add Member", page);
    btnAddPersonQuick->setObjectName("primaryBtn");
    connect(btnAddPersonQuick, &QPushButton::clicked, this, [this]() {
        onMembersClicked();
        if (m_peopleWidget) m_peopleWidget->onAddPerson();
    });

    QPushButton* btnGivingQuick = new QPushButton("💸 Giving", page);
    btnGivingQuick->setObjectName("secondaryBtn");
    connect(btnGivingQuick, &QPushButton::clicked, this, &MainWindow::onContributionsClicked);

    QPushButton* btnServiceQuick = new QPushButton("🗓️ Services", page);
    btnServiceQuick->setObjectName("secondaryBtn");
    connect(btnServiceQuick, &QPushButton::clicked, this, &MainWindow::onServiceClicked);

    QPushButton* btnCmdQuick = new QPushButton("🔍 Search (Ctrl+K)", page);
    btnCmdQuick->setObjectName("secondaryBtn");
    connect(btnCmdQuick, &QPushButton::clicked, this, [this]() {
        CommandPaletteDialog palette(this);
        palette.exec();
    });

    headerRow->addWidget(btnAddPersonQuick);
    headerRow->addWidget(btnGivingQuick);
    headerRow->addWidget(btnServiceQuick);
    headerRow->addWidget(btnCmdQuick);

    layout->addLayout(headerRow);

    // --- Top KPI Summary Cards Row ---
    QHBoxLayout* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(15);

    auto makeKpiCard = [page](const QString& title, QLabel** valueLabel, const QString& iconStr, const QString& subtext) -> QWidget* {
        QWidget* card = new QWidget(page);
        card->setObjectName("cardWidget");
        QVBoxLayout* v = new QVBoxLayout(card);
        v->setContentsMargins(16, 14, 16, 14);
        v->setSpacing(6);

        QHBoxLayout* top = new QHBoxLayout();
        QLabel* lblTitle = new QLabel(title, card);
        lblTitle->setStyleSheet("font-size: 12px; font-weight: bold; opacity: 0.8; text-transform: uppercase;");
        QLabel* lblIcon = new QLabel(iconStr, card);
        lblIcon->setStyleSheet("font-size: 18px;");
        top->addWidget(lblTitle);
        top->addStretch();
        top->addWidget(lblIcon);
        v->addLayout(top);

        *valueLabel = new QLabel("0", card);
        (*valueLabel)->setStyleSheet("font-size: 24px; font-weight: bold; margin-top: 2px;");
        v->addWidget(*valueLabel);

        QLabel* lblSub = new QLabel(subtext, card);
        lblSub->setStyleSheet("font-size: 11px; opacity: 0.6;");
        v->addWidget(lblSub);

        return card;
    };

    cardsLayout->addWidget(makeKpiCard("Members", &m_lblTotalMembers, "👥", "Active Congregation"));
    cardsLayout->addWidget(makeKpiCard("Visitors / Attenders", &m_lblTotalVisitors, "🤝", "Growth Pipeline"));
    cardsLayout->addWidget(makeKpiCard("Active Groups", &m_lblTotalGroups, "🏠", "Small Groups & Ministries"));
    cardsLayout->addWidget(makeKpiCard("Check-ins", &m_lblTotalCheckins, "📌", "Kiosk Attendance Logged"));
    cardsLayout->addWidget(makeKpiCard("Total Assets", &m_lblAssetBalance, "💰", "Asset Account Balance"));
    cardsLayout->addWidget(makeKpiCard("YTD Income", &m_lblTotalContributions, "📈", "This Year's Tithes & Offerings"));

    layout->addLayout(cardsLayout);

    // --- Bottom Layout: Financial Chart on Left, Budget & Alerts & Upcoming Services on Right ---
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(20);

    // Left: Chart View Card
    QWidget* chartCard = new QWidget(page);
    chartCard->setObjectName("cardWidget");
    QVBoxLayout* chartCardLay = new QVBoxLayout(chartCard);
    chartCardLay->setContentsMargins(15, 15, 15, 15);

    m_chartView = new QChartView(chartCard);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(340);
    QChart* chart = new QChart();
    chart->setBackgroundVisible(false);
    m_chartView->setChart(chart);
    chartCardLay->addWidget(m_chartView);
    bottomLayout->addWidget(chartCard, 3);

    // Right Column: Budget Progress, Pastoral Care Alerts, Recent Visitors, & Upcoming Services
    QWidget* rightCol = new QWidget(page);
    QVBoxLayout* rightColLayout = new QVBoxLayout(rightCol);
    rightColLayout->setContentsMargins(0, 0, 0, 0);
    rightColLayout->setSpacing(15);

    // Budget Progress Card with Styled QProgressBar
    QWidget* budgetCard = new QWidget(rightCol);
    budgetCard->setObjectName("cardWidget");
    QVBoxLayout* budgetCardLayout = new QVBoxLayout(budgetCard);
    budgetCardLayout->setContentsMargins(16, 14, 16, 14);
    budgetCardLayout->setSpacing(8);

    QHBoxLayout* budgetHeaderLay = new QHBoxLayout();
    QLabel* budgetCardTitle = new QLabel(QString("📊 Expense Budget Progress — %1").arg(QDate::currentDate().year()), budgetCard);
    budgetCardTitle->setStyleSheet("font-size: 13px; font-weight: bold;");
    budgetHeaderLay->addWidget(budgetCardTitle);
    budgetHeaderLay->addStretch();
    budgetCardLayout->addLayout(budgetHeaderLay);

    m_lblBudgetProgress = new QLabel("Calculating budget utilization...", budgetCard);
    m_lblBudgetProgress->setStyleSheet("font-size: 13px; font-weight: bold;");
    budgetCardLayout->addWidget(m_lblBudgetProgress);

    m_progressBudget = new QProgressBar(budgetCard);
    m_progressBudget->setRange(0, 100);
    m_progressBudget->setValue(0);
    m_progressBudget->setTextVisible(false);
    m_progressBudget->setFixedHeight(10);
    m_progressBudget->setStyleSheet(
        "QProgressBar { background-color: #313244; border-radius: 5px; border: none; }\n"
        "QProgressBar::chunk { background-color: #a6e3a1; border-radius: 5px; }"
    );
    budgetCardLayout->addWidget(m_progressBudget);
    rightColLayout->addWidget(budgetCard);

    // Pastoral Care & Visitors Card
    QWidget* alertsCard = new QWidget(rightCol);
    alertsCard->setObjectName("cardWidget");
    QVBoxLayout* alertsCardLayout = new QVBoxLayout(alertsCard);
    alertsCardLayout->setContentsMargins(16, 14, 16, 14);
    alertsCardLayout->setSpacing(10);

    QLabel* alertsCardTitle = new QLabel("⚠️ Member Inactivity & Care Alerts", alertsCard);
    alertsCardTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: #f38ba8;");
    m_lstPastoralAlerts = new QListWidget(alertsCard);
    m_lstPastoralAlerts->setMaximumHeight(100);
    connect(m_lstPastoralAlerts, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int personId = item->data(Qt::UserRole).toInt();
        if (personId > 0) selectPerson(personId);
    });
    alertsCardLayout->addWidget(alertsCardTitle);
    alertsCardLayout->addWidget(m_lstPastoralAlerts);

    QLabel* followupTitle = new QLabel("🤝 Recent Visitors — Follow-up", alertsCard);
    followupTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: #f9e2af;");
    m_lstVisitorFollowup = new QListWidget(alertsCard);
    m_lstVisitorFollowup->setMaximumHeight(100);
    connect(m_lstVisitorFollowup, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int personId = item->data(Qt::UserRole).toInt();
        if (personId > 0) selectPerson(personId);
    });
    alertsCardLayout->addWidget(followupTitle);
    alertsCardLayout->addWidget(m_lstVisitorFollowup);

    // Upcoming Services List
    QLabel* upcomingServicesTitle = new QLabel("🗓️ Upcoming Worship & Services", alertsCard);
    upcomingServicesTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: #89b4fa;");
    m_lstUpcomingServices = new QListWidget(alertsCard);
    m_lstUpcomingServices->setMaximumHeight(100);
    connect(m_lstUpcomingServices, &QListWidget::itemDoubleClicked, this, [this]() {
        onServiceClicked();
    });
    alertsCardLayout->addWidget(upcomingServicesTitle);
    alertsCardLayout->addWidget(m_lstUpcomingServices);

    rightColLayout->addWidget(alertsCard);

    bottomLayout->addWidget(rightCol, 2);
    layout->addLayout(bottomLayout, 1);

    return page;
}

QWidget* MainWindow::createTablePage(const QString& title, QTableView* view) {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(15);

    QLabel* header = new QLabel(title, this);
    header->setObjectName("pageHeader");
    layout->addWidget(header);
    layout->addWidget(view);

    return page;
}

void MainWindow::refreshAccountsModel() {
    QString queryStr = QString(
        "SELECT a.name AS \"Account Name\", "
        "       a.type AS \"Account Type\", "
        "       CASE "
        "         WHEN a.type IN ('ASSET', 'EXPENSE') THEN printf('%1%.2f', COALESCE(SUM(s.amount), 0.00))"
        "         ELSE printf('%1%.2f', -COALESCE(SUM(s.amount), 0.00))"
        "       END AS \"Balance\", "
        "       printf('%1%.2f', COALESCE(ab.budgeted_amount_cents / 100.0, 0.00)) AS \"Budget\", "
        "       CASE "
        "         WHEN a.type = 'EXPENSE' THEN printf('%1%.2f', COALESCE(ab.budgeted_amount_cents / 100.0, 0.00) - COALESCE(SUM(s.amount), 0.00))"
        "         ELSE '-'"
        "       END AS \"Variance\" "
        "FROM accounts a "
        "LEFT JOIN splits s ON a.id = s.account_id "
        "LEFT JOIN account_budgets ab ON a.id = ab.account_id "
        "GROUP BY a.id, a.name, a.type, ab.budgeted_amount_cents "
        "ORDER BY a.type, a.name").arg(appCurrencySymbol());
    m_accountsModel->setQuery(queryStr);
}

void MainWindow::refreshLedgerModel() {
    QString queryStr = QString(
        "SELECT t.date AS \"Date\", "
        "       t.description AS \"Description\", "
        "       a.name AS \"Account\", "
        "       printf('%1%.2f', s.amount) AS \"Amount\", "
        "       COALESCE(p.first_name || ' ' || p.last_name, '-') AS \"Member/Donor\" "
        "FROM transactions t "
        "JOIN splits s ON t.id = s.transaction_id "
        "JOIN accounts a ON s.account_id = a.id "
        "LEFT JOIN people p ON s.person_id = p.id "
        "ORDER BY t.date DESC, t.id DESC").arg(appCurrencySymbol());
    m_ledgerModel->setQuery(queryStr);
}

void MainWindow::updatePieChart() {
    QChart* chart = m_chartView->chart();
    chart->removeAllSeries();

    QPieSeries* series = new QPieSeries(chart);
    
    QSqlQuery query("SELECT f.fund_name, SUM(c.amount) "
                    "FROM contributions c "
                    "JOIN funds f ON c.fund_id = f.id "
                    "GROUP BY c.fund_id");
    while (query.next()) {
        QString fundName = query.value(0).toString();
        double amount = query.value(1).toDouble() / 100.0;
        series->append(fundName, amount);
    }

    for (auto slice : series->slices()) {
        slice->setLabelVisible(true);
        slice->setLabel(QString("%1\n%2%3").arg(slice->label(), appCurrencySymbol(), QString::number(slice->value(), 'f', 2)));
    }

    chart->addSeries(series);
    chart->setTitle("Offering Distribution by Fund");

    ThemeTokens tok = ThemeManager::instance().currentTokens();
    chart->setTheme(tok.isDark ? QChart::ChartThemeDark : QChart::ChartThemeLight);
    chart->setBackgroundBrush(QBrush(tok.bgSurface));
    chart->setTitleBrush(QBrush(tok.textPrimary));
    chart->legend()->setLabelColor(tok.textPrimary);
    chart->legend()->setAlignment(Qt::AlignRight);
}

void MainWindow::updateDashboardStats() {
    // 1. Dynamic Time-of-Day Welcome Greeting Banner
    int hour = QTime::currentTime().hour();
    QString greeting = (hour < 12) ? "Good Morning" : (hour < 17) ? "Good Afternoon" : "Good Evening";
    QSettings s("OpenEkklesia", "CHMS");
    QString churchName = s.value("churchName", "OpenEkklesia Church").toString();
    QString churchSubtitle = s.value("churchSubtitle", "Church Management & Pastoral Analytics").toString();
    if (m_lblWelcomeHeader) {
        m_lblWelcomeHeader->setText(QString("⛪ %1, %2").arg(greeting, m_username.isEmpty() ? "Pastor / Leader" : m_username));
    }
    if (m_lblSubHeader) {
        m_lblSubHeader->setText(QString("%1 — %2").arg(churchName, churchSubtitle));
    }

    QSqlQuery query;

    // Count Members (formal membership status)
    if (query.exec("SELECT COUNT(*) FROM people WHERE membership_status = 'Member'")) {
        if (query.next()) m_lblTotalMembers->setText(QString::number(query.value(0).toInt()));
    }

    // Count Visitors + Regular Attenders (people moving toward membership)
    if (query.exec("SELECT COUNT(*) FROM people WHERE membership_status IN ('Visitor', 'Regular Attender')")) {
        if (query.next()) m_lblTotalVisitors->setText(QString::number(query.value(0).toInt()));
    }

    // Count Groups
    if (query.exec("SELECT COUNT(*) FROM groups")) {
        query.next();
        m_lblTotalGroups->setText(QString::number(query.value(0).toInt()));
    }

    // Count Kiosk Check-ins (attendance ledger)
    if (query.exec("SELECT COUNT(*) FROM attendance WHERE attended = 1")) {
        if (query.next()) m_lblTotalCheckins->setText(QString::number(query.value(0).toInt()));
    }

    // Checking Account/Assets Balance
    if (query.exec("SELECT SUM(s.amount) FROM splits s JOIN accounts a ON s.account_id = a.id WHERE a.type = 'ASSET'")) {
        query.next();
        double assets = query.value(0).toDouble();
        m_lblAssetBalance->setText(QString("%1%2").arg(appCurrencySymbol(), QString::number(assets, 'f', 2)));
    }

    // Fiscal year used for YTD income and budget-vs-actual comparisons. The
    // ledger stores dates as "yyyy-MM-dd", so the calendar year is derived
    // from the transaction date prefix to stay consistent with tax statements.
    int fiscalYear = QDate::currentDate().year();
    QString fiscalYearStr = QString::number(fiscalYear);

    // YTD Income (credits on income accounts are stored negative)
    QSqlQuery incomeQuery;
    incomeQuery.prepare(
        "SELECT -SUM(s.amount) FROM splits s "
        "JOIN transactions t ON s.transaction_id = t.id "
        "JOIN accounts a ON s.account_id = a.id "
        "WHERE a.type = 'INCOME' AND substr(t.date, 1, 4) = ?");
    incomeQuery.addBindValue(fiscalYearStr);
    if (incomeQuery.exec() && incomeQuery.next()) {
        double income = incomeQuery.value(0).toDouble();
        m_lblTotalContributions->setText(QString("%1%2").arg(appCurrencySymbol(), QString::number(income, 'f', 2)));
    }

    // YTD Expenses for budget-vs-actual comparison
    double totalExpenses = 0.0;
    QSqlQuery expenseQuery;
    expenseQuery.prepare(
        "SELECT SUM(s.amount) FROM splits s "
        "JOIN transactions t ON s.transaction_id = t.id "
        "JOIN accounts a ON s.account_id = a.id "
        "WHERE a.type = 'EXPENSE' AND substr(t.date, 1, 4) = ?");
    expenseQuery.addBindValue(fiscalYearStr);
    if (expenseQuery.exec() && expenseQuery.next()) {
        totalExpenses = expenseQuery.value(0).toDouble();
    }

    double totalBudget = 0.0;
    QSqlQuery budgetQuery;
    budgetQuery.prepare("SELECT SUM(budgeted_amount_cents) FROM account_budgets WHERE fiscal_year = ?");
    budgetQuery.addBindValue(fiscalYear);
    if (budgetQuery.exec() && budgetQuery.next()) {
        totalBudget = budgetQuery.value(0).toDouble() / 100.0;
    }

    const ThemeTokens tok = ThemeManager::instance().currentTokens();

    if (totalBudget > 0.0) {
        int pct = qBound(0, static_cast<int>((totalExpenses / totalBudget) * 100), 100);
        if (m_progressBudget) {
            m_progressBudget->setValue(pct);
            QString chunkColor;
            if (totalExpenses > totalBudget) {
                chunkColor = tok.accentDanger.name();
            } else if (pct >= 85) {
                chunkColor = tok.accentWarning.name();
            } else {
                chunkColor = tok.accentSuccess.name();
            }
            m_progressBudget->setStyleSheet(
                QString("QProgressBar { background-color: %1; border-radius: 5px; } "
                        "QProgressBar::chunk { background-color: %2; border-radius: 5px; }")
                    .arg(tok.bgOverlay.name(), chunkColor));
        }
        m_lblBudgetProgress->setText(QString("%1%2 / %1%3 (%4%) — %5")
                                     .arg(appCurrencySymbol(), QString::number(totalExpenses, 'f', 2),
                                          QString::number(totalBudget, 'f', 2), QString::number(pct),
                                          QString::number(fiscalYear)));
        if (totalExpenses > totalBudget) {
            m_lblBudgetProgress->setStyleSheet(QString("font-size: 13px; color: %1; font-weight: bold;").arg(tok.accentDanger.name()));
        } else {
            m_lblBudgetProgress->setStyleSheet(QString("font-size: 13px; color: %1; font-weight: bold;").arg(tok.accentSuccess.name()));
        }
    } else {
        if (m_progressBudget) m_progressBudget->setValue(0);
        m_lblBudgetProgress->setText(QString("%1%2 / %3%4 — %5")
                                     .arg(appCurrencySymbol(), QString::number(totalExpenses, 'f', 2),
                                          appCurrencySymbol(), QString("0.00"),
                                          QString::number(fiscalYear)));
        m_lblBudgetProgress->setStyleSheet(QString("font-size: 13px; color: %1; font-weight: bold;").arg(tok.textSecondary.name()));
    }

    // ---- Member Inactivity, Care & Lapsed Giving Alerts ----
    m_lstPastoralAlerts->clear();

    // 1. Pastoral Care & Lapsed Giver Alerts
    QSqlQuery qAlerts;
    qAlerts.prepare(
        "SELECT id, first_name || ' ' || last_name, COALESCE(care_flag, ''), membership_status, "
        "       (SELECT MAX(contribution_date) FROM contributions WHERE person_id = people.id) AS last_give "
        "FROM people "
        "WHERE membership_status IN ('Member', 'Regular Attender') "
        "  AND ( "
        "        ( id NOT IN (SELECT DISTINCT person_id FROM contributions WHERE contribution_date >= date('now', '-30 days')) "
        "          AND id NOT IN (SELECT DISTINCT person_id FROM attendance WHERE timestamp >= date('now', '-30 days')) ) "
        "        OR care_flag = 'At-Risk' "
        "      ) "
        "ORDER BY (care_flag = 'At-Risk') DESC, last_name "
        "LIMIT 10");
    if (qAlerts.exec()) {
        while (qAlerts.next()) {
            int id = qAlerts.value(0).toInt();
            QString name = qAlerts.value(1).toString();
            QString flag = qAlerts.value(2).toString();
            QString status = qAlerts.value(3).toString();
            QString lastGive = qAlerts.value(4).toString();

            QString label;
            if (flag == "At-Risk") {
                label = QString("⚠️ [At-Risk] %1").arg(name);
            } else if (!lastGive.isEmpty() && QDate::fromString(lastGive, "yyyy-MM-dd").daysTo(QDate::currentDate()) > 60) {
                label = QString("💸 [Lapsed Giver] %1 (Last give: %2)").arg(name, lastGive);
            } else {
                QString tag = (status == "Regular Attender") ? "Attender" : "Member";
                label = QString("💤 [Inactive %1] %2").arg(tag, name);
            }
            QListWidgetItem* item = new QListWidgetItem(label, m_lstPastoralAlerts);
            item->setData(Qt::UserRole, id);
        }
    }

    // 2. Upcoming Birthday & Milestone Reminders
    QSqlQuery qMilestones(
        "SELECT id, first_name || ' ' || last_name, date_of_birth "
        "FROM people WHERE strftime('%m-%d', date_of_birth) BETWEEN strftime('%m-%d', 'now') AND strftime('%m-%d', 'now', '+7 days') "
        "LIMIT 5");
    if (qMilestones.exec()) {
        while (qMilestones.next()) {
            int id = qMilestones.value(0).toInt();
            QString name = qMilestones.value(1).toString();
            QString dob = qMilestones.value(2).toString();
            QListWidgetItem* item = new QListWidgetItem(QString("🎂 [Birthday This Week] %1 (%2)").arg(name, dob), m_lstPastoralAlerts);
            item->setData(Qt::UserRole, id);
        }
    }

    if (m_lstPastoralAlerts->count() == 0) {
        m_lstPastoralAlerts->addItem("All active. No member inactivity, care, or lapsed giving alerts.");
    }

    // ---- Recent Visitors Follow-up ----
    m_lstVisitorFollowup->clear();
    QSqlQuery qVisitors;
    qVisitors.prepare(
        "SELECT id, first_name || ' ' || last_name, membership_status, first_visit_date "
        "FROM people "
        "WHERE membership_status IN ('Visitor', 'Regular Attender') "
        "ORDER BY first_visit_date IS NULL, first_visit_date DESC, last_name "
        "LIMIT 10");
    if (qVisitors.exec()) {
        while (qVisitors.next()) {
            int id = qVisitors.value(0).toInt();
            QString name = qVisitors.value(1).toString();
            QString status = qVisitors.value(2).toString();
            QString visitDate = qVisitors.value(3).toString();
            if (visitDate.isEmpty()) visitDate = "—";
            QString tag = (status == "Regular Attender") ? "Attender" : "Visitor";
            QListWidgetItem* item = new QListWidgetItem(
                QString("📅 %1 — %2 (first visit: %3)").arg(name, tag, visitDate),
                m_lstVisitorFollowup);
            item->setData(Qt::UserRole, id);
        }
    }
    if (m_lstVisitorFollowup->count() == 0) {
        m_lstVisitorFollowup->addItem("No visitors currently in the pipeline.");
    }

    // ---- Upcoming Worship Services ----
    if (m_lstUpcomingServices) {
        m_lstUpcomingServices->clear();
        QSqlQuery qServices("SELECT date, title, leader FROM services WHERE date >= date('now') ORDER BY date ASC LIMIT 3");
        if (qServices.exec()) {
            while (qServices.next()) {
                QString sDate = qServices.value(0).toString();
                QString sTitle = qServices.value(1).toString();
                QString sLeader = qServices.value(2).toString();
                m_lstUpcomingServices->addItem(QString("🗓️ [%1] %2 (Leader: %3)").arg(sDate, sTitle, sLeader.isEmpty() ? "Unassigned" : sLeader));
            }
        }
        if (m_lstUpcomingServices->count() == 0) {
            m_lstUpcomingServices->addItem("No upcoming services planned yet. Double-click to plan a service.");
        }
    }

    updatePieChart();
}

void MainWindow::onDashboardClicked() {
    updateDashboardStats();
    switchPage(0);
    setActiveNav(m_btnDashboard);
}

void MainWindow::onMembersClicked() {
    if (!isModuleEnabled("people")) return;
    if (!m_peopleWidget) {
        m_peopleWidget = new PeopleWidget(this);
        m_peopleWidget->applyRole(m_userRole);
        ensurePageLoaded(1, [this]() { return m_peopleWidget; });
    }
    m_peopleWidget->refreshModel();
    switchPage(1);
    setActiveNav(m_btnMembers);
}

void MainWindow::onGroupsClicked() {
    if (!isModuleEnabled("groups")) return;
    refreshGroupsTree();
    switchPage(2);
    setActiveNav(m_btnGroups);
}

void MainWindow::onContributionsClicked() {
    if (!isModuleEnabled("finance")) return;
    if (!m_contributionsWidget) {
        m_contributionsWidget = new ContributionsWidget(this);
        m_contributionsWidget->applyRole(m_userRole);
        ensurePageLoaded(3, [this]() { return m_contributionsWidget; });
    }
    m_contributionsWidget->refreshContributions();
    switchPage(3);
    setActiveNav(m_btnContributions);
}

void MainWindow::onServiceClicked() {
    if (!isModuleEnabled("service")) return;
    if (!m_serviceWidget) {
        m_serviceWidget = new ServiceBuilderWidget(this);
        ensurePageLoaded(4, [this]() { return m_serviceWidget; });
    }
    switchPage(4);
    setActiveNav(m_btnService);
}

void MainWindow::onAccountsClicked() {
    if (!isModuleEnabled("finance")) return;
    refreshAccountsModel();
    switchPage(5);
    setActiveNav(m_btnAccounts);
}

void MainWindow::onLedgerClicked() {
    if (!isModuleEnabled("finance")) return;
    refreshLedgerModel();
    switchPage(6);
    setActiveNav(m_btnLedger);
}

void MainWindow::onGraphClicked() {
    if (!isModuleEnabled("graph")) return;
    if (!m_graphWidget) {
        m_graphWidget = new GraphWidget(this);
        m_graphWidget->applyRole(m_userRole);
        connect(m_graphWidget, &GraphWidget::navigateToProfile, this, [this](const QString& type, int id) {
            if (type == "Person") {
                selectPerson(id);
            } else if (type == "Group") {
                onGroupsClicked();
            }
        });
        ensurePageLoaded(7, [this]() { return m_graphWidget; });
    }
    m_graphWidget->loadGraph();
    switchPage(7);
    setActiveNav(m_btnGraph);
}

void MainWindow::onInventoryClicked() {
    if (!isModuleEnabled("inventory")) return;
    if (!m_inventoryWidget) {
        m_inventoryWidget = new InventoryWidget(this);
        m_inventoryWidget->applyRole(m_userRole);
        ensurePageLoaded(8, [this]() { return m_inventoryWidget; });
    }
    m_inventoryWidget->refreshTree();
    switchPage(8);
    setActiveNav(m_btnInventory);
}

void MainWindow::onVaultClicked() {
    if (!isModuleEnabled("vault")) return;
    if (!m_vaultWidget) {
        m_vaultWidget = new VaultWidget(this);
        m_vaultWidget->applyRole(m_userRole);
        ensurePageLoaded(9, [this]() { return m_vaultWidget; });
    }
    m_vaultWidget->refreshVault();
    switchPage(9);
    setActiveNav(m_btnVault);
}

void MainWindow::onSettingsClicked() {
    if (!m_settingsWidget) {
        m_settingsWidget = new SettingsWidget(this);
        connect(m_settingsWidget, &SettingsWidget::settingsSaved, this, [this]() {
            QSettings s("OpenEkklesia", "CHMS");
            QString chName = s.value("churchName", "OpenEkklesia").toString();
            setWindowTitle(QString("%1 - Church Management System").arg(chName));
            m_isDarkMode = ThemeManager::instance().currentTokens().isDark;
            applyModuleVisibility();
        });
        ensurePageLoaded(10, [this]() { return m_settingsWidget; });
    }
    m_settingsWidget->loadSettings();
    switchPage(10);
    setActiveNav(m_btnSettings);
}

void MainWindow::onFacilitiesClicked() {
    if (!isModuleEnabled("facilities")) return;
    if (!m_facilitiesWidget) {
        m_facilitiesWidget = new FacilitiesWidget(this);
        ensurePageLoaded(11, [this]() { return m_facilitiesWidget; });
    }
    switchPage(11);
    setActiveNav(m_btnFacilities);
}

void MainWindow::onAnalyticsClicked() {
    if (!isModuleEnabled("analytics")) return;
    if (!m_analyticsWidget) {
        m_analyticsWidget = new PastoralAnalyticsWidget(this);
        connect(m_analyticsWidget, &PastoralAnalyticsWidget::drillThroughRequested, this, [this](const QString& type, const QString& val) {
            onMembersClicked();
            if (m_peopleWidget) {
                m_peopleWidget->applyDrillThroughFilter(type, val);
            }
        });
        ensurePageLoaded(12, [this]() { return m_analyticsWidget; });
    }
    m_analyticsWidget->refreshAnalytics();
    switchPage(12);
    setActiveNav(m_btnAnalytics);
}

void MainWindow::onPlanningClicked() {
    if (!isModuleEnabled("planning")) return;
    if (!m_planningWidget) {
        m_planningWidget = new PlanningWidget(this);
        ensurePageLoaded(13, [this]() { return m_planningWidget; });
    }
    switchPage(13);
    setActiveNav(m_btnPlanning);
}

void MainWindow::onShepherdClicked() {
    if (!isModuleEnabled("shepherd")) return;
    if (!m_shepherdWidget) {
        m_shepherdWidget = new ShepherdWidget(this);
        connect(m_shepherdWidget, &ShepherdWidget::navigateToMember, this, &MainWindow::selectPerson);
        ensurePageLoaded(14, [this]() { return m_shepherdWidget; });
    }
    m_shepherdWidget->refreshAll();
    switchPage(14);
    setActiveNav(m_btnShepherd);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    QSettings s("OpenEkklesia", "CHMS");
    if (s.value("autoCollapseSidebar", true).toBool()) {
        bool shouldCompact = (width() < 940);
        if (m_isCompactSidebar != shouldCompact) {
            setSidebarCompact(shouldCompact);
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    qint64 elapsed = m_lastKeyPressTime.isValid() ? m_lastKeyPressTime.msecsTo(QDateTime::currentDateTime()) : 999;
    m_lastKeyPressTime = QDateTime::currentDateTime();

    if (elapsed > 60) {
        m_barcodeBuffer.clear();
    }

    QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint()) {
        m_barcodeBuffer.append(text);
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!m_barcodeBuffer.isEmpty()) {
            QString scanned = m_barcodeBuffer.trimmed();
            m_barcodeBuffer.clear();

            QSqlDatabase db = DatabaseManager::instance().database();
            QSqlQuery query(db);

            // 1. Check if it matches an asset barcode
            query.prepare("SELECT id, name FROM assets WHERE barcode = ?");
            query.addBindValue(scanned);
            if (query.exec() && query.next()) {
                int assetId = query.value(0).toInt();
                QString name = query.value(1).toString();

                selectAsset(assetId);

                QMessageBox::information(this, "Barcode Scanner Hook",
                    QString("Scanned Asset: %1\nBarcode: %2\nNavigating to Asset Inventory record.").arg(name, scanned));
                return;
            }

            // 2. Check if it matches a member barcode
            query.prepare("SELECT id, first_name || ' ' || last_name FROM people WHERE barcode = ?");
            query.addBindValue(scanned);
            if (query.exec() && query.next()) {
                QString name = query.value(1).toString();

                QMessageBox::information(this, "Barcode Scanner Hook",
                    QString("Scanned Member: %1\nBarcode: %2\nLogged check-in for attendance.").arg(name, scanned));
                return;
            }

            // Unknown barcode scanned
            QMessageBox::warning(this, "Barcode Scanner Hook",
                QString("Scanned Unknown Barcode: %1").arg(scanned));
        }
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::toggleTheme() {
    m_isDarkMode = !m_isDarkMode;
    QString targetTheme = m_isDarkMode ? "catppuccin_mocha" : "catppuccin_latte";
    ThemeManager::instance().loadTheme(targetTheme);
}

void MainWindow::launchKiosk() {
    if (!isModuleEnabled("kiosk")) return;
    KioskWindow kiosk(this);
    kiosk.exec();
}

void MainWindow::selectPerson(int personId) {
    if (!m_peopleWidget) onMembersClicked();
    m_peopleWidget->selectPerson(personId);
    switchPage(1);
    setActiveNav(m_btnMembers);
}

void MainWindow::selectGroup(int groupId) {
    onGroupsClicked();

    if (!m_groupsTreeWidget || groupId <= 0) return;

    // Groups can be nested, so search the tree recursively for the node whose
    // UserRole data holds the requested id and make it the current selection
    // (which drives refreshGroupDetailPanel()).
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findItem;
    findItem = [&](QTreeWidgetItem* parent) -> QTreeWidgetItem* {
        int count = parent ? parent->childCount() : m_groupsTreeWidget->topLevelItemCount();
        for (int i = 0; i < count; ++i) {
            QTreeWidgetItem* item = parent ? parent->child(i) : m_groupsTreeWidget->topLevelItem(i);
            if (!item) continue;
            if (item->data(0, Qt::UserRole).toInt() == groupId) {
                return item;
            }
            if (QTreeWidgetItem* found = findItem(item)) {
                return found;
            }
        }
        return nullptr;
    };

    if (QTreeWidgetItem* target = findItem(nullptr)) {
        m_groupsTreeWidget->setCurrentItem(target);
        m_groupsTreeWidget->scrollToItem(target);
    }
}

void MainWindow::selectAsset(int assetId) {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT barcode FROM assets WHERE id = ?");
    query.addBindValue(assetId);
    if (query.exec() && query.next()) {
        onInventoryClicked();
        m_inventoryWidget->selectAssetByBarcode(query.value(0).toString());
    }
}

void MainWindow::applyRolePermissions() {
    if (m_peopleWidget) m_peopleWidget->applyRole(m_userRole);
    if (m_contributionsWidget) m_contributionsWidget->applyRole(m_userRole);
    if (m_graphWidget) m_graphWidget->applyRole(m_userRole);
    if (m_inventoryWidget) m_inventoryWidget->applyRole(m_userRole);
    if (m_vaultWidget) m_vaultWidget->applyRole(m_userRole);
}

void MainWindow::applyModuleVisibility() {
    QSettings s("OpenEkklesia", "CHMS");
    bool modPeople = s.value("modules/people", true).toBool();
    bool modGroups = s.value("modules/groups", true).toBool();
    bool modGraph = s.value("modules/graph", true).toBool();
    bool modAnalytics = s.value("modules/analytics", true).toBool();
    bool modShepherd = s.value("modules/shepherd", true).toBool();
    bool modService = s.value("modules/service", true).toBool();
    bool modPlanning = s.value("modules/planning", true).toBool();
    bool modFacilities = s.value("modules/facilities", true).toBool();
    bool modFinance = s.value("modules/finance", true).toBool();
    bool modInventory = s.value("modules/inventory", true).toBool();
    bool modVault = s.value("modules/vault", true).toBool();
    bool modKiosk = s.value("modules/kiosk", true).toBool();

    // 1. Sidebar Nav Buttons
    if (m_btnMembers) m_btnMembers->setVisible(modPeople);
    if (m_btnGraph) m_btnGraph->setVisible(modGraph);
    if (m_btnAnalytics) m_btnAnalytics->setVisible(modAnalytics);
    if (m_btnShepherd) m_btnShepherd->setVisible(modShepherd);

    if (m_btnGroups) m_btnGroups->setVisible(modGroups);
    if (m_btnService) m_btnService->setVisible(modService);
    if (m_btnFacilities) m_btnFacilities->setVisible(modFacilities);
    if (m_btnPlanning) m_btnPlanning->setVisible(modPlanning);

    if (m_btnContributions) m_btnContributions->setVisible(modFinance);
    if (m_btnAccounts) m_btnAccounts->setVisible(modFinance);
    if (m_btnLedger) m_btnLedger->setVisible(modFinance);
    if (m_btnInventory) m_btnInventory->setVisible(modInventory);
    if (m_btnVault) m_btnVault->setVisible(modVault);

    if (m_btnKiosk) m_btnKiosk->setVisible(modKiosk);

    // 2. Hide Section Header and Container if all children are disabled
    bool anyCongregation = (modPeople || modGraph || modAnalytics || modShepherd);
    if (m_headerCongregation) m_headerCongregation->setVisible(anyCongregation);
    if (m_secCongregation) m_secCongregation->setVisible(anyCongregation && s.value("sidebarSecCongregation", true).toBool());

    bool anyMinistry = (modGroups || modService || modFacilities || modPlanning);
    if (m_headerMinistry) m_headerMinistry->setVisible(anyMinistry);
    if (m_secMinistry) m_secMinistry->setVisible(anyMinistry && s.value("sidebarSecMinistry", true).toBool());

    bool anyFinance = (modFinance || modInventory || modVault);
    if (m_headerFinance) m_headerFinance->setVisible(anyFinance);
    if (m_secFinance) m_secFinance->setVisible(anyFinance && s.value("sidebarSecFinance", true).toBool());

    // 3. Menu Bar Actions
    if (m_actMembers) m_actMembers->setVisible(modPeople);
    if (m_actGraph) m_actGraph->setVisible(modGraph);
    if (m_actAnalytics) m_actAnalytics->setVisible(modAnalytics);
    if (m_actShepherd) m_actShepherd->setVisible(modShepherd);

    if (m_actGroups) m_actGroups->setVisible(modGroups);
    if (m_actService) m_actService->setVisible(modService);
    if (m_actFacilities) m_actFacilities->setVisible(modFacilities);
    if (m_actPlanning) m_actPlanning->setVisible(modPlanning);

    if (m_actCont) m_actCont->setVisible(modFinance);
    if (m_actAccounts) m_actAccounts->setVisible(modFinance);
    if (m_actLedger) m_actLedger->setVisible(modFinance);
    if (m_actInventory) m_actInventory->setVisible(modInventory);
    if (m_actVault) m_actVault->setVisible(modVault);
}

bool MainWindow::isModuleEnabled(const QString& moduleKey) const {
    return QSettings("OpenEkklesia", "CHMS").value(QString("modules/%1").arg(moduleKey), true).toBool();
}

bool MainWindow::promptLogin() {
    LoginDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }

    m_userRole = dlg.role();
    m_username = dlg.username();

    // Re-apply role restrictions to any already-created widgets and refresh the
    // dashboard welcome banner so it reflects the signed-in user.
    applyRolePermissions();
    updateDashboardStats();
    return true;
}

void MainWindow::onSetBudget() {
    QDialog dlg(this);
    dlg.setWindowTitle("Set Account Budget");
    dlg.resize(320, 200);

    QFormLayout* layout = new QFormLayout(&dlg);

    QComboBox* accCombo = new QComboBox(&dlg);
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery qAcc("SELECT id, name FROM accounts WHERE type = 'EXPENSE' ORDER BY name", db);
    while (qAcc.next()) {
        accCombo->addItem(qAcc.value(1).toString(), qAcc.value(0).toInt());
    }
    layout->addRow("Expense Account:", accCombo);

    QDoubleSpinBox* budgetSpin = new QDoubleSpinBox(&dlg);
    budgetSpin->setRange(0.00, 9999999.00);
    budgetSpin->setPrefix(appCurrencySymbol() + " ");
    budgetSpin->setDecimals(2);
    layout->addRow("Budget Target ($):", budgetSpin);

    QSpinBox* yearSpin = new QSpinBox(&dlg);
    yearSpin->setRange(2020, 2050);
    yearSpin->setValue(2026);
    layout->addRow("Fiscal Year:", yearSpin);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        int accountId = accCombo->currentData().toInt();
        int budgetCents = static_cast<int>(budgetSpin->value() * 100);
        int fiscalYear = yearSpin->value();

        QSqlQuery query(db);
        query.prepare("INSERT OR REPLACE INTO account_budgets (account_id, budgeted_amount_cents, fiscal_year) VALUES (?, ?, ?)");
        query.addBindValue(accountId);
        query.addBindValue(budgetCents);
        query.addBindValue(fiscalYear);

        if (query.exec()) {
            refreshAccountsModel();
            updateDashboardStats();
            QMessageBox::information(this, "Success", "Budget target successfully updated.");
        } else {
            QMessageBox::critical(this, "Database Error", "Failed to save budget target: " + query.lastError().text());
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (m_hintsEnabled && event->type() == QEvent::Enter) {
        QWidget* w = qobject_cast<QWidget*>(watched);
        if (w) {
            QString tip = w->toolTip();
            if (!tip.isEmpty()) {
                m_lblHintText->setText(tip);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshGroupsTree() {
    if (!m_groupsTreeWidget) return;
    m_groupsTreeWidget->clear();

    bool showArchived = m_chkShowArchivedGroups && m_chkShowArchivedGroups->isChecked();

    QSqlDatabase db = QSqlDatabase::database();

    QMap<int, QList<QVariantMap>> childrenMap;
    QList<QVariantMap> topLevelGroups;

    QString queryStr = "SELECT g.id, g.name, g.category, g.description, g.parent_id, p.name AS parent_name, g.is_archived, "
                       "g.meeting_day, g.meeting_time, g.location, "
                       "(SELECT COUNT(*) FROM entity_relationships WHERE target_type = 'Group' AND target_id = g.id) AS member_count "
                       "FROM groups g "
                       "LEFT JOIN groups p ON g.parent_id = p.id ";
    if (!showArchived) {
        queryStr += "WHERE g.is_archived = 0 OR g.is_archived IS NULL ";
    }
    queryStr += "ORDER BY g.is_archived ASC, g.name ASC";

    QSqlQuery qAll(queryStr, db);

    while (qAll.next()) {
        QVariantMap row;
        row["id"] = qAll.value(0).toInt();
        row["name"] = qAll.value(1).toString();
        row["category"] = qAll.value(2).toString();
        row["description"] = qAll.value(3).toString();
        row["parent_id"] = qAll.value(4).toInt();
        row["parent_name"] = qAll.value(5).toString();
        row["is_archived"] = qAll.value(6).toInt();
        row["meeting_day"] = qAll.value(7).toString();
        row["meeting_time"] = qAll.value(8).toString();
        row["location"] = qAll.value(9).toString();
        row["member_count"] = qAll.value(10).toInt();

        int pId = row["parent_id"].toInt();
        if (pId > 0) {
            childrenMap[pId].append(row);
        } else {
            topLevelGroups.append(row);
        }
    }

    std::function<void(QTreeWidgetItem*, const QVariantMap&)> addGroupNode;
    addGroupNode = [&](QTreeWidgetItem* parentItem, const QVariantMap& groupData) {
        int id = groupData["id"].toInt();
        QString name = groupData["name"].toString();
        QString cat = groupData["category"].toString();
        QString parentName = groupData["parent_name"].toString();
        bool isArchived = groupData["is_archived"].toInt() == 1;
        QString day = groupData["meeting_day"].toString();
        QString time = groupData["meeting_time"].toString();
        QString loc = groupData["location"].toString();
        int memCount = groupData["member_count"].toInt();

        QString schedule = "-";
        if (!day.isEmpty() || !time.isEmpty()) {
            schedule = QString("%1 %2").arg(day, time).trimmed();
        }
        if (!loc.isEmpty()) {
            schedule += QString(" (%1)").arg(loc);
        }

        if (isArchived) {
            name += " [ARCHIVED]";
        }

        QTreeWidgetItem* item = nullptr;
        if (parentItem) {
            item = new QTreeWidgetItem(parentItem);
            item->setText(0, QString("↳ %1").arg(name));
            item->setForeground(0, QBrush(isArchived ? QColor("#6c7086") : QColor("#04a5e5")));
            item->setFont(0, QFont("Segoe UI", 9, QFont::DemiBold));
        } else {
            item = new QTreeWidgetItem(m_groupsTreeWidget);
            item->setText(0, name);
            if (isArchived) {
                item->setForeground(0, QBrush(QColor("#6c7086")));
            }
            item->setFont(0, QFont("Segoe UI", 10, QFont::Bold));
        }

        item->setText(1, cat);
        item->setText(2, schedule);
        item->setText(3, parentName.isEmpty() ? "-- Top Level --" : parentName);
        item->setText(4, QString("%1 members").arg(memCount));
        item->setText(5, isArchived ? "Archived" : "Active");
        item->setData(0, Qt::UserRole, id);

        if (childrenMap.contains(id)) {
            for (const auto& childData : childrenMap[id]) {
                addGroupNode(item, childData);
            }
            item->setExpanded(true);
        }
    };

    for (const auto& topData : topLevelGroups) {
        addGroupNode(nullptr, topData);
    }
}

void MainWindow::refreshGroupDetailPanel(int groupId) {
    m_selectedGroupId = groupId;
    if (!m_groupRosterTable || !m_lblGroupDetailTitle || !m_lblGroupDetailMeta || !m_lblGroupChemistryReport) return;

    ThemeTokens tok = ThemeManager::instance().currentTokens();

    if (groupId <= 0) {
        m_lblGroupDetailTitle->setText("Select a Ministry or Group");
        m_lblGroupDetailMeta->setText("Select a group from the list on the left to view active members, schedule, and pastoral dynamics.");
        m_groupRosterTable->setRowCount(0);
        m_lblGroupChemistryReport->setText("<i>No group selected.</i>");
        return;
    }

    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery qGrp(db);
    qGrp.prepare("SELECT g.name, g.category, g.description, g.meeting_day, g.meeting_time, g.location, "
                 "p.first_name || ' ' || p.last_name AS contact_name, g.max_capacity, g.notes, g.is_archived "
                 "FROM groups g "
                 "LEFT JOIN people p ON g.contact_person_id = p.id "
                 "WHERE g.id = ?");
    qGrp.addBindValue(groupId);

    if (!qGrp.exec() || !qGrp.next()) {
        m_lblGroupDetailTitle->setText("Group Not Found");
        m_lblGroupDetailMeta->setText("");
        m_groupRosterTable->setRowCount(0);
        m_lblGroupChemistryReport->setText("");
        return;
    }

    QString name = qGrp.value(0).toString();
    QString cat = qGrp.value(1).toString();
    QString desc = qGrp.value(2).toString();
    QString day = qGrp.value(3).toString();
    QString time = qGrp.value(4).toString();
    QString loc = qGrp.value(5).toString();
    QString contact = qGrp.value(6).toString();
    int capacity = qGrp.value(7).toInt();
    QString notes = qGrp.value(8).toString();
    bool isArchived = qGrp.value(9).toInt() == 1;

    m_lblGroupDetailTitle->setText(QString("🏛️ %1 %2").arg(name, isArchived ? QString("<span style='color: %1;'>(Archived)</span>").arg(tok.accentDanger.name()) : ""));
    m_lblGroupDetailTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(tok.accentPrimary.name()));

    QStringList metaParts;
    metaParts << QString("<b>Category:</b> %1").arg(cat);
    if (!day.isEmpty() || !time.isEmpty()) metaParts << QString("<b>Schedule:</b> %1 %2").arg(day, time).trimmed();
    if (!loc.isEmpty()) metaParts << QString("<b>Location:</b> %1").arg(loc);
    if (!contact.isEmpty()) metaParts << QString("<b>Primary Contact:</b> %1").arg(contact);
    if (capacity > 0) metaParts << QString("<b>Capacity:</b> Max %1 members").arg(capacity);
    if (!desc.isEmpty()) metaParts << QString("<b>Description:</b> %1").arg(desc);
    if (!notes.isEmpty()) metaParts << QString("<b>Pastoral Notes:</b> %1").arg(notes);

    m_lblGroupDetailMeta->setText(metaParts.join(" | "));
    m_lblGroupDetailMeta->setStyleSheet(QString("font-size: 11px; color: %1;").arg(tok.textSecondary.name()));

    // Dynamic style for group detail card to inherit light or dark theme cleanly
    if (m_groupDetailCard) {
        m_groupDetailCard->setStyleSheet(QString("QWidget#card { background-color: %1; border: 1px solid %2; border-radius: 10px; }")
                                          .arg(tok.bgSurface.name(), tok.borderColor.name()));
    }

    // Load Roster
    m_groupRosterTable->setRowCount(0);
    m_groupRosterTable->setStyleSheet(QString("QTableWidget { background-color: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
                                               "QHeaderView::section { background-color: %4; color: %2; font-weight: bold; border-bottom: 2px solid %5; padding: 6px; }")
                                       .arg(tok.bgInput.name(), tok.textPrimary.name(), tok.borderColor.name(), tok.bgSurface.name(), tok.accentPrimary.name()));

    QSqlQuery qRoster(db);
    qRoster.prepare("SELECT er.id, p.id, p.first_name || ' ' || p.last_name, er.relationship_type, p.membership_status, p.relational_style "
                    "FROM entity_relationships er "
                    "JOIN people p ON er.source_id = p.id "
                    "WHERE er.target_type = 'Group' AND er.target_id = ? "
                    "ORDER BY CASE er.relationship_type WHEN 'Leader' THEN 1 WHEN 'Co-Leader' THEN 2 ELSE 3 END, p.first_name");
    qRoster.addBindValue(groupId);

    int totalCount = 0;
    QMap<QString, int> stylesCount;
    QStringList burnoutAlerts;

    if (qRoster.exec()) {
        while (qRoster.next()) {
            int relId = qRoster.value(0).toInt();
            int personId = qRoster.value(1).toInt();
            QString memName = qRoster.value(2).toString();
            QString role = qRoster.value(3).toString();
            QString status = qRoster.value(4).toString();
            QString style = qRoster.value(5).toString();
            if (style.isEmpty()) style = "Unspecified";
            stylesCount[style]++;
            totalCount++;

            // Leadership Burnout Check (serving in 3+ leader roles)
            QSqlQuery qBurn(db);
            qBurn.prepare("SELECT COUNT(*) FROM entity_relationships WHERE source_type = 'Person' AND source_id = ? AND relationship_type IN ('Leader', 'Co-Leader')");
            qBurn.addBindValue(personId);
            if (qBurn.exec() && qBurn.next()) {
                int leaderRoleCount = qBurn.value(0).toInt();
                if (leaderRoleCount >= 3) {
                    burnoutAlerts.append(QString("<b>%1</b> (%2 leadership teams)").arg(memName).arg(leaderRoleCount));
                }
            }

            int r = m_groupRosterTable->rowCount();
            m_groupRosterTable->insertRow(r);

            // Member Name Item
            QTableWidgetItem* nameItem = new QTableWidgetItem(memName);
            nameItem->setForeground(QBrush(tok.textPrimary));
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            m_groupRosterTable->setItem(r, 0, nameItem);

            // Role Combo
            QComboBox* roleCombo = new QComboBox();
            roleCombo->addItems({"Member", "Leader", "Co-Leader"});
            roleCombo->setCurrentText(role);
            roleCombo->setStyleSheet(QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 4px; }")
                                     .arg(tok.bgInput.name(), tok.textPrimary.name(), tok.borderColor.name()));
            connect(roleCombo, &QComboBox::currentTextChanged, this, [this, relId](const QString& newRole) {
                QSqlQuery up;
                up.prepare("UPDATE entity_relationships SET relationship_type = ? WHERE id = ?");
                up.addBindValue(newRole);
                up.addBindValue(relId);
                up.exec();
                refreshGroupDetailPanel(m_selectedGroupId);
                refreshGroupsTree();
            });
            m_groupRosterTable->setCellWidget(r, 1, roleCombo);

            // Status Combo
            QComboBox* statusCombo = new QComboBox();
            statusCombo->addItems({"Active", "Inactive", "Guest", "Serving", "Visitor", "Member", "Regular Attender"});
            statusCombo->setCurrentText(status);
            statusCombo->setStyleSheet(QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 4px; }")
                                       .arg(tok.bgInput.name(), tok.textPrimary.name(), tok.borderColor.name()));
            connect(statusCombo, &QComboBox::currentTextChanged, this, [this, personId](const QString& newStatus) {
                QSqlQuery up;
                up.prepare("UPDATE people SET membership_status = ? WHERE id = ?");
                up.addBindValue(newStatus);
                up.addBindValue(personId);
                up.exec();
                refreshGroupDetailPanel(m_selectedGroupId);
            });
            m_groupRosterTable->setCellWidget(r, 2, statusCombo);

            // Action Button (Remove)
            QPushButton* btnRemove = new QPushButton("❌");
            btnRemove->setToolTip("Remove member from group");
            btnRemove->setStyleSheet(QString("border: none; color: %1; font-weight: bold; background: transparent;").arg(tok.accentDanger.name()));
            connect(btnRemove, &QPushButton::clicked, this, [this, relId, memName]() {
                auto confirm = QMessageBox::question(this, "Remove Member", QString("Remove '%1' from this group?").arg(memName), QMessageBox::Yes | QMessageBox::No);
                if (confirm == QMessageBox::Yes) {
                    QSqlQuery del;
                    del.prepare("DELETE FROM entity_relationships WHERE id = ?");
                    del.addBindValue(relId);
                    if (del.exec()) {
                        refreshGroupDetailPanel(m_selectedGroupId);
                        refreshGroupsTree();
                        if (m_graphWidget) m_graphWidget->loadGraph();
                    }
                }
            });
            m_groupRosterTable->setCellWidget(r, 3, btnRemove);
        }
    }

    // Dynamics & Chemistry Analysis
    m_lblGroupChemistryReport->setStyleSheet(QString("font-size: 11px; background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px; color: %3;")
                                              .arg(tok.bgInput.name(), tok.borderColor.name(), tok.textPrimary.name()));

    QString report = QString("<b>Roster Size:</b> %1 members ").arg(totalCount);
    if (capacity > 0 && totalCount > capacity) {
        report += QString("<span style='color: %1;'>(⚠️ Over Capacity: Max limit is %2)</span><br>").arg(tok.accentDanger.name()).arg(capacity);
    } else if (totalCount > 12) {
        report += QString("<span style='color: %1;'>(⚠️ Overcrowded: >12 members. Consider splitting into subgroups)</span><br>").arg(tok.accentDanger.name());
    } else {
        report += QString("<span style='color: %1;'>(Healthy Intimacy Level)</span><br>").arg(tok.accentSuccess.name());
    }

    if (!burnoutAlerts.isEmpty()) {
        report += QString("⚠️ <b>Leadership Burnout Warning:</b> %1<br>").arg(burnoutAlerts.join(", "));
    }

    report += "<b>Relational Chemistry Mix:</b> ";
    QStringList mix;
    for (auto it = stylesCount.begin(); it != stylesCount.end(); ++it) {
        mix.append(QString("%1: %2").arg(it.key()).arg(it.value()));
    }
    report += mix.join(" | ") + "<br>";

    if (!stylesCount.contains("Organizer")) {
        report += "💡 <i>Recommendation: Add an <b>Organizer</b> to handle group logistics.</i>";
    } else if (!stylesCount.contains("Facilitator")) {
        report += "💡 <i>Recommendation: Add a <b>Facilitator</b> to guide discussion.</i>";
    } else if (!stylesCount.contains("Listener") && !stylesCount.contains("Energizer")) {
        report += "💡 <i>Recommendation: Add a <b>Listener / Energizer</b> to enhance relational warmth.</i>";
    } else {
        report += "✨ <i>Balanced Group Chemistry! Great combination of roles.</i>";
    }

    m_lblGroupChemistryReport->setText(report);
}

void MainWindow::showGroupDialog(int groupId, bool isDuplicate) {
    QSqlDatabase db = QSqlDatabase::database();
    ThemeTokens tok = ThemeManager::instance().currentTokens();

    QString currentName = "";
    QString currentCat = "Ministry Team";
    QString currentDesc = "";
    int currentParentId = 0;
    QString currentDay = "Flexible / None";
    QString currentTime = "";
    QString currentLoc = "";
    int currentContactId = 0;
    int currentCap = 0;
    QString currentNotes = "";

    if (groupId > 0) {
        QSqlQuery q(db);
        q.prepare("SELECT name, category, description, parent_id, meeting_day, meeting_time, location, contact_person_id, max_capacity, notes FROM groups WHERE id = ?");
        q.addBindValue(groupId);
        if (q.exec() && q.next()) {
            currentName = q.value(0).toString();
            currentCat = q.value(1).toString();
            currentDesc = q.value(2).toString();
            currentParentId = q.value(3).toInt();
            currentDay = q.value(4).toString();
            currentTime = q.value(5).toString();
            currentLoc = q.value(6).toString();
            currentContactId = q.value(7).toInt();
            currentCap = q.value(8).toInt();
            currentNotes = q.value(9).toString();
        }
    }

    QDialog dlg(this);
    if (isDuplicate) {
        dlg.setWindowTitle("Duplicate Group");
    } else if (groupId > 0) {
        dlg.setWindowTitle(QString("Edit Group — %1").arg(currentName));
    } else {
        dlg.setWindowTitle("Create New Group");
    }

    dlg.resize(420, 480);
    dlg.setStyleSheet(QString("QDialog { background-color: %1; color: %2; } QLabel { color: %2; } QLineEdit, QComboBox, QSpinBox, QTextEdit { background-color: %3; color: %2; border: 1px solid #45475a; border-radius: 6px; padding: 4px; }")
                      .arg(tok.bgSurface.name(), tok.textPrimary.name(), tok.bgBase.name()));

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(10);

    QLineEdit* nameEdit = new QLineEdit(isDuplicate ? QString("%1 (Copy)").arg(currentName) : currentName, &dlg);
    
    QComboBox* catCombo = new QComboBox(&dlg);
    catCombo->addItems({"Ministry Team", "Home Group", "Sunday School", "Bible Study", "Serving Team"});
    if (!currentCat.isEmpty()) catCombo->setCurrentText(currentCat);

    QComboBox* parentCombo = new QComboBox(&dlg);
    parentCombo->addItem("-- None (Top Level Ministry) --", 0);
    QSqlQuery qParents("SELECT id, name FROM groups WHERE id != ? AND (parent_id IS NULL OR parent_id IN (SELECT id FROM groups WHERE parent_id IS NULL)) ORDER BY name", db);
    qParents.addBindValue(groupId);
    while (qParents.next()) {
        parentCombo->addItem(qParents.value(1).toString(), qParents.value(0).toInt());
    }
    int pIdx = parentCombo->findData(currentParentId);
    if (pIdx >= 0) parentCombo->setCurrentIndex(pIdx);

    QComboBox* dayCombo = new QComboBox(&dlg);
    dayCombo->addItems({"Flexible / None", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"});
    if (!currentDay.isEmpty()) dayCombo->setCurrentText(currentDay);

    QLineEdit* timeEdit = new QLineEdit(currentTime, &dlg);
    timeEdit->setPlaceholderText("e.g. 7:00 PM");

    QLineEdit* locationEdit = new QLineEdit(currentLoc, &dlg);
    locationEdit->setPlaceholderText("e.g. Room 204 / Fellowship Hall");

    QComboBox* contactCombo = new QComboBox(&dlg);
    contactCombo->addItem("-- None Assigned --", 0);
    QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name FROM people ORDER BY first_name", db);
    while (qPeople.next()) {
        contactCombo->addItem(qPeople.value(1).toString(), qPeople.value(0).toInt());
    }
    int cIdx = contactCombo->findData(currentContactId);
    if (cIdx >= 0) contactCombo->setCurrentIndex(cIdx);

    QSpinBox* capacitySpin = new QSpinBox(&dlg);
    capacitySpin->setRange(0, 500);
    capacitySpin->setSuffix(" members (0 = Unlimited)");
    capacitySpin->setValue(currentCap);

    QLineEdit* descEdit = new QLineEdit(currentDesc, &dlg);

    QTextEdit* notesEdit = new QTextEdit(&dlg);
    notesEdit->setMaximumHeight(60);
    notesEdit->setText(currentNotes);

    QCheckBox* chkCopyMembers = nullptr;
    if (isDuplicate) {
        chkCopyMembers = new QCheckBox("Copy current members & leaders to duplicate", &dlg);
        chkCopyMembers->setChecked(true);
    }

    form->addRow("Group Name:", nameEdit);
    form->addRow("Category:", catCombo);
    form->addRow("Parent Group:", parentCombo);
    form->addRow("Meeting Day:", dayCombo);
    form->addRow("Meeting Time:", timeEdit);
    form->addRow("Location / Room:", locationEdit);
    form->addRow("Primary Contact:", contactCombo);
    form->addRow("Max Capacity:", capacitySpin);
    form->addRow("Description:", descEdit);
    form->addRow("Pastoral Notes:", notesEdit);
    if (chkCopyMembers) form->addRow("", chkCopyMembers);

    QDialogButtonBox* buttons = new QDialogButtonBox(&dlg);
    QPushButton* btnAccept = buttons->addButton(groupId > 0 && !isDuplicate ? "Save" : "Create", QDialogButtonBox::AcceptRole);
    btnAccept->setObjectName("primaryBtn");
    QPushButton* btnCancel = buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
    btnCancel->setObjectName("secondaryBtn");

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Group name cannot be empty.");
            return;
        }

        QString cat = catCombo->currentText();
        int parentId = parentCombo->currentData().toInt();
        QString day = dayCombo->currentText();
        QString time = timeEdit->text().trimmed();
        QString loc = locationEdit->text().trimmed();
        int contactId = contactCombo->currentData().toInt();
        int cap = capacitySpin->value();
        QString desc = descEdit->text().trimmed();
        QString notes = notesEdit->toPlainText().trimmed();

        if (groupId > 0 && !isDuplicate) {
            QSqlQuery update(db);
            update.prepare("UPDATE groups SET name = ?, category = ?, parent_id = ?, meeting_day = ?, meeting_time = ?, "
                           "location = ?, contact_person_id = ?, max_capacity = ?, description = ?, notes = ? WHERE id = ?");
            update.addBindValue(name);
            update.addBindValue(cat);
            update.addBindValue(parentId > 0 ? QVariant(parentId) : QVariant(QMetaType(QMetaType::Int)));
            update.addBindValue(day);
            update.addBindValue(time);
            update.addBindValue(loc);
            update.addBindValue(contactId > 0 ? QVariant(contactId) : QVariant(QMetaType(QMetaType::Int)));
            update.addBindValue(cap);
            update.addBindValue(desc);
            update.addBindValue(notes);
            update.addBindValue(groupId);

            if (update.exec()) {
                refreshGroupsTree();
                refreshGroupDetailPanel(groupId);
                if (m_graphWidget) m_graphWidget->loadGraph();
                QMessageBox::information(this, "Success", "Group updated successfully.");
            } else {
                QMessageBox::critical(this, "Database Error", "Failed to update group: " + update.lastError().text());
            }
        } else {
            db.transaction();
            QSqlQuery ins(db);
            ins.prepare("INSERT INTO groups (name, category, parent_id, meeting_day, meeting_time, location, contact_person_id, max_capacity, description, notes, is_archived) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
            ins.addBindValue(name);
            ins.addBindValue(cat);
            ins.addBindValue(parentId > 0 ? QVariant(parentId) : QVariant(QMetaType(QMetaType::Int)));
            ins.addBindValue(day);
            ins.addBindValue(time);
            ins.addBindValue(loc);
            ins.addBindValue(contactId > 0 ? QVariant(contactId) : QVariant(QMetaType(QMetaType::Int)));
            ins.addBindValue(cap);
            ins.addBindValue(desc);
            ins.addBindValue(notes);

            if (ins.exec()) {
                int newId = ins.lastInsertId().toInt();
                if (isDuplicate && chkCopyMembers && chkCopyMembers->isChecked()) {
                    QSqlQuery copyMems(db);
                    copyMems.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type) "
                                     "SELECT source_type, source_id, 'Group', ?, relationship_type "
                                     "FROM entity_relationships WHERE target_type = 'Group' AND target_id = ?");
                    copyMems.addBindValue(newId);
                    copyMems.addBindValue(groupId);
                    copyMems.exec();
                }
                db.commit();
                refreshGroupsTree();
                refreshGroupDetailPanel(newId);
                if (m_graphWidget) m_graphWidget->loadGraph();
                QMessageBox::information(this, "Success", QString("Group '%1' created successfully.").arg(name));
            } else {
                db.rollback();
                QMessageBox::critical(this, "Database Error", "Failed to create group: " + ins.lastError().text());
            }
        }
    }
}

void MainWindow::setActiveNav(QPushButton* btn) {
    if (m_activeNavButton) {
        m_activeNavButton->setProperty("active", false);
        m_activeNavButton->style()->polish(m_activeNavButton);
    }
    if (btn) {
        btn->setProperty("active", true);
        btn->style()->polish(btn);
    }
    m_activeNavButton = btn;
}
