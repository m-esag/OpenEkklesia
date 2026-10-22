#include "servicebuilderwidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QPdfWriter>
#include <QTextDocument>
#include <QImage>
#include <QBuffer>
#include <QTextStream>
#include <QDateTime>
#include <QTableWidget>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QDragEnterEvent>
#include <QClipboard>
#include <QGuiApplication>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include "servicebuilderwidget.h"
#include "../core/thememanager.h"
#include "../core/iconmanager.h"
#include "../database/databasemanager.h"
#include "../common/icalgenerator.h"
#include "../common/churchprofile.h"
#include "ministrymatch.h"

// Run-sheet list: shows an empty-state hint, and highlights the drop target
// while the user drags an element over it (playlist-builder affordance).
class RunSheetListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        m_dragOver = true;
        QListWidget::dragEnterEvent(e);
        viewport()->update();
    }
    void dragLeaveEvent(QDragLeaveEvent* e) override {
        m_dragOver = false;
        QListWidget::dragLeaveEvent(e);
        viewport()->update();
    }
    void dropEvent(QDropEvent* e) override {
        m_dragOver = false;
        QListWidget::dropEvent(e);
        viewport()->update();
    }
    void paintEvent(QPaintEvent* e) override {
        QListWidget::paintEvent(e);
        QPainter p(viewport());
        if (m_dragOver) {
            p.fillRect(viewport()->rect(), QColor(137, 180, 250, 36));
            QPen pen(QColor(137, 180, 250), 2, Qt::DashLine);
            p.setPen(pen);
            p.drawRect(viewport()->rect().adjusted(2, 2, -2, -2));
        } else if (count() == 0) {
            p.setPen(QPalette().placeholderText().color());
            p.drawText(viewport()->rect(), Qt::AlignCenter,
                       "Drag songs, scriptures, and media here to build your service.");
        }
    }
private:
    bool m_dragOver = false;
};

// Volunteer table with the same empty-state treatment.
class VolunteerTableWidget : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
protected:
    void paintEvent(QPaintEvent* e) override {
        QTableWidget::paintEvent(e);
        if (rowCount() == 0) {
            QPainter p(viewport());
            p.setPen(QPalette().placeholderText().color());
            p.drawText(viewport()->rect(), Qt::AlignCenter,
                       "No volunteers scheduled yet â€” assign roles for this date.");
        }
    }
};

ServiceBuilderWidget::ServiceBuilderWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    loadLibrary();
    loadVolunteers();
    loadServiceDetails();
    applyRole("admin"); // default: full access until MainWindow applies the real role
}

ServiceBuilderWidget::~ServiceBuilderWidget() {}

// Pastel type tint used by both the library and the run sheet, blended at low
// alpha so it reads on light and dark themes alike.
static QColor tintForType(const QString& type) {
    if (type == "Song") return QColor("#f9e2af");
    if (type == "Scripture") return QColor("#a6e3a1");
    if (type == "Media") return QColor("#89b4fa");
    return QColor("#cba6f7"); // Text / liturgy
}

static const char* typeIconName(const QString& type) {
    if (type == "Song") return "list-music";
    if (type == "Scripture") return "book-open";
    if (type == "Media") return "play";
    return "pencil";
}

// Composes the display text ("Song: Amazing Grace (4m)") from clean parts.
static QString libraryDisplayName(const QString& type, const QString& name, int mins) {
    return QString("%1: %2 (%3m)").arg(type, name).arg(mins);
}

void ServiceBuilderWidget::applyRole(const QString& role) {
    m_readOnly = (role.compare("readonly", Qt::CaseInsensitive) == 0 ||
                  role.compare("guest", Qt::CaseInsensitive) == 0);
    const bool canEdit = !m_readOnly;

    m_btnRemove->setEnabled(canEdit);
    m_btnClear->setEnabled(canEdit);
    m_btnSave->setEnabled(canEdit);
    m_btnSchedule->setEnabled(canEdit);
    m_btnUnschedule->setEnabled(canEdit);
    m_btnAddElement->setEnabled(canEdit);
    m_btnRemoveElement->setEnabled(canEdit);
    m_btnSaveTemplate->setEnabled(canEdit);
    m_btnApplyTemplate->setEnabled(canEdit);
    m_btnMarkAttended->setEnabled(canEdit);
    m_playlist->setAcceptDrops(canEdit);
    m_playlist->setDragEnabled(canEdit);
    m_libraryList->setDragEnabled(canEdit);
    m_serviceTitleEdit->setReadOnly(m_readOnly);
    if (m_leaderEdit->lineEdit()) m_leaderEdit->lineEdit()->setReadOnly(m_readOnly);

    const QString tip = m_readOnly
        ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
        : "";
    for (QPushButton* b : {m_btnRemove, m_btnClear, m_btnSave, m_btnSchedule, m_btnUnschedule,
                           m_btnAddElement, m_btnRemoveElement,
                           m_btnSaveTemplate, m_btnApplyTemplate, m_btnMarkAttended}) {
        b->setToolTip(tip);
    }
}

