#include "commandpalette.h"
#include "mainwindow.h"
#include "iconmanager.h"
#include "../database/databasemanager.h"
#include "appsettings.h"
#include <QVBoxLayout>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QGuiApplication>

namespace {
// Move the palette selection to the next visible row in `direction` (+1/-1) so
// arrow keys never land on a row that the active filter has hidden.
void moveSelectionToVisibleRow(QListWidget* list, int direction) {
    if (!list || list->count() == 0) return;
    const int step = direction >= 0 ? 1 : -1;
    for (int row = list->currentRow() + step; row >= 0 && row < list->count(); row += step) {
        QListWidgetItem* item = list->item(row);
        if (item && !item->isHidden()) {
            list->setCurrentRow(row);
            return;
        }
    }
}
} // namespace

CommandPaletteDialog::CommandPaletteDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    setupUI();
    populateItems();

    // Install event filter on search edit for arrow keys navigation
    m_searchEdit->installEventFilter(this);
}

CommandPaletteDialog::~CommandPaletteDialog() {}

// Design policy (see DEVELOPMENT_GUIDELINES.md): the app is animation-free.
// The former fade-in here left a QGraphicsOpacityEffect installed on this
// WA_TranslucentBackground top-level, which intermittently rendered the whole
// dialog blank mid-use (bug b9eb0cf). Instant display is also the right UX
// for a keyboard-summoned palette: speed is the feature.

void CommandPaletteDialog::setupUI() {
    resize(550, 380);

    // Frame wrapper for the rounded floating-panel look. No drop-shadow
    // effect: effects on a translucent top-level window force repaints
    // through an offscreen path and have caused blank-dialog bugs here.
    QWidget* frame = new QWidget(this);
    frame->setObjectName("paletteFrame");
    frame->setStyleSheet(
        "QWidget#paletteFrame { "
        "  background-color: palette(base); "
        "  border: 1px solid palette(mid); "
        "  border-radius: 12px; "
        "}"
    );

    QVBoxLayout* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(10, 10, 10, 10);
    frameLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Type a command or search entities (Ctrl+K)...");
    m_searchEdit->setStyleSheet(
        "QLineEdit { "
        "  background-color: palette(base); "
        "  border: 1px solid palette(mid); "
        "  border-radius: 8px; "
        "  color: palette(text); "
        "  font-size: 14px; "
        "  padding: 10px 15px; "
        "}"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CommandPaletteDialog::onSearchChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &CommandPaletteDialog::executeSelection);
    frameLayout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setStyleSheet(
        "QListWidget { "
        "  background-color: transparent; "
        "  border: none; "
        "  color: palette(text); "
        "  font-size: 13px; "
        "}"
        "QListWidget::item { "
        "  padding: 8px 12px; "
        "  border-radius: 6px; "
        "}"
        "QListWidget::item:hover { "
        "  background-color: palette(mid); "
        "  color: palette(text); "
        "}"
        "QListWidget::item:selected { "
        "  background-color: palette(highlight); "
        "  color: palette(highlightedText); "
        "  font-weight: bold; "
        "}"
    );
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &CommandPaletteDialog::onItemSelected);
    frameLayout->addWidget(m_listWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(frame);
}

bool CommandPaletteDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_searchEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Down) {
            moveSelectionToVisibleRow(m_listWidget, +1);
            return true;
        } else if (keyEvent->key() == Qt::Key_Up) {
            moveSelectionToVisibleRow(m_listWidget, -1);
            return true;
        } else if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void CommandPaletteDialog::addPaletteItem(const QString& text, const QString& type, int id, const QString& iconName) {
    QListWidgetItem* item = new QListWidgetItem(text, m_listWidget);
    item->setData(Qt::UserRole, type);
    item->setData(Qt::UserRole + 1, id);
    if (!iconName.isEmpty()) {
        item->setIcon(IconManager::instance().getIcon(iconName, QColor(), 16));
    }
}

