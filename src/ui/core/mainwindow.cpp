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
#include <QStatusBar>
#include "../database/databasemanager.h"
#include "../common/appcurrency.h"
#include "../ministry/matchdialog.h"
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
#include <functional>

// QTreeWidget that supports drag-and-drop reparenting of ministry groups.
// Drops are reported through onReparent(groupId, newParentId); the caller is
// responsible for the cycle guard and database update.
class GroupsTreeWidget : public QTreeWidget {
public:
    std::function<void(int, int)> onReparent;

    explicit GroupsTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
    }

protected:
    void dropEvent(QDropEvent* event) override {
        QTreeWidgetItem* dragged = currentItem();
        if (!dragged) { QTreeWidget::dropEvent(event); return; }
        const int draggedId = dragged->data(0, Qt::UserRole).toInt();
        if (draggedId <= 0) { event->ignore(); return; }

        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        const DropIndicatorPosition pos = dropIndicatorPosition();
        int newParentId = 0;
        if (target) {
            const int targetId = target->data(0, Qt::UserRole).toInt();
            if (pos == OnItem) {
                newParentId = targetId; // dropped onto a group -> its child
            } else if (QTreeWidgetItem* tParent = target->parent()) {
                newParentId = tParent->data(0, Qt::UserRole).toInt(); // sibling
            }
        }
        if (newParentId == draggedId) { event->ignore(); return; }
        if (onReparent) onReparent(draggedId, newParentId);
        event->accept();
    }
};

class LoginDialog : public QDialog {
public:
    LoginDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("OpenEkklesia Authentication");
        resize(340, 220);

        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(20, 20, 20, 20);
        lay->setSpacing(12);

        QLabel* header = new QLabel("🔒 Enter OpenEkklesia Access PIN:", this);
        header->setObjectName("detailHeader");
        lay->addWidget(header);

        m_pinEdit = new QLineEdit(this);
        m_pinEdit->setEchoMode(QLineEdit::Password);
        m_pinEdit->setMaxLength(16);
        m_pinEdit->setPlaceholderText("PIN (e.g. 1234 for Admin)");
        m_pinEdit->setFocus();
        connect(m_pinEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLogin);
        lay->addWidget(m_pinEdit);

        QLabel* hint = new QLabel("<small style='color: #89b4fa;'>Default PINs: Admin <b>1234</b> | Pastor <b>5555</b> | Finance <b>7777</b></small>", this);
        hint->setTextFormat(Qt::RichText);
        lay->addWidget(hint);

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
        btnLogin->setDefault(true);
        connect(btnLogin, &QPushButton::clicked, this, &LoginDialog::onLogin);

        buttons->addWidget(btnGuest);
        buttons->addWidget(btnLogin);
        lay->addLayout(buttons);
    }

    QString role() const { return m_role.isEmpty() ? "admin" : m_role; }
    QString username() const { return m_username.isEmpty() ? "Admin" : m_username; }