void ServiceBuilderWidget::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(20);

    // Left Panel (Library Items)
    QWidget* libraryPanel = new QWidget(this);
    QVBoxLayout* libraryLayout = new QVBoxLayout(libraryPanel);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(12);

    QLabel* libLabel = new QLabel("Available Elements Library", this);
    libLabel->setObjectName("detailHeader");
    libraryLayout->addWidget(libLabel);

    // Filter tabs + live search so a large library stays usable.
    QHBoxLayout* libFilterRow = new QHBoxLayout();
    m_libraryFilter = new QComboBox(this);
    m_libraryFilter->setObjectName("serviceLibraryFilter");
    m_libraryFilter->addItem("All Types", QString());
    m_libraryFilter->addItem("Songs", "Song");
    m_libraryFilter->addItem("Scripture", "Scripture");
    m_libraryFilter->addItem("Media", "Media");
    m_libraryFilter->addItem("Text / Liturgy", "Text");
    connect(m_libraryFilter, &QComboBox::currentIndexChanged, this, &ServiceBuilderWidget::applyLibraryFilter);
    libFilterRow->addWidget(m_libraryFilter);

    m_librarySearch = new QLineEdit(this);
    m_librarySearch->setObjectName("serviceLibrarySearch");
    m_librarySearch->setPlaceholderText("Search elementsâ€¦");
    m_librarySearch->setClearButtonEnabled(true);
    connect(m_librarySearch, &QLineEdit::textChanged, this, &ServiceBuilderWidget::applyLibraryFilter);
    libFilterRow->addWidget(m_librarySearch, 1);
    libraryLayout->addLayout(libFilterRow);

    m_libraryList = new QListWidget(this);
    m_libraryList->setObjectName("serviceLibraryList");
    m_libraryList->setDragEnabled(true);
    m_libraryList->setDragDropMode(QAbstractItemView::DragOnly);
    m_libraryList->setAlternatingRowColors(true);
    libraryLayout->addWidget(m_libraryList);

    QLabel* dragTip = new QLabel("Tip: Drag elements into the run sheet, or double-click to add. Right-click for more.", this);
    dragTip->setObjectName("dragTip");
    dragTip->setWordWrap(true);
    dragTip->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    libraryLayout->addWidget(dragTip);

    QHBoxLayout* libActions = new QHBoxLayout();
    m_btnAddElement = new QPushButton(" Add Element", this);
    m_btnAddElement->setIcon(IconManager::instance().getIcon("plus", QColor(), 14));
    m_btnAddElement->setObjectName("secondaryBtn");
    m_btnAddElement->setToolTip("Add a custom element to the persistent library");
    connect(m_btnAddElement, &QPushButton::clicked, this, &ServiceBuilderWidget::onAddElement);
    libActions->addWidget(m_btnAddElement);

    m_btnUsageStats = new QPushButton(" Usage Stats", this);
    m_btnUsageStats->setIcon(IconManager::instance().getIcon("bar-chart-3", QColor(), 14));
    m_btnUsageStats->setObjectName("secondaryBtn");
    m_btnUsageStats->setToolTip("Which elements get reused most across saved run sheets");
    connect(m_btnUsageStats, &QPushButton::clicked, this, &ServiceBuilderWidget::onElementUsage);
    libActions->addWidget(m_btnUsageStats);

    m_btnRemoveElement = new QPushButton(" Remove Element", this);
    m_btnRemoveElement->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnRemoveElement->setObjectName("ghostDangerBtn");
    m_btnRemoveElement->setToolTip("Remove the selected library element");
    connect(m_btnRemoveElement, &QPushButton::clicked, this, &ServiceBuilderWidget::onRemoveElement);
    libActions->addWidget(m_btnRemoveElement);
    libraryLayout->addLayout(libActions);

    // Color-blind-safe legend: icons (not color) identify the type; the tint
    // is only a hint, so the run sheet stays readable in grayscale.
    m_lblTypeLegend = new QLabel("Type Legend: Songs Â· Scripture Â· Media Â· Liturgy Text", this);
    m_lblTypeLegend->setObjectName("serviceTypeLegend");
    m_lblTypeLegend->setWordWrap(true);
    m_lblTypeLegend->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    libraryLayout->addWidget(m_lblTypeLegend);

    // Double-click adds to the run sheet (playlist-builder metaphor); edit /
    // remove move to the right-click context menu.
    connect(m_libraryList, &QListWidget::itemDoubleClicked, this, &ServiceBuilderWidget::addLibraryItemToRunSheet);
    m_libraryList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_libraryList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_libraryList->itemAt(pos);
        if (!item) return;
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* add = menu.addAction(IconManager::instance().getIcon("plus", QColor(), 14), "Add to Run Sheet");
        QAction* copy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Element Name");
        menu.addSeparator();
        QAction* edit = menu.addAction(IconManager::instance().getIcon("pencil", QColor(), 14), "Edit Element");
        QAction* remove = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Element");

        if (m_readOnly) {
            add->setEnabled(false); add->setToolTip(roTooltip);
            edit->setEnabled(false); edit->setToolTip(roTooltip);
            remove->setEnabled(false); remove->setToolTip(roTooltip);
        }

        QAction* chosen = menu.exec(m_libraryList->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == add && !m_readOnly) addLibraryItemToRunSheet(item);
        else if (chosen == copy) QGuiApplication::clipboard()->setText(item->text());
        else if (chosen == edit && !m_readOnly) onEditElement(item);
        else if (chosen == remove && !m_readOnly) onRemoveElement();
    });

    mainLayout->addWidget(libraryPanel, 1);

    // Right Panel (Sunday Service Run Sheet)
    QWidget* playlistPanel = new QWidget(this);
    QVBoxLayout* playlistLayout = new QVBoxLayout(playlistPanel);
    playlistLayout->setContentsMargins(0, 0, 0, 0);
    playlistLayout->setSpacing(12);

    QHBoxLayout* headerRow = new QHBoxLayout();
    QLabel* playlistLabel = new QLabel("Sunday Service Run Sheet", this);
    playlistLabel->setObjectName("detailHeader");
    m_lblTotalDuration = new QLabel("Total Service Duration: 0 mins", this);
    m_lblTotalDuration->setObjectName("serviceDurationLabel");
    m_lblTotalDuration->setStyleSheet("font-size: 13px; font-weight: bold; color: #a6e3a1;");
    headerRow->addWidget(playlistLabel);
    headerRow->addStretch();
    headerRow->addWidget(m_lblTotalDuration);
    playlistLayout->addLayout(headerRow);

    m_playlist = new RunSheetListWidget(this);
    m_playlist->setObjectName("servicePlaylist");
    m_playlist->setAcceptDrops(true);
    m_playlist->setDragEnabled(true);
    m_playlist->setDragDropMode(QAbstractItemView::DragDrop);
    m_playlist->setDefaultDropAction(Qt::CopyAction);
    m_playlist->setAlternatingRowColors(true);
    m_playlist->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playlist, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_playlist->itemAt(pos);
        if (!item) return;
        int row = m_playlist->row(item);
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Element Details");
        menu.addSeparator();
        QAction* actUp = menu.addAction(IconManager::instance().getIcon("arrow-up", QColor(), 14), "Move Earlier in Service");
        actUp->setEnabled(row > 0 && !m_readOnly);
        QAction* actDown = menu.addAction(IconManager::instance().getIcon("arrow-down", QColor(), 14), "Move Later in Service");
        actDown->setEnabled(row < m_playlist->count() - 1 && !m_readOnly);
        QAction* actDup = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Duplicate Element");
        menu.addSeparator();
        QAction* actRemove = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Element from Run Sheet");

        if (m_readOnly) {
            actDup->setEnabled(false); actDup->setToolTip(roTooltip);
            actRemove->setEnabled(false); actRemove->setToolTip(roTooltip);
            actUp->setToolTip(roTooltip);
            actDown->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_playlist->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QGuiApplication::clipboard()->setText(item->text());
        } else if (sel == actUp && row > 0 && !m_readOnly) {
            QListWidgetItem* taken = m_playlist->takeItem(row);
            m_playlist->insertItem(row - 1, taken);
            m_playlist->setCurrentItem(taken);
        } else if (sel == actDown && row < m_playlist->count() - 1 && !m_readOnly) {
            QListWidgetItem* taken = m_playlist->takeItem(row);
            m_playlist->insertItem(row + 1, taken);
            m_playlist->setCurrentItem(taken);
        } else if (sel == actDup && !m_readOnly) {
            QListWidgetItem* dup = new QListWidgetItem(*item);
            m_playlist->insertItem(row + 1, dup);
        } else if (sel == actRemove && !m_readOnly) {
            onRemoveItem();
        }
    });
    connect(m_playlist->model(), &QAbstractItemModel::rowsInserted, this, &ServiceBuilderWidget::updateDurationCalculator);
    connect(m_playlist->model(), &QAbstractItemModel::rowsRemoved, this, &ServiceBuilderWidget::updateDurationCalculator);
    playlistLayout->addWidget(m_playlist, 1);

    m_lblTimeline = new QLabel(this);
    m_lblTimeline->setObjectName("serviceTimelineLabel");
    m_lblTimeline->setWordWrap(true);
    m_lblTimeline->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    playlistLayout->addWidget(m_lblTimeline);

    // Action rows, split so no button can be crushed. Destructive actions sit
    // apart from file/template actions.
    auto minH = [](QPushButton* b) { b->setMinimumHeight(30); return b; };

    QHBoxLayout* actionsLayout = new QHBoxLayout();
    m_btnRemove = minH(new QPushButton(" Remove Selected", this));
    m_btnRemove->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnRemove->setObjectName("dangerBtn");
    m_btnRemove->setToolTip("Remove the selected element from the run sheet");
    connect(m_btnRemove, &QPushButton::clicked, this, &ServiceBuilderWidget::onRemoveItem);
    actionsLayout->addWidget(m_btnRemove);

    m_btnClear = minH(new QPushButton(" Clear All", this));
    m_btnClear->setIcon(IconManager::instance().getIcon("refresh-cw", QColor(), 14));
    m_btnClear->setObjectName("secondaryBtn");
    m_btnClear->setToolTip("Remove every element from the run sheet (asks first)");
    connect(m_btnClear, &QPushButton::clicked, this, &ServiceBuilderWidget::onClearPlaylist);
    actionsLayout->addWidget(m_btnClear);

    m_btnCoverageCheck = minH(new QPushButton(" Coverage Check", this));
    m_btnCoverageCheck->setIcon(IconManager::instance().getIcon("search", QColor(), 14));
    m_btnCoverageCheck->setObjectName("secondaryBtn");
    m_btnCoverageCheck->setToolTip("Check for missing sermon, unassigned core roles, or run-sheet/volunteer mismatches");
    connect(m_btnCoverageCheck, &QPushButton::clicked, this, &ServiceBuilderWidget::onCoverageCheck);
    actionsLayout->addWidget(m_btnCoverageCheck);

    m_btnExportPDF = minH(new QPushButton(" Export Run Sheet PDF", this));
    m_btnExportPDF->setIcon(IconManager::instance().getIcon("download", ThemeManager::textOn(QColor("#74c7ec")), 14));
    m_btnExportPDF->setObjectName("accentBtnCyan");
    connect(m_btnExportPDF, &QPushButton::clicked, this, &ServiceBuilderWidget::onExportRunSheet);
    actionsLayout->addWidget(m_btnExportPDF);
    actionsLayout->addStretch();
    playlistLayout->addLayout(actionsLayout);

    // Second row: file backup + named templates (everything else auto-saves).
    QHBoxLayout* fileActions = new QHBoxLayout();
    m_btnSave = minH(new QPushButton(" Save Layout File", this));
    m_btnSave->setIcon(IconManager::instance().getPrimaryIcon("check", 14));
    m_btnSave->setObjectName("saveBtn");
    m_btnSave->setToolTip("Backup the run sheet to a JSON file (the arrangement already auto-saves to this service)");
    connect(m_btnSave, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveService);
    fileActions->addWidget(m_btnSave);

    m_btnLoad = minH(new QPushButton(" Load Layout File", this));
    m_btnLoad->setIcon(IconManager::instance().getPrimaryIcon("upload", 14));
    m_btnLoad->setObjectName("primaryBtn");
    connect(m_btnLoad, &QPushButton::clicked, this, &ServiceBuilderWidget::onLoadService);
    fileActions->addWidget(m_btnLoad);

    m_btnSaveTemplate = minH(new QPushButton(" Save as Template", this));
    m_btnSaveTemplate->setIcon(IconManager::instance().getIcon("copy", QColor(), 14));
    m_btnSaveTemplate->setObjectName("secondaryBtn");
    m_btnSaveTemplate->setToolTip("Save the current run sheet as a named template");
    connect(m_btnSaveTemplate, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveTemplate);
    fileActions->addWidget(m_btnSaveTemplate);

    m_btnApplyTemplate = minH(new QPushButton(" Apply Template", this));
    m_btnApplyTemplate->setIcon(IconManager::instance().getIcon("copy", QColor(), 14));
    m_btnApplyTemplate->setObjectName("secondaryBtn");
    m_btnApplyTemplate->setToolTip("Load a saved run-sheet template into the current date");
    connect(m_btnApplyTemplate, &QPushButton::clicked, this, &ServiceBuilderWidget::onApplyTemplate);
    fileActions->addWidget(m_btnApplyTemplate);
    fileActions->addStretch();
    playlistLayout->addLayout(fileActions);

    mainLayout->addWidget(playlistPanel, 4);

    // Rightmost Panel (Volunteer Scheduling)
    QWidget* volunteerPanel = new QWidget(this);
    QVBoxLayout* volunteerLayout = new QVBoxLayout(volunteerPanel);
    volunteerLayout->setContentsMargins(0, 0, 0, 0);
    volunteerLayout->setSpacing(12);

    QLabel* volHeader = new QLabel("Service Volunteers & Scheduling", this);
    volHeader->setObjectName("detailHeader");
    volunteerLayout->addWidget(volHeader);

    QHBoxLayout* dateLayout = new QHBoxLayout();
    QLabel* dateLabel = new QLabel("Service Date:", this);
    dateLabel->setObjectName("dateLabel");
    m_serviceDateEdit = new QDateEdit(this);
    m_serviceDateEdit->setObjectName("serviceDateEdit");
    m_serviceDateEdit->setCalendarPopup(true);
    m_serviceDateEdit->setDisplayFormat("yyyy-MM-dd");
    // Default to next Sunday
    QDate nextSunday = QDate::currentDate();
    while (nextSunday.dayOfWeek() != 7) {
        nextSunday = nextSunday.addDays(1);
    }
    m_serviceDateEdit->setDate(nextSunday);
    dateLayout->addWidget(dateLabel);
    dateLayout->addWidget(m_serviceDateEdit);

    m_serviceTimeEdit = new QTimeEdit(this);
    m_serviceTimeEdit->setObjectName("serviceTimeEdit");
    m_serviceTimeEdit->setDisplayFormat("h:mm AP");
    {
        QSettings s("OpenEkklesia", "CHMS");
        QTime defTime = QTime::fromString(s.value("defaultServiceTime", "10:00").toString(), "HH:mm");
        m_serviceTimeEdit->setTime(defTime.isValid() ? defTime : QTime(10, 0));
    }
    m_serviceTimeEdit->setToolTip("Service starts at â€” the run sheet shows each item's start time");
    dateLayout->addWidget(new QLabel("at:", this));
    dateLayout->addWidget(m_serviceTimeEdit);
    volunteerLayout->addLayout(dateLayout);

    connect(m_serviceTimeEdit, &QTimeEdit::timeChanged, this, [this](const QTime&) {
        persistRunSheet();
        updateTimeline();
    });

    // Service plan details (title & leader) â€” auto-saved on focus loss; the
    // run sheet itself auto-saves too, so there is exactly ONE save action
    // (none). The file/template buttons are backups, not saves.
    QFormLayout* serviceForm = new QFormLayout();
    m_serviceTitleEdit = new QLineEdit(this);
    m_serviceTitleEdit->setObjectName("serviceTitleEdit");
    m_serviceTitleEdit->setPlaceholderText("e.g. Sunday Worship Service");
    connect(m_serviceTitleEdit, &QLineEdit::editingFinished, this, &ServiceBuilderWidget::saveServiceDetails);
    serviceForm->addRow("Service Title:", m_serviceTitleEdit);

    m_leaderEdit = new QComboBox(this);
    m_leaderEdit->setObjectName("serviceLeaderEdit");
    m_leaderEdit->setEditable(true);
    m_leaderEdit->setInsertPolicy(QComboBox::NoInsert);
    m_leaderEdit->setPlaceholderText("Pick a leader or type a nameâ€¦");
    QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name",
                      DatabaseManager::instance().database());
    while (qPeople.next()) m_leaderEdit->addItem(qPeople.value(1).toString(), qPeople.value(0).toInt());
    if (m_leaderEdit->lineEdit()) {
        connect(m_leaderEdit->lineEdit(), &QLineEdit::editingFinished, this, &ServiceBuilderWidget::saveServiceDetails);
    }
    serviceForm->addRow("Service Leader:", m_leaderEdit);
    volunteerLayout->addLayout(serviceForm);

    m_lblAutoSaveStatus = new QLabel("Title, leader & run sheet auto-save to this date's service.", this);
    m_lblAutoSaveStatus->setObjectName("serviceAutoSaveLabel");
    m_lblAutoSaveStatus->setWordWrap(true);
    m_lblAutoSaveStatus->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    volunteerLayout->addWidget(m_lblAutoSaveStatus);

    m_volunteerList = new VolunteerTableWidget(this);
    m_volunteerList->setObjectName("serviceVolunteerList");
    m_volunteerList->setColumnCount(3);
    m_volunteerList->setHorizontalHeaderLabels({"Name", "Role", "Status"});
    m_volunteerList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_volunteerList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_volunteerList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_volunteerList->verticalHeader()->setVisible(false);
    m_volunteerList->setAlternatingRowColors(true);
    m_volunteerList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_volunteerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_volunteerList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_volunteerList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_volunteerList, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int r = m_volunteerList->rowAt(pos.y());
        if (r < 0) return;
        m_volunteerList->selectRow(r);
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QString vName = m_volunteerList->item(r, 0) ? m_volunteerList->item(r, 0)->text() : "";
        QString vRole = m_volunteerList->item(r, 1) ? m_volunteerList->item(r, 1)->text() : "";
        QString vStatus = m_volunteerList->item(r, 2) ? m_volunteerList->item(r, 2)->text() : "";

        QMenu menu(this);
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Volunteer Assignment");
        QAction* actIcal = menu.addAction(IconManager::instance().getIcon("calendar", QColor(), 14), "Export Volunteer iCal (.ics)");
        menu.addSeparator();
        QAction* actAttended = menu.addAction(IconManager::instance().getIcon("check-circle-2", ThemeManager::instance().currentTokens().accentSuccess, 14), "Mark Volunteer Attended");
        menu.addSeparator();
        QAction* actUnschedule = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Unassign Selected Volunteer");

        if (m_readOnly) {
            actAttended->setEnabled(false); actAttended->setToolTip(roTooltip);
            actUnschedule->setEnabled(false); actUnschedule->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_volunteerList->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QGuiApplication::clipboard()->setText(QString("%1 | Role: %2 | Status: %3").arg(vName, vRole, vStatus));
        } else if (sel == actIcal) onExportIcal();
        else if (sel == actAttended && !m_readOnly) onMarkAttended();
        else if (sel == actUnschedule && !m_readOnly) onUnscheduleVolunteer();
    });
    volunteerLayout->addWidget(m_volunteerList, 1);

    QHBoxLayout* volActions = new QHBoxLayout();
    m_btnSchedule = new QPushButton(" Assign Volunteer", this);
    m_btnSchedule->setIcon(IconManager::instance().getPrimaryIcon("user-plus", 14));
    m_btnSchedule->setObjectName("primaryBtn");
    connect(m_btnSchedule, &QPushButton::clicked, this, &ServiceBuilderWidget::onScheduleVolunteer);

    m_btnUnschedule = new QPushButton(" Unassign", this);
    m_btnUnschedule->setIcon(IconManager::instance().getIcon("user-minus", QColor(), 14));
    m_btnUnschedule->setObjectName("secondaryBtn");
    connect(m_btnUnschedule, &QPushButton::clicked, this, &ServiceBuilderWidget::onUnscheduleVolunteer);

    volActions->addWidget(m_btnSchedule);
    volActions->addWidget(m_btnUnschedule);
    volunteerLayout->addLayout(volActions);

    m_btnMarkAttended = new QPushButton(" Mark Volunteers Attended", this);
    m_btnMarkAttended->setIcon(IconManager::instance().getPrimaryIcon("check", 14));
    m_btnMarkAttended->setObjectName("saveBtn");
    m_btnMarkAttended->setToolTip("Record all scheduled volunteers as attended for this service date");
    connect(m_btnMarkAttended, &QPushButton::clicked, this, &ServiceBuilderWidget::onMarkAttended);
    volunteerLayout->addWidget(m_btnMarkAttended);

    m_btnExportIcal = new QPushButton(" Export Volunteer iCal", this);
    m_btnExportIcal->setIcon(IconManager::instance().getIcon("calendar", QColor(), 14));
    m_btnExportIcal->setObjectName("secondaryBtn");
    m_btnExportIcal->setToolTip("Export this date's volunteer roster as calendar events (.ics)");
    connect(m_btnExportIcal, &QPushButton::clicked, this, &ServiceBuilderWidget::onExportIcal);
    volunteerLayout->addWidget(m_btnExportIcal);

    mainLayout->addWidget(volunteerPanel, 2);

    connect(m_serviceDateEdit, &QDateEdit::dateChanged, this, &ServiceBuilderWidget::onServiceDateChanged);

    // Autosave the run sheet to the dated service row on any playlist change
    // (add, remove, clear, load, reorder). Silent by design: no dialogs.
    // clear() resets the model, so modelReset is wired too.
    connect(m_playlist->model(), &QAbstractItemModel::rowsInserted, this, &ServiceBuilderWidget::persistRunSheet);
    connect(m_playlist->model(), &QAbstractItemModel::rowsRemoved, this, &ServiceBuilderWidget::persistRunSheet);
    connect(m_playlist->model(), &QAbstractItemModel::rowsMoved, this, &ServiceBuilderWidget::persistRunSheet);
    connect(m_playlist->model(), &QAbstractItemModel::modelReset, this, &ServiceBuilderWidget::persistRunSheet);
}

