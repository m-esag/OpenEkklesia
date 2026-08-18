#include "commandpalette.h"
#include "mainwindow.h"
#include "../database/databasemanager.h"
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QGuiApplication>

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

void CommandPaletteDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    QGraphicsOpacityEffect* opacity = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(opacity);

    QPropertyAnimation* anim = new QPropertyAnimation(opacity, "opacity", this);
    anim->setDuration(140);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutQuad);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CommandPaletteDialog::setupUI() {
    resize(550, 380);

    // Frame wrapper to support rounded corners & shadows
    QWidget* frame = new QWidget(this);
    frame->setObjectName("paletteFrame");
    frame->setStyleSheet(
        "QWidget#paletteFrame { "
        "  background-color: #1e1e2e; "
        "  border: 1px solid #313244; "
        "  border-radius: 12px; "
        "}"
    );

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);
    shadow->setOffset(0, 5);
    shadow->setColor(QColor(0, 0, 0, 120));
    frame->setGraphicsEffect(shadow);

    QVBoxLayout* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(10, 10, 10, 10);
    frameLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Type a command or search entities (Ctrl+K)...");
    m_searchEdit->setStyleSheet(
        "QLineEdit { "
        "  background-color: #11111b; "
        "  border: 1px solid #313244; "
        "  border-radius: 8px; "
        "  color: #cdd6f4; "
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
        "  color: #cdd6f4; "
        "  font-size: 13px; "
        "}"
        "QListWidget::item { "
        "  padding: 8px 12px; "
        "  border-radius: 6px; "
        "}"
        "QListWidget::item:hover { "
        "  background-color: #313244; "
        "  color: #cdd6f4; "
        "}"
        "QListWidget::item:selected { "
        "  background-color: #89b4fa; "
        "  color: #11111b; "
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
            int currRow = m_listWidget->currentRow();
            if (currRow < m_listWidget->count() - 1) {
                m_listWidget->setCurrentRow(currRow + 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Up) {
            int currRow = m_listWidget->currentRow();
            if (currRow > 0) {
                m_listWidget->setCurrentRow(currRow - 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void CommandPaletteDialog::addPaletteItem(const QString& text, const QString& type, int id) {
    QListWidgetItem* item = new QListWidgetItem(text, m_listWidget);
    item->setData(Qt::UserRole, type);
    item->setData(Qt::UserRole + 1, id);
}

void CommandPaletteDialog::populateItems() {
    m_listWidget->clear();

    // 1. Navigation Actions
    addPaletteItem("⚙ Navigation: Go to Dashboard", "Command", 1);
    addPaletteItem("👥 Navigation: Go to Members Directory (CRM)", "Command", 2);
    addPaletteItem("🏷 Navigation: Go to Small Groups / Ministries List", "Command", 3);
    addPaletteItem("💰 Navigation: Go to Contributions Offering batch entry", "Command", 4);
    addPaletteItem("📜 Navigation: Go to Sunday Service Run-Sheet Builder", "Command", 5);
    addPaletteItem("📊 Navigation: Go to Connections Relationship Graph", "Command", 6);
    addPaletteItem("📓 Navigation: Go to Double-Entry General Ledger", "Command", 7);
    addPaletteItem("📦 Navigation: Go to Asset Inventory & Checkouts", "Command", 8);
    addPaletteItem("🔒 Navigation: Go to Elder Board Document Vault", "Command", 9);

    // 2. Control Commands
    addPaletteItem("🌓 Theme: Toggle Light / Dark Stylesheet Mode", "Command", 10);
    addPaletteItem("🖥 Kiosk: Launch Child Nursery Check-In Screen", "Command", 11);

    // 3. Database entities (People, Groups, Assets)
    QSqlDatabase db = DatabaseManager::instance().database();
    
    QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name FROM people", db);
    while (qPeople.next()) {
        int id = qPeople.value(0).toInt();
        QString name = qPeople.value(1).toString();
        addPaletteItem(QString("👤 CRM Member Profile: %1").arg(name), "Person", id);
    }

    QSqlQuery qGroups("SELECT id, name FROM groups", db);
    while (qGroups.next()) {
        int id = qGroups.value(0).toInt();
        QString name = qGroups.value(1).toString();
        addPaletteItem(QString("🏷 Ministry Group: %1").arg(name), "Group", id);
    }

    QSqlQuery qAssets("SELECT id, name, barcode FROM assets", db);
    while (qAssets.next()) {
        int id = qAssets.value(0).toInt();
        QString name = qAssets.value(1).toString();
        QString barcode = qAssets.value(2).toString();
        addPaletteItem(QString("📦 Equipment Asset: %1 (%2)").arg(name, barcode), "Asset", id);
    }

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

void CommandPaletteDialog::onSearchChanged(const QString& text) {
    QString q = text.trimmed().toLower();
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        bool matches = item->text().toLower().contains(q);
        item->setHidden(!matches);
    }

    // Select first visible
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        if (!item->isHidden()) {
            m_listWidget->setCurrentItem(item);
            break;
        }
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
                case 7: mainWin->onLedgerClicked(); break;
                case 8: mainWin->onInventoryClicked(); break;
                case 9: mainWin->onVaultClicked(); break;
                case 10: mainWin->toggleTheme(); break;
                case 11: mainWin->launchKiosk(); break;
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
    onItemSelected(m_listWidget->currentItem());
}