private:
    void onLogin() {
        QString pin = m_pinEdit->text().trimmed();
        if (pin.isEmpty()) {
            QMessageBox::warning(this, "PIN Required", "Please enter your access PIN (e.g. 1234 for Admin).");
            m_pinEdit->setFocus();
            return;
        }

        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery query(db);
        query.prepare("SELECT username, role FROM users WHERE pin = ?");
        query.addBindValue(pin);
        if (query.exec() && query.next()) {
            m_username = query.value(0).toString();
            m_role = query.value(1).toString();
            accept();
        } else {
            QMessageBox::warning(this, "Invalid PIN", QString("The access PIN '%1' is incorrect.\n\nUse 1234 for Admin access.").arg(pin));
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

    m_actMembers = congMenu->addAction("👥 People Directory");
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

    m_btnMembers = new QPushButton("👥 People", this);
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
    m_btnContributions->setToolTip("Contributions: Record donations, select funds, and view batches.");
    layFin->addWidget(m_btnContributions);

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

    m_chkShowMembersInTree = new QCheckBox("👥 Show People in Tree", this);
    m_chkShowMembersInTree->setStyleSheet("font-size: 12px; font-weight: bold;");
    connect(m_chkShowMembersInTree, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) {
        refreshGroupsTree();
    });
    grpHeaderLayout->addWidget(m_chkShowMembersInTree);

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

    // Left Panel: Group Tree List (drag & drop to reparent)
    m_groupsTreeWidget = new GroupsTreeWidget(this);
    m_groupsTreeWidget->setHeaderLabels({"Name", "Category", "Schedule & Location", "Parent Group", "Members", "Status"});
    m_groupsTreeWidget->setAlternatingRowColors(true);
    m_groupsTreeWidget->setAnimated(true);
    m_groupsTreeWidget->onReparent = [this](int groupId, int newParentId) {
        // Cycle guard: never move a group under its own descendant.
        QSqlQuery cyc;
        cyc.prepare(
            "WITH RECURSIVE descendants(id) AS ("
            "  SELECT id FROM groups WHERE parent_id = ? "
            "  UNION ALL "
            "  SELECT g.id FROM groups g JOIN descendants d ON g.parent_id = d.id"
            ") SELECT 1 FROM descendants WHERE id = ?");
        cyc.addBindValue(groupId);
        cyc.addBindValue(newParentId);
        if (cyc.exec() && cyc.next()) {
            QMessageBox::warning(this, "Invalid Move", "A group cannot be moved under one of its own subgroups.");
            return;
        }
        QSqlQuery u;
        u.prepare("UPDATE groups SET parent_id = ? WHERE id = ?");
        u.addBindValue(newParentId > 0 ? QVariant(newParentId) : QVariant(QMetaType(QMetaType::Int)));
        u.addBindValue(groupId);
        if (u.exec()) {
            refreshGroupsTree();
            refreshGroupDetailPanel(m_selectedGroupId);
            if (m_graphWidget) m_graphWidget->loadGraph();
        }
    };
    m_groupsTreeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_groupsTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_groupsTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupsTreeWidget, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_groupsTreeWidget->itemAt(pos);
        if (!item) return;
        int groupId = item->data(0, Qt::UserRole).toInt();
        QString groupName = item->text(0);
        if (groupId <= 0) return;

        QMenu menu(this);
        QAction* actEdit = menu.addAction(QString("✏️ Edit '%1' Details...").arg(groupName));
        QAction* actAddSub = menu.addAction("➕ Add Sub-Group / Sub-Ministry...");
        QAction* actAddMember = menu.addAction("👥 Add Member to Group...");
        QAction* actGraph = menu.addAction("🕸️ Highlight in Connection Graph");
        menu.addSeparator();
        QAction* actArchive = menu.addAction("🗄️ Toggle Archive Status");
        QAction* actDelete = menu.addAction("🗑️ Delete Group...");

        QAction* sel = menu.exec(m_groupsTreeWidget->viewport()->mapToGlobal(pos));
        if (!sel) return;

        m_selectedGroupId = groupId; // the right-clicked row is the target

        if (sel == actEdit) {
            onEditGroup();
        } else if (sel == actAddSub) {
            onAddGroup();
        } else if (sel == actAddMember) {
            m_selectedGroupId = groupId;
            refreshGroupDetailPanel(groupId);
        } else if (sel == actGraph) {
            onGraphClicked();
        } else if (sel == actArchive) {
            onArchiveGroup();
        } else if (sel == actDelete) {
            onDeleteGroup();
        }
    });
    splitter->addWidget(m_groupsTreeWidget);

    connect(m_groupsTreeWidget, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) {
            refreshGroupDetailPanel(0);
            return;
        }
        QString type = current->data(0, Qt::UserRole + 1).toString();
        if (type == "Person") {
            int gId = current->data(0, Qt::UserRole + 2).toInt();
            refreshGroupDetailPanel(gId);
        } else {
            int groupId = current->data(0, Qt::UserRole).toInt();
            refreshGroupDetailPanel(groupId);
        }
    });

    connect(m_groupsTreeWidget, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* current, int) {
        if (!current) return;
        QString type = current->data(0, Qt::UserRole + 1).toString();
        if (type == "Person") {
            int personId = current->data(0, Qt::UserRole).toInt();
            if (personId > 0 && m_peopleWidget) {
                m_stackedWidget->setCurrentWidget(m_peopleWidget);
                m_peopleWidget->selectPerson(personId);
            }
        }
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

    QPushButton* btnSuggest = new QPushButton("✨ Suggest People", m_groupDetailCard);
    btnSuggest->setObjectName("secondaryBtn");
    btnSuggest->setStyleSheet("padding: 4px 10px; font-size: 11px;");
    connect(btnSuggest, &QPushButton::clicked, this, [this]() {
        if (m_selectedGroupId <= 0) {
            QMessageBox::warning(this, "No Selection", "Please select a group first.");
            return;
        }
        MatchDialog dlg(m_selectedGroupId, this);
        connect(&dlg, &MatchDialog::openProfile, this, [this](int personId) { selectPerson(personId); });
        dlg.exec();
        if (!dlg.addedPersonIds().isEmpty()) {
            refreshGroupDetailPanel(m_selectedGroupId);
            refreshGroupsTree();
            if (m_graphWidget) m_graphWidget->loadGraph();
        }
    });
    rosterHeaderRow->addWidget(btnSuggest);
    detailLayout->addLayout(rosterHeaderRow);

    m_groupRosterTable = new QTableWidget(m_groupDetailCard);
    m_groupRosterTable->setColumnCount(4);
    m_groupRosterTable->setHorizontalHeaderLabels({"Member", "Role", "Status", "Action"});
    // Interactive so users can drag column borders; the Member column stretches
    // to fill, and the rest auto-fit their content but stay adjustable. A
    // minimum section size stops columns from being squished unreadably.
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_groupRosterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_groupRosterTable->horizontalHeader()->setMinimumSectionSize(70);
    m_groupRosterTable->horizontalHeader()->setDefaultSectionSize(100);
    m_groupRosterTable->horizontalHeader()->setStretchLastSection(false);
    m_groupRosterTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupRosterTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int r = m_groupRosterTable->rowAt(pos.y());
        if (r < 0 || !m_groupRosterTable->item(r, 0)) return;
        int personId = m_groupRosterTable->item(r, 0)->data(Qt::UserRole).toInt();
        int relId = m_groupRosterTable->item(r, 0)->data(Qt::UserRole + 1).toInt();
        QString memName = m_groupRosterTable->item(r, 0)->text();

        QMenu menu(this);
        QAction* actProfile = menu.addAction(QString("👤 View %1's Profile").arg(memName));
        QAction* actCareNote = menu.addAction("✍️ Log Pastoral Care Note...");
        QMenu* mRole = menu.addMenu("👑 Change Assigned Role");
        QAction* rMember = mRole->addAction("Member");
        QAction* rLeader = mRole->addAction("Leader");
        QAction* rCoLeader = mRole->addAction("Co-Leader");

        menu.addSeparator();
        QAction* actRemove = menu.addAction(QString("❌ Remove %1 from Group").arg(memName));

        QAction* sel = menu.exec(m_groupRosterTable->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actProfile) {
            selectPerson(personId);
        } else if (sel == actCareNote) {
            onLogQuickPastoralNote(personId);
        } else if (mRole->actions().contains(sel)) {
            QString newRole = sel->text();
            QSqlQuery up;
            up.prepare("UPDATE entity_relationships SET relationship_type = ? WHERE id = ?");
            up.addBindValue(newRole);
            up.addBindValue(relId);
            up.exec();
            refreshGroupDetailPanel(m_selectedGroupId);
            refreshGroupsTree();
        } else if (sel == actRemove) {
            QSqlQuery dq;
            dq.prepare("DELETE FROM entity_relationships WHERE id = ?");
            dq.addBindValue(relId);
            dq.exec();
            refreshGroupDetailPanel(m_selectedGroupId);
            refreshGroupsTree();
        }
    });
    m_groupRosterTable->setWordWrap(false);
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
    m_stackedWidget->addWidget(makePlaceholder()); // 5: Connection Graph
    m_stackedWidget->addWidget(makePlaceholder()); // 6: Asset Inventory
    m_stackedWidget->addWidget(makePlaceholder()); // 7: Document Vault
    m_stackedWidget->addWidget(makePlaceholder()); // 8: Settings
    m_stackedWidget->addWidget(makePlaceholder()); // 9: Facilities
    m_stackedWidget->addWidget(makePlaceholder()); // 10: Analytics
    m_stackedWidget->addWidget(makePlaceholder()); // 11: Planning
    m_stackedWidget->addWidget(makePlaceholder()); // 12: Shepherd's Workbench
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
    QPushButton* btnAddPersonQuick = new QPushButton("➕ Add Person", page);
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

    // --- Top KPI Section: Dynamic Summary Cards & Config Button ---
    QHBoxLayout* kpiHeaderRow = new QHBoxLayout();
    QLabel* lblKpiSection = new QLabel("Key Performance Indicators", page);
    lblKpiSection->setStyleSheet("font-size: 13px; font-weight: bold; opacity: 0.85;");
    QPushButton* btnConfigKpi = new QPushButton("⚙️ Configure KPIs", page);
    btnConfigKpi->setObjectName("secondaryBtn");
    btnConfigKpi->setStyleSheet("padding: 3px 8px; font-size: 11px;");
    connect(btnConfigKpi, &QPushButton::clicked, this, &MainWindow::onConfigureKpiCards);
    kpiHeaderRow->addWidget(lblKpiSection);
    kpiHeaderRow->addStretch();
    kpiHeaderRow->addWidget(btnConfigKpi);
    layout->addLayout(kpiHeaderRow);

    m_cardsLayout = new QHBoxLayout();
    m_cardsLayout->setSpacing(15);
    layout->addLayout(m_cardsLayout);
    rebuildKpiCards();

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

    // Right Column: Pastoral Care Alerts, Recent Visitors, & Upcoming Services
    QWidget* rightCol = new QWidget(page);
    QVBoxLayout* rightColLayout = new QVBoxLayout(rightCol);
    rightColLayout->setContentsMargins(0, 0, 0, 0);
    rightColLayout->setSpacing(15);

    // Pastoral Care & Visitors Card
    QWidget* alertsCard = new QWidget(rightCol);
    alertsCard->setObjectName("cardWidget");
    QVBoxLayout* alertsCardLayout = new QVBoxLayout(alertsCard);
    alertsCardLayout->setContentsMargins(16, 14, 16, 14);
    alertsCardLayout->setSpacing(10);

    QHBoxLayout* alertsTitleRow = new QHBoxLayout();
    QLabel* alertsCardTitle = new QLabel("⚠️ Member Inactivity & Care Alerts", alertsCard);
    alertsCardTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: #f38ba8;");
    QPushButton* btnQuickNote = new QPushButton("✍️ Quick Note", alertsCard);
    btnQuickNote->setObjectName("primaryBtn");
    btnQuickNote->setStyleSheet("padding: 2px 8px; font-size: 11px;");
    connect(btnQuickNote, &QPushButton::clicked, this, [this]() {
        int pid = -1;
        if (m_lstPastoralAlerts->currentItem()) pid = m_lstPastoralAlerts->currentItem()->data(Qt::UserRole).toInt();
        else if (m_lstVisitorFollowup->currentItem()) pid = m_lstVisitorFollowup->currentItem()->data(Qt::UserRole).toInt();
        onLogQuickPastoralNote(pid);
    });
    alertsTitleRow->addWidget(alertsCardTitle);
    alertsTitleRow->addStretch();
    alertsTitleRow->addWidget(btnQuickNote);
    alertsCardLayout->addLayout(alertsTitleRow);

    m_lstPastoralAlerts = new QListWidget(alertsCard);
    m_lstPastoralAlerts->setMaximumHeight(100);
    m_lstPastoralAlerts->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_lstPastoralAlerts, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = m_lstPastoralAlerts->itemAt(pos);
        if (!it) return;
        int pid = it->data(Qt::UserRole).toInt();
        QMenu m(this);
        QAction* aNote = m.addAction("✍️ Log Quick Pastoral Note...");
        QAction* aProf = m.addAction("👤 View Member Dossier");
        QAction* sel = m.exec(m_lstPastoralAlerts->mapToGlobal(pos));
        if (sel == aNote) onLogQuickPastoralNote(pid);
        else if (sel == aProf) selectPerson(pid);
    });
    connect(m_lstPastoralAlerts, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int personId = item->data(Qt::UserRole).toInt();
        if (personId > 0) selectPerson(personId);
    });
    alertsCardLayout->addWidget(m_lstPastoralAlerts);

    QLabel* followupTitle = new QLabel("🤝 Recent Visitors — Follow-up", alertsCard);
    followupTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: #f9e2af;");
    m_lstVisitorFollowup = new QListWidget(alertsCard);
    m_lstVisitorFollowup->setMaximumHeight(100);
    m_lstVisitorFollowup->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_lstVisitorFollowup, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = m_lstVisitorFollowup->itemAt(pos);
        if (!it) return;
        int pid = it->data(Qt::UserRole).toInt();
        QMenu m(this);
        QAction* aNote = m.addAction("✍️ Log Quick Pastoral Note...");
        QAction* aProf = m.addAction("👤 View Member Dossier");
        QAction* sel = m.exec(m_lstVisitorFollowup->mapToGlobal(pos));
        if (sel == aNote) onLogQuickPastoralNote(pid);
        else if (sel == aProf) selectPerson(pid);
    });
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

