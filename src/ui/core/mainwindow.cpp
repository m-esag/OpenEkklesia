#include "mainwindow.h"
#include "appsettings.h"
#include "../../common/tableutil.h"
#include "thememanager.h"
#include "touchhelper.h"
#include "iconmanager.h"
#include "ministryarchetypes.h"
#include "synergyengine.h"
#include "transactiondialog.h"
#include "peoplewidget.h"
#include "traymanager.h"
#include "startuplaunchmanager.h"
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
#include "usermanualdialog.h"
#include "../integrations/planningcenterdialog.h"
#include <QApplication>
#include <QGuiApplication>
#include <QHash>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QSettings>
#include <QKeyEvent>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include <QStatusBar>
#include "../database/databasemanager.h"
#include "../common/appcurrency.h"
#include "../common/apptranslator.h"
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
        // Only group rows may be reparented. Person rows carry a person id in the
        // same Qt::UserRole slot, so a person drag must never reach onReparent().
        if (dragged->data(0, Qt::UserRole + 1).toString() != "Group") { event->ignore(); return; }
        const int draggedId = dragged->data(0, Qt::UserRole).toInt();
        if (draggedId <= 0) { event->ignore(); return; }

        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        if (target && target->data(0, Qt::UserRole + 1).toString() != "Group") {
            target = target->parent(); // dropped on a member row -> use its group
        }
        const DropIndicatorPosition pos = dropIndicatorPosition();
        int newParentId = 0;
        if (target && target->data(0, Qt::UserRole + 1).toString() == "Group") {
            const int targetId = target->data(0, Qt::UserRole).toInt();
            if (pos == OnItem) {
                newParentId = targetId; // dropped onto a group -> its child
            } else if (QTreeWidgetItem* tParent = target->parent()) {
                if (tParent->data(0, Qt::UserRole + 1).toString() == "Group") {
                    newParentId = tParent->data(0, Qt::UserRole).toInt(); // sibling
                }
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
        setWindowTitle(AppTranslator::t("OpenEkklesia Authentication"));
        resize(340, 220);

        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(20, 20, 20, 20);
        lay->setSpacing(12);

        QLabel* header = new QLabel(AppTranslator::t("Enter OpenEkklesia Access PIN:"), this);
        header->setObjectName("detailHeader");
        lay->addWidget(header);

        m_pinEdit = new QLineEdit(this);
        m_pinEdit->setEchoMode(QLineEdit::Password);
        m_pinEdit->setMaxLength(16);
        m_pinEdit->setPlaceholderText(AppTranslator::t("PIN (e.g. 1234 for Admin)"));
        m_pinEdit->setFocus();
        connect(m_pinEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLogin);
        lay->addWidget(m_pinEdit);

        QLabel* hint = new QLabel(AppTranslator::t("<small style='color: palette(highlight);'>Default PINs: Admin <b>1234</b> | Pastor <b>5555</b> | Finance <b>7777</b></small>"), this);
        hint->setTextFormat(Qt::RichText);
        lay->addWidget(hint);

        QHBoxLayout* buttons = new QHBoxLayout();
        QPushButton* btnGuest = new QPushButton(AppTranslator::t("Guest (Read-Only)"), this);
        btnGuest->setObjectName("secondaryBtn");
        connect(btnGuest, &QPushButton::clicked, this, [this]() {
            m_role = "readonly";
            m_username = "Guest";
            accept();
        });

        QPushButton* btnLogin = new QPushButton(AppTranslator::t("Login"), this);
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
            QMessageBox::warning(this, AppTranslator::t("PIN Required"),
                                 AppTranslator::t("Please enter your access PIN (e.g. 1234 for Admin)."));
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
            QMessageBox::warning(this, AppTranslator::t("Invalid PIN"),
                                 QString(AppTranslator::t("The access PIN '%1' is incorrect.\n\nUse 1234 for Admin access.")).arg(pin));
            m_pinEdit->clear();
            m_pinEdit->setFocus();
        }
    }

    QLineEdit* m_pinEdit;
    QString m_role;
    QString m_username;
};

namespace {
// QStackedLayout takes the MAXIMUM of its pages' minimums; combined with the
// dense per-module toolbars built lazily on first visit, the main window's
// declared minimum grew past small/HiDPI logical desktops (1770px wide on a
// 1536px-logical 125%-scaled screen) — the window then opened oversized and
// GNOME could neither tile nor resize it. Overriding minimumSize() decouples
// the window size from page content: oversized pages clip at their right
// edge instead of inflating the window.
class IgnoreMinStackedLayout : public QStackedLayout {
public:
    using QStackedLayout::QStackedLayout;
    QSize minimumSize() const override { return QSize(0, 0); }
};

// QCheckBox::stateChanged(int) is deprecated since Qt 6.7 in favour of
// checkStateChanged(Qt::CheckState). Bind whichever signal this Qt provides so
// the same source builds against the Qt 6.4 floor and 6.7+ without warnings.
void connectCheckBoxToggled(QCheckBox* box, QObject* context, std::function<void()> onToggled) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QObject::connect(box, &QCheckBox::checkStateChanged, context,
                     [onToggled](Qt::CheckState) { onToggled(); });
#else
    QObject::connect(box, &QCheckBox::stateChanged, context,
                     [onToggled](int) { onToggled(); });
#endif
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_btnSettings(nullptr), m_sidebar(nullptr), m_logoLabel(nullptr), m_isCompactSidebar(false), m_isDarkMode(true), m_hintsEnabled(true), m_activeNavButton(nullptr)
{
    m_userRole = "admin";
    m_username = "Admin";

    setupModels();
    setupUI();
    setupMenuBar();
    qApp->installEventFilter(this);
    // Detachable touchscreen (finger + active pen): kinetic scrolling on all
    // lists/tables/scroll areas. Graph canvas keeps its own gestures.
    TouchHelper::enableKineticScrolling(this);
    setActiveNav(m_btnDashboard);
    updateDashboardStats();

    m_trayManager = new TrayManager(this);
    if (!m_trayManager->initialize()) {
        delete m_trayManager;
        m_trayManager = nullptr;
    }

    m_isDarkMode = ThemeManager::instance().currentTokens().isDark;
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](const ThemeTokens& tokens) {
        m_isDarkMode = tokens.isDark;
        IconManager::instance().clearCache();
        refreshSidebarIcons();
        updatePieChart();
        refreshGroupDetailPanel(m_selectedGroupId);
        // Views that capture tokens once at build time must be rebuilt here,
        // otherwise they keep the previous theme's styling after a mid-session
        // switch (seen as dark blueprint cards with pale gray text on the
        // light palette).
        refreshMinistryBlueprintsTab();
        updateDashboardStats();
    });
    
    // Apply role permissions & module visibility
    applyRolePermissions();
    applyModuleVisibility();

    // NOTE: Ctrl+K, Ctrl+1..Ctrl+5, F1 and Ctrl+T are registered exactly once,
    // on their QActions in setupMenuBar(). Duplicate QShortcuts here would make
    // Qt report "Ambiguous shortcut overload" and fire neither handler.

    QShortcut* zoomInShortcut = new QShortcut(QKeySequence::ZoomIn, this);
    connect(zoomInShortcut, &QShortcut::activated, this, []() {
        ThemeManager::instance().zoomIn();
    });

    QShortcut* zoomOutShortcut = new QShortcut(QKeySequence::ZoomOut, this);
    connect(zoomOutShortcut, &QShortcut::activated, this, []() {
        ThemeManager::instance().zoomOut();
    });

    QShortcut* shortcutDashD = new QShortcut(QKeySequence("Ctrl+D"), this);
    connect(shortcutDashD, &QShortcut::activated, this, &MainWindow::onDashboardClicked);
}

MainWindow::~MainWindow() {
    delete m_trayManager;
}

void MainWindow::setupMenuBar() {
    QMenuBar* mb = menuBar();
    ThemeTokens tok = ThemeManager::instance().currentTokens();

    // Sidebar panel toggle (ZCode-style): lives at the TopLeftCorner of the
    // menu bar so it stays reachable when the sidebar itself is hidden.
    m_btnToggleSidebar = new QPushButton(mb);
    m_btnToggleSidebar->setIcon(IconManager::instance().getIcon("panel-left", tok.textPrimary, 16));
    m_btnToggleSidebar->setFlat(true);
    m_btnToggleSidebar->setFixedSize(28, 24);
    m_btnToggleSidebar->setCursor(Qt::PointingHandCursor);
    m_btnToggleSidebar->setToolTip(AppTranslator::t("Toggle navigation panel"));
    connect(m_btnToggleSidebar, &QPushButton::clicked, this, [this]() {
        if (!m_sidebar) return;
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });
    mb->setCornerWidget(m_btnToggleSidebar, Qt::TopLeftCorner);

    // 1. File / Application Menu
    QMenu* fileMenu = mb->addMenu(AppTranslator::t("&File"));

    QAction* actSearch = fileMenu->addAction(AppTranslator::t("Quick Command Search..."));
    actSearch->setIcon(IconManager::instance().getIcon("search", tok.textPrimary, 14));
    actSearch->setShortcut(QKeySequence("Ctrl+K"));
    connect(actSearch, &QAction::triggered, this, [this]() {
        setWindowOpacity(0.95);
        CommandPaletteDialog palette(this);
        palette.exec();
        setWindowOpacity(1.0);
    });

    m_actKiosk = fileMenu->addAction(AppTranslator::t("Nursery Kiosk Portal"));
    m_actKiosk->setIcon(IconManager::instance().getIcon("user-check", tok.textPrimary, 14));
    connect(m_actKiosk, &QAction::triggered, this, &MainWindow::launchKiosk);

    fileMenu->addSeparator();

    // Preferences & Settings Submenu
    QMenu* prefsMenu = fileMenu->addMenu(AppTranslator::t("Preferences & Settings"));
    prefsMenu->setIcon(IconManager::instance().getIcon("settings", tok.textPrimary, 14));

    QAction* actPrefGeneral = prefsMenu->addAction(AppTranslator::t("Church Branding & Profile"));
    actPrefGeneral->setIcon(IconManager::instance().getIcon("landmark", tok.textPrimary, 14));
    connect(actPrefGeneral, &QAction::triggered, this, [this]() { openSettingsCategory(0); });

    QAction* actPrefTheme = prefsMenu->addAction(AppTranslator::t("Appearance & Theme Customizer"));
    actPrefTheme->setIcon(IconManager::instance().getIcon("sparkles", tok.textPrimary, 14));
    connect(actPrefTheme, &QAction::triggered, this, [this]() { openSettingsCategory(1); });

    QAction* actPrefModules = prefsMenu->addAction(AppTranslator::t("Module Visibility & Features"));
    actPrefModules->setIcon(IconManager::instance().getIcon("layers", tok.textPrimary, 14));
    connect(actPrefModules, &QAction::triggered, this, [this]() { openSettingsCategory(2); });

    QAction* actPrefLiturgy = prefsMenu->addAction(AppTranslator::t("Service & Liturgy Defaults"));
    actPrefLiturgy->setIcon(IconManager::instance().getIcon("list-music", tok.textPrimary, 14));
    connect(actPrefLiturgy, &QAction::triggered, this, [this]() { openSettingsCategory(3); });

    QAction* actPrefLocale = prefsMenu->addAction(AppTranslator::t("Localization & Currency Format"));
    actPrefLocale->setIcon(IconManager::instance().getIcon("calendar", tok.textPrimary, 14));
    connect(actPrefLocale, &QAction::triggered, this, [this]() { openSettingsCategory(4); });

    QAction* actPrefPastoral = prefsMenu->addAction(AppTranslator::t("Pastoral & Graph Thresholds"));
    actPrefPastoral->setIcon(IconManager::instance().getIcon("network", tok.textPrimary, 14));
    connect(actPrefPastoral, &QAction::triggered, this, [this]() { openSettingsCategory(5); });

    QAction* actPrefKiosk = prefsMenu->addAction(AppTranslator::t("Kiosk & Nursery Security"));
    actPrefKiosk->setIcon(IconManager::instance().getIcon("user-check", tok.textPrimary, 14));
    connect(actPrefKiosk, &QAction::triggered, this, [this]() { openSettingsCategory(6); });

    QAction* actPrefBackup = prefsMenu->addAction(AppTranslator::t("Database Backups & Integrations"));
    actPrefBackup->setIcon(IconManager::instance().getIcon("shield-check", tok.textPrimary, 14));
    connect(actPrefBackup, &QAction::triggered, this, [this]() { openSettingsCategory(7); });

    prefsMenu->addSeparator();

    QAction* actAllSettings = prefsMenu->addAction(AppTranslator::t("All Application Settings..."));
    actAllSettings->setIcon(IconManager::instance().getIcon("settings", tok.textPrimary, 14));
    connect(actAllSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    if (StartupLaunchManager::supported()) {
        QAction* actStartAtLogin = prefsMenu->addAction(AppTranslator::t("Start OpenEkklesia at System Login"));
        actStartAtLogin->setIcon(IconManager::instance().getIcon("calendar", tok.textPrimary, 14));
        actStartAtLogin->setCheckable(true);
        actStartAtLogin->setChecked(StartupLaunchManager::isEnabled());
        connect(actStartAtLogin, &QAction::triggered, this, [actStartAtLogin](bool checked) {
            StartupLaunchManager::setEnabled(checked);
            actStartAtLogin->setChecked(StartupLaunchManager::isEnabled());
        });
    }

    fileMenu->addSeparator();


    QAction* actSwitchUser = fileMenu->addAction(AppTranslator::t("Switch User / Sign In"));
    actSwitchUser->setIcon(IconManager::instance().getIcon("shield-check", tok.textPrimary, 14));
    connect(actSwitchUser, &QAction::triggered, this, &MainWindow::promptLogin);

    fileMenu->addSeparator();

    QAction* actExit = fileMenu->addAction(AppTranslator::t("Exit OpenEkklesia"));
    actExit->setIcon(IconManager::instance().getIcon("log-out", tok.accentDanger, 14));
    actExit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    // 2. Congregation Menu
    QMenu* congMenu = mb->addMenu(AppTranslator::t("&Congregation"));

    QAction* actDash = congMenu->addAction(AppTranslator::t("Dashboard Overview"));
    actDash->setIcon(IconManager::instance().getIcon("bar-chart-3", tok.textPrimary, 14));
    actDash->setShortcut(QKeySequence("Ctrl+1"));
    connect(actDash, &QAction::triggered, this, &MainWindow::onDashboardClicked);

    m_actMembers = congMenu->addAction(AppTranslator::t("People Directory"));
    m_actMembers->setIcon(IconManager::instance().getIcon("users", tok.textPrimary, 14));
    m_actMembers->setShortcut(QKeySequence("Ctrl+2"));
    connect(m_actMembers, &QAction::triggered, this, &MainWindow::onMembersClicked);

    m_actGraph = congMenu->addAction(AppTranslator::t("Connection Graph Map"));
    m_actGraph->setIcon(IconManager::instance().getIcon("network", tok.textPrimary, 14));
    connect(m_actGraph, &QAction::triggered, this, &MainWindow::onGraphClicked);

    m_actAnalytics = congMenu->addAction(AppTranslator::t("Pastoral Analytics"));
    m_actAnalytics->setIcon(IconManager::instance().getIcon("bar-chart-3", tok.textPrimary, 14));
    connect(m_actAnalytics, &QAction::triggered, this, &MainWindow::onAnalyticsClicked);

    QAction* actShepherd = congMenu->addAction(AppTranslator::t("Shepherd's Workbench"));
    actShepherd->setIcon(IconManager::instance().getIcon("heart-handshake", tok.textPrimary, 14));
    m_actShepherd = actShepherd;
    connect(actShepherd, &QAction::triggered, this, &MainWindow::onShepherdClicked);

    // 3. Ministry & Services Menu
    QMenu* minMenu = mb->addMenu(AppTranslator::t("&Ministry"));

    m_actGroups = minMenu->addAction(AppTranslator::t("Groups & Ministries"));
    m_actGroups->setIcon(IconManager::instance().getIcon("layers", tok.textPrimary, 14));
    m_actGroups->setShortcut(QKeySequence("Ctrl+3"));
    connect(m_actGroups, &QAction::triggered, this, &MainWindow::onGroupsClicked);

    m_actService = minMenu->addAction(AppTranslator::t("Service Liturgy Builder"));
    m_actService->setIcon(IconManager::instance().getIcon("list-music", tok.textPrimary, 14));
    m_actService->setShortcut(QKeySequence("Ctrl+5"));
    connect(m_actService, &QAction::triggered, this, &MainWindow::onServiceClicked);

    m_actFacilities = minMenu->addAction(AppTranslator::t("Facilities Matrix"));
    m_actFacilities->setIcon(IconManager::instance().getIcon("landmark", tok.textPrimary, 14));
    connect(m_actFacilities, &QAction::triggered, this, &MainWindow::onFacilitiesClicked);

    m_actPlanning = minMenu->addAction(AppTranslator::t("Ministry Planning"));
    m_actPlanning->setIcon(IconManager::instance().getIcon("calendar", tok.textPrimary, 14));
    connect(m_actPlanning, &QAction::triggered, this, &MainWindow::onPlanningClicked);

    // 4. Finance & Operations Menu
    QMenu* finMenu = mb->addMenu(AppTranslator::t("&Finance"));

    m_actCont = finMenu->addAction(AppTranslator::t("Donation Contributions"));
    m_actCont->setIcon(IconManager::instance().getIcon("hand-coins", tok.textPrimary, 14));
    m_actCont->setShortcut(QKeySequence("Ctrl+4"));
    connect(m_actCont, &QAction::triggered, this, &MainWindow::onContributionsClicked);

    finMenu->addSeparator();

    m_actInventory = finMenu->addAction(AppTranslator::t("Asset Inventory"));
    m_actInventory->setIcon(IconManager::instance().getIcon("package", tok.textPrimary, 14));
    connect(m_actInventory, &QAction::triggered, this, &MainWindow::onInventoryClicked);

    m_actVault = finMenu->addAction(AppTranslator::t("Document Vault"));
    m_actVault->setIcon(IconManager::instance().getIcon("shield-check", tok.textPrimary, 14));
    connect(m_actVault, &QAction::triggered, this, &MainWindow::onVaultClicked);

    // 5. Tools & Database Menu
    QMenu* toolsMenu = mb->addMenu(AppTranslator::t("&Tools"));

    QAction* actDbHub = toolsMenu->addAction(AppTranslator::t("Database & Backup Center"));
    actDbHub->setIcon(IconManager::instance().getIcon("shield-check", tok.textPrimary, 14));
    connect(actDbHub, &QAction::triggered, this, [this]() { openSettingsCategory(7); });

    QAction* actPco = toolsMenu->addAction(AppTranslator::t("Planning Center Online Sync..."));
    actPco->setIcon(IconManager::instance().getIcon("cloud", tok.textPrimary, 14));
    connect(actPco, &QAction::triggered, this, [this]() {
        PlanningCenterDialog dlg(this);
        dlg.exec();
    });

    QAction* actShare = toolsMenu->addAction(AppTranslator::t("Selective Data Sharing..."));
    actShare->setIcon(IconManager::instance().getIcon("share-2", tok.textPrimary, 14));
    connect(actShare, &QAction::triggered, this, [this]() { openSettingsCategory(7); });

    toolsMenu->addSeparator();

    QAction* actPastoralRules = toolsMenu->addAction(AppTranslator::t("Pastoral Health & Threshold Rules"));
    actPastoralRules->setIcon(IconManager::instance().getIcon("network", tok.textPrimary, 14));
    connect(actPastoralRules, &QAction::triggered, this, [this]() { openSettingsCategory(5); });

    QAction* actLiturgyDefaults = toolsMenu->addAction(AppTranslator::t("Service Liturgy Defaults"));
    actLiturgyDefaults->setIcon(IconManager::instance().getIcon("list-music", tok.textPrimary, 14));
    connect(actLiturgyDefaults, &QAction::triggered, this, [this]() { openSettingsCategory(3); });

    QAction* actKioskSec = toolsMenu->addAction(AppTranslator::t("Kiosk & Nursery Check-In Security"));
    actKioskSec->setIcon(IconManager::instance().getIcon("user-check", tok.textPrimary, 14));
    connect(actKioskSec, &QAction::triggered, this, [this]() { openSettingsCategory(6); });

    toolsMenu->addSeparator();

    QAction* actDangerZone = toolsMenu->addAction(AppTranslator::t("Data Maintenance & Reset (Danger Zone)..."));
    actDangerZone->setIcon(IconManager::instance().getDangerIcon("alert-triangle", 14));
    connect(actDangerZone, &QAction::triggered, this, [this]() { openSettingsCategory(8); });

    // 6. View Menu
    QMenu* viewMenu = mb->addMenu(AppTranslator::t("&View"));

    QAction* actTheme = viewMenu->addAction(AppTranslator::t("Toggle Dark / Light Theme"));
    actTheme->setIcon(IconManager::instance().getIcon("sparkles", tok.textPrimary, 14));
    actTheme->setShortcut(QKeySequence("Ctrl+T"));
    connect(actTheme, &QAction::triggered, this, &MainWindow::toggleTheme);

    QAction* actThemeGallery = viewMenu->addAction(AppTranslator::t("Theme Gallery & Color Palette..."));
    actThemeGallery->setIcon(IconManager::instance().getIcon("palette", tok.textPrimary, 14));
    connect(actThemeGallery, &QAction::triggered, this, [this]() { openSettingsCategory(1); });

    viewMenu->addSeparator();

    QAction* actCompactSidebar = viewMenu->addAction(AppTranslator::t("Compact Sidebar Mode"));
    actCompactSidebar->setCheckable(true);
    actCompactSidebar->setChecked(m_isCompactSidebar);
    connect(actCompactSidebar, &QAction::toggled, this, &MainWindow::setSidebarCompact);

    QAction* actShowHints = viewMenu->addAction(AppTranslator::t("Show Help Hints Panel"));
    actShowHints->setCheckable(true);
    actShowHints->setChecked(m_hintsEnabled);
    connect(actShowHints, &QAction::toggled, this, [this](bool checked) {
        if (m_chkShowHints) m_chkShowHints->setChecked(checked);
    });

    viewMenu->addSeparator();

    QAction* actWorkspaceModules = viewMenu->addAction(AppTranslator::t("Workspace Module Manager..."));
    actWorkspaceModules->setIcon(IconManager::instance().getIcon("layers", tok.textPrimary, 14));
    connect(actWorkspaceModules, &QAction::triggered, this, [this]() { openSettingsCategory(2); });

    // 7. Help Menu
    QMenu* helpMenu = mb->addMenu(AppTranslator::t("&Help"));

    m_actUserManual = helpMenu->addAction(AppTranslator::t("User Manual"));
    m_actUserManual->setIcon(IconManager::instance().getIcon("book-open", tok.textPrimary, 14));
    m_actUserManual->setShortcut(QKeySequence("F1"));
    m_actUserManual->setToolTip(AppTranslator::t("Open In-App User Manual & Help Center (F1)"));
    connect(m_actUserManual, &QAction::triggered, this, [this]() {
        onShowUserManual();
    });

    QAction* actAbout = helpMenu->addAction(AppTranslator::t("About OpenEkklesia"));
    actAbout->setIcon(IconManager::instance().getIcon("landmark", tok.textPrimary, 14));
    connect(actAbout, &QAction::triggered, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("About OpenEkklesia");
        dlg.resize(460, 260);
        QVBoxLayout* lay = new QVBoxLayout(&dlg);
        lay->setSpacing(14);

        QLabel* lblTitle = new QLabel("<h3>OpenEkklesia ChMS</h3>", &dlg);
        QLabel* lblDesc = new QLabel(
            "<b>Version 0.8.5-rc1</b> — Privacy-First Open-Source Church Management & Pastoral Analytics Suite.<br><br>"
            "Features double-entry fund accounting, relational connection graph visualization, "
            "Smart Bridge v2.0 group dynamics synergy engine, service liturgy flow builder, "
            "facilities management, document vault, and nursery kiosk check-in portal.", &dlg);
        lblDesc->setWordWrap(true);

        lay->addWidget(lblTitle);
        lay->addWidget(lblDesc);

        QHBoxLayout* btnRow = new QHBoxLayout();
        QPushButton* btnManual = new QPushButton(" Open User Manual (F1)", &dlg);
        btnManual->setIcon(IconManager::instance().getPrimaryIcon("book-open", 14));
        btnManual->setObjectName("primaryBtn");
        connect(btnManual, &QPushButton::clicked, this, [this, &dlg]() {
            dlg.accept();
            onShowUserManual();
        });

        QPushButton* btnOk = new QPushButton("Close", &dlg);
        connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

        btnRow->addWidget(btnManual);
        btnRow->addStretch();
        btnRow->addWidget(btnOk);
        lay->addLayout(btnRow);

        dlg.exec();
    });

    // 8. Top Right Quick-Access Strip (Corner Widget)
    QWidget* cornerWidget = new QWidget(mb);
    QHBoxLayout* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 2, 10, 2);
    cornerLayout->setSpacing(6);

    m_btnTopSearch = new QPushButton(AppTranslator::t(" Search (Ctrl+K)"), cornerWidget);
    m_btnTopSearch->setIcon(IconManager::instance().getIcon("search", QColor(), 13));
    m_btnTopSearch->setCursor(Qt::PointingHandCursor);
    m_btnTopSearch->setToolTip(AppTranslator::t("Open Quick Command Palette (Ctrl+K)"));
    m_btnTopSearch->setStyleSheet(
        "QPushButton { background-color: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 12px; padding: 3px 10px; font-size: 11px; color: var(--text-secondary); }"
        "QPushButton:hover { border-color: var(--accent-primary); color: var(--text-primary); background-color: var(--surface-hover); }"
    );
    connect(m_btnTopSearch, &QPushButton::clicked, this, [this]() {
        CommandPaletteDialog palette(this);
        palette.exec();
    });
    cornerLayout->addWidget(m_btnTopSearch);

    m_btnTopTheme = new QPushButton(cornerWidget);
    m_btnTopTheme->setIcon(IconManager::instance().getIcon(tok.isDark ? "sun" : "moon", QColor(), 14));
    m_btnTopTheme->setCursor(Qt::PointingHandCursor);
    m_btnTopTheme->setToolTip(AppTranslator::t("Toggle Dark / Light Theme (Ctrl+T)"));
    m_btnTopTheme->setStyleSheet(
        "QPushButton { background-color: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 12px; padding: 3px 8px; font-size: 11px; color: var(--text-primary); }"
        "QPushButton:hover { border-color: var(--accent-primary); background-color: var(--surface-hover); }"
    );
    connect(m_btnTopTheme, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    cornerLayout->addWidget(m_btnTopTheme);

    m_btnTopRole = new QPushButton(cornerWidget);
    m_btnTopRole->setCursor(Qt::PointingHandCursor);
    m_btnTopRole->setToolTip("Current User Role — Click to Switch User / Sign In with PIN");
    m_btnTopRole->setStyleSheet(
        "QPushButton { background-color: var(--bg-surface); border: 1px solid var(--border-color); border-radius: 12px; padding: 3px 10px; font-size: 11px; font-weight: bold; color: var(--accent-primary); }"
        "QPushButton:hover { background-color: var(--surface-hover); border-color: var(--accent-primary); }"
    );
    connect(m_btnTopRole, &QPushButton::clicked, this, &MainWindow::promptLogin);
    cornerLayout->addWidget(m_btnTopRole);

    updateTopBarRoleBadge();

    mb->setCornerWidget(cornerWidget, Qt::TopRightCorner);
}

