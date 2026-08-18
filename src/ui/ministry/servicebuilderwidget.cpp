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
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPdfWriter>
#include <QTextDocument>
#include "../database/databasemanager.h"

ServiceBuilderWidget::ServiceBuilderWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    seedLibrary();
    loadVolunteers();
    loadServiceDetails();
}

ServiceBuilderWidget::~ServiceBuilderWidget() {}

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
    m_libraryList->setDragEnabled(true);
    m_libraryList->setDragDropMode(QAbstractItemView::DragOnly);
    m_libraryList->setAlternatingRowColors(true);
    libraryLayout->addWidget(m_libraryList);

    QLabel* dragTip = new QLabel("Tip: Drag items from here to the right panel.", this);
    dragTip->setObjectName("dragTip");
    libraryLayout->addWidget(dragTip);

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
    m_lblTotalDuration->setStyleSheet("font-size: 13px; font-weight: bold; color: #a6e3a1;");
    headerRow->addWidget(playlistLabel);
    headerRow->addStretch();
    headerRow->addWidget(m_lblTotalDuration);
    playlistLayout->addLayout(headerRow);

    m_playlist = new QListWidget(this);
    m_playlist->setAcceptDrops(true);
    m_playlist->setDragEnabled(true);
    m_playlist->setDragDropMode(QAbstractItemView::DragDrop);
    m_playlist->setDefaultDropAction(Qt::CopyAction);
    m_playlist->setAlternatingRowColors(true);
    connect(m_playlist->model(), &QAbstractItemModel::rowsInserted, this, &ServiceBuilderWidget::updateDurationCalculator);
    connect(m_playlist->model(), &QAbstractItemModel::rowsRemoved, this, &ServiceBuilderWidget::updateDurationCalculator);
    playlistLayout->addWidget(m_playlist);

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

    m_btnExportPDF = new QPushButton("📄 Export Run Sheet PDF", this);
    m_btnExportPDF->setObjectName("accentBtnCyan");
    connect(m_btnExportPDF, &QPushButton::clicked, this, &ServiceBuilderWidget::onExportRunSheet);
    actionsLayout->addWidget(m_btnExportPDF);

    actionsLayout->addStretch();

    m_btnSave = new QPushButton("Save Layout", this);
    m_btnSave->setObjectName("saveBtn");
    connect(m_btnSave, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveService);
    actionsLayout->addWidget(m_btnSave);

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
    volunteerLayout->addLayout(dateLayout);

    // Service plan details (title & leader) persisted to the services table
    QFormLayout* serviceForm = new QFormLayout();
    m_serviceTitleEdit = new QLineEdit(this);
    m_serviceTitleEdit->setPlaceholderText("e.g. Sunday Worship Service");
    serviceForm->addRow("Service Title:", m_serviceTitleEdit);

    m_leaderEdit = new QLineEdit(this);
    m_leaderEdit->setPlaceholderText("e.g. Pastor James Davis");
    serviceForm->addRow("Service Leader:", m_leaderEdit);
    volunteerLayout->addLayout(serviceForm);

    m_btnSaveServiceDetails = new QPushButton("💾 Save Service Details", this);
    m_btnSaveServiceDetails->setObjectName("saveBtn");
    connect(m_btnSaveServiceDetails, &QPushButton::clicked, this, &ServiceBuilderWidget::onSaveServiceDetails);
    volunteerLayout->addWidget(m_btnSaveServiceDetails);

    m_volunteerList = new QListWidget(this);
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

    mainLayout->addWidget(volunteerPanel, 2);

    connect(m_serviceDateEdit, &QDateEdit::dateChanged, this, &ServiceBuilderWidget::onServiceDateChanged);
}