void ServiceBuilderWidget::loadLibrary() {
    m_libraryList->clear();

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    if (!q.exec("SELECT id, name, type, duration_mins FROM service_library ORDER BY id")) {
        qWarning() << "Failed to load service library:" << q.lastError().text();
        return;
    }
    while (q.next()) {
        const int id = q.value(0).toInt();
        const QString name = q.value(1).toString();
        const QString type = q.value(2).toString();
        const int mins = q.value(3).toInt();

        QListWidgetItem* item = new QListWidgetItem(libraryDisplayName(type, name, mins), m_libraryList);
        item->setIcon(IconManager::instance().getIcon(typeIconName(type), tintForType(type), 16));
        item->setData(Qt::UserRole, type);
        item->setData(Qt::UserRole + 1, mins);
        item->setData(Qt::UserRole + 2, id);
        item->setData(Qt::ToolTipRole, QString("Type: %1 Â· %2 min Â· double-click to add to the run sheet").arg(type).arg(mins));
        QColor tint = tintForType(type);
        tint.setAlpha(60);
        item->setBackground(tint);
    }
    applyLibraryFilter();
}

// Live filter: type combo + free-text search, applied by hiding items.
void ServiceBuilderWidget::applyLibraryFilter() {
    const QString type = m_libraryFilter->currentData().toString();
    const QString needle = m_librarySearch->text().trimmed().toLower();
    for (int i = 0; i < m_libraryList->count(); ++i) {
        QListWidgetItem* item = m_libraryList->item(i);
        const bool typeOk = type.isEmpty() || item->data(Qt::UserRole).toString() == type;
        const bool textOk = needle.isEmpty() || item->text().toLower().contains(needle);
        item->setHidden(!(typeOk && textOk));
    }
}

