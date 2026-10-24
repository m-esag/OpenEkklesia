#include "kioskwindow.h"
#include "../database/databasemanager.h"
#include "thememanager.h"
#include "touchhelper.h"
#include "iconmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QSettings>
#include <QMouseEvent>
#include <QMessageBox>
#include <QTimer>

KioskWindow::KioskWindow(QWidget* parent)
    : QDialog(parent), m_isAdminUnlocked(false)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // Detachable touchscreen + active pen: accept touch so taps never depend
    // on synthesis timing; buttons already meet finger-target sizes.
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setupUI();
    if (TouchHelper::touchModeEnabled() && m_entryEdit) {
        // Finger-sized entry on a 13" detachable held at arm's length.
        m_entryEdit->setMinimumHeight(64);
    }

    // Idle lockout from Settings (default 45 s): return to the check-in state
    // after inactivity, so a public kiosk never lingers on someone's screen.
    const int idleSecs = qBound(5, QSettings("OpenEkklesia", "CHMS").value("kioskTimeoutSeconds", 45).toInt(), 3600);
    m_idleTimer = new QTimer(this);
    m_idleTimer->setInterval(idleSecs * 1000);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        m_entryEdit->clear();
        m_statusLabel->clear();
        m_statusLabel->setStyleSheet("");
        m_isAdminUnlocked = false;
        // The timer is single-shot: re-arm it, otherwise the kiosk only resets
        // once per session and later idle periods leave the screen unattended.
        restartIdleTimer();
    });
    restartIdleTimer();
}

KioskWindow::~KioskWindow() {}