void MainWindow::setupUI() {
    resize(1100, 700);
    // Hard floor independent of page content (see the stacked-widget policy
    // in the layout setup): fits a 1280px-logical HiDPI desktop with room
    // for window decorations.
    setMinimumSize(1000, 620);
    setWindowTitle(AppTranslator::t("OpenEkklesia - Church Management System"));

    // F1 (User Manual) and Ctrl+T (Theme Toggle) are registered once, on the
    // Help/View menu QActions in setupMenuBar().

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
    sidebarLayout->setContentsMargins(12, 14, 12, 10);
    sidebarLayout->setSpacing(8);

    QLabel* logoLabel = new QLabel("OpenEkklesia", this);
    logoLabel->setObjectName("logoLabel");
    sidebarLayout->addWidget(logoLabel);
    sidebarLayout->addSpacing(10);

    AppSettings settings;

    // 1. CONGREGATION Section
    m_headerCongregation = new QPushButton(AppTranslator::t(" CONGREGATION"), this);
    m_headerCongregation->setObjectName("sectionHeaderBtn");
    m_headerCongregation->setCursor(Qt::PointingHandCursor);
    
    sidebarLayout->addWidget(m_headerCongregation);

    m_secCongregation = new QWidget(this);
    QVBoxLayout* layCong = new QVBoxLayout(m_secCongregation);
    layCong->setContentsMargins(0, 0, 0, 0);
    layCong->setSpacing(6);

    m_btnDashboard = new QPushButton(AppTranslator::t("Dashboard"), this);
    m_btnDashboard->setObjectName("navButton");
    m_btnDashboard->setToolTip(AppTranslator::t("Dashboard: View overall member counts, fund stats, and budget progress."));
    layCong->addWidget(m_btnDashboard);

    m_btnMembers = new QPushButton(AppTranslator::t("People"), this);
    m_btnMembers->setObjectName("navButton");
    m_btnMembers->setToolTip(AppTranslator::t("Members: Search, add, edit member details and view activity history."));
    layCong->addWidget(m_btnMembers);

    m_btnGraph = new QPushButton(AppTranslator::t("Connection Graph"), this);
    m_btnGraph->setObjectName("navButton");
    m_btnGraph->setToolTip(AppTranslator::t("Connection Graph: View relationships, care flags, and small group clusters."));
    layCong->addWidget(m_btnGraph);

    m_btnAnalytics = new QPushButton(AppTranslator::t("Pastoral Analytics"), this);
    m_btnAnalytics->setObjectName("navButton");
    m_btnAnalytics->setToolTip(AppTranslator::t("Pastoral Analytics: View 5-Point Health Radar and Discipleship Pipeline."));
    layCong->addWidget(m_btnAnalytics);

    m_btnShepherd = new QPushButton(AppTranslator::t("Shepherd's Workbench"), this);
    m_btnShepherd->setObjectName("navButton");
    m_btnShepherd->setToolTip(AppTranslator::t("Shepherd's Workbench: Today's follow-up queue, member care timeline, and prayer list."));
    layCong->addWidget(m_btnShepherd);

    sidebarLayout->addWidget(m_secCongregation);

    bool showCong = settings.value("sidebarSecCongregation", true).toBool();
    m_secCongregation->setVisible(showCong);
    m_headerCongregation->setIcon(IconManager::instance().getIcon(showCong ? "chevron-down" : "chevron-right", QColor(), 12));

    connect(m_headerCongregation, &QPushButton::clicked, this, [this]() {
        bool nowVis = !m_secCongregation->isVisible();
        m_secCongregation->setVisible(nowVis);
        m_headerCongregation->setIcon(IconManager::instance().getIcon(nowVis ? "chevron-down" : "chevron-right", QColor(), 12));
        AppSettings s;
        s.setValue("sidebarSecCongregation", nowVis);
    });

    // 2. MINISTRY & SERVICES Section
    m_headerMinistry = new QPushButton(AppTranslator::t(" MINISTRY && SERVICES"), this);
    m_headerMinistry->setObjectName("sectionHeaderBtn");
    m_headerMinistry->setCursor(Qt::PointingHandCursor);
    
    sidebarLayout->addWidget(m_headerMinistry);

    m_secMinistry = new QWidget(this);
    QVBoxLayout* layMin = new QVBoxLayout(m_secMinistry);
    layMin->setContentsMargins(0, 0, 0, 0);
    layMin->setSpacing(6);

    m_btnGroups = new QPushButton(AppTranslator::t("Groups && Ministries"), this);
    m_btnGroups->setObjectName("navButton");
    m_btnGroups->setToolTip(AppTranslator::t("Groups & Ministries: Coordinate small groups, classes, ministry blueprints, and volunteer teams."));
    layMin->addWidget(m_btnGroups);

    m_btnService = new QPushButton(AppTranslator::t("Service Builder"), this);
    m_btnService->setObjectName("navButton");
    m_btnService->setToolTip(AppTranslator::t("Service Builder: Plan liturgy, run-sheets, and schedule volunteers."));
    layMin->addWidget(m_btnService);

    m_btnFacilities = new QPushButton(AppTranslator::t("Facilities Matrix"), this);
    m_btnFacilities->setObjectName("navButton");
    m_btnFacilities->setToolTip(AppTranslator::t("Facilities Matrix: Reserve church spaces and audio/visual equipment."));
    layMin->addWidget(m_btnFacilities);

    m_btnPlanning = new QPushButton(AppTranslator::t("Ministry Planning"), this);
    m_btnPlanning->setObjectName("navButton");
    m_btnPlanning->setToolTip(AppTranslator::t("Ministry Planning: View Product Roadmaps and Project Gantt Charts."));
    layMin->addWidget(m_btnPlanning);

    sidebarLayout->addWidget(m_secMinistry);

    bool showMin = settings.value("sidebarSecMinistry", true).toBool();
    m_secMinistry->setVisible(showMin);
    m_headerMinistry->setIcon(IconManager::instance().getIcon(showMin ? "chevron-down" : "chevron-right", QColor(), 12));

    connect(m_headerMinistry, &QPushButton::clicked, this, [this]() {
        bool nowVis = !m_secMinistry->isVisible();
        m_secMinistry->setVisible(nowVis);
        m_headerMinistry->setIcon(IconManager::instance().getIcon(nowVis ? "chevron-down" : "chevron-right", QColor(), 12));
        AppSettings s;
        s.setValue("sidebarSecMinistry", nowVis);
    });

    // 3. FINANCE & OPERATIONS Section
    m_headerFinance = new QPushButton(AppTranslator::t(" FINANCE && OPERATIONS"), this);
    m_headerFinance->setObjectName("sectionHeaderBtn");
    m_headerFinance->setCursor(Qt::PointingHandCursor);
    
    sidebarLayout->addWidget(m_headerFinance);

    m_secFinance = new QWidget(this);
    QVBoxLayout* layFin = new QVBoxLayout(m_secFinance);
    layFin->setContentsMargins(0, 0, 0, 0);
    layFin->setSpacing(6);

    m_btnContributions = new QPushButton(AppTranslator::t("Contributions"), this);
    m_btnContributions->setObjectName("navButton");
    m_btnContributions->setToolTip(AppTranslator::t("Contributions: Record donations, select funds, and view batches."));
    layFin->addWidget(m_btnContributions);

    m_btnInventory = new QPushButton(AppTranslator::t("Asset Inventory"), this);
    m_btnInventory->setObjectName("navButton");
    m_btnInventory->setToolTip(AppTranslator::t("Asset Inventory: Manage church assets, checkouts, and barcodes."));
    layFin->addWidget(m_btnInventory);

    m_btnVault = new QPushButton(AppTranslator::t("Document Vault"), this);
    m_btnVault->setObjectName("navButton");
    m_btnVault->setToolTip(AppTranslator::t("Document Vault: Securely store board minutes, bylaws, and official PDFs."));
    layFin->addWidget(m_btnVault);

    sidebarLayout->addWidget(m_secFinance);

    bool showFin = settings.value("sidebarSecFinance", true).toBool();
    m_secFinance->setVisible(showFin);
    m_headerFinance->setIcon(IconManager::instance().getIcon(showFin ? "chevron-down" : "chevron-right", QColor(), 12));

    connect(m_headerFinance, &QPushButton::clicked, this, [this]() {
        bool nowVis = !m_secFinance->isVisible();
        m_secFinance->setVisible(nowVis);
        m_headerFinance->setIcon(IconManager::instance().getIcon(nowVis ? "chevron-down" : "chevron-right", QColor(), 12));
        AppSettings s;
        s.setValue("sidebarSecFinance", nowVis);
    });

    refreshSidebarIcons();

    sidebarLayout->addStretch();

    // Bottom footer (ZCode-style): compact utility row pinned to the bottom —
    // Kiosk, Help, Settings gear and the signed-in user chip. The version
    // lives in Help -> About.
    QFrame* sidebarFooter = new QFrame(sidebar);
    sidebarFooter->setObjectName("sidebarFooter");
    QHBoxLayout* footerLayout = new QHBoxLayout(sidebarFooter);
    footerLayout->setContentsMargins(4, 6, 4, 6);
    footerLayout->setSpacing(4);

    auto footerBtn = [this, footerLayout](QPushButton* btn, const QString& icon, const QString& tip) {
        btn->setIcon(IconManager::instance().getIcon(icon, QColor(), 16));
        btn->setIconSize(QSize(16, 16));
        btn->setObjectName("navButton");
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(34, 34);
        footerLayout->addWidget(btn);
    };

    m_btnKiosk = new QPushButton(sidebarFooter);
    footerBtn(m_btnKiosk, "user-check", AppTranslator::t("Kiosk: Launch full-screen self-checkin portal."));
    connect(m_btnKiosk, &QPushButton::clicked, this, &MainWindow::launchKiosk);

    QPushButton* btnManual = new QPushButton(sidebarFooter);
    footerBtn(btnManual, "book-open", AppTranslator::t("User Manual: Open interactive documentation (F1)."));
    connect(btnManual, &QPushButton::clicked, this, [this]() { onShowUserManual(); });

    m_btnSettings = new QPushButton(sidebarFooter);
    footerBtn(m_btnSettings, "settings", AppTranslator::t("Settings: Configure church details, themes, modules & backups."));
    connect(m_btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);

    footerLayout->addStretch();

    m_footerUserChip = new QLabel(sidebarFooter);
    m_footerUserChip->setObjectName("footerUserChip");
    m_footerUserChip->setToolTip(AppTranslator::t("Signed-in user — click to switch user / sign in with PIN."));
    m_footerUserChip->setCursor(Qt::PointingHandCursor);
    connect(m_footerUserChip, &QLabel::linkActivated, this, [this]() { promptLogin(); });
    footerLayout->addWidget(m_footerUserChip);

    sidebarLayout->addWidget(sidebarFooter);

    m_sidebar = sidebar;
    m_logoLabel = logoLabel;
    m_isCompactSidebar = false;

    mainLayout->addWidget(sidebar);

    // Stacked widget for pages (plain container + layout subclass so page
    // minimums cannot resize the window; see IgnoreMinStackedLayout above).
    QWidget* mainContent = new QWidget(this);
    mainContent->setObjectName("mainContent");
    m_stackedWidget = new IgnoreMinStackedLayout(mainContent);
    mainLayout->addWidget(mainContent);

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
    // m_btnKiosk is already connected where it is created (bottom dock).
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

    // Pause the graph's force-layout simulation when leaving its page: the
    // O(n^2) pass keeps running at 30 FPS on an invisible canvas otherwise
    // (every drag/filter change also resumes it from other pages). Restore
    // on re-entry unless the user manually froze the layout.
    const int leavingIndex = m_stackedWidget->currentIndex();

    // Warn before leaving Settings with unsaved edits. Every sidebar route
    // funnels through here, and onSettingsClicked reloads stored values on
    // re-entry, so navigating away would otherwise discard edits silently
    // (module toggles included). Cancel keeps the current page; because the
    // nav handlers call setActiveNav() after switchPage(), returning early
    // also keeps the sidebar highlight on Settings.
    if (leavingIndex == 8 && m_settingsWidget && m_settingsWidget->isDirty()) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this, AppTranslator::t("Unsaved Settings"),
            AppTranslator::t("You have unsaved changes in Settings.\n\nSave them before leaving?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::Save) m_settingsWidget->saveSettings();
    }

    if (leavingIndex == 5 && m_graphWidget && m_graphWidget->physicsRunning()) {
        m_graphWidget->pausePhysics();
        m_graphPausedByPageLeave = true;
    }
    if (index == 5 && m_graphWidget && m_graphPausedByPageLeave) {
        m_graphWidget->resumePhysics();
        m_graphPausedByPageLeave = false;
    }

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
        m_logoLabel->setText(compact ? "" : "OpenEkklesia");
    }
}

