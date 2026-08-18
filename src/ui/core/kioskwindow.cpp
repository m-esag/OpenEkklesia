#include "kioskwindow.h"
#include "../database/databasemanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>

KioskWindow::KioskWindow(QWidget* parent)
    : QDialog(parent), m_isAdminUnlocked(false)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setupUI();
}

KioskWindow::~KioskWindow() {}

void KioskWindow::setupUI() {
    // Kiosk Background Styling
    setStyleSheet(
        "QDialog { background-color: #11111b; }"
        "QLabel { color: #cdd6f4; }"
        "QLineEdit { background-color: #1e1e2e; border: 2px solid #313244; border-radius: 8px; color: #cdd6f4; font-size: 24px; padding: 12px; font-weight: bold; font-family: monospace; }"
        "QPushButton { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 10px; font-size: 20px; font-weight: bold; min-height: 55px; min-width: 70px; }"
        "QPushButton:hover { background-color: #45475a; }"
        "QPushButton:pressed { background-color: #585b70; }"
        "QPushButton#checkinBtn { background-color: #a6e3a1; color: #11111b; font-size: 20px; }"
        "QPushButton#checkinBtn:hover { background-color: #94e2d5; }"
        "QPushButton#unlockBtn { background-color: #f38ba8; color: #11111b; font-size: 16px; }"
        "QPushButton#unlockBtn:hover { background-color: #f5e0dc; }"
    );

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(60, 60, 60, 60);
    mainLayout->setSpacing(30);

    // Kiosk Headers
    QLabel* titleLabel = new QLabel("OpenEkklesia Child Check-in Kiosk", this);
    titleLabel->setStyleSheet("font-size: 32px; font-weight: bold; color: #89b4fa;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    QLabel* subtitle = new QLabel("Please scan your family barcode card or enter your phone number below:", this);
    subtitle->setStyleSheet("font-size: 18px; color: #a6adc8;");
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);

    // Entry field
    m_entryEdit = new QLineEdit(this);
    m_entryEdit->setAlignment(Qt::AlignCenter);
    m_entryEdit->setFocus();
    mainLayout->addWidget(m_entryEdit);

    // Status overlay message
    m_statusLabel = new QLabel("", this);
    m_statusLabel->setStyleSheet("font-size: 20px; font-weight: bold; padding: 10px; border-radius: 8px;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    // Grid representing Keypad
    QWidget* keypadWidget = new QWidget(this);
    QGridLayout* keypad = new QGridLayout(keypadWidget);
    keypad->setSpacing(15);

    // Populate buttons 1-9
    int count = 1;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            QPushButton* btn = new QPushButton(QString::number(count), this);
            connect(btn, &QPushButton::clicked, this, &KioskWindow::onNumberClicked);
            keypad->addWidget(btn, r, c);
            count++;
        }
    }

    // Row 4 keypad
    QPushButton* btnClear = new QPushButton("CLEAR", this);
    connect(btnClear, &QPushButton::clicked, this, &KioskWindow::onClearClicked);
    keypad->addWidget(btnClear, 3, 0);

    QPushButton* btnZero = new QPushButton("0", this);
    connect(btnZero, &QPushButton::clicked, this, &KioskWindow::onNumberClicked);
    keypad->addWidget(btnZero, 3, 1);

    QPushButton* btnBack = new QPushButton("⌫", this);
    connect(btnBack, &QPushButton::clicked, this, &KioskWindow::onBackspaceClicked);
    keypad->addWidget(btnBack, 3, 2);

    mainLayout->addWidget(keypadWidget, 0, Qt::AlignCenter);

    // Bottom Action buttons
    QHBoxLayout* actions = new QHBoxLayout();
    
    QPushButton* btnUnlock = new QPushButton("🔒 Exit Kiosk", this);
    btnUnlock->setObjectName("unlockBtn");
    connect(btnUnlock, &QPushButton::clicked, this, &KioskWindow::onAdminUnlockClicked);
    actions->addWidget(btnUnlock);

    QPushButton* btnCheckin = new QPushButton("✓ CHECK IN", this);
    btnCheckin->setObjectName("checkinBtn");
    connect(btnCheckin, &QPushButton::clicked, this, &KioskWindow::onCheckinClicked);
    actions->addWidget(btnCheckin);

    mainLayout->addLayout(actions);
}

void KioskWindow::onNumberClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (btn) {
        m_entryEdit->insert(btn->text());
    }
}

void KioskWindow::onBackspaceClicked() {
    m_entryEdit->backspace();
}

void KioskWindow::onClearClicked() {
    m_entryEdit->clear();
}

void KioskWindow::onCheckinClicked() {
    QString val = m_entryEdit->text().trimmed();
    if (val.isEmpty()) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT id, first_name || ' ' || last_name FROM people WHERE barcode = ? OR phone_number = ?");
    query.addBindValue(val);
    query.addBindValue(val);

    if (query.exec() && query.next()) {
        int personId = query.value(0).toInt();
        QString name = query.value(1).toString();

        // Record the self-service check-in in the attendance ledger.
        QSqlQuery ins(db);
        ins.prepare("INSERT INTO attendance (service_id, person_id, attended) VALUES (NULL, ?, 1)");
        ins.addBindValue(personId);
        if (!ins.exec()) {
            qWarning() << "Failed to record attendance:" << ins.lastError().text();
        }

        flashStatus(QString("Check-in Successful! Welcome, %1!").arg(name), true);
        m_entryEdit->clear();
    } else {
        flashStatus("Check-in Failed: ID or Phone number not recognized.", false);
    }
}

void KioskWindow::flashStatus(const QString& message, bool success) {
    m_statusLabel->setText(message);
    if (success) {
        m_statusLabel->setStyleSheet("color: #11111b; background-color: #a6e3a1; font-size: 20px; font-weight: bold; padding: 10px; border-radius: 8px;");
    } else {
        m_statusLabel->setStyleSheet("color: #11111b; background-color: #f38ba8; font-size: 20px; font-weight: bold; padding: 10px; border-radius: 8px;");
    }

    // Automatically hide the toast notification after 3.5 seconds
    QTimer::singleShot(3500, this, [this]() {
        m_statusLabel->clear();
        m_statusLabel->setStyleSheet("");
    });
}

void KioskWindow::onAdminUnlockClicked() {
    bool ok;
    QString pin = QInputDialog::getText(this, "Admin Unlock", "Enter Admin Security PIN to Exit Kiosk Mode:", QLineEdit::Password, "", &ok);
    if (ok) {
        // Resolve the admin PIN from the users table instead of hardcoding it.
        QString adminPin = "1234"; // fallback if no admin user is configured
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("SELECT pin FROM users WHERE role = 'admin' LIMIT 1");
        if (q.exec() && q.next()) {
            adminPin = q.value(0).toString();
        }

        if (pin == adminPin) {
            m_isAdminUnlocked = true;
            accept();
        } else {
            QMessageBox::warning(this, "Security Denied", "Incorrect Security PIN. Kiosk locked.");
        }
    }
}

void KioskWindow::closeEvent(QCloseEvent* event) {
    if (m_isAdminUnlocked) {
        event->accept();
    } else {
        event->ignore(); // block exit
    }
}

void KioskWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && !m_isAdminUnlocked) {
        return; // Consume Esc key
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onCheckinClicked();
        return;
    }
    QDialog::keyPressEvent(event);
}