void KioskWindow::setupUI() {
    // Kiosk Background Styling (token-driven so it follows the active theme)
    {
        const ThemeTokens t = ThemeManager::instance().currentTokens();
        // textOn() picks the higher-contrast foreground, so the kiosk keeps AA
        // contrast on every accent background in every theme.
        const QString onAccent = ThemeManager::textOn(t.accentPrimary).name();
        const QString onDanger = ThemeManager::textOn(t.accentDanger).name();
        // NOTE: QString::arg() replaces placeholders sequentially (lowest
        // remaining %N), not by argument position. The %5 slot must therefore
        // exist, or every argument from accentPrimary onward shifts one slot
        // and CHECK IN renders as red-on-green.
        setStyleSheet(QString(
            "QDialog { background-color: %1; }"
            "QLabel { color: %2; }"
            "QLineEdit { background-color: %3; border: 2px solid %4; border-radius: 8px; color: %2; font-size: 24px; padding: 12px; font-weight: bold; font-family: monospace; }"
            "QLineEdit:focus { border: 2px solid %5; }"
            "QPushButton { background-color: %6; color: %2; border: 1px solid %4; border-radius: 10px; font-size: 20px; font-weight: bold; min-height: 55px; min-width: 70px; }"
            "QPushButton:hover { background-color: %4; }"
            "QPushButton:pressed { background-color: %6; }"
            "QPushButton#checkinBtn { background-color: %8; color: %9; font-size: 20px; }"
            "QPushButton#checkinBtn:hover { background-color: %8; }"
            "QPushButton#unlockBtn { background-color: %7; color: %10; font-size: 16px; }"
            "QPushButton#unlockBtn:hover { background-color: %7; }"
        ).arg(t.bgInput.name(), t.textPrimary.name(), t.bgInput.name(), t.borderColor.name(),
              t.accentPrimary.name(), t.bgSurface.name(), t.accentDanger.name(),
              t.accentSuccess.name(), onAccent, onDanger));
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(60, 60, 60, 60);
    mainLayout->setSpacing(30);

    // Kiosk Headers
    QLabel* titleLabel = new QLabel("OpenEkklesia Child Check-in Kiosk", this);
    titleLabel->setStyleSheet(QString("font-size: 32px; font-weight: bold; color: %1;").arg(ThemeManager::instance().currentTokens().accentPrimary.name()));
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    QLabel* subtitle = new QLabel("Please scan your family barcode card or enter your phone number below:", this);
    subtitle->setStyleSheet(QString("font-size: 18px; color: %1;").arg(ThemeManager::instance().currentTokens().textSecondary.name()));
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

    QPushButton* btnBack = new QPushButton(this);
    btnBack->setIcon(IconManager::instance().getIcon("delete", QColor(), 22));
    btnBack->setToolTip("Backspace");
    connect(btnBack, &QPushButton::clicked, this, &KioskWindow::onBackspaceClicked);
    keypad->addWidget(btnBack, 3, 2);

    mainLayout->addWidget(keypadWidget, 0, Qt::AlignCenter);

    // Bottom Action buttons
    QHBoxLayout* actions = new QHBoxLayout();
    
    QPushButton* btnUnlock = new QPushButton(" Exit Kiosk", this);
    btnUnlock->setIcon(IconManager::instance().getDangerIcon("lock", 16));
    btnUnlock->setObjectName("unlockBtn");
    connect(btnUnlock, &QPushButton::clicked, this, &KioskWindow::onAdminUnlockClicked);
    actions->addWidget(btnUnlock);

    QPushButton* btnCheckin = new QPushButton(" CHECK IN", this);
    btnCheckin->setIcon(IconManager::instance().getPrimaryIcon("check", 16));
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
    restartIdleTimer();
}

void KioskWindow::onBackspaceClicked() {
    m_entryEdit->backspace();
    restartIdleTimer();
}

void KioskWindow::onClearClicked() {
    m_entryEdit->clear();
    restartIdleTimer();
}

void KioskWindow::onCheckinClicked() {
    restartIdleTimer();
    QString val = m_entryEdit->text().trimmed();
    if (val.isEmpty()) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    // Prefer exact barcode match; fall back to phone. Collect all matches so
    // shared family phones show a choice instead of checking in the wrong person.
    query.prepare("SELECT id, first_name || ' ' || last_name FROM people WHERE barcode = ?");
    query.addBindValue(val);
    QList<QPair<int, QString>> matches;
    if (query.exec()) {
        while (query.next()) matches << qMakePair(query.value(0).toInt(), query.value(1).toString());
    }
    if (matches.isEmpty()) {
        QSqlQuery pq(db);
        pq.prepare("SELECT id, first_name || ' ' || last_name FROM people WHERE phone_number = ?");
        pq.addBindValue(val);
        if (pq.exec()) {
            while (pq.next()) matches << qMakePair(pq.value(0).toInt(), pq.value(1).toString());
        }
    }

    if (matches.isEmpty()) {
        flashStatus("Check-in Failed: ID or Phone number not recognized.", false);
        return;
    }
    if (matches.size() > 1) {
        QStringList names;
        for (const auto& m : matches) names << QString("%1 (#%2)").arg(m.second).arg(m.first);
        bool ok = false;
        QString picked = QInputDialog::getItem(this, "Multiple Matches",
            "Several people share this number. Who is checking in?", names, 0, false, &ok);
        if (!ok) return;
        const int idx = names.indexOf(picked);
        if (idx >= 0) {
            QPair<int, QString> keep = matches.at(idx);
            matches.clear();
            matches << keep;
        }
    }
    {
        int personId = matches.first().first;
        QString name = matches.first().second;

        // Dedupe: ignore a repeat tap for the same person on the same day.
        QSqlQuery dup(db);
        dup.prepare("SELECT id FROM attendance WHERE person_id = ? AND date(timestamp,'localtime') = date('now','localtime') LIMIT 1");
        dup.addBindValue(personId);
        if (dup.exec() && dup.next()) {
            flashStatus(QString("%1 is already checked in today.").arg(name), true);
            m_entryEdit->clear();
            return;
        }
        // Record the self-service check-in in the attendance ledger.
        QSqlQuery ins(db);
        ins.prepare("INSERT INTO attendance (service_id, person_id, attended) VALUES (NULL, ?, 1)");
        ins.addBindValue(personId);
        if (!ins.exec()) {
            // Never claim success (or clear the field) when the write failed:
            // the family must be able to retry immediately.
            qWarning() << "Failed to record attendance:" << ins.lastError().text();
            flashStatus("Check-in Failed: attendance could not be saved. Please try again or see an usher.", false);
            return;
        }

        flashStatus(QString("Check-in Successful! Welcome, %1!").arg(name), true);
        m_entryEdit->clear();
    }
}

void KioskWindow::flashStatus(const QString& message, bool success) {
    m_statusLabel->setText(message);
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    if (success) {
        m_statusLabel->setStyleSheet(QString("color: %1; background-color: %2; font-size: 20px; font-weight: bold; padding: 10px; border-radius: 8px;")
                                        .arg(ThemeManager::textOn(tok.accentSuccess).name(), tok.accentSuccess.name()));
    } else {
        m_statusLabel->setStyleSheet(QString("color: %1; background-color: %2; font-size: 20px; font-weight: bold; padding: 10px; border-radius: 8px;")
                                        .arg(ThemeManager::textOn(tok.accentDanger).name(), tok.accentDanger.name()));
    }

    // Automatically hide the toast notification after 3.5 seconds
    QTimer::singleShot(3500, this, [this]() {
        m_statusLabel->clear();
        m_statusLabel->setStyleSheet("");
    });
}

void KioskWindow::onAdminUnlockClicked() {
    // Settings: kioskRequirePin (default true) — internal devices can allow
    // exiting without the admin PIN.
    const bool requirePin = QSettings("OpenEkklesia", "CHMS").value("kioskRequirePin", true).toBool();
    if (!requirePin) {
        m_isAdminUnlocked = true;
        accept();
        return;
    }

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

// Idle lockout: after kioskTimeoutSeconds without input, clear the screen and
// lock back to the check-in state (Settings → Kiosk & Security).
void KioskWindow::restartIdleTimer() {
    if (m_idleTimer) m_idleTimer->start();
}

void KioskWindow::mousePressEvent(QMouseEvent* event) {
    restartIdleTimer();
    QDialog::mousePressEvent(event);
}

void KioskWindow::closeEvent(QCloseEvent* event) {
    if (m_isAdminUnlocked) {
        event->accept();
    } else {
        event->ignore(); // block exit
    }
}

void KioskWindow::keyPressEvent(QKeyEvent* event) {
    restartIdleTimer();
    if (event->key() == Qt::Key_Escape && !m_isAdminUnlocked) {
        return; // Consume Esc key
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onCheckinClicked();
        return;
    }
    QDialog::keyPressEvent(event);
}