// Double-click / context menu: clone the library element into the run sheet.
void ServiceBuilderWidget::addLibraryItemToRunSheet(QListWidgetItem* item) {
    if (!item) return;
    m_playlist->addItem(item->clone()); // rowsInserted -> autosave + duration
}

// Shared element editor used by add and edit flows.
static bool elementEditorDialog(QWidget* parent, const QString& title,
                                QString& name, QString& type, int& mins) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.resize(360, 180);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QLineEdit* nameEdit = new QLineEdit(name, &dlg);
    nameEdit->setPlaceholderText("e.g. Amazing Grace");
    form->addRow("Name:", nameEdit);

    QComboBox* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({"Song", "Scripture", "Media", "Text"});
    if (!type.isEmpty()) typeCombo->setCurrentText(type);
    form->addRow("Type:", typeCombo);

    QSpinBox* minsSpin = new QSpinBox(&dlg);
    minsSpin->setRange(0, 240);
    minsSpin->setSuffix(" min");
    minsSpin->setValue(mins);
    form->addRow("Duration:", minsSpin);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    name = nameEdit->text().trimmed();
    type = typeCombo->currentText();
    mins = minsSpin->value();
    return !name.isEmpty();
}

void ServiceBuilderWidget::onAddElement() {
    QString name, type = "Song";
    int mins = 4;
    if (!elementEditorDialog(this, "Add Library Element", name, type, mins)) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("INSERT INTO service_library (name, type, duration_mins, is_custom) VALUES (?, ?, ?, 1)");
    q.addBindValue(name);
    q.addBindValue(type);
    q.addBindValue(mins);
    if (q.exec()) {
        loadLibrary();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to add element: " + q.lastError().text());
    }
}

void ServiceBuilderWidget::onEditElement(QListWidgetItem* item) {
    if (!item) return;
    const int libId = item->data(Qt::UserRole + 2).toInt();
    if (libId <= 0) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery fetch(db);
    fetch.prepare("SELECT name, type, duration_mins FROM service_library WHERE id = ?");
    fetch.addBindValue(libId);
    if (!(fetch.exec() && fetch.next())) return;

    QString name = fetch.value(0).toString();
    QString type = fetch.value(1).toString();
    int mins = fetch.value(2).toInt();
    if (!elementEditorDialog(this, "Edit Library Element", name, type, mins)) return;

    QSqlQuery up(db);
    up.prepare("UPDATE service_library SET name = ?, type = ?, duration_mins = ? WHERE id = ?");
    up.addBindValue(name);
    up.addBindValue(type);
    up.addBindValue(mins);
    up.addBindValue(libId);
    if (up.exec()) {
        loadLibrary();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to update element: " + up.lastError().text());
    }
}

void ServiceBuilderWidget::onRemoveElement() {
    QListWidgetItem* item = m_libraryList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Notice", "Select a library element to remove.");
        return;
    }
    const int libId = item->data(Qt::UserRole + 2).toInt();
    if (libId <= 0) return;

    auto res = QMessageBox::question(this, "Remove Element",
                                     QString("Remove '%1' from the library?").arg(item->text()),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("DELETE FROM service_library WHERE id = ?");
    q.addBindValue(libId);
    if (q.exec()) {
        loadLibrary();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to remove element: " + q.lastError().text());
    }
}

void ServiceBuilderWidget::onRemoveItem() {
    QListWidgetItem* item = m_playlist->currentItem();
    if (item) {
        delete m_playlist->takeItem(m_playlist->row(item));
    }
}

void ServiceBuilderWidget::onClearPlaylist() {
    if (m_playlist->count() == 0) return;
    
    auto res = QMessageBox::question(this, "Clear Run Sheet", 
                                     "Are you sure you want to remove all items from the current service run sheet?",
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes) {
        m_playlist->clear();
    }
}

QJsonArray ServiceBuilderWidget::playlistToJson() const {
    QJsonArray array;
    for (int i = 0; i < m_playlist->count(); ++i) {
        QListWidgetItem* item = m_playlist->item(i);
        QJsonObject obj;
        obj["title"] = item->text();
        obj["type"] = item->data(Qt::UserRole).toString();
        obj["durationMins"] = item->data(Qt::UserRole + 1).toInt();
        array.append(obj);
    }
    return array;
}

void ServiceBuilderWidget::loadPlaylistFromJson(const QJsonArray& array) {
    m_playlist->clear();
    for (int i = 0; i < array.size(); ++i) {
        const QJsonObject obj = array[i].toObject();
        QListWidgetItem* item = new QListWidgetItem(obj["title"].toString(), m_playlist);
        item->setData(Qt::UserRole, obj["type"].toString());
        item->setData(Qt::UserRole + 1, obj["durationMins"].toInt());
    }
    updateDurationCalculator(); // apply tints + totals from the loaded data
}

// Autosaves the run sheet JSON + start time into the services row for the
// selected date.
void ServiceBuilderWidget::persistRunSheet() {
    if (m_readOnly) return;
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("INSERT INTO services (date, run_sheet, start_time) VALUES (?, ?, ?) "
              "ON CONFLICT(date) DO UPDATE SET run_sheet = excluded.run_sheet, start_time = excluded.start_time");
    q.addBindValue(m_serviceDateEdit->date().toString("yyyy-MM-dd"));
    q.addBindValue(QString::fromUtf8(QJsonDocument(playlistToJson()).toJson(QJsonDocument::Compact)));
    q.addBindValue(m_serviceTimeEdit->time().toString("HH:mm"));
    if (!q.exec()) {
        qWarning() << "Failed to autosave run sheet:" << q.lastError().text();
    }
}

void ServiceBuilderWidget::loadRunSheetFromDb(const QDate& date) {
    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("SELECT run_sheet FROM services WHERE date = ?");
    q.addBindValue(date.toString("yyyy-MM-dd"));
    if (q.exec() && q.next()) {
        const QString raw = q.value(0).toString();
        if (raw.isEmpty()) {
            m_playlist->clear(); // no saved arrangement for this date
            return;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            loadPlaylistFromJson(doc.array());
            return;
        }
        qWarning() << "Corrupt run_sheet for" << date.toString("yyyy-MM-dd") << ":" << err.errorString();
        // Keep the current playlist instead of silently wiping it.
        return;
    }
    m_playlist->clear(); // no saved arrangement for this date
}