void MainWindow::populatePages() {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
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
    groupsLayout->setContentsMargins(20, 20, 20, 20);
    groupsLayout->setSpacing(12);

    m_groupsTabWidget = new QTabWidget(groupsPage);
    m_groupsTabWidget->setObjectName("groupsTabWidget");

    // --- TAB 1: ACTIVE GROUPS & SERVING ROSTERS ---
    QWidget* tabActiveGroups = new QWidget(m_groupsTabWidget);
    QVBoxLayout* tabActiveLayout = new QVBoxLayout(tabActiveGroups);
    tabActiveLayout->setContentsMargins(10, 12, 10, 10);
    tabActiveLayout->setSpacing(12);

    // Header Row. The five action buttons live here; the three view filter
    // checkboxes are on their own row below — a single row containing both
    // measured ~1356px minimum width, which (plus the sidebar) exceeded the
    // 1280px logical desktop of a 150%-scaled 1920px panel. A window whose
    // declared minimum exceeds the screen opens oversized and GNOME cannot
    // tile or shrink it (Super+Up etc. do nothing).
    QHBoxLayout* grpHeaderLayout = new QHBoxLayout();
    QLabel* grpHeader = new QLabel(AppTranslator::t("Active Groups & Ministries"), this);
    grpHeader->setObjectName("pageHeader");
    grpHeaderLayout->addWidget(grpHeader);
    grpHeaderLayout->addStretch();

    // Second header row: the view filter checkboxes (populated below, added
    // to the page after the action row).
    QHBoxLayout* grpFilterLayout = new QHBoxLayout();
    QLabel* lblFilters = new QLabel("View:", this);
    lblFilters->setStyleSheet("font-size: 12px; font-weight: bold; color: palette(mid);");
    grpFilterLayout->addWidget(lblFilters);

    m_chkShowArchivedGroups = new QCheckBox("Show Archived Groups", this);
    m_chkShowArchivedGroups->setStyleSheet("font-size: 12px; font-weight: bold;");
    connectCheckBoxToggled(m_chkShowArchivedGroups, this, [this]() {
        refreshGroupsTree();
    });
    grpFilterLayout->addWidget(m_chkShowArchivedGroups);

    m_chkShowHiddenGroups = new QCheckBox("Show Hidden", this);
    m_chkShowHiddenGroups->setStyleSheet("font-size: 12px; font-weight: bold;");
    connectCheckBoxToggled(m_chkShowHiddenGroups, this, [this]() {
        refreshGroupsTree();
    });
    grpFilterLayout->addWidget(m_chkShowHiddenGroups);

    m_chkShowMembersInTree = new QCheckBox("Show People in Tree", this);
    m_chkShowMembersInTree->setStyleSheet("font-size: 12px; font-weight: bold;");
    connectCheckBoxToggled(m_chkShowMembersInTree, this, [this]() {
        refreshGroupsTree();
    });
    grpFilterLayout->addWidget(m_chkShowMembersInTree);

    m_btnAddGroup = new QPushButton(" Add Group", this);
    m_btnAddGroup->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnAddGroup->setObjectName("primaryBtn");
    connect(m_btnAddGroup, &QPushButton::clicked, this, [this]() {
        int defaultParentId = 0;
        QTreeWidgetItem* item = m_groupsTreeWidget ? m_groupsTreeWidget->currentItem() : nullptr;
        if (item) {
            bool isRoot = item->data(0, Qt::UserRole + 3).toBool();
            if (isRoot) {
                defaultParentId = item->data(0, Qt::UserRole).toInt();
            } else {
                int gId = item->data(0, Qt::UserRole).toInt();
                QSqlQuery qP;
                qP.prepare("SELECT parent_id FROM groups WHERE id = ?");
                qP.addBindValue(gId);
                if (qP.exec() && qP.next()) {
                    defaultParentId = qP.value(0).toInt();
                    if (defaultParentId <= 0) defaultParentId = gId;
                }
            }
        }
        showGroupDialog(0, false, defaultParentId);
    });
    grpHeaderLayout->addWidget(m_btnAddGroup);

    m_btnEditGroup = new QPushButton(" Edit Group", this);
    m_btnEditGroup->setIcon(IconManager::instance().getIcon("pencil", QColor(), 14));
    m_btnEditGroup->setObjectName("secondaryBtn");
    connect(m_btnEditGroup, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_groupsTreeWidget->currentItem();
        if (!item) {
            QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group to edit first."));
            return;
        }
        int groupId = item->data(0, Qt::UserRole).toInt();
        showGroupDialog(groupId, false);
    });
    grpHeaderLayout->addWidget(m_btnEditGroup);

    m_btnDuplicateGroup = new QPushButton(" Duplicate Group", this);
    m_btnDuplicateGroup->setIcon(IconManager::instance().getIcon("copy", QColor(), 14));
    m_btnDuplicateGroup->setObjectName("secondaryBtn");
    connect(m_btnDuplicateGroup, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = m_groupsTreeWidget->currentItem();
        if (!item) {
            QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group to duplicate first."));
            return;
        }
        int groupId = item->data(0, Qt::UserRole).toInt();
        showGroupDialog(groupId, true);
    });
    grpHeaderLayout->addWidget(m_btnDuplicateGroup);

    m_btnArchiveGroup = new QPushButton(" Archive / Unarchive", this);
    m_btnArchiveGroup->setIcon(IconManager::instance().getIcon("archive", QColor(), 14));
    m_btnArchiveGroup->setObjectName("secondaryBtn");
    connect(m_btnArchiveGroup, &QPushButton::clicked, this, &MainWindow::onArchiveGroup);
    grpHeaderLayout->addWidget(m_btnArchiveGroup);

    m_btnDeleteGroup = new QPushButton(" Delete Group", this);
    m_btnDeleteGroup->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDeleteGroup->setObjectName("dangerBtn");
    connect(m_btnDeleteGroup, &QPushButton::clicked, this, &MainWindow::onDeleteGroup);
    grpHeaderLayout->addWidget(m_btnDeleteGroup);

    tabActiveLayout->addLayout(grpHeaderLayout);
    tabActiveLayout->addLayout(grpFilterLayout);

    // Master-Detail Splitter View
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);

    // Left Panel: Group Tree List (drag & drop to reparent)
    m_groupsTreeWidget = new GroupsTreeWidget(this);
    m_groupsTreeWidget->setHeaderLabels({"Name", "Category", "Schedule & Location", "Parent Group", "Members", "Status"});
    m_groupsTreeWidget->setAlternatingRowColors(true);
    m_groupsTreeWidget->setAnimated(true);
    m_groupsTreeWidget->onReparent = [this](int groupId, int newParentId) {
        if (isReadOnlyRole()) return; // guests must not restructure ministries
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
            QMessageBox::warning(this, AppTranslator::t("Invalid Move"), AppTranslator::t("A group cannot be moved under one of its own subgroups."));
            return;
        }
        QSqlQuery u;
        u.prepare("UPDATE groups SET parent_id = ? WHERE id = ?");
        u.addBindValue(newParentId > 0 ? QVariant(newParentId) : QVariant(QMetaType(QMetaType::Int)));
        u.addBindValue(groupId);
        if (u.exec()) {
            DatabaseManager::bumpDataGeneration();
            refreshGroupsTree();
            refreshGroupDetailPanel(m_selectedGroupId);
            if (m_graphWidget) m_graphWidget->loadGraph();
        }
    };
    m_groupsTreeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_groupsTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_groupsTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupsTreeWidget, &QTreeWidget::customContextMenuRequested, this, [this, tok](const QPoint& pos) {
        QTreeWidgetItem* item = m_groupsTreeWidget->itemAt(pos);
        if (!item) return;
        int groupId = item->data(0, Qt::UserRole).toInt();
        QString groupName = item->text(0);
        if (groupId <= 0) return;

        bool isRoot = item->data(0, Qt::UserRole + 3).toBool();
        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";
        const QString rootTooltip = "Top-level Ministry Blueprints are system-locked and cannot be modified.";

        QMenu menu(this);
        QAction* actEdit = menu.addAction(QString("Edit '%1' Details...").arg(groupName));
        actEdit->setIcon(IconManager::instance().getIcon("pencil", tok.textPrimary, 14));
        QAction* actCopy = menu.addAction("Copy Group Name");
        actCopy->setIcon(IconManager::instance().getIcon("copy", tok.textPrimary, 14));
        QAction* actAddSub = menu.addAction("Add Sub-Group / Sub-Ministry...");
        actAddSub->setIcon(IconManager::instance().getIcon("plus", tok.textPrimary, 14));
        QAction* actAddMember = menu.addAction("Add Member to Group...");
        actAddMember->setIcon(IconManager::instance().getIcon("users", tok.textPrimary, 14));
        QAction* actGraph = menu.addAction("Highlight in Connection Graph");
        actGraph->setIcon(IconManager::instance().getIcon("network", tok.textPrimary, 14));
        menu.addSeparator();
        QAction* actArchive = menu.addAction("Toggle Archive Status");
        actArchive->setIcon(IconManager::instance().getIcon("layers", tok.textPrimary, 14));
        QAction* actToggleHidden = menu.addAction("Hide / Unhide (removes from tree & graph)");
        actToggleHidden->setIcon(IconManager::instance().getIcon("eye-off", tok.textPrimary, 14));
        QAction* actDelete = menu.addAction("Delete Group...");
        actDelete->setIcon(IconManager::instance().getIcon("trash-2", tok.accentDanger, 14));

        if (isReadOnly) {
            actEdit->setEnabled(false); actEdit->setToolTip(roTooltip);
            actAddSub->setEnabled(false); actAddSub->setToolTip(roTooltip);
            actAddMember->setEnabled(false); actAddMember->setToolTip(roTooltip);
            actArchive->setEnabled(false); actArchive->setToolTip(roTooltip);
            actToggleHidden->setEnabled(false); actToggleHidden->setToolTip(roTooltip);
            actDelete->setEnabled(false); actDelete->setToolTip(roTooltip);
        } else if (isRoot) {
            actArchive->setEnabled(false); actArchive->setToolTip(rootTooltip);
            actDelete->setEnabled(false); actDelete->setToolTip(rootTooltip);
            actAddMember->setEnabled(false);
            actAddMember->setToolTip("Direct membership is disabled on Ministry Blueprints. Create or select a sub-group/team to add members.");
        }

        QAction* sel = menu.exec(m_groupsTreeWidget->viewport()->mapToGlobal(pos));
        if (!sel) return;

        m_selectedGroupId = groupId;

        if (sel == actCopy) {
            QGuiApplication::clipboard()->setText(groupName);
        } else if (sel == actEdit && !isReadOnly) {
            onEditGroup();
        } else if (sel == actAddSub && !isReadOnly) {
            onAddGroup(groupId);
        } else if (sel == actAddMember && !isReadOnly) {
            m_selectedGroupId = groupId;
            refreshGroupDetailPanel(groupId);
        } else if (sel == actGraph) {
            onGraphClicked();
        } else if (sel == actArchive && !isReadOnly && !isRoot) {
            onArchiveGroup();
        } else if (sel == actToggleHidden && !isReadOnly) {
            onToggleHiddenGroup();
        } else if (sel == actDelete && !isReadOnly && !isRoot) {
            onDeleteGroup();
        }
    });
    splitter->addWidget(m_groupsTreeWidget);

    connect(m_groupsTreeWidget, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) {
            if (m_btnDeleteGroup) m_btnDeleteGroup->setEnabled(false);
            if (m_btnArchiveGroup) m_btnArchiveGroup->setEnabled(false);
            refreshGroupDetailPanel(0);
            return;
        }
        bool isRoot = current->data(0, Qt::UserRole + 3).toBool();
        const bool readOnly = isReadOnlyRole();
        if (m_btnDeleteGroup) {
            m_btnDeleteGroup->setEnabled(!isRoot && !readOnly);
            m_btnDeleteGroup->setToolTip(readOnly ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
                                                   : (isRoot ? "Top-level Ministry Blueprints are system-locked and cannot be deleted." : ""));
        }
        if (m_btnArchiveGroup) {
            m_btnArchiveGroup->setEnabled(!isRoot && !readOnly);
            m_btnArchiveGroup->setToolTip(readOnly ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
                                                   : (isRoot ? "Top-level Ministry Blueprints are system-locked and cannot be archived." : ""));
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
    m_lblRosterTitle = new QLabel("Active Roster & Roles", m_groupDetailCard);
    m_lblRosterTitle->setStyleSheet("font-size: 13px; font-weight: bold;");
    rosterHeaderRow->addWidget(m_lblRosterTitle);
    rosterHeaderRow->addStretch();

    m_btnCreateSubTeam = new QPushButton(" + Create Team / Sub-Group", m_groupDetailCard);
    m_btnCreateSubTeam->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnCreateSubTeam->setObjectName("primaryBtn");
    m_btnCreateSubTeam->setStyleSheet("padding: 4px 10px; font-size: 11px;");
    m_btnCreateSubTeam->setVisible(false);
    connect(m_btnCreateSubTeam, &QPushButton::clicked, this, [this]() {
        if (m_selectedGroupId > 0) {
            onAddGroup(m_selectedGroupId);
        }
    });
    rosterHeaderRow->addWidget(m_btnCreateSubTeam);

    m_btnAddRosterMember = new QPushButton(" Add Member", m_groupDetailCard);
    m_btnAddRosterMember->setIcon(IconManager::instance().getPrimaryIcon("user-plus", 14));
    m_btnAddRosterMember->setObjectName("primaryBtn");
    m_btnAddRosterMember->setStyleSheet("padding: 4px 10px; font-size: 11px;");
    connect(m_btnAddRosterMember, &QPushButton::clicked, this, [this]() {
        if (isReadOnlyRole()) {
            QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot change group rosters. Sign in with the Admin PIN (1234) to modify."));
            return;
        }
        if (m_selectedGroupId <= 0) {
            QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
            return;
        }
        // Guard: Root Ministry Blueprints do not allow direct members
        QSqlQuery qCheckRoot;
        qCheckRoot.prepare("SELECT parent_id FROM groups WHERE id = ?");
        qCheckRoot.addBindValue(m_selectedGroupId);
        if (qCheckRoot.exec() && qCheckRoot.next() && (qCheckRoot.value(0).isNull() || qCheckRoot.value(0).toInt() <= 0)) {
            QMessageBox::information(this, AppTranslator::t("Ministry Blueprint"), AppTranslator::t("Direct membership is disabled on Ministry Blueprints.\n\nPlease create or select a sub-group/team under this blueprint to enroll members."));
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle("Add Member to Group");
        dlg.resize(350, 180);
        QFormLayout* form = new QFormLayout(&dlg);
        form->setSpacing(12);

        QComboBox* personCombo = new QComboBox(&dlg);
        QSqlQuery qPeople;
        qPeople.prepare(
            "SELECT id, first_name || ' ' || last_name || ' (' || COALESCE(membership_status, 'Member') || ')' "
            "FROM people "
            "WHERE id NOT IN ("
            "    SELECT source_id FROM entity_relationships "
            "    WHERE target_type = 'Group' AND target_id = ? AND source_type = 'Person'"
            ") "
            "ORDER BY first_name, last_name");
        qPeople.addBindValue(m_selectedGroupId);
        if (qPeople.exec()) {
            while (qPeople.next()) {
                personCombo->addItem(qPeople.value(1).toString(), qPeople.value(0).toInt());
            }
        }
        if (personCombo->count() == 0) {
            personCombo->addItem("— All active members are already in this group —", 0);
            personCombo->setEnabled(false);
        }

        QComboBox* roleCombo = new QComboBox(&dlg);
        roleCombo->addItems({"Member", "Leader", "Co-Leader"});

        QComboBox* statusCombo = new QComboBox(&dlg);
        statusCombo->addItems({"Active", "Temporary", "Inactive"});
        statusCombo->setItemData(0, "Regularly active member or leader", Qt::ToolTipRole);
        statusCombo->setItemData(1, "Temporary: from time to time volunteers or in process", Qt::ToolTipRole);
        statusCombo->setItemData(2, "Inactive: paused or no longer active", Qt::ToolTipRole);

        form->addRow("Select Member:", personCombo);
        form->addRow("Assigned Role:", roleCombo);
        form->addRow("Group Status:", statusCombo);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        QPushButton* btnAdd = buttons->button(QDialogButtonBox::Ok);
        if (btnAdd) btnAdd->setText("Add");
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            int personId = personCombo->currentData().toInt();
            QString role = roleCombo->currentText();
            QString status = statusCombo->currentText();
            if (personId > 0) {
                QSqlDatabase db = QSqlDatabase::database();
                QSqlQuery check(db);
                check.prepare("SELECT id FROM entity_relationships WHERE source_type = 'Person' AND source_id = ? AND target_type = 'Group' AND target_id = ?");
                check.addBindValue(personId);
                check.addBindValue(m_selectedGroupId);
                if (check.exec() && check.next()) {
                    int relId = check.value(0).toInt();
                    QSqlQuery up(db);
                    up.prepare("UPDATE entity_relationships SET relationship_type = ?, status = ? WHERE id = ?");
                    up.addBindValue(role);
                    up.addBindValue(status);
                    up.addBindValue(relId);
                    if (up.exec()) {
                        refreshGroupDetailPanel(m_selectedGroupId);
                        refreshGroupsTree();
                        if (m_graphWidget) m_graphWidget->loadGraph();
                    } else {
                        QMessageBox::critical(this, AppTranslator::t("Database Error"), AppTranslator::t("Failed to update member in this group:\n") + up.lastError().text());
                    }
                } else {
                    QSqlQuery ins(db);
                    ins.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status, connection_source, joined_date, role) "
                                "VALUES ('Person', ?, 'Group', ?, ?, ?, 'manual', date('now','localtime'), ?)");
                    ins.addBindValue(personId);
                    ins.addBindValue(m_selectedGroupId);
                    ins.addBindValue(role);
                    ins.addBindValue(status);
                    ins.addBindValue(role);
                    if (ins.exec()) {
                        refreshGroupDetailPanel(m_selectedGroupId);
                        refreshGroupsTree();
                        if (m_graphWidget) m_graphWidget->loadGraph();
                    } else {
                        QMessageBox::critical(this, AppTranslator::t("Database Error"), AppTranslator::t("Failed to add member to group:\n") + ins.lastError().text());
                    }
                }
            }
        }
    });
    rosterHeaderRow->addWidget(m_btnAddRosterMember);

    m_btnSuggestPeople = new QPushButton(" Suggest People", m_groupDetailCard);
    m_btnSuggestPeople->setIcon(IconManager::instance().getIcon("sparkles", tok.accentPrimary, 14));
    m_btnSuggestPeople->setObjectName("secondaryBtn");
    m_btnSuggestPeople->setStyleSheet("padding: 4px 10px; font-size: 11px;");
    connect(m_btnSuggestPeople, &QPushButton::clicked, this, [this]() {
        if (isReadOnlyRole()) {
            QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot change group rosters. Sign in with the Admin PIN (1234) to modify."));
            return;
        }
        if (m_selectedGroupId <= 0) {
            QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
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
    rosterHeaderRow->addWidget(m_btnSuggestPeople);
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
    TableUtil::applyStandardTableSetup(m_groupRosterTable, false);
    m_groupRosterTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupRosterTable, &QTableWidget::customContextMenuRequested, this, [this, tok](const QPoint& pos) {
        int r = m_groupRosterTable->rowAt(pos.y());
        if (r < 0 || !m_groupRosterTable->item(r, 0)) return;
        int personId = m_groupRosterTable->item(r, 0)->data(Qt::UserRole).toInt();
        int relId = m_groupRosterTable->item(r, 0)->data(Qt::UserRole + 1).toInt();
        QString memName = m_groupRosterTable->item(r, 0)->text();

        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actProfile = menu.addAction(QString("View %1's Profile").arg(memName));
        actProfile->setIcon(IconManager::instance().getIcon("users", tok.textPrimary, 14));
        QAction* actCopy = menu.addAction(QString("Copy %1's Details").arg(memName));
        actCopy->setIcon(IconManager::instance().getIcon("copy", tok.textPrimary, 14));
        menu.addSeparator();
        QAction* actCareNote = menu.addAction("Log Pastoral Care Note...");
        actCareNote->setIcon(IconManager::instance().getIcon("pencil", tok.textPrimary, 14));
        QMenu* mRole = menu.addMenu("Change Assigned Role");
        mRole->setIcon(IconManager::instance().getIcon("shield-check", tok.textPrimary, 14));
        QAction* aLeader = mRole->addAction("Make Leader");
        QAction* aCoLeader = mRole->addAction("Make Co-Leader");
        QAction* aMember = mRole->addAction("Make Regular Member");

        menu.addSeparator();
        QAction* actRemove = menu.addAction(QString("Remove %1 from Group").arg(memName));
        actRemove->setIcon(IconManager::instance().getIcon("trash-2", tok.accentDanger, 14));

        if (isReadOnly) {
            actCareNote->setEnabled(false); actCareNote->setToolTip(roTooltip);
            mRole->setEnabled(false); mRole->setToolTip(roTooltip);
            actRemove->setEnabled(false); actRemove->setToolTip(roTooltip);
        }

        QAction* chosen = menu.exec(m_groupRosterTable->viewport()->mapToGlobal(pos));
        if (chosen == actProfile) {
            selectPerson(personId);
        } else if (chosen == actCopy) {
            QString role = m_groupRosterTable->item(r, 1) ? m_groupRosterTable->item(r, 1)->text() : "";
            QGuiApplication::clipboard()->setText(QString("%1 (%2)").arg(memName, role));
        } else if (chosen == actCareNote && !isReadOnly) {
            onLogQuickPastoralNote(personId);
        } else if (chosen == aLeader && !isReadOnly) {
            QSqlQuery up; up.prepare("UPDATE entity_relationships SET relationship_type = 'Leader' WHERE id = ?"); up.addBindValue(relId); up.exec();
            refreshGroupDetailPanel(m_selectedGroupId); refreshGroupsTree();
        } else if (chosen == aCoLeader && !isReadOnly) {
            QSqlQuery up; up.prepare("UPDATE entity_relationships SET relationship_type = 'Co-Leader' WHERE id = ?"); up.addBindValue(relId); up.exec();
            refreshGroupDetailPanel(m_selectedGroupId); refreshGroupsTree();
        } else if (chosen == aMember && !isReadOnly) {
            QSqlQuery up; up.prepare("UPDATE entity_relationships SET relationship_type = 'Member' WHERE id = ?"); up.addBindValue(relId); up.exec();
            refreshGroupDetailPanel(m_selectedGroupId); refreshGroupsTree();
        } else if (chosen == actRemove && !isReadOnly) {
            QSqlQuery del; del.prepare("DELETE FROM entity_relationships WHERE id = ?"); del.addBindValue(relId);
            if (del.exec()) { DatabaseManager::bumpDataGeneration(); refreshGroupDetailPanel(m_selectedGroupId); refreshGroupsTree(); }
        }
    });
    m_groupRosterTable->setWordWrap(false);
    m_groupRosterTable->verticalHeader()->setVisible(false);
    m_groupRosterTable->setAlternatingRowColors(true);
    detailLayout->addWidget(m_groupRosterTable);

    // Group Dynamics & Chemistry Section
    QLabel* chemistryTitle = new QLabel("Group Dynamics & Chemistry Analysis", m_groupDetailCard);
    chemistryTitle->setStyleSheet("font-size: 12px; font-weight: bold; margin-top: 4px;");
    detailLayout->addWidget(chemistryTitle);

    m_lblGroupChemistryReport = new QLabel(m_groupDetailCard);
    m_lblGroupChemistryReport->setWordWrap(true);
    detailLayout->addWidget(m_lblGroupChemistryReport);

    QPushButton* btnViewInGraph = new QPushButton(" View in Connection Graph", m_groupDetailCard);
    btnViewInGraph->setIcon(IconManager::instance().getIcon("network", tok.accentPrimary, 14));
    btnViewInGraph->setObjectName("secondaryBtn");
    connect(btnViewInGraph, &QPushButton::clicked, this, [this]() {
        if (m_selectedGroupId > 0) {
            onGraphClicked();
        } else {
            QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
        }
    });
    detailLayout->addWidget(btnViewInGraph);

    splitter->addWidget(m_groupDetailCard);
    splitter->setSizes({600, 400});

    tabActiveLayout->addWidget(splitter);

    // --- TAB 2: MINISTRY BLUEPRINTS & ARCHETYPE CATALOG ---
    m_tabMinistryBlueprints = new QWidget(m_groupsTabWidget);
    setupMinistryBlueprintsTab(m_tabMinistryBlueprints);

    m_groupsTabWidget->addTab(tabActiveGroups, IconManager::instance().getIcon("layers", 14), "Active Groups && Serving Rosters");
    m_groupsTabWidget->addTab(m_tabMinistryBlueprints, IconManager::instance().getIcon("building-2", 14), "Ministry Blueprints && Archetype Catalog");

    groupsLayout->addWidget(m_groupsTabWidget);

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
    m_lblWelcomeHeader = new QLabel(AppTranslator::t("Welcome to OpenEkklesia"), page);
    m_lblWelcomeHeader->setObjectName("pageHeader");
    m_lblWelcomeHeader->setStyleSheet("font-size: 22px; font-weight: bold; margin: 0;");

    m_lblSubHeader = new QLabel(page);
    m_lblSubHeader->setStyleSheet("font-size: 13px; opacity: 0.8;");
    headerTextLay->addWidget(m_lblWelcomeHeader);
    headerTextLay->addWidget(m_lblSubHeader);

    headerRow->addLayout(headerTextLay);
    headerRow->addStretch();

    // Quick Actions Bar
    QPushButton* btnAddPersonQuick = new QPushButton(" Add Person", page);
    btnAddPersonQuick->setIcon(IconManager::instance().getPrimaryIcon("user-plus", 14));
    btnAddPersonQuick->setObjectName("primaryBtn");
    connect(btnAddPersonQuick, &QPushButton::clicked, this, [this]() {
        onMembersClicked();
        if (m_peopleWidget) m_peopleWidget->onAddPerson();
    });

    QPushButton* btnGivingQuick = new QPushButton(" Giving", page);
    btnGivingQuick->setIcon(IconManager::instance().getIcon("hand-coins", QColor(), 14));
    btnGivingQuick->setObjectName("secondaryBtn");
    connect(btnGivingQuick, &QPushButton::clicked, this, &MainWindow::onContributionsClicked);

    QPushButton* btnServiceQuick = new QPushButton(" Services", page);
    btnServiceQuick->setIcon(IconManager::instance().getIcon("list-music", QColor(), 14));
    btnServiceQuick->setObjectName("secondaryBtn");
    connect(btnServiceQuick, &QPushButton::clicked, this, &MainWindow::onServiceClicked);

    QPushButton* btnCmdQuick = new QPushButton(" Search (Ctrl+K)", page);
    btnCmdQuick->setIcon(IconManager::instance().getIcon("search", QColor(), 14));
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
    QPushButton* btnConfigKpi = new QPushButton(" Configure KPIs", page);
    btnConfigKpi->setIcon(IconManager::instance().getIcon("settings", QColor(), 14));
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
    QLabel* alertsCardTitle = new QLabel("Member Inactivity & Care Alerts", alertsCard);
    alertsCardTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: " + ThemeManager::instance().currentTokens().accentDanger.name() + ";");
    m_btnQuickNote = new QPushButton(" Quick Note", alertsCard);
    m_btnQuickNote->setIcon(IconManager::instance().getPrimaryIcon("pencil", 14));
    m_btnQuickNote->setObjectName("primaryBtn");
    m_btnQuickNote->setStyleSheet("padding: 2px 8px; font-size: 11px;");
    connect(m_btnQuickNote, &QPushButton::clicked, this, [this]() {
        int pid = -1;
        if (m_lstPastoralAlerts->currentItem()) pid = m_lstPastoralAlerts->currentItem()->data(Qt::UserRole).toInt();
        else if (m_lstVisitorFollowup->currentItem()) pid = m_lstVisitorFollowup->currentItem()->data(Qt::UserRole).toInt();
        onLogQuickPastoralNote(pid);
    });
    alertsTitleRow->addWidget(alertsCardTitle);
    alertsTitleRow->addStretch();
    alertsTitleRow->addWidget(m_btnQuickNote);
    alertsCardLayout->addLayout(alertsTitleRow);

    m_lstPastoralAlerts = new QListWidget(alertsCard);
    m_lstPastoralAlerts->setMaximumHeight(100);
    m_lstPastoralAlerts->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_lstPastoralAlerts, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = m_lstPastoralAlerts->itemAt(pos);
        if (!it) return;
        int pid = it->data(Qt::UserRole).toInt();
        QMenu m(this);
        QAction* aNote = m.addAction("Log Quick Pastoral Note...");
        aNote->setIcon(IconManager::instance().getIcon("pencil", ThemeManager::instance().currentTokens().textPrimary, 14));
        QAction* aProf = m.addAction("View Member Dossier");
        aProf->setIcon(IconManager::instance().getIcon("users", ThemeManager::instance().currentTokens().textPrimary, 14));
        QAction* sel = m.exec(m_lstPastoralAlerts->mapToGlobal(pos));
        if (sel == aNote) onLogQuickPastoralNote(pid);
        else if (sel == aProf) selectPerson(pid);
    });
    connect(m_lstPastoralAlerts, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int personId = item->data(Qt::UserRole).toInt();
        if (personId > 0) selectPerson(personId);
    });
    alertsCardLayout->addWidget(m_lstPastoralAlerts);

    QLabel* followupTitle = new QLabel("Recent Visitors — Follow-up", alertsCard);
    followupTitle->setStyleSheet(QString("font-size: 12px; font-weight: bold; text-transform: uppercase; color: %1;")
                                     .arg(ThemeManager::instance().currentTokens().accentWarning.name()));
    m_lstVisitorFollowup = new QListWidget(alertsCard);
    m_lstVisitorFollowup->setMaximumHeight(100);
    m_lstVisitorFollowup->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_lstVisitorFollowup, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = m_lstVisitorFollowup->itemAt(pos);
        if (!it) return;
        int pid = it->data(Qt::UserRole).toInt();
        QMenu m(this);
        QAction* aNote = m.addAction("Log Quick Pastoral Note...");
        aNote->setIcon(IconManager::instance().getIcon("pencil", ThemeManager::instance().currentTokens().textPrimary, 14));
        QAction* aProf = m.addAction("View Member Dossier");
        aProf->setIcon(IconManager::instance().getIcon("users", ThemeManager::instance().currentTokens().textPrimary, 14));
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
    QLabel* upcomingServicesTitle = new QLabel("Upcoming Worship & Services", alertsCard);
    upcomingServicesTitle->setStyleSheet("font-size: 12px; font-weight: bold; text-transform: uppercase; color: palette(highlight);");
    m_lstUpcomingServices = new QListWidget(alertsCard);
    m_lstUpcomingServices->setMaximumHeight(100);
    m_lstUpcomingServices->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_lstUpcomingServices, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_lstUpcomingServices->itemAt(pos);
        if (!item) return;
        const ThemeTokens tok = ThemeManager::instance().currentTokens();

        QMenu menu(this);
        QAction* actOpen = menu.addAction(IconManager::instance().getIcon("music", tok.accentPrimary, 14), "Open in Service Builder");
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Service Summary");

        QAction* sel = menu.exec(m_lstUpcomingServices->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actOpen) onServiceClicked();
        else if (sel == actCopy) QGuiApplication::clipboard()->setText(item->text());
    });
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

    AppSettings s;
    QStringList defaultKpis = {"members", "visitors", "groups", "shepherd_due", "giving_month", "giving_ytd"};
    m_activeKpis = s.value("dashboardKpis", defaultKpis).toStringList();
    if (m_activeKpis.isEmpty()) m_activeKpis = defaultKpis;

    auto makeCard = [this](const QString& id, const QString& title, const QString& iconName, const QString& subtext) {
        ThemeTokens tok = ThemeManager::instance().currentTokens();
        QWidget* card = new QWidget(this);
        card->setObjectName("cardWidget");
        QVBoxLayout* v = new QVBoxLayout(card);
        v->setContentsMargins(16, 14, 16, 14);
        v->setSpacing(6);

        QHBoxLayout* top = new QHBoxLayout();
        QLabel* lblTitle = new QLabel(title, card);
        lblTitle->setStyleSheet("font-size: 12px; font-weight: bold; opacity: 0.8; text-transform: uppercase;");
        QLabel* lblIcon = new QLabel(card);
        lblIcon->setPixmap(IconManager::instance().getPixmap(iconName, tok.accentPrimary, 18));
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
        info.icon = iconName;
        info.subtext = subtext;
        info.widget = card;
        info.valLabel = valLabel;
        m_kpiRegistry.insert(id, info);

        m_cardsLayout->addWidget(card);
    };

    for (const QString& kpiId : m_activeKpis) {
        if (kpiId == "members") makeCard("members", "Members", "users", "Active Congregation");
        else if (kpiId == "visitors") makeCard("visitors", "Visitors / Attenders", "heart-handshake", "Growth Pipeline");
        else if (kpiId == "groups") makeCard("groups", "Active Groups", "layers", "Small Groups & Ministries");
        else if (kpiId == "shepherd_due") makeCard("shepherd_due", "Follow-ups Due", "calendar", "Pastoral Queue (Today)");
        else if (kpiId == "at_risk") makeCard("at_risk", "At-Risk Members", "alert-triangle", "Pastoral Care Flags");
        else if (kpiId == "unconnected") makeCard("unconnected", "Unconnected", "network", "Not in Small Group");
        else if (kpiId == "giving_month") makeCard("giving_month", "Giving (This Month)", "hand-coins", "Offerings Recorded");
        else if (kpiId == "giving_ytd") makeCard("giving_ytd", "Giving (YTD)", "bar-chart-3", "Annual Tithes");
        else if (kpiId == "checkins") makeCard("checkins", "Check-ins", "user-check", "Kiosk Attendance");
        else if (kpiId == "total_assets") makeCard("total_assets", "Total Assets", "landmark", "Account Balances");
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
        {"members", "Church Members (Formal Membership)"},
        {"visitors", "Growth Pipeline (Visitors & Regular Attenders)"},
        {"groups", "Active Ministries & Small Groups"},
        {"shepherd_due", "Follow-ups Due Today (Shepherd's Workbench)"},
        {"at_risk", "At-Risk Members & Care Flags"},
        {"unconnected", "Unconnected Members (No Small Group)"},
        {"giving_month", "Giving Recorded This Month"},
        {"giving_ytd", "Giving Year-to-Date"},
        {"checkins", "Kiosk Check-ins (Attendance)"},
        {"total_assets", "Total Assets / Account Balances"}
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
            QMessageBox::warning(&dlg, AppTranslator::t("Selection Limit"), AppTranslator::t("Please select between 2 and 8 KPI cards for optimal layout."));
            return;
        }
        AppSettings s;
        s.setValue("dashboardKpis", selected);
        m_activeKpis = selected;
        rebuildKpiCards();
        dlg.accept();
    });

    dlg.exec();
}

