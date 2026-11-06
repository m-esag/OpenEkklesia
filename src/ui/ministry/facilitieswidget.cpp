#include "facilitieswidget.h"
#include "apptranslator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QHeaderView>
#include <QMenu>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QTime>
#include <QDate>
#include <QStringList>
#include <QClipboard>
#include <QGuiApplication>
#include "../database/databasemanager.h"
#include "../core/iconmanager.h"
#include "../../common/icalgenerator.h"

FacilitiesWidget::FacilitiesWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshReservations();
}

void FacilitiesWidget::applyRole(const QString& role) {
    m_userRole = role;
    const bool isReadOnly = (role == "readonly" || role == "guest");
    const bool canEdit = !isReadOnly;
    const QString roTip = isReadOnly ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify." : "";
    if (m_btnBook) {
        m_btnBook->setEnabled(canEdit);
        m_btnBook->setToolTip(roTip);
    }
    if (m_btnDelete) {
        m_btnDelete->setEnabled(canEdit);
        m_btnDelete->setToolTip(roTip);
    }
}

void FacilitiesWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(15);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* header = new QLabel(AppTranslator::t("Facilities & Equipment Scheduling Matrix"), this);
    header->setObjectName("pageHeader");
    headerLayout->addWidget(header);
    headerLayout->addStretch();

    m_btnBook = new QPushButton(" Book Space / Gear", this);
    m_btnBook->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnBook->setObjectName("primaryBtn");
    connect(m_btnBook, &QPushButton::clicked, this, &FacilitiesWidget::onBookFacility);
    headerLayout->addWidget(m_btnBook);

    m_btnDelete = new QPushButton(" Cancel Reservation", this);
    m_btnDelete->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDelete->setObjectName("dangerBtn");
    connect(m_btnDelete, &QPushButton::clicked, this, &FacilitiesWidget::onDeleteReservation);
    headerLayout->addWidget(m_btnDelete);

    mainLayout->addLayout(headerLayout);

    // Filter Bar
    QHBoxLayout* filterLayout = new QHBoxLayout();
    QLabel* dateLbl = new QLabel("Filter Reservation Date:", this);
    m_dateFilter = new QDateEdit(QDate::currentDate(), this);
    m_dateFilter->setCalendarPopup(true);
    m_dateFilter->setDisplayFormat("yyyy-MM-dd");
    connect(m_dateFilter, &QDateEdit::dateChanged, this, &FacilitiesWidget::onDateFilterChanged);

    filterLayout->addWidget(dateLbl);
    filterLayout->addWidget(m_dateFilter);
    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Table
    m_table = new QTableView(this);
    m_model = new QSqlQueryModel(this);
    m_table->setModel(m_model);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex idx = m_table->indexAt(pos);
        if (!idx.isValid()) return;
        int row = idx.row();
        m_table->selectRow(row);
        QString facility = m_model->index(row, 1).data().toString();
        QString eventName = m_model->index(row, 2).data().toString();
        QString dateStr = m_model->index(row, 3).data().toString();
        QString timeWindow = m_model->index(row, 4).data().toString();

        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Reservation Details");
        QAction* actIcal = menu.addAction(IconManager::instance().getIcon("calendar", QColor(), 14), "Export Calendar Reminder (.ics)...");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Cancel Reservation");
        if (isReadOnly) {
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actCopy) {
            QString summary = QString("%1 | Facility: %2 | Date: %3 | Time: %4").arg(eventName, facility, dateStr, timeWindow);
            QGuiApplication::clipboard()->setText(summary);
        } else if (sel == actIcal) {
            QString defaultFile = QString("Booking_%1_%2.ics").arg(facility, dateStr).replace(" ", "_").replace("/", "_");
            QString fileName = QFileDialog::getSaveFileName(this, "Save Calendar Event (.ics)", defaultFile, "iCalendar Files (*.ics)");
            if (!fileName.isEmpty()) {
                const QDate reservationDate = QDate::fromString(dateStr, "yyyy-MM-dd");
                // The table shows "start - end" (built from the start_time and
                // end_time columns); parse it so the export is a timed event.
                QTime startTime;
                QTime endTime;
                const QStringList window = timeWindow.split(" - ", Qt::SkipEmptyParts);
                if (window.size() == 2) {
                    startTime = QTime::fromString(window.at(0).trimmed(), "HH:mm");
                    endTime = QTime::fromString(window.at(1).trimmed(), "HH:mm");
                }
                const QString description = QString("Facility Booking: %1\nTime: %2")
                                               .arg(eventName, timeWindow);
                const QString ics = ICalGenerator::eventIcal(
                    reservationDate, startTime, endTime,
                    QString("%1 (%2)").arg(eventName, facility), description, facility);
                if (ics.isEmpty()) {
                    QMessageBox::warning(this, "Export Failed",
                                        "The reservation date could not be read; nothing was exported.");
                    return;
                }
                QFile file(fileName);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&file);
                    out << ics;
                    file.close();
                    QMessageBox::information(this, "Success", "Calendar file exported!");
                }
            }
        } else if (sel == actDelete && !isReadOnly) {
            onDeleteReservation();
        }
    });
    mainLayout->addWidget(m_table);
}