void ServiceBuilderWidget::onSaveService() {
    if (m_playlist->count() == 0) {
        QMessageBox::warning(this, "No Layout", "The Sunday Run Sheet is empty. Add elements before saving.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Save Service Plan", QString(), "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    QJsonDocument doc(playlistToJson());
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        QMessageBox::information(this, "Success", "Service arrangement successfully saved locally.");
    } else {
        QMessageBox::critical(this, "File Error", "Could not open file for writing: " + file.errorString());
    }
}

void ServiceBuilderWidget::onLoadService() {
    QString fileName = QFileDialog::getOpenFileName(this, "Load Service Plan", QString(), "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "File Error", "Could not open file for reading: " + file.errorString());
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    if (!doc.isArray()) {
        QMessageBox::critical(this, "Invalid Format", "The selected JSON file does not contain a valid service arrangement array.");
        return;
    }

    loadPlaylistFromJson(doc.array());
}

void ServiceBuilderWidget::loadVolunteers() {
    m_volunteerList->setRowCount(0);
    QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");

    QSqlDatabase db = DatabaseManager::instance().database();

    // Which of today's volunteers are already marked attended for this service.
    QSet<int> attended;
    QSqlQuery qAtt(db);
    qAtt.prepare("SELECT person_id FROM attendance "
                 "WHERE service_id = (SELECT id FROM services WHERE date = ?) AND attended = 1");
    qAtt.addBindValue(dateStr);
    if (qAtt.exec()) {
        while (qAtt.next()) attended.insert(qAtt.value(0).toInt());
    }

    QSqlQuery query(db);
    query.prepare("SELECT sv.id, sv.person_id, p.first_name || ' ' || p.last_name, sv.role "
                  "FROM service_volunteers sv "
                  "JOIN people p ON sv.person_id = p.id "
                  "WHERE sv.service_date = ? "
                  "ORDER BY sv.role");
    query.addBindValue(dateStr);

    int row = 0;
    if (query.exec()) {
        while (query.next()) {
            const int id = query.value(0).toInt();
            const int pid = query.value(1).toInt();
            const QString name = query.value(2).toString();
            const QString role = query.value(3).toString();

            m_volunteerList->insertRow(row);
            QTableWidgetItem* nameItem = new QTableWidgetItem(name);
            nameItem->setData(Qt::UserRole, id);      // service_volunteers.id
            nameItem->setData(Qt::UserRole + 1, pid); // person id
            m_volunteerList->setItem(row, 0, nameItem);
            m_volunteerList->setItem(row, 1, new QTableWidgetItem(role));
            m_volunteerList->setItem(row, 2, new QTableWidgetItem(attended.contains(pid) ? "Attended" : "Scheduled"));
            row++;
        }
    }
}

void ServiceBuilderWidget::loadServiceDetails() {
    QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT title, leader, start_time FROM services WHERE date = ?");
    query.addBindValue(dateStr);

    if (query.exec() && query.next()) {
        m_serviceTitleEdit->setText(query.value(0).toString());
        m_leaderEdit->setEditText(query.value(1).toString());
        const QString startTime = query.value(2).toString();
        if (!startTime.isEmpty()) {
            m_serviceTimeEdit->setTime(QTime::fromString(startTime, "HH:mm"));
        }
    } else {
        m_serviceTitleEdit->clear();
        m_leaderEdit->setEditText(QString());
    }

    loadRunSheetFromDb(m_serviceDateEdit->date());
}

// Auto-save hook for title/leader (called on editingFinished). Silent: the
// run sheet already autosaves, so there is a single, implicit save action.
void ServiceBuilderWidget::saveServiceDetails() {
    if (m_readOnly) return;
    const QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");
    const QString title = m_serviceTitleEdit->text().trimmed();
    const QString leader = m_leaderEdit->currentText().trimmed();
    if (title.isEmpty() && leader.isEmpty()) return; // nothing meaningful to store

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("INSERT INTO services (date, title, leader, run_sheet, start_time) VALUES (?, ?, ?, ?, ?) "
                  "ON CONFLICT(date) DO UPDATE SET title = excluded.title, leader = excluded.leader");
    query.addBindValue(dateStr);
    query.addBindValue(title);
    query.addBindValue(leader);
    query.addBindValue(QString::fromUtf8(QJsonDocument(playlistToJson()).toJson(QJsonDocument::Compact)));
    query.addBindValue(m_serviceTimeEdit->time().toString("HH:mm"));
    if (!query.exec()) {
        qWarning() << "Failed to auto-save service details:" << query.lastError().text();
    }
}

void ServiceBuilderWidget::onSaveTemplate() {
    if (m_playlist->count() == 0) {
        QMessageBox::warning(this, "No Layout", "The run sheet is empty. Add elements before saving a template.");
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save as Template",
                                               "Template name (e.g. Standard Sunday):", QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery exists(db);
    exists.prepare("SELECT 1 FROM service_templates WHERE name = ?");
    exists.addBindValue(name);
    if (exists.exec() && exists.next()) {
        auto res = QMessageBox::question(this, "Template Exists",
                                         QString("A template named '%1' already exists. Overwrite it?").arg(name),
                                         QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO service_templates (name, run_sheet) VALUES (?, ?) "
              "ON CONFLICT(name) DO UPDATE SET run_sheet = excluded.run_sheet");
    q.addBindValue(name);
    q.addBindValue(QString::fromUtf8(QJsonDocument(playlistToJson()).toJson(QJsonDocument::Compact)));
    if (q.exec()) {
        QMessageBox::information(this, "Success", QString("Template '%1' saved.").arg(name));
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to save template: " + q.lastError().text());
    }
}

void ServiceBuilderWidget::onApplyTemplate() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    if (!q.exec("SELECT name FROM service_templates ORDER BY name")) {
        QMessageBox::critical(this, "Database Error", "Failed to list templates: " + q.lastError().text());
        return;
    }

    QStringList names;
    while (q.next()) names.append(q.value(0).toString());
    if (names.isEmpty()) {
        QMessageBox::information(this, "No Templates", "No saved templates yet. Build a run sheet and use 'Save as Template'.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Apply Run-Sheet Template");
    dlg.resize(380, 300);
    auto* lay = new QVBoxLayout(&dlg);

    QListWidget* list = new QListWidget(&dlg);
    list->addItems(names);
    lay->addWidget(list, 1);

    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* btnApply = new QPushButton("Apply", &dlg);
    btnApply->setObjectName("primaryBtn");
    btns->addWidget(btnApply);
    QPushButton* btnDelete = new QPushButton("Delete Template", &dlg);
    btnDelete->setObjectName("dangerBtn");
    btns->addWidget(btnDelete);
    btns->addStretch();
    QPushButton* btnClose = new QPushButton("Close", &dlg);
    btnClose->setObjectName("secondaryBtn");
    btns->addWidget(btnClose);
    lay->addLayout(btns);

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);

    // Apply: load the template's run sheet into the current date (autosaves).
    connect(btnApply, &QPushButton::clicked, &dlg, [&]() {
        QListWidgetItem* item = list->currentItem();
        if (!item) return;
        QSqlQuery fetch(db);
        fetch.prepare("SELECT run_sheet FROM service_templates WHERE name = ?");
        fetch.addBindValue(item->text());
        if (fetch.exec() && fetch.next()) {
            const QJsonDocument doc = QJsonDocument::fromJson(fetch.value(0).toString().toUtf8());
            if (doc.isArray()) loadPlaylistFromJson(doc.array());
            dlg.accept();
        }
    });

    // Delete: remove the selected template and refresh the list.
    connect(btnDelete, &QPushButton::clicked, &dlg, [&]() {
        QListWidgetItem* item = list->currentItem();
        if (!item) return;
        QSqlQuery del(db);
        del.prepare("DELETE FROM service_templates WHERE name = ?");
        del.addBindValue(item->text());
        if (del.exec()) {
            delete list->takeItem(list->row(item));
        }
    });

    dlg.exec();
}

void ServiceBuilderWidget::onServiceDateChanged(const QDate& date) {
    Q_UNUSED(date);
    loadVolunteers();
    loadServiceDetails();
}

void ServiceBuilderWidget::onUnscheduleVolunteer() {
    const int row = m_volunteerList->currentRow();
    if (row < 0 || !m_volunteerList->item(row, 0)) {
        QMessageBox::warning(this, "Notice", "Select a scheduled volunteer to unassign.");
        return;
    }

    int id = m_volunteerList->item(row, 0)->data(Qt::UserRole).toInt();
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("DELETE FROM service_volunteers WHERE id = ?");
    query.addBindValue(id);

    if (query.exec()) {
        loadVolunteers();
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to delete assignment: " + query.lastError().text());
    }
}

void ServiceBuilderWidget::onScheduleVolunteer() {
    QDialog dlg(this);
    dlg.setWindowTitle("Smart Volunteer Assignment & Rostering");
    dlg.resize(380, 240);

    QFormLayout* layout = new QFormLayout(&dlg);
    layout->setSpacing(12);

    QSqlDatabase db = DatabaseManager::instance().database();

    QComboBox* roleCombo = new QComboBox(&dlg);
    roleCombo->addItems({"Preacher", "Worship Leader", "Acoustic Guitar", "Electric Guitar", "Vocalist", "Bass Guitar", "Drums", "Nursery Care", "Audio Engineer", "Slide Operator", "Usher"});
    layout->addRow("Assign Role:", roleCombo);

    QComboBox* teamFilterCombo = new QComboBox(&dlg);
    teamFilterCombo->addItem("â€” Relevant Ministry Team (Auto) â€”", 0);
    teamFilterCombo->addItem("All Church Members", -1);
    QSqlQuery qGroups("SELECT id, name FROM groups WHERE is_archived = 0 ORDER BY name", db);
    while (qGroups.next()) {
        teamFilterCombo->addItem(qGroups.value(1).toString(), qGroups.value(0).toInt());
    }
    layout->addRow("Ministry Team:", teamFilterCombo);

    QComboBox* personCombo = new QComboBox(&dlg);

    // Rank candidates for the selected role & team: skill fit dominates, team load
    // and recent service frequency are penalized (burnout guard). Falls back
    // to group roster / alphabetical list with availability tags.
    auto populatePeople = [&]() {
        const QString role = roleCombo->currentText();
        int selectedTeamId = teamFilterCombo->currentData().toInt();
        personCombo->clear();

        // Resolve auto-matching group if index == 0
        int filterGroupId = selectedTeamId;
        if (selectedTeamId == 0) {
            QString keyword;
            if (role.contains("Audio", Qt::CaseInsensitive) || role.contains("Slide", Qt::CaseInsensitive)) {
                keyword = "%Tech%";
            } else if (role.contains("Worship", Qt::CaseInsensitive) || role.contains("Guitar", Qt::CaseInsensitive) ||
                       role.contains("Vocalist", Qt::CaseInsensitive) || role.contains("Drums", Qt::CaseInsensitive) ||
                       role.contains("Bass", Qt::CaseInsensitive)) {
                keyword = "%Worship%";
            } else if (role.contains("Nursery", Qt::CaseInsensitive)) {
                keyword = "%Children%";
            } else if (role.contains("Usher", Qt::CaseInsensitive)) {
                keyword = "%Hospitality%";
            }

            if (!keyword.isEmpty()) {
                QSqlQuery qFind(db);
                qFind.prepare("SELECT id FROM groups WHERE name LIKE ? OR category LIKE ? LIMIT 1");
                qFind.addBindValue(keyword);
                qFind.addBindValue(keyword);
                if (qFind.exec() && qFind.next()) {
                    filterGroupId = qFind.value(0).toInt();
                }
            }
        }

        QSet<int> teamMemberIds;
        bool filterByTeam = (filterGroupId > 0);
        if (filterByTeam) {
            QSqlQuery qTm(db);
            qTm.prepare("SELECT source_id FROM entity_relationships WHERE target_type = 'Group' AND target_id = ? AND source_type = 'Person'");
            qTm.addBindValue(filterGroupId);
            if (qTm.exec()) {
                while (qTm.next()) teamMemberIds.insert(qTm.value(0).toInt());
            }
        }

        const QList<MatchResult> ranked = MinistryRecommender::rankByRole(db, role);
        if (ranked.isEmpty()) {
            QSqlQuery qPeople(db);
            if (filterByTeam && !teamMemberIds.isEmpty()) {
                qPeople.prepare(
                    "SELECT p.id, p.first_name || ' ' || p.last_name, "
                    "       (SELECT COUNT(*) FROM entity_relationships er WHERE er.source_type = 'Person' AND er.source_id = p.id AND er.target_type = 'Group') AS group_count, "
                    "       (SELECT COUNT(*) FROM service_volunteers sv WHERE sv.person_id = p.id AND sv.service_date >= date('now', '-30 days')) AS recent_serves, "
                    "       er2.relationship_type "
                    "FROM people p "
                    "JOIN entity_relationships er2 ON er2.source_id = p.id AND er2.target_type = 'Group' AND er2.target_id = ? "
                    "ORDER BY (er2.relationship_type LIKE '%Leader%') DESC, last_name, first_name");
                qPeople.addBindValue(filterGroupId);
            } else {
                qPeople.prepare(
                    "SELECT p.id, p.first_name || ' ' || p.last_name, "
                    "       (SELECT COUNT(*) FROM entity_relationships er WHERE er.source_type = 'Person' AND er.source_id = p.id AND er.target_type = 'Group') AS group_count, "
                    "       (SELECT COUNT(*) FROM service_volunteers sv WHERE sv.person_id = p.id AND sv.service_date >= date('now', '-30 days')) AS recent_serves, "
                    "       '' "
                    "FROM people p ORDER BY last_name, first_name");
            }
            if (qPeople.exec()) {
                while (qPeople.next()) {
                    int pid = qPeople.value(0).toInt();
                    QString name = qPeople.value(1).toString();
                    int groupCount = qPeople.value(2).toInt();
                    int recentServes = qPeople.value(3).toInt();
                    QString groupRole = qPeople.value(4).toString();

                    QString tag = name;
                    if (!groupRole.isEmpty()) {
                        tag += QString(" [%1]").arg(groupRole);
                    }
                    if (recentServes >= 3) {
                        tag += QString(" (Served %1x past 30 days)").arg(recentServes);
                    } else if (groupCount >= 3) {
                        tag += QString(" (In %1 active teams)").arg(groupCount);
                    } else {
                        tag += " (Available)";
                    }
                    personCombo->addItem(tag, pid);
                }
            }
            if (personCombo->count() == 0 && filterByTeam) {
                personCombo->addItem("â€” No members enrolled in this ministry team yet â€”", 0);
            }
            return;
        }

        for (const MatchResult& r : ranked) {
            if (filterByTeam && !teamMemberIds.contains(r.personId)) continue;
            QString label = QString("%1  [%2%]").arg(r.name).arg(r.score);
            if (!r.reasons.isEmpty()) label += "  Â· " + r.reasons.join(" Â· ");
            personCombo->addItem(label, r.personId);
        }

        if (personCombo->count() == 0 && filterByTeam) {
            personCombo->addItem("â€” No team members match this specific role (Switch to All Church Members) â€”", 0);
        }
    };
    connect(roleCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) { populatePeople(); });
    connect(teamFilterCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) { populatePeople(); });
    populatePeople();
    layout->addRow("Select Volunteer:", personCombo);

    QLabel* smartHint = new QLabel("Smart Suggestions: ranked by skill fit (match %), penalized for team load and repeated service â€” best fit first.", &dlg);
    smartHint->setWordWrap(true);
    smartHint->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    layout->addRow(smartHint);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        int personId = personCombo->currentData().toInt();
        QString role = roleCombo->currentText();
        QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");

        // 1. Conflict check for same date
        QSqlQuery qConflict(db);
        qConflict.prepare("SELECT role FROM service_volunteers WHERE person_id = ? AND service_date = ?");
        qConflict.addBindValue(personId);
        qConflict.addBindValue(dateStr);

        if (qConflict.exec() && qConflict.next()) {
            QString existingRole = qConflict.value(0).toString();
            auto res = QMessageBox::question(this, "Scheduling Conflict Warning",
                                             QString("This volunteer is already scheduled as [%1] on %2.\nSchedule anyway?").arg(existingRole, dateStr),
                                             QMessageBox::Yes | QMessageBox::No);
            if (res == QMessageBox::No) {
                return;
            }
        }

        // 2. Family schedule conflict check (spouses serving on same date)
        QSqlQuery qFamily(db);
        qFamily.prepare(
            "SELECT p.first_name || ' ' || p.last_name, sv.role "
            "FROM service_volunteers sv "
            "JOIN people p ON sv.person_id = p.id "
            "WHERE sv.service_date = ? AND p.household_id = (SELECT household_id FROM people WHERE id = ?) AND p.id != ?");
        qFamily.addBindValue(dateStr);
        qFamily.addBindValue(personId);
        qFamily.addBindValue(personId);
        if (qFamily.exec() && qFamily.next()) {
            QString familyMemberName = qFamily.value(0).toString();
            QString familyMemberRole = qFamily.value(1).toString();
            QMessageBox::information(this, "Family Schedule Match",
                                     QString("â„¹ï¸ Family Note: Household member %1 is also serving as [%2] on %3.").arg(familyMemberName, familyMemberRole, dateStr));
        }

        // Insert assignment
        QSqlQuery qInsert(db);
        qInsert.prepare("INSERT INTO service_volunteers (service_date, person_id, role) VALUES (?, ?, ?)");
        qInsert.addBindValue(dateStr);
        qInsert.addBindValue(personId);
        qInsert.addBindValue(role);

        if (qInsert.exec()) {
            loadVolunteers();
        } else {
            QMessageBox::critical(this, "Database Error", "Failed to assign volunteer: " + qInsert.lastError().text());
        }
    }
}