void ServiceBuilderWidget::seedLibrary() {
    struct SeedItem {
        QString title;
        QString type; // Song, Scripture, Media, Text
        QString colorHex;
    };

    QList<SeedItem> seeds = {
        // Songs
        {"🎵 Song: Amazing Grace", "Song", "#f9e2af"},
        {"🎵 Song: How Great Thou Art", "Song", "#f9e2af"},
        {"🎵 Song: 10,000 Reasons", "Song", "#f9e2af"},
        {"🎵 Song: Cornerstone", "Song", "#f9e2af"},
        // Scriptures
        {"📖 Scripture: Psalm 23", "Scripture", "#a6e3a1"},
        {"📖 Scripture: John 3:16", "Scripture", "#a6e3a1"},
        {"📖 Scripture: Romans 8:28", "Scripture", "#a6e3a1"},
        // Media / Slides
        {"🎬 Media: Welcome & Announcements", "Media", "#89b4fa"},
        {"🎬 Media: Kids Dismissal Slide", "Media", "#89b4fa"},
        {"🎬 Media: Sermon Presentation", "Media", "#89b4fa"},
        {"🎬 Media: Closing Slideshow", "Media", "#89b4fa"},
        // Liturgy
        {"💬 Prayer: Opening Thanksgiving", "Text", "#cba6f7"},
        {"💬 Prayer: Pastoral Intercession", "Text", "#cba6f7"},
        {"💬 Message: Sunday Sermon", "Text", "#cba6f7"},
        {"💬 Liturgy: Benediction", "Text", "#cba6f7"}
    };

    for (const auto& seed : seeds) {
        QListWidgetItem* item = new QListWidgetItem(seed.title, m_libraryList);
        item->setData(Qt::UserRole, seed.type);
        item->setData(Qt::ToolTipRole, QString("Type: %1").arg(seed.type));
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

void ServiceBuilderWidget::onSaveService() {
    if (m_playlist->count() == 0) {
        QMessageBox::warning(this, "No Layout", "The Sunday Run Sheet is empty. Add elements before saving.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Save Service Plan", QString(), "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    QJsonArray array;
    for (int i = 0; i < m_playlist->count(); ++i) {
        QListWidgetItem* item = m_playlist->item(i);
        QJsonObject obj;
        obj["title"] = item->text();
        obj["type"] = item->data(Qt::UserRole).toString();
        array.append(obj);
    }

    QJsonDocument doc(array);
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

    m_playlist->clear();
    QJsonArray array = doc.array();
    for (int i = 0; i < array.size(); ++i) {
        QJsonObject obj = array[i].toObject();
        QString title = obj["title"].toString();
        QString type = obj["type"].toString();

        QListWidgetItem* item = new QListWidgetItem(title, m_playlist);
        item->setData(Qt::UserRole, type);
    }
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
    query.prepare("SELECT title, leader FROM services WHERE date = ?");
    query.addBindValue(dateStr);

    if (query.exec() && query.next()) {
        m_serviceTitleEdit->setText(query.value(0).toString());
        m_leaderEdit->setText(query.value(1).toString());
    } else {
        m_serviceTitleEdit->clear();
        m_leaderEdit->clear();
    }
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

    QComboBox* personCombo = new QComboBox(&dlg);
    QSqlDatabase db = DatabaseManager::instance().database();
    
    // Fetch volunteers with smart burnout indicators (# of active groups & recent service count)
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
    layout->addRow("Select Volunteer:", personCombo);

    QComboBox* roleCombo = new QComboBox(&dlg);
    roleCombo->addItems({"Preacher", "Worship Leader", "Acoustic Guitar", "Electric Guitar", "Vocalist", "Bass Guitar", "Drums", "Nursery Care", "Audio Engineer", "Slide Operator", "Usher"});
    layout->addRow("Assign Role:", roleCombo);

    QLabel* smartHint = new QLabel("🤖 Smart Suggestions: ⭐ Highly Available | ⚡ Active Teams | ⚠️ High Frequency Warning", &dlg);
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
        QString text = m_playlist->item(i)->text();
        // Parse format e.g. "Song Name (4m)" or "Sermon (30m)"
        int openParen = text.lastIndexOf('(');
        int closeParen = text.lastIndexOf('m');
        if (openParen >= 0 && closeParen > openParen) {
            bool ok = false;
            int mins = text.mid(openParen + 1, closeParen - openParen - 1).toInt(&ok);
            if (ok) totalMins += mins;
        }
    }
    m_lblTotalDuration->setText(QString("⏱️ Total Service Duration: %1 mins").arg(totalMins));
}

void ServiceBuilderWidget::onExportRunSheet() {
    if (m_playlist->count() == 0) {
        QMessageBox::warning(this, "Notice", "The Sunday Service Run Sheet is currently empty.");
        return;
    }

    QString savePath = QFileDialog::getSaveFileName(this, "Export Run Sheet PDF",
                                                     QString("Service_Run_Sheet_%1.pdf").arg(m_serviceDateEdit->date().toString("yyyy-MM-dd")),
                                                     "PDF Documents (*.pdf)");
    if (savePath.isEmpty()) return;

    QPdfWriter pdfWriter(savePath);
    pdfWriter.setPageSize(QPageSize(QPageSize::Letter));

    QString dateStr = m_serviceDateEdit->date().toString("MMMM d, yyyy");
    QString itemsHtml = "<ol style='font-size: 14px;'>";
    for (int i = 0; i < m_playlist->count(); ++i) {
        itemsHtml += QString("<li><b>%1</b></li>").arg(m_playlist->item(i)->text());
    }
    itemsHtml += "</ol>";

    QString volHtml = "<ul>";
    for (int i = 0; i < m_volunteerList->count(); ++i) {
        volHtml += QString("<li>%1</li>").arg(m_volunteerList->item(i)->text());
    }
    volHtml += "</ul>";

    QString html = QString(
        "<html><body style='font-family: Arial, sans-serif; margin: 30px;'>"
        "<h1 style='color: #1e66f5;'>OpenEkklesia — Sunday Order of Service</h1>"
        "<h3>Service Date: %1</h3>"
        "<hr>"
        "<h3>📋 Order of Service / Liturgy Run Sheet</h3>%2"
        "<hr>"
        "<h3>👥 Assigned Volunteers & Roster</h3>%3"
        "<br><p style='font-size: 11px; color: #666;'>Generated via OpenEkklesia ChMS Suite</p>"
        "</body></html>"
    ).arg(dateStr, itemsHtml, volHtml);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&pdfWriter);

    QMessageBox::information(this, "Export Success", "Service Run Sheet PDF exported successfully!");
}