void MainWindow::rebuildKpiCards() {
    if (!m_cardsLayout) return;

    while (QLayoutItem* item = m_cardsLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_kpiRegistry.clear();

    QSettings s("OpenEkklesia", "CHMS");
    QStringList defaultKpis = {"members", "visitors", "groups", "shepherd_due", "giving_month", "giving_ytd"};
    m_activeKpis = s.value("dashboardKpis", defaultKpis).toStringList();
    if (m_activeKpis.isEmpty()) m_activeKpis = defaultKpis;

    auto makeCard = [this](const QString& id, const QString& title, const QString& iconStr, const QString& subtext) {
        QWidget* card = new QWidget(this);
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

        QLabel* valLabel = new QLabel("0", card);
        valLabel->setStyleSheet("font-size: 24px; font-weight: bold; margin-top: 2px;");
        v->addWidget(valLabel);

        QLabel* lblSub = new QLabel(subtext, card);
        lblSub->setStyleSheet("font-size: 11px; opacity: 0.6;");
        v->addWidget(lblSub);

        KpiCardInfo info;
        info.id = id;
        info.title = title;
        info.icon = iconStr;
        info.subtext = subtext;
        info.widget = card;
        info.valLabel = valLabel;
        m_kpiRegistry.insert(id, info);

        m_cardsLayout->addWidget(card);
    };

    for (const QString& kpiId : m_activeKpis) {
        if (kpiId == "members") makeCard("members", "Members", "👥", "Active Congregation");
        else if (kpiId == "visitors") makeCard("visitors", "Visitors / Attenders", "🤝", "Growth Pipeline");
        else if (kpiId == "groups") makeCard("groups", "Active Groups", "🏠", "Small Groups & Ministries");
        else if (kpiId == "shepherd_due") makeCard("shepherd_due", "Follow-ups Due", "📌", "Pastoral Queue (Today)");
        else if (kpiId == "at_risk") makeCard("at_risk", "At-Risk Members", "⚠️", "Pastoral Care Flags");
        else if (kpiId == "unconnected") makeCard("unconnected", "Unconnected", "👥", "Not in Small Group");
        else if (kpiId == "giving_month") makeCard("giving_month", "Giving (This Month)", "💰", "Offerings Recorded");
        else if (kpiId == "giving_ytd") makeCard("giving_ytd", "Giving (YTD)", "📈", "Annual Tithes");
        else if (kpiId == "checkins") makeCard("checkins", "Check-ins", "📌", "Kiosk Attendance");
        else if (kpiId == "total_assets") makeCard("total_assets", "Total Assets", "🏦", "Account Balances");
    }

    updateDashboardStats();
}

void MainWindow::onConfigureKpiCards() {
    QDialog dlg(this);
    dlg.setWindowTitle("Configure Dashboard KPI Cards");
    dlg.resize(480, 400);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(12);

    QLabel* info = new QLabel("Select between 2 and 8 Key Performance Indicator cards to display on your main dashboard:", &dlg);
    info->setWordWrap(true);
    lay->addWidget(info);

    QList<QPair<QString, QString>> available = {
        {"members", "👥 Church Members (Formal Membership)"},
        {"visitors", "🤝 Growth Pipeline (Visitors & Regular Attenders)"},
        {"groups", "🏠 Active Ministries & Small Groups"},
        {"shepherd_due", "📌 Follow-ups Due Today (Shepherd's Workbench)"},
        {"at_risk", "⚠️ At-Risk Members & Care Flags"},
        {"unconnected", "👥 Unconnected Members (No Small Group)"},
        {"giving_month", "💰 Giving Recorded This Month"},
        {"giving_ytd", "📈 Giving Year-to-Date"},
        {"checkins", "📌 Kiosk Check-ins (Attendance)"},
        {"total_assets", "🏦 Total Assets / Account Balances"}
    };

    QMap<QString, QCheckBox*> checkBoxes;
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(10);
    int row = 0;
    for (const auto& pair : available) {
        QCheckBox* chk = new QCheckBox(pair.second, &dlg);
        chk->setChecked(m_activeKpis.contains(pair.first));
        checkBoxes.insert(pair.first, chk);
        grid->addWidget(chk, row / 2, row % 2);
        ++row;
    }
    lay->addLayout(grid);

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* btnSave = new QPushButton("Save Dashboard Layout", &dlg);
    btnSave->setObjectName("primaryBtn");
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btnCancel->setObjectName("secondaryBtn");
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnSave);
    lay->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnSave, &QPushButton::clicked, &dlg, [this, &dlg, checkBoxes, available]() {
        QStringList selected;
        for (const auto& pair : available) {
            if (checkBoxes.value(pair.first)->isChecked()) {
                selected << pair.first;
            }
        }
        if (selected.size() < 2 || selected.size() > 8) {
            QMessageBox::warning(&dlg, "Selection Limit", "Please select between 2 and 8 KPI cards for optimal layout.");
            return;
        }
        QSettings s("OpenEkklesia", "CHMS");
        s.setValue("dashboardKpis", selected);
        m_activeKpis = selected;
        rebuildKpiCards();
        dlg.accept();
    });

    dlg.exec();
}