void ServiceBuilderWidget::updateDurationCalculator() {
    int totalMins = 0;
    for (int i = 0; i < m_playlist->count(); ++i) {
        QListWidgetItem* item = m_playlist->item(i);

        // Re-apply the type tint (drop-in items inherit the library color).
        QColor tint = tintForType(item->data(Qt::UserRole).toString());
        tint.setAlpha(60);
        item->setBackground(tint);

        // Prefer the explicit duration carried on the item; fall back to
        // parsing "(Nm)" out of the title for items from older save files.
        int mins = item->data(Qt::UserRole + 1).toInt();
        if (mins <= 0) {
            const QString text = item->text();
            const int openParen = text.lastIndexOf('(');
            const int closeParen = text.lastIndexOf('m');
            if (openParen >= 0 && closeParen > openParen) {
                bool ok = false;
                mins = text.mid(openParen + 1, closeParen - openParen - 1).toInt(&ok);
                if (!ok) mins = 0;
            }
        }
        totalMins += qMax(0, mins);
    }

    // Overrun guard: dynamically loaded from Settings targetServiceDuration
    QSettings s("OpenEkklesia", "CHMS");
    const int targetDuration = s.value("targetServiceDuration", 75).toInt();
    const int maxDuration = targetDuration + 15;
    const int overrun = totalMins - maxDuration;
    const ThemeTokens tokDur = ThemeManager::instance().currentTokens();
    if (overrun > 0) {
        m_lblTotalDuration->setText(QString("Total: %1 mins â€” overruns by %2 min (limit %3)")
                                        .arg(totalMins)
                                        .arg(overrun)
                                        .arg(maxDuration));
        m_lblTotalDuration->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokDur.accentDanger.name()));
    } else if (totalMins > targetDuration) {
        m_lblTotalDuration->setText(QString("Total: %1 mins (aim â‰¤ %2 min)")
                                        .arg(totalMins)
                                        .arg(targetDuration));
        m_lblTotalDuration->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokDur.accentWarning.name()));
    } else {
        m_lblTotalDuration->setText(QString("Total: %1 mins").arg(totalMins));
        m_lblTotalDuration->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokDur.accentSuccess.name()));
    }

    updateTimeline();
}