void CommandPaletteDialog::populateItems() {
    m_listWidget->clear();

    // Module-aware filtering (issue NOTES.md §6): read the same enabled-module
    // setting the sidebar does (Modules → Settings). Disabled modules do not
    // contribute navigation commands, so the palette matches the visible UI.
    AppSettings modSet;
    const bool modPeople     = modSet.value("modules/people", true).toBool();
    const bool modGroups     = modSet.value("modules/groups", true).toBool();
    const bool modGraph      = modSet.value("modules/graph", true).toBool();
    const bool modShepherd   = modSet.value("modules/shepherd", true).toBool();
    const bool modService    = modSet.value("modules/service", true).toBool();
    const bool modPlanning   = modSet.value("modules/planning", true).toBool();
    const bool modGiving     = modSet.value("modules/giving",
                                            modSet.value("modules/finance", true).toBool()).toBool();
    const bool modFacilities = modSet.value("modules/facilities", true).toBool();
    const bool modInventory  = modSet.value("modules/inventory", true).toBool();
    const bool modVault      = modSet.value("modules/vault", true).toBool();
    const bool modKiosk      = modSet.value("modules/kiosk", true).toBool();

    // 1. Navigation Actions
    addPaletteItem("Go to Dashboard", "Command", 1, "bar-chart-3");
    if (modPeople)     addPaletteItem("Go to People Directory (CRM)", "Command", 2, "users");
    if (modGroups)     addPaletteItem("Go to Small Groups / Ministries List", "Command", 3, "layers");
    if (modGiving)     addPaletteItem("Go to Giving & Sunday Batches", "Command", 4, "hand-coins");
    if (modService)    addPaletteItem("Go to Sunday Service Run-Sheet Builder", "Command", 5, "list-music");
    if (modGraph)      addPaletteItem("Go to Connections Relationship Graph", "Command", 6, "network");
    if (modInventory)  addPaletteItem("Go to Asset Inventory & Checkouts", "Command", 8, "package");
    if (modVault)      addPaletteItem("Go to Elder Board Document Vault", "Command", 9, "shield-check");
    if (modKiosk)      addPaletteItem("Launch Child Nursery Check-In Screen", "Command", 11, "user-check");
    if (modShepherd)   addPaletteItem("Go to Shepherd's Care Workbench", "Command", 12, "heart-handshake");
    if (modPlanning)   addPaletteItem("Go to Ministry Planning & Roadmaps", "Command", 13, "calendar");
    if (modFacilities) addPaletteItem("Go to Facilities & Room Reservations", "Command", 14, "landmark");
    addPaletteItem("Go to Settings & Preferences", "Command", 15, "settings");
    addPaletteItem("Open User Manual & Help Center (F1)", "Command", 16, "book-open");
    addPaletteItem("Toggle Light / Dark Theme Mode", "Command", 10, "sparkles");

    // 2. (control commands already registered above)

    // 3. Database entities (People, Groups, Assets)
    QSqlDatabase db = DatabaseManager::instance().database();
    
    QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name FROM people", db);
    while (qPeople.next()) {
        int id = qPeople.value(0).toInt();
        QString name = qPeople.value(1).toString();
        addPaletteItem(QString("CRM Profile: %1").arg(name), "Person", id, "user");
    }

    QSqlQuery qGroups("SELECT id, name FROM groups", db);
    while (qGroups.next()) {
        int id = qGroups.value(0).toInt();
        QString name = qGroups.value(1).toString();
        addPaletteItem(QString("Ministry Group: %1").arg(name), "Group", id, "layers");
    }

    QSqlQuery qAssets("SELECT id, name, barcode FROM assets", db);
    while (qAssets.next()) {
        int id = qAssets.value(0).toInt();
        QString name = qAssets.value(1).toString();
        QString barcode = qAssets.value(2).toString();
        addPaletteItem(QString("Equipment Asset: %1 (%2)").arg(name, barcode), "Asset", id, "package");
    }

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

void CommandPaletteDialog::onSearchChanged(const QString& text) {
    QString q = text.trimmed().toLower();
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        bool matches = q.isEmpty() || item->text().toLower().contains(q);
        item->setHidden(!matches);
    }

    // Select first visible
    bool foundVisible = false;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        if (!item->isHidden()) {
            m_listWidget->setCurrentItem(item);
            foundVisible = true;
            break;
        }
    }
    if (!foundVisible) {
        // No matches: drop the current row so Enter cannot execute a stale,
        // now-invisible item.
        m_listWidget->setCurrentRow(-1);
        m_listWidget->clearSelection();
    }
}

void CommandPaletteDialog::onItemSelected(QListWidgetItem* item) {
    if (!item) return;

    QString type = item->data(Qt::UserRole).toString();
    int id = item->data(Qt::UserRole + 1).toInt();

    MainWindow* mainWin = qobject_cast<MainWindow*>(parentWidget());
    if (mainWin) {
        if (type == "Command") {
            switch (id) {
                case 1: mainWin->onDashboardClicked(); break;
                case 2: mainWin->onMembersClicked(); break;
                case 3: mainWin->onGroupsClicked(); break;
                case 4: mainWin->onContributionsClicked(); break;
                case 5: mainWin->onServiceClicked(); break;
                case 6: mainWin->onGraphClicked(); break;
                case 8: mainWin->onInventoryClicked(); break;
                case 9: mainWin->onVaultClicked(); break;
                case 10: mainWin->toggleTheme(); break;
                case 11: mainWin->launchKiosk(); break;
                case 12: mainWin->onShepherdClicked(); break;
                case 13: mainWin->onPlanningClicked(); break;
                case 14: mainWin->onFacilitiesClicked(); break;
                case 15: mainWin->onSettingsClicked(); break;
                case 16: mainWin->onShowUserManual(); break;
            }
        } else if (type == "Person") {
            mainWin->selectPerson(id);
        } else if (type == "Group") {
            mainWin->selectGroup(id);
        } else if (type == "Asset") {
            mainWin->selectAsset(id);
        }
    }

    accept();
}

void CommandPaletteDialog::executeSelection() {
    QListWidgetItem* current = m_listWidget->currentItem();
    // Rows hidden by the active filter must never be executed.
    if (!current || current->isHidden()) return;
    onItemSelected(current);
}