void MainWindow::onLogQuickPastoralNote(int prefillPersonId) {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot log pastoral care notes. Sign in with the Admin PIN (1234) to record care interactions."));
        return;
    }
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
        QMessageBox::warning(this, AppTranslator::t("Missing Details"), AppTranslator::t("Please enter a short note describing the care interaction."));
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

    QMessageBox::information(this, AppTranslator::t("Note Logged"), QString(AppTranslator::t("Pastoral care note logged for %1.")).arg(personCombo->currentText()));
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
    QString greeting = (hour < 12) ? AppTranslator::t("Good Morning")
                                   : (hour < 17) ? AppTranslator::t("Good Afternoon")
                                                 : AppTranslator::t("Good Evening");
    AppSettings s;
    QString churchName = s.value("churchName", "OpenEkklesia Church").toString();
    QString churchSubtitle = s.value("churchSubtitle", "Church Management & Pastoral Analytics").toString();
    if (m_lblWelcomeHeader) {
        m_lblWelcomeHeader->setText(QString("%1, %2").arg(greeting, m_username.isEmpty() ? AppTranslator::t("Pastor / Leader") : m_username));
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
        y.prepare("SELECT COALESCE(SUM(amount), 0) FROM contributions "
                  "WHERE substr(contribution_date, 1, 4) = ? AND contribution_date <= date('now','localtime')");
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
    // 'Inactivity threshold' from Settings (default 90 days, matching the
    // Settings page default) drives who counts as a lapsed giver / inactive
    // attender.
    AppSettings set;
    const int inactivityDays = qBound(7, set.value("inactivityDays", 90).toInt(), 365);
    const QString inactivityExpr = QString("date('now', '-%1 days')").arg(inactivityDays);

    QSqlQuery qAlerts;
    qAlerts.prepare(
        "SELECT id, first_name || ' ' || last_name, COALESCE(care_flag, ''), membership_status, "
        "       (SELECT MAX(contribution_date) FROM contributions WHERE person_id = people.id) AS last_give "
        "FROM people "
        "WHERE membership_status IN ('Member', 'Regular Attender') "
        "  AND ( "
        "        ( NOT EXISTS (SELECT 1 FROM contributions WHERE person_id = people.id AND contribution_date >= " + inactivityExpr + ") "
        "          AND NOT EXISTS (SELECT 1 FROM attendance WHERE person_id = people.id AND timestamp >= " + inactivityExpr + ") ) "
        "        OR care_flag IN ('At-Risk', 'Needs Contact') "
        "      ) "
        "ORDER BY (care_flag IN ('At-Risk', 'Needs Contact')) DESC, last_name "
        "LIMIT 10");
    if (qAlerts.exec()) {
        while (qAlerts.next()) {
            int id = qAlerts.value(0).toInt();
            QString name = qAlerts.value(1).toString();
            QString careFlag = qAlerts.value(2).toString();
            QString membership = qAlerts.value(3).toString();
            QString lastGive = qAlerts.value(4).toString();

            QString label;
            if (careFlag == "At-Risk" || careFlag == "Needs Contact") {
                label = QString("[%1] %2").arg(careFlag, name);
            } else if (!lastGive.isEmpty() && QDate::fromString(lastGive, "yyyy-MM-dd").daysTo(QDate::currentDate()) > 60) {
                label = QString("[Lapsed Giver] %1 (Last give: %2)").arg(name, lastGive);
            } else {
                QString tag = (membership == "Regular Attender") ? "Attender" : "Member";
                label = QString("[Inactive %1] %2").arg(tag, name);
            }
            QListWidgetItem* item = new QListWidgetItem(label, m_lstPastoralAlerts);
            item->setData(Qt::UserRole, id);
        }
    }

    // Birthday window ("next 7 days") is computed in C++ so a Dec -> Jan wrap
    // does not silently drop matches: the old SQL BETWEEN over strftime('%m-%d')
    // compared e.g. '12-28' > '01-03' as strings and matched nothing.
    QStringList birthdayDays;
    for (int i = 0; i <= 7; ++i) {
        birthdayDays << QDate::currentDate().addDays(i).toString("MM-dd");
    }
    QStringList birthdayPlaceholders;
    for (qsizetype i = 0; i < birthdayDays.size(); ++i) birthdayPlaceholders << "?";
    QMap<int, QList<QPair<int, QString>>> birthdaysByDay; // days away -> (person id, label)
    QSqlQuery qMilestones;
    qMilestones.prepare(QString(
        "SELECT id, first_name || ' ' || last_name, date_of_birth, strftime('%m-%d', date_of_birth) "
        "FROM people WHERE date_of_birth IS NOT NULL AND date_of_birth != '' "
        "AND strftime('%m-%d', date_of_birth) IN (%1)").arg(birthdayPlaceholders.join(", ")));
    for (const QString& day : birthdayDays) qMilestones.addBindValue(day);
    if (qMilestones.exec()) {
        while (qMilestones.next()) {
            const int daysAway = static_cast<int>(birthdayDays.indexOf(qMilestones.value(3).toString()));
            if (daysAway < 0) continue;
            birthdaysByDay[daysAway].append(qMakePair(
                qMilestones.value(0).toInt(),
                QString("[Birthday This Week] %1 (%2)").arg(qMilestones.value(1).toString(),
                                                             qMilestones.value(2).toString())));
        }
    }
    int birthdaysShown = 0;
    for (auto it = birthdaysByDay.cbegin(); it != birthdaysByDay.cend() && birthdaysShown < 5; ++it) {
        for (const auto& birthday : it.value()) {
            if (birthdaysShown >= 5) break;
            QListWidgetItem* item = new QListWidgetItem(birthday.second, m_lstPastoralAlerts);
            item->setData(Qt::UserRole, birthday.first);
            ++birthdaysShown;
        }
    }

    if (m_lstPastoralAlerts->count() == 0) {
        m_lstPastoralAlerts->addItem("All active. No member inactivity, care, or lapsed giving alerts.");
    }

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
                QString("%1 — %2 (first visit: %3)").arg(name, tag, visitDate),
                m_lstVisitorFollowup);
            item->setData(Qt::UserRole, id);
        }
    }
    if (m_lstVisitorFollowup->count() == 0) {
        m_lstVisitorFollowup->addItem("No visitors currently in the pipeline.");
    }

    if (m_lstUpcomingServices) {
        m_lstUpcomingServices->clear();
        QSqlQuery qServices("SELECT date, title, leader FROM services WHERE date >= date('now') AND title IS NOT NULL AND title != '' ORDER BY date ASC LIMIT 3");
        if (qServices.exec()) {
            while (qServices.next()) {
                QString sDate = qServices.value(0).toString();
                QString sTitle = qServices.value(1).toString();
                QString sLeader = qServices.value(2).toString();
                m_lstUpcomingServices->addItem(QString("[%1] %2 (Leader: %3)").arg(sDate, sTitle, sLeader.isEmpty() ? "Unassigned" : sLeader));
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
        // A committed ledger entry changes the ASSET-account balances behind the
        // dashboard's Total Assets KPI, so refresh the dashboard immediately.
        connect(m_contributionsWidget, &ContributionsWidget::ledgerChanged,
                this, &MainWindow::updateDashboardStats);
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
    m_graphWidget->loadGraphIfDirty();
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
            AppSettings s;
            QString chName = s.value("churchName", "OpenEkklesia").toString();
            setWindowTitle(QString(AppTranslator::t("%1 - Church Management System")).arg(chName));
            m_isDarkMode = ThemeManager::instance().currentTokens().isDark;
            applyModuleVisibility();
        });
        ensurePageLoaded(8, [this]() { return m_settingsWidget; });
    }
    m_settingsWidget->loadSettings();
    switchPage(8);
    setActiveNav(m_btnSettings);
}

void MainWindow::openSettingsCategory(int categoryIndex) {
    onSettingsClicked();
    if (m_settingsWidget) {
        m_settingsWidget->selectCategory(categoryIndex);
    }
}