void FacilitiesWidget::refreshReservations() {
    QString dateStr = m_dateFilter->date().toString("yyyy-MM-dd");
    QSqlDatabase db = DatabaseManager::instance().database();

    QSqlQuery q(db);
    q.prepare(
        "SELECT id AS \"ID\", "
        "       facility_name AS \"Space / Equipment\", "
        "       event_name AS \"Event Purpose\", "
        "       reservation_date AS \"Date\", "
        "       (start_time || ' - ' || end_time) AS \"Time Window\", "
        "       reserved_by AS \"Reserved By\" "
        "FROM facility_reservations "
        "WHERE reservation_date = ? "
        "ORDER BY start_time ASC");
    q.addBindValue(dateStr);
    q.exec();
    m_model->setQuery(std::move(q));
    m_table->hideColumn(0);
}

void FacilitiesWidget::onDateFilterChanged(const QDate&) {
    refreshReservations();
}

void FacilitiesWidget::onBookFacility() {
    QDialog dlg(this);
    dlg.setWindowTitle("Book Space or Equipment");
    dlg.resize(360, 260);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QComboBox* facilityCombo = new QComboBox(&dlg);
    facilityCombo->addItems({
        "Sanctuary / Main Worship Center",
        "Fellowship Hall & Kitchen",
        "Youth Room / Annex",
        "Nursery Classroom A",
        "Conference Room 1",
        "Portable Sound System & Mic Kit",
        "HD Projector & Mobile Screen"
    });

    QLineEdit* eventEdit = new QLineEdit(&dlg);
    eventEdit->setPlaceholderText("e.g. Youth Worship Night, Choir Rehearsal");

    QDateEdit* dateEdit = new QDateEdit(m_dateFilter->date(), &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");

    QTimeEdit* startEdit = new QTimeEdit(QTime(18, 0), &dlg);
    QTimeEdit* endEdit = new QTimeEdit(QTime(20, 0), &dlg);

    QLineEdit* organizerEdit = new QLineEdit(&dlg);
    organizerEdit->setPlaceholderText("Leader / Contact Person");

    form->addRow("Space / Equipment:", facilityCombo);
    form->addRow("Event Purpose:", eventEdit);
    form->addRow("Date:", dateEdit);
    form->addRow("Start Time:", startEdit);
    form->addRow("End Time:", endEdit);
    form->addRow("Reserved By:", organizerEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString facility = facilityCombo->currentText();
        QString eventName = eventEdit->text().trimmed();
        QString resDate = dateEdit->date().toString("yyyy-MM-dd");
        QString startTime = startEdit->time().toString("hh:mm");
        QString endTime = endEdit->time().toString("hh:mm");
        QString organizer = organizerEdit->text().trimmed();

        if (eventName.isEmpty() || organizer.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Event purpose and organizer name are required.");
            return;
        }

        QSqlDatabase db = DatabaseManager::instance().database();
        // Conflict check
        QSqlQuery qConflict(db);
        qConflict.prepare(
            "SELECT event_name, start_time, end_time FROM facility_reservations "
            "WHERE facility_name = ? AND reservation_date = ? "
            "  AND NOT (end_time <= ? OR start_time >= ?)");
        qConflict.addBindValue(facility);
        qConflict.addBindValue(resDate);
        qConflict.addBindValue(startTime);
        qConflict.addBindValue(endTime);

        if (qConflict.exec() && qConflict.next()) {
            QString confEvent = qConflict.value(0).toString();
            QString confStart = qConflict.value(1).toString();
            QString confEnd = qConflict.value(2).toString();
            QMessageBox::critical(this, "Scheduling Conflict!",
                                  QString("Cannot book '%1'. It is already reserved by [%2] from %3 to %4.")
                                      .arg(facility, confEvent, confStart, confEnd));
            return;
        }

        QSqlQuery qInsert(db);
        qInsert.prepare(
            "INSERT INTO facility_reservations (facility_name, event_name, reservation_date, start_time, end_time, reserved_by) "
            "VALUES (?, ?, ?, ?, ?, ?)");
        qInsert.addBindValue(facility);
        qInsert.addBindValue(eventName);
        qInsert.addBindValue(resDate);
        qInsert.addBindValue(startTime);
        qInsert.addBindValue(endTime);
        qInsert.addBindValue(organizer);

        if (qInsert.exec()) {
            m_dateFilter->setDate(dateEdit->date());
            refreshReservations();
            QMessageBox::information(this, "Success", "Reservation confirmed cleanly!");
        } else {
            QMessageBox::critical(this, "Database Error", "Failed to book space: " + qInsert.lastError().text());
        }
    }
}

void FacilitiesWidget::onDeleteReservation() {
    int row = m_table->currentIndex().row();
    if (row < 0) {
        QMessageBox::warning(this, "Selection Required", "Please select a reservation row to cancel.");
        return;
    }
    int resId = m_model->index(row, 0).data().toInt();
    QString facility = m_model->index(row, 1).data().toString();
    QString eventName = m_model->index(row, 2).data().toString();

    auto reply = QMessageBox::question(this, "Confirm Cancellation",
                                       QString("Cancel reservation for '%1' (%2)?").arg(facility, eventName),
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery qDel(db);
        qDel.prepare("DELETE FROM facility_reservations WHERE id = ?");
        qDel.addBindValue(resId);
        if (qDel.exec()) {
            refreshReservations();
            QMessageBox::information(this, "Cancelled", "Reservation cancelled.");
        } else {
            QMessageBox::critical(this, "Error", "Failed to cancel: " + qDel.lastError().text());
        }
    }
}