// Computes each run-sheet item's start time from the service start time and
// cumulative durations, and shows the timeline under the playlist.
void ServiceBuilderWidget::updateTimeline() {
    if (!m_lblTimeline) return;
    if (m_playlist->count() == 0) {
        m_lblTimeline->setText(QString("â–¶ Starts %1 â€” add elements to see the timeline.")
                                   .arg(m_serviceTimeEdit->time().toString("h:mm AP")));
        return;
    }

    const int kMaxLines = 12;
    QTime cursor = m_serviceTimeEdit->time();
    QStringList lines;
    for (int i = 0; i < m_playlist->count() && lines.size() < kMaxLines; ++i) {
        QListWidgetItem* item = m_playlist->item(i);
        const int mins = qMax(0, item->data(Qt::UserRole + 1).toInt());
        lines << QString("%1  %2").arg(cursor.toString("h:mm AP"), item->text());
        cursor = cursor.addSecs(mins * 60);
    }
    if (m_playlist->count() > kMaxLines) {
        lines << QString("â€¦ +%1 more").arg(m_playlist->count() - kMaxLines);
    }
    m_lblTimeline->setText("â–¶ " + lines.join("\n"));
}

QStringList ServiceBuilderWidget::collectCoverageIssues() const {
    QStringList issues;
    const int itemCount = m_playlist->count();
    const int volCount = m_volunteerList->rowCount();

    // 1. Sermon / message present?
    bool hasSermon = false;
    for (int i = 0; i < itemCount; ++i) {
        if (m_playlist->item(i)->text().contains("Sermon", Qt::CaseInsensitive)) {
            hasSermon = true;
            break;
        }
    }
    if (itemCount > 0 && !hasSermon) issues << "No sermon / message element in the run sheet.";

    // 2. Core roles assigned? (Volunteer table: col 1 = role.)
    QSet<QString> assignedRoles;
    for (int i = 0; i < volCount; ++i) {
        if (m_volunteerList->item(i, 1)) {
            assignedRoles.insert(m_volunteerList->item(i, 1)->text());
        }
    }
    for (const QString& core : {QStringLiteral("Preacher"), QStringLiteral("Worship Leader")}) {
        if (!assignedRoles.contains(core)) issues << QString("No %1 assigned.").arg(core);
    }

    // 3. Run sheet / volunteers mismatch.
    if (itemCount == 0 && volCount > 0) issues << "Run sheet is empty but volunteers are scheduled.";
    if (itemCount > 0 && volCount == 0) issues << "No volunteers scheduled for this service.";

    // 4. Overrun beyond the 90-minute guard.
    int totalMins = 0;
    for (int i = 0; i < itemCount; ++i) {
        totalMins += qMax(0, m_playlist->item(i)->data(Qt::UserRole + 1).toInt());
    }
    if (totalMins > 90) issues << QString("Run sheet overruns by %1 min (90-min guard).").arg(totalMins - 90);

    return issues;
}

void ServiceBuilderWidget::onCoverageCheck() {
    const QStringList issues = collectCoverageIssues();
    if (issues.isEmpty()) {
        QMessageBox::information(this, "Coverage Check", "No coverage issues â€” the run sheet looks complete.");
    } else {
        QMessageBox::warning(this, "Coverage Check",
                             "Coverage issues found:\n\nâ€¢ " + issues.join("\nâ€¢ "));
    }
}

// Records every scheduled volunteer for the selected date as attended.
// Dedup-safe: people already marked for that service are skipped.
int ServiceBuilderWidget::markVolunteersAttended() {
    QSqlDatabase db = DatabaseManager::instance().database();
    const QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");

    // Ensure a services row exists for the date (created by autosave, but a
    // volunteer-only service may not have one yet).
    QSqlQuery ensure(db);
    ensure.prepare("INSERT INTO services (date, run_sheet) VALUES (?, '[]') "
                   "ON CONFLICT(date) DO NOTHING");
    ensure.addBindValue(dateStr);
    if (!ensure.exec()) return 0;

    QSqlQuery idq(db);
    idq.prepare("SELECT id FROM services WHERE date = ?");
    idq.addBindValue(dateStr);
    if (!(idq.exec() && idq.next())) return 0;
    const int serviceId = idq.value(0).toInt();

    QSqlQuery vols(db);
    vols.prepare("SELECT person_id FROM service_volunteers WHERE service_date = ?");
    vols.addBindValue(dateStr);
    if (!vols.exec()) return 0;

    int marked = 0;
    while (vols.next()) {
        const int pid = vols.value(0).toInt();
        QSqlQuery dup(db);
        dup.prepare("SELECT 1 FROM attendance WHERE service_id = ? AND person_id = ?");
        dup.addBindValue(serviceId);
        dup.addBindValue(pid);
        if (dup.exec() && dup.next()) continue; // already attended

        QSqlQuery ins(db);
        ins.prepare("INSERT INTO attendance (service_id, person_id, attended) VALUES (?, ?, 1)");
        ins.addBindValue(serviceId);
        ins.addBindValue(pid);
        if (ins.exec()) marked++;
    }
    return marked;
}

void ServiceBuilderWidget::onMarkAttended() {
    if (m_volunteerList->rowCount() == 0) {
        QMessageBox::information(this, "No Volunteers",
                                 "No volunteers are scheduled for this date to mark as attended.");
        return;
    }
    const int marked = markVolunteersAttended();
    QMessageBox::information(this, "Attendance Recorded",
                             QString("Marked %1 volunteer(s) as attended for %2.")
                                 .arg(marked)
                                 .arg(m_serviceDateEdit->date().toString("yyyy-MM-dd")));
}

// Mirrors Shepherd's iCal export: one VEVENT per volunteer with a reminder
// alarm, using datetime (not all-day) events at the service start time.
QString ServiceBuilderWidget::volunteerIcal(const QDate& date, const QTime& startTime,
                                             const QList<QPair<QString, QString>>& roleNames,
                                             int durationMinutes) {
    return ICalGenerator::volunteerServiceIcal(date, startTime, roleNames, durationMinutes);
}