void MainWindow::onFacilitiesClicked() {
    if (!isModuleEnabled("facilities")) return;
    if (!m_facilitiesWidget) {
        m_facilitiesWidget = new FacilitiesWidget(this);
        m_facilitiesWidget->applyRole(m_userRole);
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
            // The drill-through reloads the People roster, which used to discard
            // typed profile edits silently. Ask first (one prompt per click); on
            // Cancel keep the analytics view and abort without touching either
            // the People filter widgets or the profile editor.
            if (m_peopleWidget && !m_peopleWidget->confirmDiscardEdits()) return;
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
        m_shepherdWidget->applyRole(m_userRole);
        connect(m_shepherdWidget, &ShepherdWidget::navigateToMember, this, &MainWindow::selectPerson);
        ensurePageLoaded(12, [this]() { return m_shepherdWidget; });
    }
    m_shepherdWidget->refreshAll();
    switchPage(12);
    setActiveNav(m_btnShepherd);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    AppSettings s;
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
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    if (m_btnTopTheme) {
        m_btnTopTheme->setIcon(IconManager::instance().getIcon(tok.isDark ? "sun" : "moon", QColor(), 14));
    }
    updateTopBarRoleBadge();
}

void MainWindow::updateTopBarRoleBadge() {
    if (!m_btnTopRole) return;
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    const bool isAdmin = (m_userRole == "admin");
    const bool isGuest = (m_userRole == "readonly");
    QString label = isAdmin ? " Admin" : (isGuest ? " Guest (Read-Only)" : QString(" %1").arg(m_userRole));
    QString iconName = isAdmin ? "shield-check" : (isGuest ? "user" : "shield");
    QColor col = isAdmin ? tok.accentPrimary : (isGuest ? tok.textMuted : tok.accentSuccess);
    m_btnTopRole->setText(label);
    m_btnTopRole->setIcon(IconManager::instance().getIcon(iconName, col, 13));
}

void MainWindow::launchKiosk() {
    if (!isModuleEnabled("kiosk")) return;
    KioskWindow kiosk(this);
    kiosk.exec();
}

void MainWindow::selectPerson(int personId) {
    // Dashboard/alerts/palette profile drill-through reloads the People view:
    // never discard typed profile edits without asking. Cancel aborts the
    // navigation and leaves the current page, filter and editor untouched.
    if (personId > 0 && m_peopleWidget && !m_peopleWidget->confirmDiscardEdits()) return;
    if (!m_peopleWidget) onMembersClicked();
    if (!m_peopleWidget) return; // module disabled: nothing to navigate to
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
        if (!m_inventoryWidget) return; // module disabled: nothing to navigate to
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
    if (m_facilitiesWidget) m_facilitiesWidget->applyRole(m_userRole);
    if (m_shepherdWidget) m_shepherdWidget->applyRole(m_userRole);
    if (m_settingsWidget) m_settingsWidget->applyRole(m_userRole);

    // Groups module write controls: guest (read-only) sessions may browse
    // ministries and rosters but must not change them. Handlers additionally
    // guard themselves, so keyboard/context-menu paths cannot bypass this.
    const bool readOnly = isReadOnlyRole();
    const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";
    auto gateButton = [readOnly, &roTooltip](QPushButton* btn) {
        if (!btn) return;
        btn->setEnabled(!readOnly);
        btn->setToolTip(readOnly ? roTooltip : QString());
    };
    gateButton(m_btnAddGroup);
    gateButton(m_btnEditGroup);
    gateButton(m_btnDuplicateGroup);
    gateButton(m_btnArchiveGroup);
    gateButton(m_btnDeleteGroup);
    gateButton(m_btnAddRosterMember);
    gateButton(m_btnCreateSubTeam);
    gateButton(m_btnSuggestPeople);
    gateButton(m_btnDefineBlueprint);
    gateButton(m_btnQuickNote);
    if (m_groupsTreeWidget) m_groupsTreeWidget->setDragEnabled(!readOnly);

    // Re-render the dynamically built role-dependent controls (roster combos and
    // blueprint card buttons are created on refresh, so they must follow the
    // signed-in role without waiting for the next manual refresh).
    if (m_blueprintsGridContainer) refreshMinistryBlueprintsTab();
    if (m_groupRosterTable && m_selectedGroupId > 0) refreshGroupDetailPanel(m_selectedGroupId);

    if (statusBar()) {
        statusBar()->showMessage(QString(AppTranslator::t("Signed in as: %1 (%2)")).arg(m_username, m_userRole));
        if (m_footerUserChip)
            m_footerUserChip->setText(QString("<b style='color:var(--text-primary)'>%1</b>").arg(m_username.toHtmlEscaped()));
    }
    updateTopBarRoleBadge();
}

bool MainWindow::isReadOnlyRole() const {
    return m_userRole == "readonly" || m_userRole == "guest";
}

void MainWindow::applyModuleVisibility() {
    AppSettings s;
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
    if (m_actKiosk) m_actKiosk->setVisible(modKiosk);
}