void MainWindow::onLogQuickPastoralNote(int prefillPersonId) {
    QDialog dlg(this);
    dlg.setWindowTitle("Log Quick Pastoral Note");
    dlg.resize(440, 380);
    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QComboBox* personCombo = new QComboBox(&dlg);
    personCombo->setMinimumWidth(260);
    QSqlQuery pq;
    pq.exec("SELECT id, first_name || ' ' || last_name || ' (' || membership_status || ')' FROM people ORDER BY first_name");
    while (pq.next()) {
        personCombo->addItem(pq.value(1).toString(), pq.value(0).toInt());
    }
    if (prefillPersonId > 0) {
        int idx = personCombo->findData(prefillPersonId);
        if (idx >= 0) personCombo->setCurrentIndex(idx);
    }
    form->addRow("Member:", personCombo);

    QComboBox* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({"Phone Call", "Visitation", "Counseling", "Prayer", "Hospital Visit", "Follow-up Call", "Benevolence", "Discipleship", "Other"});
    form->addRow("Care Type:", typeCombo);

    QTextEdit* notesEdit = new QTextEdit(&dlg);
    notesEdit->setPlaceholderText("Enter brief conversation notes, pastoral observations, prayer requests...");
    form->addRow("Notes:", notesEdit);

    QCheckBox* followUpCheck = new QCheckBox("Schedule 7-day follow-up in Shepherd's Queue", &dlg);
    followUpCheck->setChecked(true);
    form->addRow("", followUpCheck);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    int personId = personCombo->currentData().toInt();
    if (personId <= 0) return;

    QString notes = notesEdit->toPlainText().trimmed();
    if (notes.isEmpty()) {
        QMessageBox::warning(this, "Missing Details", "Please enter a short note describing the care interaction.");
        return;
    }

    QString careType = typeCombo->currentText();
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("INSERT INTO care_logs (person_id, care_type, notes, follow_up) VALUES (?, ?, ?, ?)");
    q.addBindValue(personId);
    q.addBindValue(careType);
    q.addBindValue(notes);
    q.addBindValue(followUpCheck->isChecked() ? 1 : 0);
    q.exec();

    if (followUpCheck->isChecked()) {
        QSqlQuery qPastoral(db);
        qPastoral.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, outcome, follow_up_date, follow_up_done) "
                          "VALUES (?, ?, date('now', 'localtime'), ?, 'Logged via Dashboard Quick Note', date('now', '+7 days', 'localtime'), 0)");
        qPastoral.addBindValue(personId);
        qPastoral.addBindValue(careType);
        qPastoral.addBindValue(notes);
        qPastoral.exec();
    }

    updateDashboardStats();
    if (m_shepherdWidget) m_shepherdWidget->refreshAll();

    QMessageBox::information(this, "Note Logged", QString("Pastoral care note logged for %1.").arg(personCombo->currentText()));
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
    // Guard: rebuildKpiCards() calls us while createDashboardPage() is still
    // building the dashboard (alerts/upcoming/budget widgets not yet created).
    // The real refresh happens once the page is complete (e.g. ctor line 176).
    // m_lstVisitorFollowup is created last, so it is the sentinel.
    if (!m_lstPastoralAlerts || !m_lstVisitorFollowup) return;

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

    // 2. Compute Active KPI Card Values
    if (m_kpiRegistry.contains("members") && m_kpiRegistry["members"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM people WHERE membership_status = 'Member'") && query.next()) {
            m_kpiRegistry["members"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("visitors") && m_kpiRegistry["visitors"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM people WHERE membership_status IN ('Visitor', 'Regular Attender')") && query.next()) {
            m_kpiRegistry["visitors"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("groups") && m_kpiRegistry["groups"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM groups") && query.next()) {
            m_kpiRegistry["groups"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("shepherd_due") && m_kpiRegistry["shepherd_due"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM pastoral_activities WHERE follow_up_date IS NOT NULL AND follow_up_date <= date('now', 'localtime') AND follow_up_done = 0") && query.next()) {
            m_kpiRegistry["shepherd_due"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("at_risk") && m_kpiRegistry["at_risk"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM people WHERE care_flag IN ('At-Risk', 'Needs Contact')") && query.next()) {
            m_kpiRegistry["at_risk"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("unconnected") && m_kpiRegistry["unconnected"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM people WHERE id NOT IN (SELECT DISTINCT source_id FROM entity_relationships WHERE source_type = 'Person' AND target_type = 'Group')") && query.next()) {
            m_kpiRegistry["unconnected"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("checkins") && m_kpiRegistry["checkins"].valLabel) {
        if (query.exec("SELECT COUNT(*) FROM attendance WHERE attended = 1") && query.next()) {
            m_kpiRegistry["checkins"].valLabel->setText(QString::number(query.value(0).toInt()));
        }
    }
    if (m_kpiRegistry.contains("giving_month") && m_kpiRegistry["giving_month"].valLabel) {
        QSqlQuery m;
        m.prepare("SELECT COALESCE(SUM(amount), 0) FROM contributions WHERE strftime('%Y-%m', contribution_date) = ?");
        m.addBindValue(QDate::currentDate().toString("yyyy-MM"));
        if (m.exec() && m.next()) {
            double monthGiving = m.value(0).toDouble() / 100.0;
            m_kpiRegistry["giving_month"].valLabel->setText(QString("%1%2").arg(appCurrencySymbol(), QString::number(monthGiving, 'f', 2)));
        }
    }
    int fiscalYear = QDate::currentDate().year();
    QString fiscalYearStr = QString::number(fiscalYear);
    if (m_kpiRegistry.contains("giving_ytd") && m_kpiRegistry["giving_ytd"].valLabel) {
        QSqlQuery y;
        y.prepare("SELECT COALESCE(SUM(amount), 0) FROM contributions WHERE substr(contribution_date, 1, 4) = ?");
        y.addBindValue(fiscalYearStr);
        if (y.exec() && y.next()) {
            double ytd = y.value(0).toDouble() / 100.0;
            m_kpiRegistry["giving_ytd"].valLabel->setText(QString("%1%2").arg(appCurrencySymbol(), QString::number(ytd, 'f', 2)));
        }
    }
    if (m_kpiRegistry.contains("total_assets") && m_kpiRegistry["total_assets"].valLabel) {
        QSqlQuery a;
        if (a.exec("SELECT COALESCE(SUM(s.amount), 0) FROM splits s JOIN accounts a ON s.account_id = a.id WHERE a.type = 'ASSET'") && a.next()) {
            double assets = a.value(0).toDouble();
            m_kpiRegistry["total_assets"].valLabel->setText(QString("%1%2").arg(appCurrencySymbol(), QString::number(assets, 'f', 2)));
        }
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
        QSqlQuery qServices("SELECT date, title, leader FROM services WHERE date >= date('now') AND title IS NOT NULL AND title != '' ORDER BY date ASC LIMIT 3");
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
    if (!isModuleEnabled("giving")) return;
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
        m_serviceWidget->applyRole(m_userRole);
        ensurePageLoaded(4, [this]() { return m_serviceWidget; });
    }
    switchPage(4);
    setActiveNav(m_btnService);
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
        ensurePageLoaded(5, [this]() { return m_graphWidget; });
    }
    m_graphWidget->loadGraph();
    switchPage(5);
    setActiveNav(m_btnGraph);
}

void MainWindow::onInventoryClicked() {
    if (!isModuleEnabled("inventory")) return;
    if (!m_inventoryWidget) {
        m_inventoryWidget = new InventoryWidget(this);
        m_inventoryWidget->applyRole(m_userRole);
        ensurePageLoaded(6, [this]() { return m_inventoryWidget; });
    }
    m_inventoryWidget->refreshTree();
    switchPage(6);
    setActiveNav(m_btnInventory);
}

void MainWindow::onVaultClicked() {
    if (!isModuleEnabled("vault")) return;
    if (!m_vaultWidget) {
        m_vaultWidget = new VaultWidget(this);
        m_vaultWidget->applyRole(m_userRole);
        ensurePageLoaded(7, [this]() { return m_vaultWidget; });
    }
    m_vaultWidget->refreshVault();
    switchPage(7);
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
        ensurePageLoaded(8, [this]() { return m_settingsWidget; });
    }
    m_settingsWidget->loadSettings();
    switchPage(8);
    setActiveNav(m_btnSettings);
}

void MainWindow::onFacilitiesClicked() {
    if (!isModuleEnabled("facilities")) return;
    if (!m_facilitiesWidget) {
        m_facilitiesWidget = new FacilitiesWidget(this);
        ensurePageLoaded(9, [this]() { return m_facilitiesWidget; });
    }
    switchPage(9);
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
        ensurePageLoaded(10, [this]() { return m_analyticsWidget; });
    }
    m_analyticsWidget->refreshAnalytics();
    switchPage(10);
    setActiveNav(m_btnAnalytics);
}

void MainWindow::onPlanningClicked() {
    if (!isModuleEnabled("planning")) return;
    if (!m_planningWidget) {
        m_planningWidget = new PlanningWidget(this);
        m_planningWidget->applyRole(m_userRole);
        ensurePageLoaded(11, [this]() { return m_planningWidget; });
    }
    switchPage(11);
    setActiveNav(m_btnPlanning);
}

void MainWindow::onShepherdClicked() {
    if (!isModuleEnabled("shepherd")) return;
    if (!m_shepherdWidget) {
        m_shepherdWidget = new ShepherdWidget(this);
        connect(m_shepherdWidget, &ShepherdWidget::navigateToMember, this, &MainWindow::selectPerson);
        ensurePageLoaded(12, [this]() { return m_shepherdWidget; });
    }
    m_shepherdWidget->refreshAll();
    switchPage(12);
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
    if (m_serviceWidget) m_serviceWidget->applyRole(m_userRole);
    if (m_planningWidget) m_planningWidget->applyRole(m_userRole);

    if (statusBar()) {
        statusBar()->showMessage(QString("Signed in as: %1 (%2)").arg(m_username, m_userRole));
    }
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
    // Giving (contributions, funds, batches) is this app's core finance job.
    bool modFinance = s.value("modules/finance", true).toBool(); // legacy key
    bool modGiving = s.value("modules/giving", modFinance).toBool();
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

    if (m_btnContributions) m_btnContributions->setVisible(modGiving);
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

    bool anyFinance = (modGiving || modInventory || modVault);
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

    if (m_actCont) m_actCont->setVisible(modGiving);
    if (m_actInventory) m_actInventory->setVisible(modInventory);
    if (m_actVault) m_actVault->setVisible(modVault);
}

bool MainWindow::isModuleEnabled(const QString& moduleKey, bool defaultValue) const {
    return QSettings("OpenEkklesia", "CHMS").value(QString("modules/%1").arg(moduleKey), defaultValue).toBool();
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
        item->setData(0, Qt::UserRole + 1, "Group");

        if (m_chkShowMembersInTree && m_chkShowMembersInTree->isChecked()) {
            QSqlQuery qMembers(db);
            qMembers.prepare(
                "SELECT p.id, p.first_name || ' ' || p.last_name, er.relationship_type, p.phone, p.email, p.status, p.care_flag "
                "FROM entity_relationships er JOIN people p ON p.id = er.source_id "
                "WHERE er.target_type = 'Group' AND er.source_type = 'Person' AND er.target_id = ? "
                "ORDER BY CASE WHEN er.relationship_type IN ('Leader', 'Co-Leader', 'Teacher') THEN 0 ELSE 1 END, p.first_name");
            qMembers.addBindValue(id);
            if (qMembers.exec()) {
                while (qMembers.next()) {
                    QTreeWidgetItem* pItem = new QTreeWidgetItem(item);
                    int pId = qMembers.value(0).toInt();
                    QString pName = qMembers.value(1).toString();
                    QString pRole = qMembers.value(2).toString();
                    QString pPhone = qMembers.value(3).toString();
                    QString pEmail = qMembers.value(4).toString();
                    QString pStatus = qMembers.value(5).toString();
                    QString pCare = qMembers.value(6).toString();

                    pItem->setText(0, QString("      👤 %1").arg(pName));
                    pItem->setFont(0, QFont("Segoe UI", 9));
                    pItem->setForeground(0, QBrush(QColor("#cdd6f4")));
                    pItem->setText(1, pRole.isEmpty() ? "Member" : pRole);
                    if (pRole == "Leader" || pRole == "Co-Leader") {
                        pItem->setForeground(1, QBrush(QColor("#f9e2af")));
                        pItem->setFont(1, QFont("Segoe UI", 9, QFont::Bold));
                    }
                    QString contact = pPhone.isEmpty() ? pEmail : pPhone;
                    pItem->setText(2, contact.isEmpty() ? "—" : contact);
                    pItem->setText(3, name);
                    pItem->setText(4, "—");
                    pItem->setText(5, pCare.isEmpty() || pCare == "Normal" ? pStatus : QString("%1 [%2]").arg(pStatus, pCare));
                    pItem->setData(0, Qt::UserRole, pId);
                    pItem->setData(0, Qt::UserRole + 1, "Person");
                    pItem->setData(0, Qt::UserRole + 2, id);
                }
            }
        }

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
                 "p.first_name || ' ' || p.last_name AS contact_name, g.max_capacity, g.notes, g.is_archived, "
                 "g.recruitment_status "
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
    QString recruitStatus = qGrp.value(10).toString().trimmed();
    if (recruitStatus.isEmpty()) recruitStatus = "Open";

    m_lblGroupDetailTitle->setText(QString("🏛️ %1 %2").arg(name, isArchived ? QString("<span style='color: %1;'>(Archived)</span>").arg(tok.accentDanger.name()) : ""));
    m_lblGroupDetailTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(tok.accentPrimary.name()));

    QStringList metaParts;
    metaParts << QString("<b>Category:</b> %1").arg(cat);
    if (recruitStatus == "Elected") {
        metaParts << "<span style='background-color: #382418; color: #fab387; border: 1px solid #fab387; border-radius: 4px; padding: 1px 5px; font-weight: bold;'>🗳️ Elected Governance Body (Annual Vote)</span>";
    } else if (recruitStatus == "Seasonal") {
        metaParts << "<span style='background-color: #1e1e2e; color: #f9e2af; border: 1px solid #f9e2af; border-radius: 4px; padding: 1px 5px; font-weight: bold;'>📅 Seasonal Cohort (Closed Enrollment)</span>";
    } else if (recruitStatus == "Closed") {
        metaParts << "<span style='background-color: #313244; color: palette(placeholderText); border: 1px solid #a6adc8; border-radius: 4px; padding: 1px 5px; font-weight: bold;'>🔒 Closed (Invite Only)</span>";
    } else {
        metaParts << "<span style='background-color: #18332b; color: #a6e3a1; border: 1px solid #a6e3a1; border-radius: 4px; padding: 1px 5px; font-weight: bold;'>🟢 Open for Recruitment</span>";
    }
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
            nameItem->setData(Qt::UserRole, personId);
            nameItem->setData(Qt::UserRole + 1, relId);
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
    QString currentName, currentCat, currentDesc, currentDay, currentTime, currentLoc, currentNotes, currentSkills, currentRecruitStatus = "Open";
    int currentParentId = 0, currentContactId = 0, currentCap = 0, currentMinAge = 0, currentMaxAge = 0;

    if (groupId > 0) {
        QSqlQuery q(db);
        q.prepare("SELECT name, category, description, parent_id, meeting_day, meeting_time, location, contact_person_id, max_capacity, notes, required_skills, min_age, max_age, recruitment_status FROM groups WHERE id = ?");
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
            currentSkills = q.value(10).toString();
            currentMinAge = q.value(11).toInt();
            currentMaxAge = q.value(12).toInt();
            currentRecruitStatus = q.value(13).toString().trimmed();
            if (currentRecruitStatus.isEmpty()) currentRecruitStatus = "Open";
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

    dlg.resize(440, 560);
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
    // Offer ANY group as a parent except the group itself and its own
    // descendants (cycle-safe), so subgroups can nest arbitrarily deep.
    QSqlQuery qParents(db);
    qParents.prepare(
        "WITH RECURSIVE descendants(id) AS ("
        "  SELECT id FROM groups WHERE parent_id = ? "
        "  UNION ALL "
        "  SELECT g.id FROM groups g JOIN descendants d ON g.parent_id = d.id"
        ") "
        "SELECT id, name FROM groups "
        "WHERE id != ? AND id NOT IN (SELECT id FROM descendants) "
        "ORDER BY name");
    qParents.addBindValue(groupId);
    qParents.addBindValue(groupId);
    qParents.exec();
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

    // Ministry Match profile: what this group is looking for. Used by
    // "Suggest People" (declared skills win over inferred ones).
    QLineEdit* skillsEdit = new QLineEdit(currentSkills, &dlg);
    skillsEdit->setPlaceholderText("e.g. guitar, vocals, teaching — comma separated");

    QSpinBox* minAgeSpin = new QSpinBox(&dlg);
    minAgeSpin->setRange(0, 120);
    minAgeSpin->setSpecialValueText("Any");
    minAgeSpin->setValue(currentMinAge);
    minAgeSpin->setToolTip("Preferred minimum age for match suggestions (0 = Any)");

    QSpinBox* maxAgeSpin = new QSpinBox(&dlg);
    maxAgeSpin->setRange(0, 120);
    maxAgeSpin->setSpecialValueText("Any");
    maxAgeSpin->setValue(currentMaxAge);
    maxAgeSpin->setToolTip("Preferred maximum age for match suggestions (0 = Any)");
    QHBoxLayout* ageBandRow = new QHBoxLayout();
    ageBandRow->addWidget(minAgeSpin);
    ageBandRow->addWidget(new QLabel("–", &dlg));
    ageBandRow->addWidget(maxAgeSpin);
    ageBandRow->addStretch();

    QLineEdit* descEdit = new QLineEdit(currentDesc, &dlg);

    QComboBox* recruitCombo = new QComboBox(&dlg);
    recruitCombo->addItem("🟢 Open — Continuous Enrollment / Recommendations Active", "Open");
    recruitCombo->addItem("🗳️ Elected — Governance / Annual Vote (Board, Deacons, Elders)", "Elected");
    recruitCombo->addItem("📅 Seasonal — Biannual Cohort (Closed Enrollment)", "Seasonal");
    recruitCombo->addItem("🔒 Closed — Invitation Only", "Closed");
    int rIdx = recruitCombo->findData(currentRecruitStatus);
    if (rIdx >= 0) recruitCombo->setCurrentIndex(rIdx);

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
    form->addRow("Recruitment:", recruitCombo);
    form->addRow("Meeting Day:", dayCombo);
    form->addRow("Meeting Time:", timeEdit);
    form->addRow("Location / Room:", locationEdit);
    form->addRow("Primary Contact:", contactCombo);
    form->addRow("Max Capacity:", capacitySpin);
    form->addRow("Match Skills:", skillsEdit);
    form->addRow("Preferred Age:", ageBandRow);
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

        // Match profile (declared skills + preferred age band).
        QString skills = skillsEdit->text().trimmed();
        int minAge = minAgeSpin->value();
        int maxAge = maxAgeSpin->value();
        if (minAge > 0 && maxAge > 0 && minAge > maxAge) std::swap(minAge, maxAge);
        QString recruitStatus = recruitCombo->currentData().toString();

        if (groupId > 0 && !isDuplicate) {
            QSqlQuery update(db);
            update.prepare("UPDATE groups SET name = ?, category = ?, parent_id = ?, meeting_day = ?, meeting_time = ?, "
                           "location = ?, contact_person_id = ?, max_capacity = ?, description = ?, notes = ?, "
                           "required_skills = ?, min_age = ?, max_age = ?, recruitment_status = ? WHERE id = ?");
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
            update.addBindValue(skills.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(skills));
            update.addBindValue(minAge);
            update.addBindValue(maxAge);
            update.addBindValue(recruitStatus);
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
            ins.prepare("INSERT INTO groups (name, category, parent_id, meeting_day, meeting_time, location, contact_person_id, max_capacity, description, notes, required_skills, min_age, max_age, recruitment_status, is_archived) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
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
            ins.addBindValue(skills.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(skills));
            ins.addBindValue(minAge);
            ins.addBindValue(maxAge);
            ins.addBindValue(recruitStatus);

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

// --- Groups-tree context-menu actions (right-click on a group) ---

void MainWindow::onEditGroup() {
    if (m_selectedGroupId > 0) {
        showGroupDialog(m_selectedGroupId, false);
    } else {
        QMessageBox::warning(this, "No Selection", "Please select a group first.");
    }
}

void MainWindow::onAddGroup() {
    showGroupDialog(0, false); // parent chosen in the dialog (cycle-safe)
}

void MainWindow::onArchiveGroup() {
    if (m_selectedGroupId <= 0) {
        QMessageBox::warning(this, "No Selection", "Please select a group first.");
        return;
    }
    QSqlQuery q;
    q.prepare("UPDATE groups SET is_archived = CASE WHEN is_archived = 1 THEN 0 ELSE 1 END WHERE id = ?");
    q.addBindValue(m_selectedGroupId);
    if (q.exec()) {
        refreshGroupsTree();
        refreshGroupDetailPanel(m_selectedGroupId);
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to toggle archive status: " + q.lastError().text());
    }
}

void MainWindow::onDeleteGroup() {
    if (m_selectedGroupId <= 0) {
        QMessageBox::warning(this, "No Selection", "Please select a group first.");
        return;
    }
    QSqlQuery nameQ;
    nameQ.prepare("SELECT name FROM groups WHERE id = ?");
    nameQ.addBindValue(m_selectedGroupId);
    QString name = (nameQ.exec() && nameQ.next()) ? nameQ.value(0).toString() : QString("Group #%1").arg(m_selectedGroupId);

    if (QMessageBox::question(this, "Delete Group",
                              QString("Delete '%1' and its sub-groups? Members are kept — only the group links are removed.")
                                  .arg(name),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    db.transaction();
    QSqlQuery q(db);
    // Detach sub-groups and remove member links first (FK-safe).
    q.exec("UPDATE groups SET parent_id = NULL WHERE parent_id = " + QString::number(m_selectedGroupId));
    q.prepare("DELETE FROM entity_relationships WHERE target_type = 'Group' AND target_id = ?");
    q.addBindValue(m_selectedGroupId);
    q.exec();
    q.prepare("DELETE FROM groups WHERE id = ?");
    q.addBindValue(m_selectedGroupId);
    const bool ok = q.exec();
    if (ok) {
        db.commit();
        refreshGroupsTree();
        refreshGroupDetailPanel(0);
        if (m_graphWidget) m_graphWidget->loadGraph();
    } else {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to delete group: " + q.lastError().text());
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
