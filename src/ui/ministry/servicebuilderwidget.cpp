#include "servicebuilderwidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
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
#include <QTextStream>
#include <QDateTime>
#include <QTableWidget>
#include <QHeaderView>
#include "../database/databasemanager.h"
#include "ministrymatch.h"

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

static QString typeEmoji(const QString& type) {
    if (type == "Song") return QStringLiteral("🎵");
    if (type == "Scripture") return QStringLiteral("📖");
    if (type == "Media") return QStringLiteral("🎬");
    return QStringLiteral("💬");
}

// Composes the display text ("🎵 Song: Amazing Grace (4m)") from clean parts.
static QString libraryDisplayName(const QString& type, const QString& name, int mins) {
    return QString("%1 %2: %3 (%4m)").arg(typeEmoji(type), type, name).arg(mins);
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
    m_btnSaveServiceDetails->setEnabled(canEdit);
    m_btnAddElement->setEnabled(canEdit);
    m_btnRemoveElement->setEnabled(canEdit);
    m_btnSaveTemplate->setEnabled(canEdit);
    m_btnApplyTemplate->setEnabled(canEdit);
    m_btnMarkAttended->setEnabled(canEdit);
    m_playlist->setAcceptDrops(canEdit);
    m_playlist->setDragEnabled(canEdit);
    m_libraryList->setDragEnabled(canEdit);

    const QString tip = m_readOnly
        ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
        : "";
    for (QPushButton* b : {m_btnRemove, m_btnClear, m_btnSave, m_btnSchedule, m_btnUnschedule,
                           m_btnSaveServiceDetails, m_btnAddElement, m_btnRemoveElement,
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

    m_libraryList = new QListWidget(this);
    m_libraryList->setObjectName("serviceLibraryList");
    m_libraryList->setDragEnabled(true);
    m_libraryList->setDragDropMode(QAbstractItemView::DragOnly);
    m_libraryList->setAlternatingRowColors(true);
    libraryLayout->addWidget(m_libraryList);

    QLabel* dragTip = new QLabel("Tip: Drag items from here to the right panel. Double-click to edit.", this);
    dragTip->setObjectName("dragTip");
    dragTip->setWordWrap(true);
    libraryLayout->addWidget(dragTip);

    QHBoxLayout* libActions = new QHBoxLayout();
    m_btnAddElement = new QPushButton("➕ Add Element", this);
    m_btnAddElement->setObjectName("secondaryBtn");
    m_btnAddElement->setToolTip("Add a custom element to the persistent library");
    connect(m_btnAddElement, &QPushButton::clicked, this, &ServiceBuilderWidget::onAddElement);
    libActions->addWidget(m_btnAddElement);

    m_btnUsageStats = new QPushButton("📊 Usage Stats", this);
    m_btnUsageStats->setObjectName("secondaryBtn");
    m_btnUsageStats->setToolTip("Which elements get reused most across saved run sheets");
    connect(m_btnUsageStats, &QPushButton::clicked, this, &ServiceBuilderWidget::onElementUsage);
    libActions->addWidget(m_btnUsageStats);

    m_btnRemoveElement = new QPushButton("✖ Remove Element", this);
    m_btnRemoveElement->setObjectName("ghostDangerBtn");
    m_btnRemoveElement->setToolTip("Remove the selected library element");
    connect(m_btnRemoveElement, &QPushButton::clicked, this, &ServiceBuilderWidget::onRemoveElement);
    libActions->addWidget(m_btnRemoveElement);
    libraryLayout->addLayout(libActions);

    // Color-blind-safe legend: icons (not color) identify the type; the tint
    // is only a hint, so the run sheet stays readable in grayscale.
    m_lblTypeLegend = new QLabel("Legend: 🎵 Song · 📖 Scripture · 🎬 Media · 💬 Text — icons identify the type, color is only a hint (color-blind safe)", this);
    m_lblTypeLegend->setObjectName("serviceTypeLegend");
    m_lblTypeLegend->setWordWrap(true);
    m_lblTypeLegend->setStyleSheet("font-size: 11px; color: #a6adc8;");
    libraryLayout->addWidget(m_lblTypeLegend);

    mainLayout->addWidget(libraryPanel, 2);

    // Right Panel (Sunday Service Run Sheet)
    QWidget* playlistPanel = new QWidget(this);
    QVBoxLayout* playlistLayout = new QVBoxLayout(playlistPanel);
    playlistLayout->setContentsMargins(0, 0, 0, 0);
    playlistLayout->setSpacing(12);

    QHBoxLayout* headerRow = new QHBoxLayout();
    QLabel* playlistLabel = new QLabel("Sunday Service Run Sheet", this);
    playlistLabel->setObjectName("detailHeader");
    m_lblTotalDuration = new QLabel("⏱️ Total Service Duration: 0 mins", this);
    m_lblTotalDuration->setObjectName("serviceDurationLabel");
    m_lblTotalDuration->setStyleSheet("font-size: 13px; font-weight: bold; color: #a6e3a1;");
    headerRow->addWidget(playlistLabel);
    headerRow->addStretch();
    headerRow->addWidget(m_lblTotalDuration);
    playlistLayout->addLayout(headerRow);

    m_playlist = new QListWidget(this);
    m_playlist->setObjectName("servicePlaylist");
    m_playlist->setAcceptDrops(true);
    m_playlist->setDragEnabled(true);
    m_playlist->setDragDropMode(QAbstractItemView::DragDrop);
    m_playlist->setDefaultDropAction(Qt::CopyAction);
    m_playlist->setAlternatingRowColors(true);
    connect(m_playlist->model(), &QAbstractItemModel::rowsInserted, this, &ServiceBuilderWidget::updateDurationCalculator);
    connect(m_playlist->model(), &QAbstractItemModel::rowsRemoved, this, &ServiceBuilderWidget::updateDurationCalculator);
    playlistLayout->addWidget(m_playlist);

    m_lblTimeline = new QLabel(this);
    m_lblTimeline->setObjectName("serviceTimelineLabel");
    m_lblTimeline->setWordWrap(true);
    m_lblTimeline->setStyleSheet("font-size: 11px; color: #a6adc8;");
    playlistLayout->addWidget(m_lblTimeline);

    // Action buttons for Sunday Run Sheet
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    
    m_btnRemove = new QPushButton("Remove Item", this);
    m_btnRemove->setObjectName("dangerBtn");
    connect(m_btnRemove, &QPushButton::clicked, this, &ServiceBuilderWidget::onRemoveItem);
    actionsLayout->addWidget(m_btnRemove);

    m_btnClear = new QPushButton("Clear All", this);
    m_btnClear->setObjectName("secondaryBtn");
    connect(m_btnClear, &QPushButton::clicked, this, &ServiceBuilderWidget::onClearPlaylist);
    actionsLayout->addWidget(m_btnClear);

    m_btnCoverageCheck = new QPushButton("🔍 Coverage Check", this);
    m_btnCoverageCheck->setObjectName("secondaryBtn");
    m_btnCoverageCheck->setToolTip("Check for missing sermon, unassigned core roles, or run-sheet/volunteer mismatches");
    connect(m_btnCoverageCheck, &QPushButton::clicked, this, &ServiceBuilderWidget::onCoverageCheck);
    actionsLayout->addWidget(m_btnCoverageCheck);

    m_btnExportPDF = new QPushButton("📄 Export Run Sheet PDF", this);
    m_btnExportPDF->setObjectName("accentBtnCyan");
    connect(m_btnExportPDF, &QPushButton::clicked, this, &ServiceBuilderWidget::onExportRunSheet);
    actionsLayout->addWidget(m_btnExportPDF);

    actionsLayout->addStretch();

    m_btnSave = new QPushButton("Save Layout", this);
    m_btnSave->setObjectName("saveBtn");
    connect(m_btnSave, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveService);
    actionsLayout->addWidget(m_btnSave);

    m_btnSaveTemplate = new QPushButton("💾 Save as Template", this);
    m_btnSaveTemplate->setObjectName("secondaryBtn");
    m_btnSaveTemplate->setToolTip("Save the current run sheet as a named template");
    connect(m_btnSaveTemplate, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveTemplate);
    actionsLayout->addWidget(m_btnSaveTemplate);

    m_btnApplyTemplate = new QPushButton("📋 Apply Template", this);
    m_btnApplyTemplate->setObjectName("secondaryBtn");
    m_btnApplyTemplate->setToolTip("Load a saved run-sheet template into the current date");
    connect(m_btnApplyTemplate, &QPushButton::clicked, this, &ServiceBuilderWidget::onApplyTemplate);
    actionsLayout->addWidget(m_btnApplyTemplate);

    m_btnLoad = new QPushButton("Load Layout", this);
    m_btnLoad->setObjectName("primaryBtn");
    connect(m_btnLoad, &QPushButton::clicked, this, &ServiceBuilderWidget::onLoadService);
    actionsLayout->addWidget(m_btnLoad);

    playlistLayout->addLayout(actionsLayout);

    mainLayout->addWidget(playlistPanel, 3);

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
    m_serviceTimeEdit->setTime(QTime(10, 0));
    m_serviceTimeEdit->setToolTip("Service starts at — the run sheet shows each item's start time");
    dateLayout->addWidget(new QLabel("at:", this));
    dateLayout->addWidget(m_serviceTimeEdit);
    volunteerLayout->addLayout(dateLayout);

    connect(m_serviceTimeEdit, &QTimeEdit::timeChanged, this, [this](const QTime&) {
        persistRunSheet();
        updateTimeline();
    });

    // Service plan details (title & leader) persisted to the services table
    QFormLayout* serviceForm = new QFormLayout();
    m_serviceTitleEdit = new QLineEdit(this);
    m_serviceTitleEdit->setObjectName("serviceTitleEdit");
    m_serviceTitleEdit->setPlaceholderText("e.g. Sunday Worship Service");
    serviceForm->addRow("Service Title:", m_serviceTitleEdit);

    m_leaderEdit = new QLineEdit(this);
    m_leaderEdit->setObjectName("serviceLeaderEdit");
    m_leaderEdit->setPlaceholderText("e.g. Pastor James Davis");
    serviceForm->addRow("Service Leader:", m_leaderEdit);
    volunteerLayout->addLayout(serviceForm);

    m_btnSaveServiceDetails = new QPushButton("💾 Save Service Details", this);
    m_btnSaveServiceDetails->setObjectName("saveBtn");
    connect(m_btnSaveServiceDetails, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveServiceDetails);
    volunteerLayout->addWidget(m_btnSaveServiceDetails);

    m_volunteerList = new QListWidget(this);
    m_volunteerList->setObjectName("serviceVolunteerList");
    m_volunteerList->setAlternatingRowColors(true);
    volunteerLayout->addWidget(m_volunteerList);

    QHBoxLayout* volActions = new QHBoxLayout();
    m_btnSchedule = new QPushButton("Assign Volunteer", this);
    m_btnSchedule->setObjectName("primaryBtn");
    connect(m_btnSchedule, &QPushButton::clicked, this, &ServiceBuilderWidget::onScheduleVolunteer);

    m_btnUnschedule = new QPushButton("Unassign", this);
    m_btnUnschedule->setObjectName("secondaryBtn");
    connect(m_btnUnschedule, &QPushButton::clicked, this, &ServiceBuilderWidget::onUnscheduleVolunteer);

    volActions->addWidget(m_btnSchedule);
    volActions->addWidget(m_btnUnschedule);
    volunteerLayout->addLayout(volActions);

    m_btnMarkAttended = new QPushButton("✅ Mark Volunteers Attended", this);
    m_btnMarkAttended->setObjectName("saveBtn");
    m_btnMarkAttended->setToolTip("Record all scheduled volunteers as attended for this service date");
    connect(m_btnMarkAttended, &QPushButton::clicked, this, &ServiceBuilderWidget::onMarkAttended);
    volunteerLayout->addWidget(m_btnMarkAttended);

    m_btnExportIcal = new QPushButton("📅 Export Volunteer iCal", this);
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

    connect(m_libraryList, &QListWidget::itemDoubleClicked, this, &ServiceBuilderWidget::onEditElement);
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
        item->setData(Qt::UserRole, type);
        item->setData(Qt::UserRole + 1, mins);
        item->setData(Qt::UserRole + 2, id);
        item->setData(Qt::ToolTipRole, QString("Type: %1 · %2 min · double-click to edit").arg(type).arg(mins));
        QColor tint = tintForType(type);
        tint.setAlpha(60);
        item->setBackground(tint);
    }
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
    QSqlQuery q(db);
    q.prepare("SELECT run_sheet FROM services WHERE date = ?");
    q.addBindValue(date.toString("yyyy-MM-dd"));
    if (q.exec() && q.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toString().toUtf8());
        if (doc.isArray()) {
            loadPlaylistFromJson(doc.array());
            return;
        }
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
    m_volunteerList->clear();
    QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT sv.id, p.first_name || ' ' || p.last_name, sv.role "
                  "FROM service_volunteers sv "
                  "JOIN people p ON sv.person_id = p.id "
                  "WHERE sv.service_date = ? "
                  "ORDER BY sv.role");
    query.addBindValue(dateStr);

    if (query.exec()) {
        while (query.next()) {
            int id = query.value(0).toInt();
            QString name = query.value(1).toString();
            QString role = query.value(2).toString();

            QListWidgetItem* item = new QListWidgetItem(QString("[%1] %2").arg(role, name), m_volunteerList);
            item->setData(Qt::UserRole, id);
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
        m_leaderEdit->setText(query.value(1).toString());
        const QString startTime = query.value(2).toString();
        if (!startTime.isEmpty()) {
            m_serviceTimeEdit->setTime(QTime::fromString(startTime, "HH:mm"));
        }
    } else {
        m_serviceTitleEdit->clear();
        m_leaderEdit->clear();
    }

    loadRunSheetFromDb(m_serviceDateEdit->date());
}

void ServiceBuilderWidget::onSaveServiceDetails() {
    QString dateStr = m_serviceDateEdit->date().toString("yyyy-MM-dd");
    QString title = m_serviceTitleEdit->text().trimmed();
    QString leader = m_leaderEdit->text().trimmed();

    if (title.isEmpty()) {
        QMessageBox::warning(this, "Missing Title", "Enter a service title before saving the service details.");
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("INSERT INTO services (date, title, leader) VALUES (?, ?, ?) "
                  "ON CONFLICT(date) DO UPDATE SET title = excluded.title, leader = excluded.leader");
    query.addBindValue(dateStr);
    query.addBindValue(title);
    query.addBindValue(leader);

    if (query.exec()) {
        QMessageBox::information(this, "Success", "Service details saved. It will now appear on the dashboard's upcoming services list.");
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to save service details: " + query.lastError().text());
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
    dlg.setWindowTitle("📋 Apply Run-Sheet Template");
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
    QListWidgetItem* item = m_volunteerList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Notice", "Select a scheduled volunteer to unassign.");
        return;
    }

    int id = item->data(Qt::UserRole).toInt();
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
    dlg.setWindowTitle("🤖 Smart Volunteer Assignment & Rostering");
    dlg.resize(380, 240);

    QFormLayout* layout = new QFormLayout(&dlg);
    layout->setSpacing(12);

    QSqlDatabase db = DatabaseManager::instance().database();

    QComboBox* roleCombo = new QComboBox(&dlg);
    roleCombo->addItems({"Preacher", "Worship Leader", "Acoustic Guitar", "Electric Guitar", "Vocalist", "Bass Guitar", "Drums", "Nursery Care", "Audio Engineer", "Slide Operator", "Usher"});
    layout->addRow("Assign Role:", roleCombo);

    QComboBox* personCombo = new QComboBox(&dlg);

    // Rank candidates for the selected role: skill fit dominates, team load
    // and recent service frequency are penalized (burnout guard). Falls back
    // to the plain alphabetical list with availability tags for roles the
    // ranker does not recognize.
    auto populatePeople = [&]() {
        const QString role = roleCombo->currentText();
        personCombo->clear();

        const QList<MatchResult> ranked = MinistryRecommender::rankByRole(db, role);
        if (ranked.isEmpty()) {
            QSqlQuery qPeople(
                "SELECT p.id, p.first_name || ' ' || p.last_name, "
                "       (SELECT COUNT(*) FROM entity_relationships er WHERE er.source_type = 'Person' AND er.source_id = p.id AND er.target_type = 'Group') AS group_count, "
                "       (SELECT COUNT(*) FROM service_volunteers sv WHERE sv.person_id = p.id AND sv.service_date >= date('now', '-30 days')) AS recent_serves "
                "FROM people p ORDER BY last_name, first_name", db);
            while (qPeople.next()) {
                int pid = qPeople.value(0).toInt();
                QString name = qPeople.value(1).toString();
                int groupCount = qPeople.value(2).toInt();
                int recentServes = qPeople.value(3).toInt();

                QString tag = name;
                if (recentServes >= 3) {
                    tag += QString(" ⚠️ (Served %1x past 30 days)").arg(recentServes);
                } else if (groupCount >= 3) {
                    tag += QString(" ⚡ (In %1 active teams)").arg(groupCount);
                } else {
                    tag += " ⭐ (Available)";
                }
                personCombo->addItem(tag, pid);
            }
            return;
        }

        for (const MatchResult& r : ranked) {
            QString label = QString("%1  🎯 %2%").arg(r.name).arg(r.score);
            if (!r.reasons.isEmpty()) label += "  · " + r.reasons.join(" · ");
            personCombo->addItem(label, r.personId);
        }
    };
    connect(roleCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) { populatePeople(); });
    populatePeople();
    layout->addRow("Select Volunteer:", personCombo);

    QLabel* smartHint = new QLabel("🤖 Smart Suggestions: ranked by skill fit (🎯 match %), penalized for team load and repeated service — best fit first.", &dlg);
    smartHint->setWordWrap(true);
    smartHint->setStyleSheet("font-size: 11px; color: #a6adc8;");
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
                                     QString("ℹ️ Family Note: Household member %1 is also serving as [%2] on %3.").arg(familyMemberName, familyMemberRole, dateStr));
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

    // Overrun guard: flag services that run longer than 90 minutes.
    const int kOverrunLimit = 90;
    if (totalMins > kOverrunLimit) {
        m_lblTotalDuration->setText(QString("⏱️ Total: %1 mins ⚠️ overruns by %2 min (limit %3)")
                                        .arg(totalMins).arg(totalMins - kOverrunLimit).arg(kOverrunLimit));
        m_lblTotalDuration->setStyleSheet("font-size: 13px; font-weight: bold; color: #f38ba8;");
    } else {
        m_lblTotalDuration->setText(QString("⏱️ Total Service Duration: %1 mins").arg(totalMins));
        m_lblTotalDuration->setStyleSheet("font-size: 13px; font-weight: bold; color: #a6e3a1;");
    }

    updateTimeline();
}