bool MainWindow::isModuleEnabled(const QString& moduleKey, bool defaultValue) const {
    return AppSettings().value(QString("modules/%1").arg(moduleKey), defaultValue).toBool();
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
            if (!tip.isEmpty() && m_lblHintText) {
                m_lblHintText->setText(tip);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshGroupsTree() {
    if (!m_groupsTreeWidget) return;
    // Suppress currentItemChanged while clearing/repopulating: clear() emits
    // a null-current signal whose handler wipes the detail panel (and can
    // re-enter refreshes) before we get the chance to restore the selection.
    m_groupsTreeWidget->blockSignals(true);
    // Rebuild from scratch. The population below appends new QTreeWidgetItems;
    // without this clear every refresh (Show Archived toggle, page re-entry,
    // post-edit refresh) stacked another full copy of the tree on top of the
    // previous one.
    m_groupsTreeWidget->clear();
    ThemeTokens tok = ThemeManager::instance().currentTokens();

    bool showArchived = m_chkShowArchivedGroups && m_chkShowArchivedGroups->isChecked();
    bool showHidden = m_chkShowHiddenGroups && m_chkShowHiddenGroups->isChecked();

    QSqlDatabase db = QSqlDatabase::database();

    QMap<int, QList<QVariantMap>> childrenMap;
    QList<QVariantMap> topLevelGroups;
    QMap<int, bool> fetchedGroupIds;

    QString queryStr = "SELECT g.id, g.name, g.category, g.description, g.parent_id, p.name AS parent_name, g.is_archived, "
                       "g.is_hidden, g.meeting_day, g.meeting_time, g.location, "
                       "(SELECT COUNT(*) FROM entity_relationships WHERE target_type = 'Group' AND target_id = g.id) AS member_count "
                       "FROM groups g "
                       "LEFT JOIN groups p ON g.parent_id = p.id ";
    QStringList whereClauses;
    if (!showArchived) {
        whereClauses << "(g.is_archived = 0 OR g.is_archived IS NULL)";
    }
    if (!showHidden) {
        whereClauses << "(g.is_hidden = 0 OR g.is_hidden IS NULL)";
    }
    if (!whereClauses.isEmpty()) {
        queryStr += "WHERE " + whereClauses.join(" AND ") + " ";
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
        row["is_hidden"] = qAll.value(7).toInt();
        row["meeting_day"] = qAll.value(8).toString();
        row["meeting_time"] = qAll.value(9).toString();
        row["location"] = qAll.value(10).toString();
        row["member_count"] = qAll.value(11).toInt();

        int pId = row["parent_id"].toInt();
        fetchedGroupIds.insert(row["id"].toInt(), true);
        if (pId > 0) {
            childrenMap[pId].append(row);
        } else {
            topLevelGroups.append(row);
        }
    }

    // Promote live sub-groups whose parent was filtered out (archived/hidden)
    // to the top level. Without this, hiding or archiving a parent silently
    // dropped its still-active descendants from the tree.
    for (auto it = childrenMap.begin(); it != childrenMap.end(); ) {
        if (!fetchedGroupIds.contains(it.key())) {
            for (QVariantMap orphan : it.value()) {
                orphan["promoted_orphan"] = true;
                topLevelGroups.append(orphan);
            }
            it = childrenMap.erase(it);
        } else {
            ++it;
        }
    }

    // Bulk-fetch memberships for every visible group in ONE query (the
    // per-group query was N+1: one SELECT per group row per tree rebuild).
    QMap<int, QList<QVariantMap>> membersByGroup;
    if (m_chkShowMembersInTree && m_chkShowMembersInTree->isChecked()) {
        QSqlQuery qAllMembers(db);
        qAllMembers.prepare(
            "SELECT er.target_id, p.id, p.first_name || ' ' || p.last_name, er.relationship_type, p.phone_number, p.email, COALESCE(er.status, 'Active'), p.care_flag "
            "FROM entity_relationships er JOIN people p ON p.id = er.source_id "
            "WHERE er.target_type = 'Group' AND er.source_type = 'Person' "
            "ORDER BY CASE WHEN er.relationship_type IN ('Leader', 'Co-Leader', 'Teacher') THEN 0 ELSE 1 END, p.first_name");
        if (qAllMembers.exec()) {
            while (qAllMembers.next()) {
                QVariantMap m;
                m["id"] = qAllMembers.value(1).toInt();
                m["name"] = qAllMembers.value(2).toString();
                m["role"] = qAllMembers.value(3).toString();
                m["phone"] = qAllMembers.value(4).toString();
                m["email"] = qAllMembers.value(5).toString();
                m["status"] = qAllMembers.value(6).toString();
                m["care"] = qAllMembers.value(7).toString();
                membersByGroup[qAllMembers.value(0).toInt()].append(m);
            }
        }
    }

    std::function<void(QTreeWidgetItem*, const QVariantMap&)> addGroupNode;
    addGroupNode = [&](QTreeWidgetItem* parentItem, const QVariantMap& groupData) {
        int id = groupData["id"].toInt();
        QString name = groupData["name"].toString();
        QString cat = groupData["category"].toString();
        QString parentName = groupData["parent_name"].toString();
        bool isArchived = groupData["is_archived"].toInt() == 1;
        bool isHidden = groupData["is_hidden"].toInt() == 1;
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
        if (isHidden) {
            name += " [HIDDEN]";
        }

        QTreeWidgetItem* item = nullptr;
        if (parentItem) {
            item = new QTreeWidgetItem(parentItem);
            item->setText(0, QString("↳ %1").arg(name));
            item->setForeground(0, QBrush(isArchived ? tok.textMuted : tok.accentPrimary));
            item->setFont(0, QFont("Segoe UI", 9, QFont::DemiBold));
            item->setData(0, Qt::UserRole + 3, false); // Child group
        } else {
            item = new QTreeWidgetItem(m_groupsTreeWidget);
            if (groupData.value("promoted_orphan").toBool()) {
                // A normal sub-group whose parent is filtered out of the tree:
                // show it at top level, but keep its non-root semantics (no
                // blueprint emoji, archive/delete/roster rules still apply).
                item->setText(0, name);
                item->setForeground(0, QBrush(isArchived ? tok.textMuted : tok.accentPrimary));
                item->setFont(0, QFont("Segoe UI", 9, QFont::DemiBold));
                item->setData(0, Qt::UserRole + 3, false); // Child group
            } else {
                QString displayTitle = name;
                if (!displayTitle.startsWith("🏛️") && !displayTitle.startsWith("📋")) {
                    displayTitle = QString("🏛️ %1").arg(displayTitle);
                }
                item->setText(0, displayTitle);
                if (isArchived) {
                    item->setForeground(0, QBrush(tok.textMuted));
                } else {
                    item->setForeground(0, QBrush(tok.accentWarning)); // Bold highlight for fixed ministries
                }
                item->setFont(0, QFont("Segoe UI", 10, QFont::Bold));
                item->setData(0, Qt::UserRole + 3, true); // Root ministry blueprint
            }
        }

        item->setText(1, cat.isEmpty() ? "Ministry Blueprint" : cat);
        item->setText(2, schedule);
        item->setText(3, parentName.isEmpty() ? "-- Top Level --" : parentName);
        item->setText(4, QString("%1 members").arg(memCount));
        item->setText(5, isArchived ? "Archived" : "Active");
        item->setData(0, Qt::UserRole, id);
        item->setData(0, Qt::UserRole + 1, "Group");

        if (m_chkShowMembersInTree && m_chkShowMembersInTree->isChecked()) {
            for (const QVariantMap& mv : membersByGroup.value(id)) {
                QTreeWidgetItem* pItem = new QTreeWidgetItem(item);
                const int pId = mv["id"].toInt();
                const QString pName = mv["name"].toString();
                const QString pRole = mv["role"].toString();
                const QString pPhone = mv["phone"].toString();
                const QString pEmail = mv["email"].toString();
                const QString pStatus = mv["status"].toString();
                const QString pCare = mv["care"].toString();
                {
                const QString pCare = mv["care"].toString();

                    pItem->setText(0, QString("      %1").arg(pName));
                    pItem->setIcon(0, IconManager::instance().getIcon("users", QColor(), 14));
                    pItem->setFont(0, QFont("Segoe UI", 9));
                    pItem->setForeground(0, QBrush(tok.textPrimary));
                    pItem->setText(1, pRole.isEmpty() ? "Member" : pRole);
                    if (pRole == "Leader" || pRole == "Co-Leader") {
                        pItem->setForeground(1, QBrush(tok.accentWarning));
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
                    // Member rows are read-only in the drag & drop tree: they must
                    // never be dragged or used as a drop target for reparenting.
                    Qt::ItemFlags pFlags = pItem->flags();
                    pFlags.setFlag(Qt::ItemIsDragEnabled, false);
                    pFlags.setFlag(Qt::ItemIsDropEnabled, false);
                    pItem->setFlags(pFlags);
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

    // Restore the previous selection (if its group survived the rebuild), so
    // the detail panel the caller just refreshed isn't wiped by a null-current
    // signal, then unblock.
    if (m_selectedGroupId > 0) {
        QTreeWidgetItemIterator it(m_groupsTreeWidget);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toInt() == m_selectedGroupId) {
                m_groupsTreeWidget->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }
    m_groupsTreeWidget->blockSignals(false);
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
                 "g.recruitment_status, g.parent_id, g.contact_person_id "
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
    int parentId = qGrp.value(11).toInt();
    const bool readOnly = isReadOnlyRole();
    const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

    // NOTE: the primary contact/leader enrollment is persisted by
    // showGroupDialog()'s save path. Viewing a group must stay read-only, so no
    // INSERTs happen here.


    // Check if this group has child sub-teams (making it a top-level Ministry Blueprint / Folder)
    QList<int> childGroupIds;
    QMap<int, QString> childNames;
    QSqlQuery qSub(db);
    qSub.prepare("SELECT id, name FROM groups WHERE parent_id = ? AND (is_archived = 0 OR is_archived IS NULL)");
    qSub.addBindValue(groupId);
    if (qSub.exec()) {
        while (qSub.next()) {
            int cid = qSub.value(0).toInt();
            childGroupIds.append(cid);
            childNames[cid] = qSub.value(1).toString();
        }
    }
    bool isRootMinistry = (parentId == 0);

    if (m_lblRosterTitle) {
        m_lblRosterTitle->setText(isRootMinistry ? "Sub-Teams & Consolidated Overview" : "Active Roster & Roles");
    }
    if (m_btnAddRosterMember) {
        m_btnAddRosterMember->setVisible(!isRootMinistry);
    }
    if (m_btnCreateSubTeam) {
        m_btnCreateSubTeam->setVisible(isRootMinistry);
    }

    if (isRootMinistry) {
        m_lblGroupDetailTitle->setText(QString("🏛️ %1 <span style='font-size: 11px; font-weight: normal; color: %2;'>(Ministry Blueprint & Macro Overview)</span>").arg(name, tok.accentPrimary.name()));
    } else {
        m_lblGroupDetailTitle->setText(QString("%1 %2").arg(name, isArchived ? QString("<span style='color: %1;'>(Archived)</span>").arg(tok.accentDanger.name()) : ""));
    }
    m_lblGroupDetailTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(tok.accentPrimary.name()));

    QStringList metaParts;
    // Badges adapt to the theme: translucent token backgrounds instead of the
    // old dark-theme-only blobs (#1e1e2e / #382418 / #18332b).
    auto badge = [&tok](const QColor& accent, const QString& text) {
        return QString("<span style='background-color: rgba(%1, %2, %3, 0.14); color: %4; border: 1px solid %4; border-radius: 4px; padding: 1px 5px; font-weight: bold;'>%5</span>")
            .arg(accent.red()).arg(accent.green()).arg(accent.blue()).arg(accent.name(), text);
    };
    metaParts << QString("<b>Category:</b> %1").arg(cat);
    if (isRootMinistry) {
        metaParts << badge(tok.accentPrimary, QString("Sub-Teams: %1 serving groups").arg(childGroupIds.size()));
    }
    if (recruitStatus == "Elected") {
        metaParts << badge(tok.accentWarning, "Elected Governance Body (Annual Vote)");
    } else if (recruitStatus == "Seasonal") {
        metaParts << badge(tok.accentWarning, "Seasonal Cohort (Closed Enrollment)");
    } else if (recruitStatus == "Closed") {
        metaParts << QString("<span style='background-color: palette(mid); color: palette(placeholderText); border: 1px solid %1; border-radius: 4px; padding: 1px 5px; font-weight: bold;'>Closed (Invite Only)</span>").arg(tok.textMuted.name());
    } else {
        metaParts << badge(tok.accentSuccess, "Open for Recruitment");
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

    // Adjust Table Headers for Sub-Teams if Root Ministry
    if (isRootMinistry) {
        m_groupRosterTable->setColumnCount(5);
        m_groupRosterTable->setHorizontalHeaderLabels({"Member", "Role", "Sub-Team", "Status", "Action"});
    } else {
        m_groupRosterTable->setColumnCount(4);
        m_groupRosterTable->setHorizontalHeaderLabels({"Member", "Role", "Status", "Action"});
    }

    QSqlQuery qRoster(db);
    if (isRootMinistry) {
        QStringList idStrs;
        idStrs << QString::number(groupId);
        for (int cid : childGroupIds) idStrs << QString::number(cid);
        qRoster.prepare(QString(
            "SELECT er.id, p.id, p.first_name || ' ' || p.last_name, er.relationship_type, COALESCE(er.status, 'Active'), p.relational_style, g.name AS team_name "
            "FROM entity_relationships er "
            "JOIN people p ON er.source_id = p.id "
            "JOIN groups g ON er.target_id = g.id "
            "WHERE er.target_type = 'Group' AND er.target_id IN (%1) "
            "ORDER BY g.name, CASE er.relationship_type WHEN 'Leader' THEN 1 WHEN 'Co-Leader' THEN 2 ELSE 3 END, p.first_name").arg(idStrs.join(",")));
    } else {
        qRoster.prepare(
            "SELECT er.id, p.id, p.first_name || ' ' || p.last_name, er.relationship_type, COALESCE(er.status, 'Active'), p.relational_style, '' AS team_name "
            "FROM entity_relationships er "
            "JOIN people p ON er.source_id = p.id "
            "WHERE er.target_type = 'Group' AND er.target_id = ? "
            "ORDER BY CASE er.relationship_type WHEN 'Leader' THEN 1 WHEN 'Co-Leader' THEN 2 ELSE 3 END, p.first_name");
        qRoster.addBindValue(groupId);
    }

    int totalCount = 0;
    QMap<QString, int> stylesCount;
    QStringList burnoutAlerts;

    // Burnout counts for the whole roster in ONE grouped query (was one
    // COUNT per member per panel refresh).
    QHash<int, int> leaderCounts;
    {
        QSqlQuery qBurnAll(db);
        qBurnAll.exec("SELECT source_id, COUNT(*) FROM entity_relationships "
                      "WHERE source_type = 'Person' AND relationship_type IN ('Leader', 'Co-Leader') "
                      "GROUP BY source_id");
        while (qBurnAll.next()) leaderCounts.insert(qBurnAll.value(0).toInt(), qBurnAll.value(1).toInt());
    }

    if (qRoster.exec()) {
        while (qRoster.next()) {
            int relId = qRoster.value(0).toInt();
            int personId = qRoster.value(1).toInt();
            QString memName = qRoster.value(2).toString();
            QString role = qRoster.value(3).toString();
            QString status = qRoster.value(4).toString();
            QString style = qRoster.value(5).toString();
            QString teamName = qRoster.value(6).toString();
            if (style.isEmpty()) style = "Unspecified";
            stylesCount[style]++;
            totalCount++;

            // Leadership Burnout Check (serving in 3+ leader roles)
            const int leaderRoleCount = leaderCounts.value(personId, 0);
            {
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
            // activated(), not currentTextChanged: the latter fires on every
            // arrow-key highlight while the popup is open, which rebuilt the
            // roster (destroying the open popup), rewrote the tree, and
            // persisted browsed-but-uncommitted roles to the DB.
            connect(roleCombo, QOverload<int>::of(&QComboBox::activated), this, [this, relId, role, roleCombo](int idx) {
                if (isReadOnlyRole()) return;
                const QString newRole = roleCombo->itemText(idx);
                if (newRole == role) return;
                QSqlQuery up;
                up.prepare("UPDATE entity_relationships SET relationship_type = ? WHERE id = ?");
                up.addBindValue(newRole);
                up.addBindValue(relId);
                up.exec();
                refreshGroupDetailPanel(m_selectedGroupId);
            });
            if (readOnly) {
                roleCombo->setEnabled(false);
                roleCombo->setToolTip(roTooltip);
            }
            m_groupRosterTable->setCellWidget(r, 1, roleCombo);

            int statusCol = 2;
            if (isRootMinistry) {
                QTableWidgetItem* teamItem = new QTableWidgetItem(teamName);
                teamItem->setForeground(QBrush(tok.accentPrimary));
                teamItem->setFlags(teamItem->flags() & ~Qt::ItemIsEditable);
                m_groupRosterTable->setItem(r, 2, teamItem);
                statusCol = 3;
            }

            // Status Combo (Active, Temporary [time-to-time/in-process], Inactive)
            QComboBox* statusCombo = new QComboBox();
            statusCombo->addItems({"Active", "Temporary", "Inactive"});
            statusCombo->setItemData(0, "Regularly active member or leader", Qt::ToolTipRole);
            statusCombo->setItemData(1, "Temporary: from time to time volunteers or in process", Qt::ToolTipRole);
            statusCombo->setItemData(2, "Inactive: paused or no longer active", Qt::ToolTipRole);

            QString displayStatus = "Active";
            if (status == "Inactive") {
                displayStatus = "Inactive";
            } else if (status == "Temporary" || status == "Visitor" || status == "Guest" || status == "Serving") {
                displayStatus = "Temporary";
            } else {
                displayStatus = "Active";
            }
            statusCombo->setCurrentText(displayStatus);
            statusCombo->setStyleSheet(QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 4px; }")
                                       .arg(tok.bgInput.name(), tok.textPrimary.name(), tok.borderColor.name()));
            connect(statusCombo, QOverload<int>::of(&QComboBox::activated), this, [this, relId, status, statusCombo](int idx) {
                if (isReadOnlyRole()) return;
                const QString newStatus = statusCombo->itemText(idx);
                if (newStatus == status) return;
                QSqlQuery up;
                up.prepare("UPDATE entity_relationships SET status = ? WHERE id = ?");
                up.addBindValue(newStatus);
                up.addBindValue(relId);
                up.exec();
                refreshGroupDetailPanel(m_selectedGroupId);
            });
            if (readOnly) {
                statusCombo->setEnabled(false);
                statusCombo->setToolTip(roTooltip);
            }
            m_groupRosterTable->setCellWidget(r, statusCol, statusCombo);

            // Action Button (Remove)
            QPushButton* btnRemove = new QPushButton();
            btnRemove->setIcon(IconManager::instance().getIcon("trash-2", tok.accentDanger, 14));
            btnRemove->setToolTip("Remove member from group");
            btnRemove->setStyleSheet(QString("border: none; color: %1; font-weight: bold; background: transparent;").arg(tok.accentDanger.name()));
            connect(btnRemove, &QPushButton::clicked, this, [this, relId, memName]() {
                if (isReadOnlyRole()) return;
                auto confirm = QMessageBox::question(this, AppTranslator::t("Remove Member"), QString(AppTranslator::t("Remove '%1' from this group?")).arg(memName), QMessageBox::Yes | QMessageBox::No);
                if (confirm == QMessageBox::Yes) {
                    QSqlQuery del;
                    del.prepare("DELETE FROM entity_relationships WHERE id = ?");
                    del.addBindValue(relId);
                    if (del.exec()) {
                        DatabaseManager::bumpDataGeneration();
                        refreshGroupDetailPanel(m_selectedGroupId);
                        refreshGroupsTree();
                        if (m_graphWidget) m_graphWidget->loadGraph();
                    }
                }
            });
            if (readOnly) {
                btnRemove->setEnabled(false);
                btnRemove->setToolTip(roTooltip);
            }
            m_groupRosterTable->setCellWidget(r, statusCol + 1, btnRemove);
        }
    }

    // Archetype & Relational Dynamics Analysis
    m_lblGroupChemistryReport->setStyleSheet(QString("font-size: 11px; background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px; color: %3;")
                                              .arg(tok.bgInput.name(), tok.borderColor.name(), tok.textPrimary.name()));

    QMap<QString, int> archCount;
    archCount["Catalyst"] = 0;
    archCount["Shepherd"] = 0;
    archCount["Architect"] = 0;
    archCount["Spark"] = 0;
    archCount["Anchor"] = 0;

    QSqlQuery qArch(db);
    if (isRootMinistry) {
        QStringList idStrs;
        idStrs << QString::number(groupId);
        for (int cid : childGroupIds) idStrs << QString::number(cid);
        qArch.prepare(QString(
            "SELECT p.ministry_personality, p.personality_secondary "
            "FROM entity_relationships er "
            "JOIN people p ON er.source_id = p.id "
            "WHERE er.target_type = 'Group' AND er.target_id IN (%1)").arg(idStrs.join(",")));
    } else {
        qArch.prepare("SELECT p.ministry_personality, p.personality_secondary "
                      "FROM entity_relationships er "
                      "JOIN people p ON er.source_id = p.id "
                      "WHERE er.target_type = 'Group' AND er.target_id = ?");
        qArch.addBindValue(groupId);
    }
    if (qArch.exec()) {
        while (qArch.next()) {
            QString pri = qArch.value(0).toString().trimmed();
            if (archCount.contains(pri)) archCount[pri]++;
            else if (!pri.isEmpty()) archCount["Anchor"]++;
        }
    }

    QString report = QString("<b>%1 Roster Size:</b> %2 members ").arg(isRootMinistry ? "Department Total" : "Roster Size").arg(totalCount);
    if (capacity > 0 && totalCount > capacity) {
        report += QString("<span style='color: %1;'>(Over Capacity: Max limit is %2)</span><br>").arg(tok.accentDanger.name()).arg(capacity);
    } else if (totalCount > 12 && !isRootMinistry) {
        report += QString("<span style='color: %1;'>(Overcrowded: >12 members. Consider splitting into subgroups)</span><br>").arg(tok.accentDanger.name());
    }

    if (!burnoutAlerts.isEmpty()) {
        report += QString("<b>Leadership Burnout Warning:</b> %1<br>").arg(burnoutAlerts.join(", "));
    }

    report += QString("<div style='margin-top: 4px; margin-bottom: 4px;'><b>🧬 %1 Archetype Composition:</b><br>")
                  .arg(isRootMinistry ? "Department Blueprint" : "Team");
    // Archetype brand colors are fixed accents, but the pastels only read on
    // dark themes — switch to darker equivalents on light themes.
    const bool dark = tok.isDark;
    const QString colCatalyst = dark ? "#fab387" : "#c2410c";
    const QString colShepherd = dark ? "#a6e3a1" : "#15803d";
    const QString colArchitect = dark ? "#89b4fa" : "#1d4ed8";
    const QString colSpark = dark ? "#f9e2af" : "#b45309";
    const QString colAnchor = dark ? "#cba6f7" : "#7e22ce";
    report += QString("<span style='color: %6;'>🔥 Catalyst: %1</span> &nbsp;|&nbsp; "
                      "<span style='color: %7;'>🌿 Shepherd: %2</span> &nbsp;|&nbsp; "
                      "<span style='color: %8;'>📐 Architect: %3</span> &nbsp;|&nbsp; "
                      "<span style='color: %9;'>⚡ Spark: %4</span> &nbsp;|&nbsp; "
                      "<span style='color: %10;'>⚓ Anchor: %5</span></div>")
                  .arg(archCount["Catalyst"])
                  .arg(archCount["Shepherd"])
                  .arg(archCount["Architect"])
                  .arg(archCount["Spark"])
                  .arg(archCount["Anchor"])
                  .arg(colCatalyst, colShepherd, colArchitect, colSpark, colAnchor);

    // Dynamic Team Health Diagnostics
    QStringList warnings;
    QStringList suggestions;

    if (totalCount > 0) {
        const QString colDanger = tok.accentDanger.name();
        const QString colWarn = dark ? "#fab387" : "#c2410c";
        const QString colAmber = tok.accentWarning.name();
        if (archCount["Anchor"] == 0 && archCount["Architect"] == 0) {
            warnings.append(QString("<span style='color: %1;'>⚠️ <b>Missing Execution Backbone</b>: No Anchor or Architect on team.</span>").arg(colDanger));
            suggestions.append("Anchor or Architect");
        }
        if (archCount["Shepherd"] == 0) {
            warnings.append(QString("<span style='color: %1;'>⚠️ <b>Missing Relational Care</b>: No Shepherd in roster.</span>").arg(colWarn));
            suggestions.append("Shepherd");
        }
        if (archCount["Catalyst"] >= 3) {
            warnings.append(QString("<span style='color: %1;'>⚠️ <b>High Vision Density</b>: ≥3 Catalysts may create competing agendas.</span>").arg(colAmber));
        }
        if (archCount["Spark"] >= 4) {
            warnings.append(QString("<span style='color: %1;'>⚠️ <b>High Energy Surge</b>: ≥4 Sparks require strong Anchor grounding.</span>").arg(colAmber));
        }
    }

    if (!warnings.isEmpty()) {
        report += warnings.join("<br>") + "<br>";
    }

    if (!suggestions.isEmpty()) {
        report += QString("<i>💡 <b>Smart Bridge Priority:</b> Engine will prioritize candidates with <b>%1</b> archetypes for this %2.</i>")
                      .arg(suggestions.join(" & "), isRootMinistry ? "department" : "group");
    } else if (totalCount > 0) {
        report += QString("<i style='color: %1;'>✨ <b>Balanced Group Chemistry!</b> Great equilibrium across all behavioral dimensions.</i>")
                      .arg(tok.accentSuccess.name());
    }

    m_lblGroupChemistryReport->setText(report);
}

void MainWindow::showGroupDialog(int groupId, bool isDuplicate, int defaultParentId) {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot create or edit groups. Sign in with the Admin PIN (1234) to modify ministry structure."));
        return;
    }
    QSqlDatabase db = QSqlDatabase::database();
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QString currentName, currentCat, currentDesc, currentDay, currentTime, currentLoc, currentNotes, currentSkills, currentRecruitStatus = "Open";
    QString currentPriArch = "Shepherd", currentSecArch = "Anchor";
    int currentParentId = defaultParentId, currentContactId = 0, currentCap = 0, currentMinAge = 0, currentMaxAge = 0;

    if (groupId > 0) {
        QSqlQuery q(db);
        q.prepare("SELECT name, category, description, parent_id, meeting_day, meeting_time, location, contact_person_id, max_capacity, notes, required_skills, min_age, max_age, recruitment_status, ideal_primary_archetype, ideal_secondary_archetype FROM groups WHERE id = ?");
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
            currentPriArch = q.value(14).toString();
            currentSecArch = q.value(15).toString();
        }
    }

    if (groupId == 0) {
        if (currentParentId <= 0) {
            QTreeWidgetItem* curItem = m_groupsTreeWidget ? m_groupsTreeWidget->currentItem() : nullptr;
            if (curItem) {
                currentParentId = curItem->data(0, Qt::UserRole).toInt();
            }
        }
        if (currentParentId <= 0) {
            QSqlQuery qFirst("SELECT id FROM groups WHERE parent_id IS NULL OR parent_id = 0 ORDER BY id ASC LIMIT 1", db);
            if (qFirst.next()) {
                currentParentId = qFirst.value(0).toInt();
            }
        }
    }

    if (groupId == 0 && currentParentId > 0) {
        // Automatically inherit blueprint from parent ministry
        QSqlQuery qP(db);
        qP.prepare("SELECT name, category, ideal_primary_archetype, ideal_secondary_archetype, required_skills FROM groups WHERE id = ?");
        qP.addBindValue(currentParentId);
        if (qP.exec() && qP.next()) {
            QString pName = qP.value(0).toString();
            currentCat = qP.value(1).toString();
            currentPriArch = qP.value(2).toString();
            currentSecArch = qP.value(3).toString();
            currentSkills = qP.value(4).toString();

            // Match against canonical blueprint to ensure precise archetype values
            for (const auto& bp : MinistryArchetypeRegistry::canonicalBlueprints()) {
                if (bp.displayName == currentCat || bp.categoryKey == currentCat ||
                    bp.displayName == pName || bp.categoryKey == pName ||
                    currentCat.contains(bp.categoryKey, Qt::CaseInsensitive) ||
                    pName.contains(bp.categoryKey, Qt::CaseInsensitive)) {
                    currentCat = bp.displayName;
                    currentPriArch = bp.primaryArchetype;
                    currentSecArch = bp.secondaryArchetype;
                    if (currentSkills.isEmpty()) currentSkills = bp.requiredGifts.join(", ");
                    break;
                }
            }
        }
    }

    QDialog dlg(this);
    if (isDuplicate) {
        dlg.setWindowTitle("Duplicate Group");
    } else if (groupId > 0) {
        dlg.setWindowTitle(QString("Edit Group — %1").arg(currentName));
    } else {
        dlg.setWindowTitle("Create New Group / Ministry");
    }

    dlg.resize(480, 620);
    dlg.setStyleSheet(QString("QDialog { background-color: %1; color: %2; } QLabel { color: %2; } QLineEdit, QComboBox, QSpinBox, QTextEdit { background-color: %3; color: %2; border: 1px solid palette(mid); border-radius: 6px; padding: 4px; }")
                      .arg(tok.bgSurface.name(), tok.textPrimary.name(), tok.bgBase.name()));

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(10);

    QLineEdit* nameEdit = new QLineEdit(isDuplicate ? QString("%1 (Copy)").arg(currentName) : currentName, &dlg);
    
    QComboBox* parentCombo = new QComboBox(&dlg);
    if (groupId > 0 && currentParentId <= 0) {
        parentCombo->addItem("🏛️ -- Root Ministry Blueprint --", 0);
    }

    QSqlQuery qParents(db);
    qParents.prepare(
        "WITH RECURSIVE descendants(id) AS ("
        "  SELECT id FROM groups WHERE parent_id = ? "
        "  UNION ALL "
        "  SELECT g.id FROM groups g JOIN descendants d ON g.parent_id = d.id"
        ") "
        "SELECT id, name, parent_id FROM groups "
        "WHERE id != ? AND id NOT IN (SELECT id FROM descendants) "
        "ORDER BY (parent_id IS NULL OR parent_id = 0) DESC, name ASC");
    qParents.addBindValue(groupId);
    qParents.addBindValue(groupId);
    if (qParents.exec()) {
        while (qParents.next()) {
            int pid = qParents.value(0).toInt();
            QString pname = qParents.value(1).toString();
            int parentOfP = qParents.value(2).toInt();
            QString label = (parentOfP <= 0) ? QString("🏛️ %1 (Top-Level Blueprint)").arg(pname) : QString("   ↳ %1").arg(pname);
            parentCombo->addItem(label, pid);
        }
    }
    int pIdx = parentCombo->findData(currentParentId);
    if (pIdx >= 0) {
        parentCombo->setCurrentIndex(pIdx);
    } else if (parentCombo->count() > 0) {
        parentCombo->setCurrentIndex(0);
        currentParentId = parentCombo->currentData().toInt();
    }

    QComboBox* catCombo = new QComboBox(&dlg);
    for (const auto& bp : MinistryArchetypeRegistry::canonicalBlueprints()) {
        catCombo->addItem(bp.displayName, bp.categoryKey);
    }
    if (!currentCat.isEmpty()) {
        int cIdx = catCombo->findText(currentCat, Qt::MatchContains);
        if (cIdx >= 0) catCombo->setCurrentIndex(cIdx);
    }

    // Ideal Archetypes
    QComboBox* idealPrimaryCombo = new QComboBox(&dlg);
    const struct { ChurchArchetype arch; const char* name; const char* icon; } kArchItems[] = {
        {ChurchArchetype::Catalyst, "Catalyst", "🔥"},
        {ChurchArchetype::Shepherd, "Shepherd", "🌿"},
        {ChurchArchetype::Architect, "Architect", "📐"},
        {ChurchArchetype::Spark, "Spark", "⚡"},
        {ChurchArchetype::Anchor, "Anchor", "⚓"}
    };
    for (const auto& a : kArchItems) {
        idealPrimaryCombo->addItem(QString("%1 %2").arg(a.icon, a.name), a.name);
    }

    QComboBox* idealSecondaryCombo = new QComboBox(&dlg);
    idealSecondaryCombo->addItem("— None —", "None");
    for (const auto& a : kArchItems) {
        idealSecondaryCombo->addItem(QString("%1 %2").arg(a.icon, a.name), a.name);
    }

    if (!currentPriArch.isEmpty()) {
        int idx = idealPrimaryCombo->findData(currentPriArch);
        if (idx >= 0) idealPrimaryCombo->setCurrentIndex(idx);
    }
    if (!currentSecArch.isEmpty()) {
        int idx = idealSecondaryCombo->findData(currentSecArch);
        if (idx >= 0) idealSecondaryCombo->setCurrentIndex(idx);
    }

    QLineEdit* skillsEdit = new QLineEdit(currentSkills, &dlg);
    skillsEdit->setPlaceholderText("e.g. Hospitality, Helps, Teaching");

    // Parent Selection -> Auto-Cascade Blueprint Defaults
    connect(parentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [parentCombo, catCombo, idealPrimaryCombo, idealSecondaryCombo, skillsEdit, db](int) {
        int pid = parentCombo->currentData().toInt();
        if (pid > 0) {
            QSqlQuery qP(db);
            qP.prepare("SELECT name, category, ideal_primary_archetype, ideal_secondary_archetype, required_skills FROM groups WHERE id = ?");
            qP.addBindValue(pid);
            if (qP.exec() && qP.next()) {
                QString pName = qP.value(0).toString();
                QString pCat = qP.value(1).toString();
                QString pPri = qP.value(2).toString();
                QString pSec = qP.value(3).toString();
                QString pSkills = qP.value(4).toString();

                for (const auto& bp : MinistryArchetypeRegistry::canonicalBlueprints()) {
                    if (bp.displayName == pCat || bp.categoryKey == pCat ||
                        bp.displayName == pName || bp.categoryKey == pName ||
                        pCat.contains(bp.categoryKey, Qt::CaseInsensitive) ||
                        pName.contains(bp.categoryKey, Qt::CaseInsensitive)) {
                        pCat = bp.displayName;
                        pPri = bp.primaryArchetype;
                        pSec = bp.secondaryArchetype;
                        if (pSkills.isEmpty()) pSkills = bp.requiredGifts.join(", ");
                        break;
                    }
                }

                if (!pCat.isEmpty()) {
                    int cIdx = catCombo->findText(pCat, Qt::MatchContains);
                    if (cIdx >= 0) catCombo->setCurrentIndex(cIdx);
                }
                if (!pPri.isEmpty()) {
                    int idx = idealPrimaryCombo->findData(pPri);
                    if (idx >= 0) idealPrimaryCombo->setCurrentIndex(idx);
                }
                if (!pSec.isEmpty()) {
                    int idx = idealSecondaryCombo->findData(pSec);
                    if (idx >= 0) idealSecondaryCombo->setCurrentIndex(idx);
                }
                if (skillsEdit->text().trimmed().isEmpty() && !pSkills.isEmpty()) {
                    skillsEdit->setText(pSkills);
                }
            }
        }
    });

    // Auto-prefill archetype and skills when Category changes
    connect(catCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [catCombo, idealPrimaryCombo, idealSecondaryCombo, skillsEdit](int) {
        QString key = catCombo->currentData().toString();
        MinistryBlueprint bp = MinistryArchetypeRegistry::getBlueprint(key);
        int priIdx = idealPrimaryCombo->findData(bp.primaryArchetype);
        if (priIdx >= 0) idealPrimaryCombo->setCurrentIndex(priIdx);
        int secIdx = idealSecondaryCombo->findData(bp.secondaryArchetype);
        if (secIdx >= 0) idealSecondaryCombo->setCurrentIndex(secIdx);
        if (skillsEdit->text().trimmed().isEmpty()) {
            skillsEdit->setText(bp.requiredGifts.join(", "));
        }
    });

    QComboBox* dayCombo = new QComboBox(&dlg);
    dayCombo->addItems({"Flexible / None", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"});
    if (!currentDay.isEmpty()) dayCombo->setCurrentText(currentDay);

    QLineEdit* timeEdit = new QLineEdit(currentTime, &dlg);
    timeEdit->setPlaceholderText("e.g. 7:00 PM");

    QLineEdit* locationEdit = new QLineEdit(currentLoc, &dlg);
    locationEdit->setPlaceholderText("e.g. Room 204 / Fellowship Hall");

    // Smart Leader / Contact Selector: enrolled roster first + searchable modal lookup
    QHBoxLayout* contactRow = new QHBoxLayout();
    contactRow->setSpacing(6);
    QComboBox* contactCombo = new QComboBox(&dlg);
    contactCombo->addItem("-- None Assigned --", 0);

    QSet<int> enrolledIds;
    if (groupId > 0) {
        QSqlQuery qEnrolled(db);
        qEnrolled.prepare(
            "SELECT p.id, p.first_name || ' ' || p.last_name, er.relationship_type "
            "FROM entity_relationships er "
            "JOIN people p ON er.source_id = p.id "
            "WHERE er.target_type = 'Group' AND er.target_id = ? "
            "ORDER BY (er.relationship_type LIKE '%Leader%') DESC, p.last_name, p.first_name");
        qEnrolled.addBindValue(groupId);
        if (qEnrolled.exec()) {
            while (qEnrolled.next()) {
                int pid = qEnrolled.value(0).toInt();
                QString name = qEnrolled.value(1).toString();
                QString role = qEnrolled.value(2).toString();
                contactCombo->addItem(QString("%1 (Group %2)").arg(name, role.isEmpty() ? "Member" : role), pid);
                enrolledIds.insert(pid);
            }
        }
    }

    if (currentContactId > 0 && !enrolledIds.contains(currentContactId)) {
        QSqlQuery qC(db);
        qC.prepare("SELECT first_name || ' ' || last_name FROM people WHERE id = ?");
        qC.addBindValue(currentContactId);
        if (qC.exec() && qC.next()) {
            contactCombo->addItem(QString("%1 (Assigned Contact)").arg(qC.value(0).toString()), currentContactId);
            enrolledIds.insert(currentContactId);
        }
    }

    int cIdx = contactCombo->findData(currentContactId);
    if (cIdx >= 0) contactCombo->setCurrentIndex(cIdx);

    QPushButton* btnSearchContact = new QPushButton(" Search Member...", &dlg);
    btnSearchContact->setIcon(IconManager::instance().getIcon("search", tok.textPrimary, 12));
    btnSearchContact->setObjectName("secondaryBtn");
    btnSearchContact->setToolTip("Search all church members to assign as primary leader/contact");
    contactRow->addWidget(contactCombo, 1);
    contactRow->addWidget(btnSearchContact);

    connect(btnSearchContact, &QPushButton::clicked, &dlg, [&dlg, contactCombo, db, tok]() {
        QDialog searchDlg(&dlg);
        searchDlg.setWindowTitle("Select Primary Leader / Contact");
        searchDlg.resize(380, 420);
        searchDlg.setStyleSheet(QString("QDialog { background-color: %1; color: %2; } QLabel { color: %2; } QLineEdit { background-color: %3; color: %2; border: 1px solid palette(mid); border-radius: 6px; padding: 5px; } QListWidget { background-color: %3; color: %2; border: 1px solid palette(mid); border-radius: 6px; }")
                                  .arg(tok.bgSurface.name(), tok.textPrimary.name(), tok.bgBase.name()));
        QVBoxLayout* sLay = new QVBoxLayout(&searchDlg);
        sLay->setContentsMargins(12, 12, 12, 12);
        sLay->setSpacing(8);

        QLineEdit* sEdit = new QLineEdit(&searchDlg);
        sEdit->setPlaceholderText("Type name to search church members...");
        sEdit->setClearButtonEnabled(true);
        QListWidget* sList = new QListWidget(&searchDlg);
        sLay->addWidget(sEdit);
        sLay->addWidget(sList, 1);

        auto populateSearch = [&](const QString& filter) {
            sList->clear();
            QSqlQuery qS(db);
            if (filter.trimmed().isEmpty()) {
                qS.exec("SELECT id, first_name || ' ' || last_name, COALESCE(membership_status, 'Member'), care_flag FROM people ORDER BY first_name LIMIT 35");
            } else {
                qS.prepare("SELECT id, first_name || ' ' || last_name, COALESCE(membership_status, 'Member'), care_flag FROM people WHERE first_name LIKE ? OR last_name LIKE ? ORDER BY first_name LIMIT 50");
                qS.addBindValue("%" + filter.trimmed() + "%");
                qS.addBindValue("%" + filter.trimmed() + "%");
                qS.exec();
            }
            while (qS.next()) {
                int pid = qS.value(0).toInt();
                QString pName = qS.value(1).toString();
                QString pStatus = qS.value(2).toString();
                QString pFlag = qS.value(3).toString();
                QString label = pName + " (" + pStatus + (!pFlag.isEmpty() ? " • " + pFlag : "") + ")";
                QListWidgetItem* it = new QListWidgetItem(IconManager::instance().getIcon("user", tok.textPrimary, 14), label, sList);
                it->setData(Qt::UserRole, pid);
                it->setData(Qt::UserRole + 1, pName);
            }
        };
        populateSearch("");
        QObject::connect(sEdit, &QLineEdit::textChanged, &searchDlg, populateSearch);

        QDialogButtonBox* sBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &searchDlg);
        QPushButton* sOk = sBox->button(QDialogButtonBox::Ok);
        if (sOk) sOk->setObjectName("primaryBtn");
        QPushButton* sCancel = sBox->button(QDialogButtonBox::Cancel);
        if (sCancel) sCancel->setObjectName("secondaryBtn");
        sLay->addWidget(sBox);

        QObject::connect(sBox, &QDialogButtonBox::accepted, &searchDlg, &QDialog::accept);
        QObject::connect(sBox, &QDialogButtonBox::rejected, &searchDlg, &QDialog::reject);
        QObject::connect(sList, &QListWidget::itemDoubleClicked, &searchDlg, &QDialog::accept);

        if (searchDlg.exec() == QDialog::Accepted && sList->currentItem()) {
            int selId = sList->currentItem()->data(Qt::UserRole).toInt();
            QString selName = sList->currentItem()->data(Qt::UserRole + 1).toString();
            int existingIdx = contactCombo->findData(selId);
            if (existingIdx >= 0) {
                contactCombo->setCurrentIndex(existingIdx);
            } else {
                contactCombo->addItem(QString("%1 (Assigned Leader)").arg(selName), selId);
                contactCombo->setCurrentIndex(contactCombo->count() - 1);
            }
        }
    });

    QSpinBox* capacitySpin = new QSpinBox(&dlg);
    capacitySpin->setRange(0, 500);
    capacitySpin->setSuffix(" members (0 = Unlimited)");
    capacitySpin->setValue(currentCap);

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
    recruitCombo->addItem("Open — Continuous Enrollment / Recommendations Active", "Open");
    recruitCombo->addItem("Elected — Governance / Annual Vote (Board, Deacons, Elders)", "Elected");
    recruitCombo->addItem("Seasonal — Biannual Cohort (Closed Enrollment)", "Seasonal");
    recruitCombo->addItem("Closed — Invitation Only", "Closed");
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
    form->addRow("Target Primary Archetype:", idealPrimaryCombo);
    form->addRow("Target Secondary Archetype:", idealSecondaryCombo);
    form->addRow("Required Gifts & Skills:", skillsEdit);
    form->addRow("Parent Group:", parentCombo);
    form->addRow("Recruitment Status:", recruitCombo);
    form->addRow("Meeting Day:", dayCombo);
    form->addRow("Meeting Time:", timeEdit);
    form->addRow("Location / Room:", locationEdit);
    form->addRow("Primary Leader/Contact:", contactRow);
    form->addRow("Max Capacity:", capacitySpin);
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
            QMessageBox::warning(this, AppTranslator::t("Validation Error"), AppTranslator::t("Group name cannot be empty."));
            return;
        }

        QString cat = catCombo->currentText();
        QString priArch = idealPrimaryCombo->currentData().toString();
        QString secArch = idealSecondaryCombo->currentData().toString();
        if (secArch == "None" || secArch.contains("None")) secArch = "";
        int parentId = parentCombo->currentData().toInt();
        QString day = dayCombo->currentText();
        QString time = timeEdit->text().trimmed();
        QString loc = locationEdit->text().trimmed();
        int contactId = contactCombo->currentData().toInt();
        int cap = capacitySpin->value();
        QString desc = descEdit->text().trimmed();
        QString notes = notesEdit->toPlainText().trimmed();

        QString skills = skillsEdit->text().trimmed();
        int minAge = minAgeSpin->value();
        int maxAge = maxAgeSpin->value();
        if (minAge > 0 && maxAge > 0 && minAge > maxAge) std::swap(minAge, maxAge);
        QString recruitStatus = recruitCombo->currentData().toString();

        if (groupId > 0 && !isDuplicate) {
            QSqlQuery update(db);
            update.prepare("UPDATE groups SET name = ?, category = ?, parent_id = ?, meeting_day = ?, meeting_time = ?, "
                           "location = ?, contact_person_id = ?, max_capacity = ?, description = ?, notes = ?, "
                           "required_skills = ?, min_age = ?, max_age = ?, recruitment_status = ?, "
                           "ideal_primary_archetype = ?, ideal_secondary_archetype = ? WHERE id = ?");
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
            update.addBindValue(priArch);
            update.addBindValue(secArch);
            update.addBindValue(groupId);

            if (update.exec()) {
                if (contactId > 0) {
                    QSqlQuery checkRel(db);
                    checkRel.prepare("SELECT id, relationship_type FROM entity_relationships WHERE source_type = 'Person' AND source_id = ? AND target_type = 'Group' AND target_id = ?");
                    checkRel.addBindValue(contactId);
                    checkRel.addBindValue(groupId);
                    if (checkRel.exec() && checkRel.next()) {
                        int relId = checkRel.value(0).toInt();
                        QString rType = checkRel.value(1).toString();
                        if (rType != "Leader") {
                            QSqlQuery upRel(db);
                            upRel.prepare("UPDATE entity_relationships SET relationship_type = 'Leader', status = 'Active' WHERE id = ?");
                            upRel.addBindValue(relId);
                            if (upRel.exec()) DatabaseManager::bumpDataGeneration();
                        }
                    } else {
                        QSqlQuery insRel(db);
                        insRel.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status, connection_source, joined_date) "
                                       "VALUES ('Person', ?, 'Group', ?, 'Leader', 'Active', 'manual', date('now','localtime'))");
                        insRel.addBindValue(contactId);
                        insRel.addBindValue(groupId);
                        if (insRel.exec()) DatabaseManager::bumpDataGeneration();
                    }
                }
                refreshGroupsTree();
                refreshGroupDetailPanel(groupId);
                if (m_graphWidget) m_graphWidget->loadGraph();
                QMessageBox::information(this, AppTranslator::t("Success"), AppTranslator::t("Group updated successfully."));
            } else {
                QMessageBox::critical(this, AppTranslator::t("Database Error"), AppTranslator::t("Failed to update group: ") + update.lastError().text());
            }
        } else {
            db.transaction();
            QSqlQuery ins(db);
            ins.prepare("INSERT INTO groups (name, category, parent_id, meeting_day, meeting_time, location, contact_person_id, max_capacity, description, notes, required_skills, min_age, max_age, recruitment_status, ideal_primary_archetype, ideal_secondary_archetype, is_archived) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
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
            ins.addBindValue(priArch);
            ins.addBindValue(secArch);

            if (ins.exec()) {
                int newId = ins.lastInsertId().toInt();
                if (contactId > 0) {
                    QSqlQuery insRel(db);
                    insRel.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, status, connection_source, joined_date) "
                                   "VALUES ('Person', ?, 'Group', ?, 'Leader', 'Active', 'manual', date('now','localtime'))");
                    insRel.addBindValue(contactId);
                    insRel.addBindValue(newId);
                    if (insRel.exec()) DatabaseManager::bumpDataGeneration();
                }
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

                // Automatically switch to Active Groups & Serving Rosters tab
                if (m_groupsTabWidget) {
                    m_groupsTabWidget->setCurrentIndex(0);
                }

                refreshGroupsTree();

                // Auto-expand parent blueprint and select new team
                if (m_groupsTreeWidget) {
                    QTreeWidgetItemIterator it(m_groupsTreeWidget);
                    QTreeWidgetItem* targetItem = nullptr;
                    while (*it) {
                        if ((*it)->data(0, Qt::UserRole).toInt() == parentId) {
                            (*it)->setExpanded(true);
                        }
                        if ((*it)->data(0, Qt::UserRole).toInt() == newId) {
                            targetItem = *it;
                        }
                        ++it;
                    }
                    if (targetItem) {
                        m_groupsTreeWidget->setCurrentItem(targetItem);
                        m_groupsTreeWidget->scrollToItem(targetItem);
                    }
                }

                m_selectedGroupId = newId;
                refreshGroupDetailPanel(newId);
                if (m_graphWidget) m_graphWidget->loadGraph();
                QMessageBox::information(this, AppTranslator::t("Success"), AppTranslator::t("Group created successfully."));
            } else {
                db.rollback();
                QMessageBox::critical(this, AppTranslator::t("Database Error"), AppTranslator::t("Failed to create group: ") + ins.lastError().text());
            }
        }
    }
}

// --- Groups-tree context-menu actions (right-click on a group) ---

void MainWindow::onEditGroup() {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot edit groups. Sign in with the Admin PIN (1234) to modify ministry structure."));
        return;
    }
    if (m_selectedGroupId > 0) {
        showGroupDialog(m_selectedGroupId, false);
    } else {
        QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
    }
}