void ServiceBuilderWidget::onExportIcal() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QList<QPair<QString, QString>> roleNames;
    QSqlQuery q(db);
    q.prepare("SELECT sv.role, p.first_name || ' ' || p.last_name "
              "FROM service_volunteers sv "
              "JOIN people p ON p.id = sv.person_id "
              "WHERE sv.service_date = ? ORDER BY sv.role");
    q.addBindValue(m_serviceDateEdit->date().toString("yyyy-MM-dd"));
    if (q.exec()) {
        while (q.next()) roleNames.append({q.value(0).toString(), q.value(1).toString()});
    }
    if (roleNames.isEmpty()) {
        QMessageBox::information(this, "No Volunteers",
                                 "No volunteers are scheduled for this date to export.");
        return;
    }

    const QString defaultFile = QString("Volunteer_Schedule_%1.ics")
                                    .arg(m_serviceDateEdit->date().toString("yyyy-MM-dd"));
    QString fileName = QFileDialog::getSaveFileName(this, "Export Volunteer Schedule (.ics)",
                                                    defaultFile, "iCalendar Files (*.ics)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "File Error", "Could not create calendar file: " + file.errorString());
        return;
    }
    QTextStream out(&file);
    int totalMins = 0;
    for (int i = 0; i < m_playlist->count(); ++i) {
        QListWidgetItem* item = m_playlist->item(i);
        const int d = item->data(Qt::UserRole + 1).toInt();
        if (d > 0) totalMins += d;
    }
    if (totalMins <= 0) totalMins = 120;
    out << volunteerIcal(m_serviceDateEdit->date(), m_serviceTimeEdit->time(), roleNames, totalMins);
    file.close();

    QMessageBox::information(this, "Calendar Exported",
                             QString("Exported %1 volunteer event(s).\n\nOpen in Google Calendar, Apple Calendar, Outlook, or Thunderbird:\n%2")
                                 .arg(roleNames.size()).arg(fileName));
}

// Aggregates element reuse across every saved service run sheet.
QList<ElementUsage> ServiceBuilderWidget::elementUsageStats(const QSqlDatabase& db, int limit) {
    QMap<QString, ElementUsage> byKey;
    QSqlQuery q(db);
    if (!q.exec("SELECT date, run_sheet FROM services "
                "WHERE run_sheet IS NOT NULL AND run_sheet != ''")) {
        return {};
    }
    while (q.next()) {
        const QString date = q.value(0).toString();
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(1).toString().toUtf8());
        if (!doc.isArray()) continue;
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            const QJsonObject obj = v.toObject();
            const QString title = obj["title"].toString();
            const QString type = obj["type"].toString();
            if (title.isEmpty()) continue;
            ElementUsage& u = byKey[title.toLower()];
            u.name = title;
            u.type = type;
            u.count++;
            if (date > u.lastUsedDate) u.lastUsedDate = date;
        }
    }

    QList<ElementUsage> out = byKey.values();
    std::sort(out.begin(), out.end(), [](const ElementUsage& a, const ElementUsage& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.name < b.name;
    });
    if (out.size() > qMax(0, limit)) out.resize(qMax(0, limit));
    return out;
}

void ServiceBuilderWidget::onElementUsage() {
    const QList<ElementUsage> stats = elementUsageStats(DatabaseManager::instance().database(), 100);
    if (stats.isEmpty()) {
        QMessageBox::information(this, "Element Usage",
                                 "No run sheets saved yet. Build and save a service arrangement first.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Element Usage Stats");
    dlg.resize(620, 440);
    auto* lay = new QVBoxLayout(&dlg);

    QLabel* note = new QLabel("How often each element appears across saved service run sheets â€” "
                              "use the most-reused items to guide worship planning.", &dlg);
    note->setWordWrap(true);
    lay->addWidget(note);

    auto* table = new QTableWidget(stats.size(), 5, &dlg);
    table->setHorizontalHeaderLabels({"#", "Element", "Type", "Times Used", "Last Used"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    for (int i = 0; i < stats.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        table->setItem(i, 1, new QTableWidgetItem(stats[i].name));
        table->setItem(i, 2, new QTableWidgetItem(stats[i].type));
        table->setItem(i, 3, new QTableWidgetItem(QString::number(stats[i].count)));
        table->setItem(i, 4, new QTableWidgetItem(stats[i].lastUsedDate));
    }
    lay->addWidget(table, 1);

    QPushButton* btnClose = new QPushButton("Close", &dlg);
    btnClose->setObjectName("secondaryBtn");
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);

    dlg.exec();
}

void ServiceBuilderWidget::onExportRunSheet() {
    if (m_playlist->count() == 0) {
        QMessageBox::warning(this, "Notice", "The Sunday Service Run Sheet is currently empty.");
        return;
    }

    // Coverage guard: warn before export; the user can still proceed.
    const QStringList issues = collectCoverageIssues();
    if (!issues.isEmpty()) {
        auto res = QMessageBox::question(this, "Coverage Check",
                                         "Coverage issues found:\n\nâ€¢ " + issues.join("\nâ€¢ ") +
                                         "\n\nExport anyway?",
                                         QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;
    }

    QString savePath = QFileDialog::getSaveFileName(this, "Export Run Sheet PDF",
                                                     QString("Service_Run_Sheet_%1.pdf").arg(m_serviceDateEdit->date().toString("yyyy-MM-dd")),
                                                     "PDF Documents (*.pdf)");
    if (savePath.isEmpty()) return;

    QPdfWriter pdfWriter(savePath);
    pdfWriter.setPageSize(QPageSize(QPageSize::Letter));

    const QString dateStr = m_serviceDateEdit->date().toString("MMMM d, yyyy");
    const QString title = m_serviceTitleEdit->text().trimmed().isEmpty()
        ? "Sunday Worship Service"
        : m_serviceTitleEdit->text().trimmed();
    const QString leader = m_leaderEdit->currentText().trimmed().isEmpty() ? "Unassigned" : m_leaderEdit->currentText().trimmed();
    const QString timeStr = m_serviceTimeEdit->time().toString("h:mm AP");

    // Church profile header (logo, name, address) via the shared ChurchProfile
    // helper â€” no Tax ID on run sheets (team decision Q3).
    const QString churchHeaderHtml = ChurchProfile::renderHtmlHeader(
        QString("%1 Â· %2 Â· Starts %3").arg(title, dateStr, timeStr), QString(), false);

    // Run sheet with per-item start times computed from the service start.
    QTime cursor = m_serviceTimeEdit->time();
    QString itemsHtml = "<ol style='font-size: 14px;'>";
    for (int i = 0; i < m_playlist->count(); ++i) {
        QListWidgetItem* item = m_playlist->item(i);
        const int mins = qMax(0, item->data(Qt::UserRole + 1).toInt());
        itemsHtml += QString("<li><b>%1</b> â€” %2</li>").arg(cursor.toString("h:mm AP"), item->text());
        cursor = cursor.addSecs(mins * 60);
    }
    itemsHtml += "</ol>";

    // Volunteer roster with a contact strip (phone / email) so the media
    // team can reach each other.
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery qVol(db);
    qVol.prepare("SELECT sv.role, p.first_name || ' ' || p.last_name, "
                 "COALESCE(p.phone_number, ''), COALESCE(p.email, '') "
                 "FROM service_volunteers sv "
                 "JOIN people p ON p.id = sv.person_id "
                 "WHERE sv.service_date = ? ORDER BY sv.role");
    qVol.addBindValue(m_serviceDateEdit->date().toString("yyyy-MM-dd"));
    QString volHtml = "<table style='width: 100%; border-collapse: collapse; font-size: 13px;'>"
                      "<tr style='background: #eef1f8;'><th align='left'>Role</th><th align='left'>Name</th>"
                      "<th align='left'>Phone</th><th align='left'>Email</th></tr>";
    if (qVol.exec()) {
        while (qVol.next()) {
            volHtml += QString("<tr><td><b>%1</b></td><td>%2</td><td>%3</td><td>%4</td></tr>")
                           .arg(qVol.value(0).toString(), qVol.value(1).toString(),
                                qVol.value(2).toString(), qVol.value(3).toString());
        }
    }
    volHtml += "</table>";

    QString html = QString(
        "<html><body style='font-family: Arial, sans-serif; margin: 30px;'>"
        "%1" // church branding header (logo, name, address, report title)
        "<p>Service Leader: %2</p>"
        "<hr>"
        "<h3>Order of Service / Liturgy Run Sheet</h3>%3"
        "<hr>"
        "<h3>Assigned Volunteers & Roster (contact strip)</h3>%4"
        "<br><p style='font-size: 11px; color: #666;'>Generated via OpenEkklesia ChMS Suite</p>"
        "</body></html>"
    ).arg(churchHeaderHtml, leader, itemsHtml, volHtml);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&pdfWriter);

    QFileInfo doneInfo(savePath);
    if (!doneInfo.exists() || doneInfo.size() == 0) {
        QMessageBox::warning(this, "Export Failed",
            "Could not write run-sheet PDF (check disk space and permissions).");
        return;
    }
    QMessageBox::information(this, "Export Success", "Service Run Sheet PDF exported successfully!");
}