// Computes each run-sheet item's start time from the service start time and
// cumulative durations, and shows the timeline under the playlist.
void ServiceBuilderWidget::updateTimeline() {
    if (!m_lblTimeline) return;
    if (m_playlist->count() == 0) {
        m_lblTimeline->setText(QString("▶ Starts %1 — add elements to see the timeline.")
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
        lines << QString("… +%1 more").arg(m_playlist->count() - kMaxLines);
    }
    m_lblTimeline->setText("▶ " + lines.join("\n"));
}

QStringList ServiceBuilderWidget::collectCoverageIssues() const {
    QStringList issues;
    const int itemCount = m_playlist->count();
    const int volCount = m_volunteerList->count();

    // 1. Sermon / message present?
    bool hasSermon = false;
    for (int i = 0; i < itemCount; ++i) {
        if (m_playlist->item(i)->text().contains("Sermon", Qt::CaseInsensitive)) {
            hasSermon = true;
            break;
        }
    }
    if (itemCount > 0 && !hasSermon) issues << "No sermon / message element in the run sheet.";

    // 2. Core roles assigned? (Volunteer rows render as "[Role] Name".)
    QSet<QString> assignedRoles;
    for (int i = 0; i < volCount; ++i) {
        const QString t = m_volunteerList->item(i)->text();
        if (t.startsWith('[')) {
            const int end = t.indexOf(']');
            if (end > 1) assignedRoles.insert(t.mid(1, end - 1));
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
        QMessageBox::information(this, "Coverage Check", "✅ No coverage issues — the run sheet looks complete.");
    } else {
        QMessageBox::warning(this, "Coverage Check",
                             "Coverage issues found:\n\n• " + issues.join("\n• "));
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
    if (m_volunteerList->count() == 0) {
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
                                            const QList<QPair<QString, QString>>& roleNames) {
    auto esc = [](QString s) {
        return s.replace(',', "\\,").replace(';', "\\;").replace('\n', "\\n");
    };

    QString out;
    out += "BEGIN:VCALENDAR\n";
    out += "VERSION:2.0\n";
    out += "PRODID:-//OpenEkklesia//Service Builder//EN\n";
    out += "CALSCALE:GREGORIAN\n";

    const QString dtStart = date.toString("yyyyMMdd") + "T" + startTime.toString("HHmmss");
    QTime endTime = startTime.addSecs(2 * 3600); // 2-hour service block
    if (endTime < startTime) endTime = QTime(23, 59); // overnight edge: clamp
    const QString dtEnd = date.toString("yyyyMMdd") + "T" + endTime.toString("HHmmss");
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddThhmmssZ");

    int idx = 0;
    for (const auto& rn : roleNames) {
        ++idx;
        out += "BEGIN:VEVENT\n";
        out += QString("UID:oe-vol-%1-%2@openekklesia.local\n").arg(idx).arg(date.toString("yyyyMMdd"));
        out += QString("DTSTAMP:%1\n").arg(stamp);
        out += QString("DTSTART:%1\n").arg(dtStart);
        out += QString("DTEND:%1\n").arg(dtEnd);
        out += QString("SUMMARY:Serve as %1 (%2)\n").arg(esc(rn.first), esc(rn.second));
        out += QString("DESCRIPTION:Volunteer schedule for %1 — %2\\nRole: %3\n")
                   .arg(date.toString("MMMM d, yyyy"), esc(rn.second), esc(rn.first));
        out += "STATUS:CONFIRMED\n";
        out += "BEGIN:VALARM\n";
        out += "TRIGGER:-PT60M\n";
        out += "ACTION:DISPLAY\n";
        out += QString("DESCRIPTION:Reminder: %1 serves as %2\n").arg(esc(rn.second), esc(rn.first));
        out += "END:VALARM\n";
        out += "END:VEVENT\n";
    }
    out += "END:VCALENDAR\n";
    return out;
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
    out << volunteerIcal(m_serviceDateEdit->date(), m_serviceTimeEdit->time(), roleNames);
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
    dlg.setWindowTitle("📊 Element Usage Stats");
    dlg.resize(620, 440);
    auto* lay = new QVBoxLayout(&dlg);

    QLabel* note = new QLabel("How often each element appears across saved service run sheets — "
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
                                         "Coverage issues found:\n\n• " + issues.join("\n• ") +
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
    const QString leader = m_leaderEdit->text().trimmed().isEmpty() ? "Unassigned" : m_leaderEdit->text().trimmed();
    const QString timeStr = m_serviceTimeEdit->time().toString("h:mm AP");

    // Run sheet with per-item start times computed from the service start.
    QTime cursor = m_serviceTimeEdit->time();
    QString itemsHtml = "<ol style='font-size: 14px;'>";
    for (int i = 0; i < m_playlist->count(); ++i) {
        QListWidgetItem* item = m_playlist->item(i);
        const int mins = qMax(0, item->data(Qt::UserRole + 1).toInt());
        itemsHtml += QString("<li><b>%1</b> — %2</li>").arg(cursor.toString("h:mm AP"), item->text());
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
        "<h1 style='color: #1e66f5;'>OpenEkklesia — Sunday Order of Service</h1>"
        "<h3>%1 · %2 · Starts %3</h3>"
        "<p>Service Leader: %4</p>"
        "<hr>"
        "<h3>📋 Order of Service / Liturgy Run Sheet</h3>%5"
        "<hr>"
        "<h3>👥 Assigned Volunteers & Roster (contact strip)</h3>%6"
        "<br><p style='font-size: 11px; color: #666;'>Generated via OpenEkklesia ChMS Suite</p>"
        "</body></html>"
    ).arg(title, dateStr, timeStr, leader, itemsHtml, volHtml);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&pdfWriter);

    QMessageBox::information(this, "Export Success", "Service Run Sheet PDF exported successfully!");
}