void MainWindow::onAddGroup(int defaultParentId) {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot create groups. Sign in with the Admin PIN (1234) to modify ministry structure."));
        return;
    }
    showGroupDialog(0, false, defaultParentId);
}

void MainWindow::onArchiveGroup() {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, AppTranslator::t("Read-Only Mode"), AppTranslator::t("Guests cannot archive groups. Sign in with the Admin PIN (1234) to modify ministry structure."));
        return;
    }
    if (m_selectedGroupId <= 0) {
        QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
        return;
    }
    // Root guard: top-level ministry blueprints are system-locked.
    QSqlQuery rootQ;
    rootQ.prepare("SELECT parent_id FROM groups WHERE id = ?");
    rootQ.addBindValue(m_selectedGroupId);
    const bool isRoot = (!rootQ.exec() || !rootQ.next()) ? false : rootQ.value(0).isNull();
    if (isRoot) {
        QMessageBox::information(this, AppTranslator::t("Protected Ministry"), AppTranslator::t("Top-level Ministry Blueprints are system-locked and cannot be archived."));
        return;
    }

    QSqlQuery qState;
    qState.prepare("SELECT is_archived FROM groups WHERE id = ?");
    qState.addBindValue(m_selectedGroupId);
    const int current = (qState.exec() && qState.next()) ? qState.value(0).toInt() : 0;
    const int target = (current == 1) ? 0 : 1;
    const QString action = (target == 1) ? AppTranslator::t("archive") : AppTranslator::t("unarchive");

    if (QMessageBox::question(this, AppTranslator::t("Confirm Action"),
                              QString(AppTranslator::t("Are you sure you want to %1 this group and all of its sub-groups?")).arg(action),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // Cascade to the whole subtree so archiving a ministry also archives its
    // teams (mirrors Hide/Unhide) and its live sub-teams keep a visible parent.
    QSqlQuery q;
    q.prepare("WITH RECURSIVE subtree(id) AS ("
              "  SELECT id FROM groups WHERE id = ? "
              "  UNION ALL "
              "  SELECT g.id FROM groups g JOIN subtree s ON g.parent_id = s.id"
              ") UPDATE groups SET is_archived = ? WHERE id IN (SELECT id FROM subtree)");
    q.addBindValue(m_selectedGroupId);
    q.addBindValue(target);
    if (q.exec()) {
        DatabaseManager::bumpDataGeneration();
        refreshGroupsTree();
        refreshGroupDetailPanel(m_selectedGroupId);
        if (m_graphWidget) m_graphWidget->loadGraph();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to toggle archive status: " + q.lastError().text());
    }
}

void MainWindow::onDeleteGroup() {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, "Read-Only Mode", "Guests cannot delete groups. Sign in with the Admin PIN (1234) to modify ministry structure.");
        return;
    }
    if (m_selectedGroupId <= 0) {
        QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
        return;
    }
    // Root guard: top-level ministry blueprints are system-locked.
    QSqlQuery rootQ;
    rootQ.prepare("SELECT parent_id FROM groups WHERE id = ?");
    rootQ.addBindValue(m_selectedGroupId);
    const bool isRoot = (!rootQ.exec() || !rootQ.next()) ? false : rootQ.value(0).isNull();
    if (isRoot) {
        QMessageBox::information(this, "Protected Ministry", "Top-level Ministry Blueprints are system-locked and cannot be deleted.");
        return;
    }

    QSqlQuery nameQ;
    nameQ.prepare("SELECT name FROM groups WHERE id = ?");
    nameQ.addBindValue(m_selectedGroupId);
    QString name = (nameQ.exec() && nameQ.next()) ? nameQ.value(0).toString() : QString("Group #%1").arg(m_selectedGroupId);

    if (QMessageBox::question(this, "Delete Group",
                              QString("Delete '%1'? Members are kept; sub-groups are moved up to its parent.")
                                  .arg(name),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    db.transaction();
    QSqlQuery q(db);
    // Preserve hierarchy: re-parent sub-groups to this group's parent (null if top-level),
    // then remove member links and the group itself.
    QSqlQuery qParent(db);
    qParent.prepare("SELECT parent_id FROM groups WHERE id = ?");
    qParent.addBindValue(m_selectedGroupId);
    int grandparent = 0;
    if (qParent.exec() && qParent.next()) grandparent = qParent.value(0).toInt();

    q.prepare("UPDATE groups SET parent_id = ? WHERE parent_id = ?");
    q.addBindValue(grandparent > 0 ? QVariant(grandparent) : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(m_selectedGroupId);
    q.exec();

    q.prepare("DELETE FROM entity_relationships WHERE target_type = 'Group' AND target_id = ?");
    q.addBindValue(m_selectedGroupId);
    q.exec();

    q.prepare("DELETE FROM groups WHERE id = ?");
    q.addBindValue(m_selectedGroupId);
    const bool ok = q.exec();
    if (ok) {
        db.commit();
        m_selectedGroupId = 0;
        refreshGroupsTree();
        refreshGroupDetailPanel(0);
        if (m_graphWidget) m_graphWidget->loadGraph();
    } else {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to delete group: " + q.lastError().text());
    }
}

void MainWindow::onToggleHiddenGroup() {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, "Read-Only Mode", "Guests cannot hide groups. Sign in with the Admin PIN (1234) to modify ministry structure.");
        return;
    }
    if (m_selectedGroupId <= 0) {
        QMessageBox::warning(this, AppTranslator::t("No Selection"), AppTranslator::t("Please select a group first."));
        return;
    }
    QSqlQuery qState;
    qState.prepare("SELECT is_hidden FROM groups WHERE id = ?");
    qState.addBindValue(m_selectedGroupId);
    const int current = (qState.exec() && qState.next()) ? qState.value(0).toInt() : 0;
    const int target = (current == 1) ? 0 : 1;
    const QString action = (target == 1) ? "hide" : "unhide";

    if (QMessageBox::question(this, "Confirm Action",
                              QString("Are you sure you want to %1 this group and all of its sub-groups? Hidden groups leave the tree, graph and Smart Bridge but keep their data.")
                                  .arg(action),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // Cascade to the whole subtree so hiding a ministry hides its teams, and
    // unhiding restores them together.
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery q(db);
    q.prepare("WITH RECURSIVE subtree(id) AS ("
              "  SELECT id FROM groups WHERE id = ? "
              "  UNION ALL "
              "  SELECT g.id FROM groups g JOIN subtree s ON g.parent_id = s.id"
              ") UPDATE groups SET is_hidden = ? WHERE id IN (SELECT id FROM subtree)");
    q.addBindValue(m_selectedGroupId);
    q.addBindValue(target);
    if (q.exec()) {
        refreshGroupsTree();
        refreshGroupDetailPanel(m_selectedGroupId);
        if (m_graphWidget) m_graphWidget->loadGraph();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to update hidden state: " + q.lastError().text());
    }
}

void MainWindow::setupMinistryBlueprintsTab(QWidget* container) {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QVBoxLayout* rootLay = new QVBoxLayout(container);
    rootLay->setContentsMargins(12, 12, 12, 12);
    rootLay->setSpacing(12);

    QHBoxLayout* headerRow = new QHBoxLayout();
    QVBoxLayout* titlesLay = new QVBoxLayout();
    QLabel* lblHeader = new QLabel("Ministry Blueprints & Archetype Catalog", container);
    lblHeader->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(tok.accentPrimary.name()));
    titlesLay->addWidget(lblHeader);

    QLabel* lblSub = new QLabel("Behavioral DNA profiles and target relational balance for church ministries. Top-level group headings and dynamic target ratios are automatically synchronized.", container);
    lblSub->setStyleSheet(QString("font-size: 11px; color: %1;").arg(tok.textSecondary.name()));
    lblSub->setWordWrap(true);
    titlesLay->addWidget(lblSub);
    headerRow->addLayout(titlesLay, 1);

    m_chkShowArchivedBlueprints = new QCheckBox("Show Archived", container);
    m_chkShowArchivedBlueprints->setChecked(false);
    connect(m_chkShowArchivedBlueprints, &QCheckBox::toggled, this, &MainWindow::refreshMinistryBlueprintsTab);
    headerRow->addWidget(m_chkShowArchivedBlueprints);

    m_btnDefineBlueprint = new QPushButton(" Define New Blueprint", container);
    m_btnDefineBlueprint->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnDefineBlueprint->setObjectName("primaryBtn");
    m_btnDefineBlueprint->setStyleSheet("padding: 6px 12px; font-size: 12px; font-weight: bold;");
    connect(m_btnDefineBlueprint, &QPushButton::clicked, this, [this]() {
        if (isReadOnlyRole()) return;
        showBlueprintDialog(0);
    });
    headerRow->addWidget(m_btnDefineBlueprint);
    rootLay->addLayout(headerRow);

    QScrollArea* scroll = new QScrollArea(container);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_blueprintsGridContainer = new QWidget(scroll);
    // Without this, the grid's content minimum (two ~900px blueprint cards)
    // propagates through the scroll area into the QTabWidget — which sizes
    // for ALL tabs, including this hidden one — forcing the whole window
    // wider whenever the Groups & Ministries page activates. Ignored lets
    // the scroll area do its job instead.
    m_blueprintsGridContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    scroll->setWidget(m_blueprintsGridContainer);
    rootLay->addWidget(scroll, 1);

    refreshMinistryBlueprintsTab();
}

void MainWindow::refreshMinistryBlueprintsTab() {
    if (!m_blueprintsGridContainer) return;

    ThemeTokens tok = ThemeManager::instance().currentTokens();

    // Clear previous contents by reusing the container's grid layout. Creating a
    // second QGridLayout on the same widget while the old one is still installed
    // (deleteLater only runs later) makes Qt warn "already has a layout", and the
    // card widgets' churn is the pattern that corrupted the Smart Bridge panel,
    // so widgets are still deleted via deleteLater.
    QGridLayout* grid = qobject_cast<QGridLayout*>(m_blueprintsGridContainer->layout());
    if (grid) {
        QLayoutItem* item;
        while ((item = grid->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    } else {
        delete m_blueprintsGridContainer->layout(); // stale non-grid layout, if any
        grid = new QGridLayout(m_blueprintsGridContainer);
    }
    grid->setContentsMargins(5, 5, 5, 5);
    grid->setSpacing(12);

    bool showArchived = m_chkShowArchivedBlueprints && m_chkShowArchivedBlueprints->isChecked();
    const auto blueprints = MinistryArchetypeRegistry::allBlueprints(showArchived);
    const bool readOnly = isReadOnlyRole();
    const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";
    int row = 0, col = 0;

    for (const auto& bp : blueprints) {
        QFrame* card = new QFrame(m_blueprintsGridContainer);
        card->setObjectName("cardWidget");
        card->setStyleSheet(QString("QFrame#cardWidget { background-color: %1; border: 1px solid %2; border-radius: 10px; padding: 10px; }")
                                .arg(tok.bgSurface.name(), tok.borderColor.name()));
        QVBoxLayout* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(10, 10, 10, 10);
        cardLay->setSpacing(8);

        // Title Row
        QHBoxLayout* topRow = new QHBoxLayout();
        QLabel* lblTitle = new QLabel(QString("<b>%1</b>").arg(bp.displayName), card);
        lblTitle->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tok.accentPrimary.name()));
        topRow->addWidget(lblTitle);

        if (bp.isCustom) {
            QLabel* lblCustomBadge = new QLabel("Custom", card);
            lblCustomBadge->setStyleSheet(QString("background-color: rgba(%1, %2, %3, 0.15); border: 1px solid %4; border-radius: 4px; padding: 1px 5px; font-size: 9px; font-weight: bold; color: %4;")
                                              .arg(tok.accentPrimary.red()).arg(tok.accentPrimary.green()).arg(tok.accentPrimary.blue()).arg(tok.accentPrimary.name()));
            topRow->addWidget(lblCustomBadge);
        }
        if (bp.isArchived) {
            QLabel* lblArchBadge = new QLabel("Archived", card);
            lblArchBadge->setStyleSheet(QString("background-color: rgba(%1, %2, %3, 0.15); border: 1px solid %4; border-radius: 4px; padding: 1px 5px; font-size: 9px; font-weight: bold; color: %4;")
                                            .arg(tok.accentDanger.red()).arg(tok.accentDanger.green()).arg(tok.accentDanger.blue()).arg(tok.accentDanger.name()));
            topRow->addWidget(lblArchBadge);
        }

        topRow->addStretch();
        cardLay->addLayout(topRow);

        // Description
        QLabel* lblDesc = new QLabel(bp.description, card);
        lblDesc->setStyleSheet(QString("font-size: 11px; color: %1;").arg(tok.textSecondary.name()));
        lblDesc->setWordWrap(true);
        cardLay->addWidget(lblDesc);

        // Archetypes Badges
        QString priIcon = bp.primaryArchetype == "Catalyst" ? "🔥" :
                          bp.primaryArchetype == "Shepherd" ? "🌿" :
                          bp.primaryArchetype == "Architect" ? "📐" :
                          bp.primaryArchetype == "Spark" ? "⚡" : "⚓";
        QString secIcon = bp.secondaryArchetype == "Catalyst" ? "🔥" :
                          bp.secondaryArchetype == "Shepherd" ? "🌿" :
                          bp.secondaryArchetype == "Architect" ? "📐" :
                          bp.secondaryArchetype == "Spark" ? "⚡" : "⚓";

        QHBoxLayout* archRow = new QHBoxLayout();
        archRow->setSpacing(6);
        QLabel* lblPriBadge = new QLabel(QString("Primary: %1 %2").arg(priIcon, bp.primaryArchetype), card);
        lblPriBadge->setStyleSheet(QString("background-color: rgba(%1, %2, %3, 0.15); border: 1px solid %4; border-radius: 6px; padding: 2px 6px; font-size: 10px; font-weight: bold; color: %4;")
            .arg(tok.accentPrimary.red()).arg(tok.accentPrimary.green()).arg(tok.accentPrimary.blue()).arg(tok.accentPrimary.name()));
        archRow->addWidget(lblPriBadge);

        if (!bp.secondaryArchetype.isEmpty() && bp.secondaryArchetype != "None") {
            QLabel* lblSecBadge = new QLabel(QString("Secondary: %1 %2").arg(secIcon, bp.secondaryArchetype), card);
            lblSecBadge->setStyleSheet(QString("background-color: rgba(%1, %2, %3, 0.15); border: 1px solid %4; border-radius: 6px; padding: 2px 6px; font-size: 10px; font-weight: bold; color: %4;")
                .arg(tok.accentSuccess.red()).arg(tok.accentSuccess.green()).arg(tok.accentSuccess.blue()).arg(tok.accentSuccess.name()));
            archRow->addWidget(lblSecBadge);
        }
        archRow->addStretch();
        cardLay->addLayout(archRow);

        // Required Gifts
        QLabel* lblGifts = new QLabel(QString("<b>Spiritual Gifts:</b> %1").arg(bp.requiredGifts.join(", ")), card);
        lblGifts->setStyleSheet(QString("font-size: 10px; color: %1;").arg(tok.textPrimary.name()));
        lblGifts->setWordWrap(true);
        cardLay->addWidget(lblGifts);

        // Behavioral Vector Breakdown Bar
        QLabel* lblVector = new QLabel(QString("<span style='font-size: 10px; color: %1;'>"
                                               "<b>Target Mix:</b> "
                                               "Org %2% | Lis %3% | Doe %4% | Ene %5% | Str %6% | Con %7%</span>")
                                           .arg(tok.textSecondary.name())
                                           .arg(static_cast<int>(bp.targetVector.organizer * 100))
                                           .arg(static_cast<int>(bp.targetVector.listener * 100))
                                           .arg(static_cast<int>(bp.targetVector.doer * 100))
                                           .arg(static_cast<int>(bp.targetVector.energizer * 100))
                                           .arg(static_cast<int>(bp.targetVector.strategist * 100))
                                           .arg(static_cast<int>(bp.targetVector.connector * 100)), card);
        cardLay->addWidget(lblVector);

        // Actions Row
        QHBoxLayout* actionsRow = new QHBoxLayout();
        actionsRow->setSpacing(8);

        QPushButton* btnCreate = new QPushButton(" Launch Team", card);
        btnCreate->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
        btnCreate->setObjectName("primaryBtn");
        btnCreate->setStyleSheet("font-size: 11px; padding: 4px 10px;");
        btnCreate->setEnabled(!bp.isArchived && !readOnly);
        if (readOnly) btnCreate->setToolTip(roTooltip);
        connect(btnCreate, &QPushButton::clicked, this, [this, bp]() {
            if (isReadOnlyRole()) return;
            int rootId = 0;
            QSqlQuery qRoot;
            qRoot.prepare("SELECT id FROM groups WHERE (name = ? OR category = ? OR name LIKE ? OR category LIKE ?) AND (parent_id IS NULL OR parent_id = 0) ORDER BY id ASC LIMIT 1");
            qRoot.addBindValue(bp.displayName);
            qRoot.addBindValue(bp.categoryKey);
            qRoot.addBindValue("%" + bp.categoryKey + "%");
            qRoot.addBindValue("%" + bp.categoryKey + "%");
            if (qRoot.exec() && qRoot.next()) {
                rootId = qRoot.value(0).toInt();
            } else if (bp.id > 0) {
                // Non-primary canonical blueprints ship without a heading
                // group; materialize it so the sub-team lands under a real
                // root instead of becoming an orphan top-level group.
                rootId = DatabaseManager::ensureBlueprintHeadingGroup(bp.id);
            }
            showGroupDialog(0, false, rootId);
        });
        actionsRow->addWidget(btnCreate);

        if (bp.isCustom) {
            QPushButton* btnEdit = new QPushButton(" Edit", card);
            btnEdit->setIcon(IconManager::instance().getIcon("edit-3", tok.textPrimary, 12));
            btnEdit->setObjectName("secondaryBtn");
            btnEdit->setStyleSheet("font-size: 11px; padding: 4px 10px;");
            btnEdit->setEnabled(!readOnly);
            if (readOnly) btnEdit->setToolTip(roTooltip);
            connect(btnEdit, &QPushButton::clicked, this, [this, bp]() {
                if (isReadOnlyRole()) return;
                showBlueprintDialog(bp.id);
            });
            actionsRow->addWidget(btnEdit);

            if (!bp.isArchived) {
                QPushButton* btnArchive = new QPushButton(" Archive", card);
                btnArchive->setIcon(IconManager::instance().getDangerIcon("trash-2", 12));
                btnArchive->setObjectName("secondaryBtn");
                btnArchive->setStyleSheet("font-size: 11px; padding: 4px 10px;");
                btnArchive->setEnabled(!readOnly);
                if (readOnly) btnArchive->setToolTip(roTooltip);
                connect(btnArchive, &QPushButton::clicked, this, [this, bp]() {
                    if (isReadOnlyRole()) return;
                    int ret = QMessageBox::question(this, "Archive Blueprint",
                        QString("Are you sure you want to archive the blueprint '%1'?\n\n"
                                "This will also archive its top-level ministry group heading and ALL child sub-teams, "
                                "removing them from active ministry match recommendations.")
                                .arg(bp.displayName),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                    if (ret == QMessageBox::Yes) {
                        DatabaseManager::archiveBlueprint(bp.id, true);
                        refreshMinistryBlueprintsTab();
                        refreshGroupsTree();
                    }
                });
                actionsRow->addWidget(btnArchive);
            } else {
                QPushButton* btnRestore = new QPushButton(" Restore", card);
                btnRestore->setIcon(IconManager::instance().getIcon("refresh-cw", tok.accentSuccess, 12));
                btnRestore->setObjectName("secondaryBtn");
                btnRestore->setStyleSheet("font-size: 11px; padding: 4px 10px;");
                btnRestore->setEnabled(!readOnly);
                if (readOnly) btnRestore->setToolTip(roTooltip);
                connect(btnRestore, &QPushButton::clicked, this, [this, bp]() {
                    if (isReadOnlyRole()) return;
                    DatabaseManager::archiveBlueprint(bp.id, false);
                    refreshMinistryBlueprintsTab();
                    refreshGroupsTree();
                });
                actionsRow->addWidget(btnRestore);
            }
        }

        actionsRow->addStretch();
        cardLay->addLayout(actionsRow);

        grid->addWidget(card, row, col);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }
}

void MainWindow::showBlueprintDialog(int blueprintId) {
    if (isReadOnlyRole()) {
        QMessageBox::warning(this, "Read-Only Mode", "Guests cannot create or edit ministry blueprints. Sign in with the Admin PIN (1234) to modify.");
        return;
    }
    QSqlDatabase db = QSqlDatabase::database();
    ThemeTokens tok = ThemeManager::instance().currentTokens();

    QString currentCat, currentName, currentPri = "Catalyst", currentSec = "Spark", currentGifts, currentDesc;

    if (blueprintId > 0) {
        QSqlQuery q(db);
        q.prepare("SELECT category_key, display_name, ideal_primary, ideal_secondary, required_gifts, description FROM ministry_archetypes WHERE id = ?");
        q.addBindValue(blueprintId);
        if (q.exec() && q.next()) {
            currentCat = q.value(0).toString();
            currentName = q.value(1).toString();
            currentPri = q.value(2).toString();
            currentSec = q.value(3).toString();
            currentGifts = q.value(4).toString();
            currentDesc = q.value(5).toString();
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(blueprintId > 0 ? "Edit Blueprint Ministry" : "Define New Blueprint Ministry");
    dlg.resize(460, 480);
    dlg.setStyleSheet(QString("QDialog { background-color: %1; color: %2; } QLabel { color: %2; } QLineEdit, QTextEdit, QComboBox { background-color: %3; color: %2; border: 1px solid palette(mid); border-radius: 6px; padding: 5px; }")
                          .arg(tok.bgSurface.name(), tok.textPrimary.name(), tok.bgBase.name()));

    QVBoxLayout* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(10);

    QLineEdit* nameEdit = new QLineEdit(currentName, &dlg);
    nameEdit->setPlaceholderText("e.g. Matrimonios / Marriage Ministry");

    QLineEdit* catEdit = new QLineEdit(currentCat, &dlg);
    catEdit->setPlaceholderText("e.g. Matrimonios");

    if (blueprintId == 0) {
        connect(nameEdit, &QLineEdit::textChanged, &dlg, [catEdit](const QString& text) {
            if (catEdit->text().trimmed().isEmpty() || catEdit->property("auto").toBool()) {
                QString derived = text.split("/").first().trimmed();
                catEdit->setProperty("auto", true);
                catEdit->setText(derived);
            }
        });
        connect(catEdit, &QLineEdit::textEdited, &dlg, [catEdit]() {
            catEdit->setProperty("auto", false);
        });
    }

    QComboBox* priCombo = new QComboBox(&dlg);
    priCombo->addItem("🔥 Catalyst (Vision & Initiation)", "Catalyst");
    priCombo->addItem("🌿 Shepherd (Care & Discipleship)", "Shepherd");
    priCombo->addItem("📐 Architect (Systems & Logistics)", "Architect");
    priCombo->addItem("⚡ Spark (Energy & Recruiting)", "Spark");
    priCombo->addItem("⚓ Anchor (Steadfast Execution)", "Anchor");
    int pIdx = priCombo->findData(currentPri);
    if (pIdx >= 0) priCombo->setCurrentIndex(pIdx);

    QComboBox* secCombo = new QComboBox(&dlg);
    secCombo->addItem("-- None / Pure Archetype --", "None");
    secCombo->addItem("🔥 Catalyst (Vision & Initiation)", "Catalyst");
    secCombo->addItem("🌿 Shepherd (Care & Discipleship)", "Shepherd");
    secCombo->addItem("📐 Architect (Systems & Logistics)", "Architect");
    secCombo->addItem("⚡ Spark (Energy & Recruiting)", "Spark");
    secCombo->addItem("⚓ Anchor (Steadfast Execution)", "Anchor");
    int sIdx = secCombo->findData(currentSec);
    if (sIdx >= 0) secCombo->setCurrentIndex(sIdx);

    QLineEdit* giftsEdit = new QLineEdit(currentGifts, &dlg);
    giftsEdit->setPlaceholderText("e.g. Pastoral Care, Hospitality, Teaching");

    QTextEdit* descEdit = new QTextEdit(&dlg);
    descEdit->setPlaceholderText("Describe the ministry's mission, culture, and core responsibilities...");
    descEdit->setPlainText(currentDesc);
    descEdit->setMaximumHeight(90);

    form->addRow("Ministry Name:", nameEdit);
    form->addRow("Category Key:", catEdit);
    form->addRow("Primary Archetype:", priCombo);
    form->addRow("Secondary Archetype:", secCombo);
    form->addRow("Key Spiritual Gifts:", giftsEdit);
    form->addRow("Mission Description:", descEdit);
    root->addLayout(form);

    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QPushButton* okBtn = box->button(QDialogButtonBox::Ok);
    if (okBtn) {
        okBtn->setText(blueprintId > 0 ? "Save Blueprint" : "Create Blueprint");
        okBtn->setObjectName("primaryBtn");
    }
    QPushButton* cancelBtn = box->button(QDialogButtonBox::Cancel);
    if (cancelBtn) cancelBtn->setObjectName("secondaryBtn");
    root->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, &dlg, [&]() {
        QString name = nameEdit->text().trimmed();
        QString cat = catEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dlg, "Validation Error", "Please provide a Ministry Name.");
            nameEdit->setFocus();
            return;
        }
        if (cat.isEmpty()) {
            QMessageBox::warning(&dlg, "Validation Error", "Please provide a Category Key.");
            catEdit->setFocus();
            return;
        }
        QString pri = priCombo->currentData().toString();
        QString sec = secCombo->currentData().toString();
        QStringList gifts = giftsEdit->text().split(",", Qt::SkipEmptyParts);
        for (QString& g : gifts) g = g.trimmed();
        QString desc = descEdit->toPlainText().trimmed();

        bool ok = false;
        if (blueprintId == 0) {
            int newBpId = 0, newGrpId = 0;
            ok = DatabaseManager::createBlueprint(cat, name, pri, sec, gifts, desc, &newBpId, &newGrpId, db);
            if (ok) {
                QMessageBox::information(this, "Success", QString("Blueprint '%1' created!\n\nIts top-level heading group has been added to the Groups tree.").arg(name));
            } else {
                QMessageBox::critical(&dlg, "Error", "Failed to create blueprint. Category Key may already exist.");
                return;
            }
        } else {
            ok = DatabaseManager::updateBlueprint(blueprintId, cat, name, pri, sec, gifts, desc, db);
            if (ok) {
                QMessageBox::information(this, "Success", QString("Blueprint '%1' updated and its top-level group heading synchronized.").arg(name));
            } else {
                QMessageBox::critical(&dlg, "Error", "Failed to update blueprint.");
                return;
            }
        }

        dlg.accept();
        refreshMinistryBlueprintsTab();
        refreshGroupsTree();
    });
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
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

void MainWindow::refreshSidebarIcons() {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QColor navCol = tok.isDark ? QColor("#cdd6f4") : QColor("#4c4f69");

    auto setNav = [&](QPushButton* btn, const QString& name, const QString& text) {
        if (!btn) return;
        btn->setIcon(IconManager::instance().getIcon(name, navCol, 18));
        btn->setIconSize(QSize(18, 18));
        btn->setText("  " + text);
    };

    setNav(m_btnDashboard, "bar-chart-3", AppTranslator::t("Dashboard"));
    setNav(m_btnMembers, "users", AppTranslator::t("People"));
    setNav(m_btnGraph, "network", AppTranslator::t("Connection Graph"));
    setNav(m_btnAnalytics, "bar-chart-3", AppTranslator::t("Pastoral Analytics"));
    setNav(m_btnShepherd, "heart-handshake", AppTranslator::t("Shepherd's Workbench"));
    setNav(m_btnGroups, "layers", AppTranslator::t("Groups & Ministries"));
    setNav(m_btnService, "list-music", AppTranslator::t("Service Builder"));
    setNav(m_btnFacilities, "landmark", AppTranslator::t("Facilities Matrix"));
    setNav(m_btnPlanning, "calendar", AppTranslator::t("Ministry Planning"));
    setNav(m_btnContributions, "hand-coins", AppTranslator::t("Giving & Batches"));
    setNav(m_btnInventory, "package", AppTranslator::t("Asset Inventory"));
    setNav(m_btnVault, "shield-check", AppTranslator::t("Document Vault"));
    setNav(m_btnSettings, "settings", AppTranslator::t("Settings"));
}

void MainWindow::onShowUserManual(const QString& section) {
    UserManualDialog::showManual(this, section);
}
