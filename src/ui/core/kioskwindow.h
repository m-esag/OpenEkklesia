#ifndef KIOSKWINDOW_H
#define KIOSKWINDOW_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QEvent>

class KioskWindow : public QDialog {
    Q_OBJECT

public:
    explicit KioskWindow(QWidget* parent = nullptr);
    ~KioskWindow();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onNumberClicked();
    void onBackspaceClicked();
    void onClearClicked();
    void onCheckinClicked();
    void onAdminUnlockClicked();

private:
    void setupUI();
    void flashStatus(const QString& message, bool success);
    void restartIdleTimer();

    QLineEdit* m_entryEdit;
    QLabel* m_statusLabel;
    QTimer* m_idleTimer = nullptr;
    bool m_isAdminUnlocked;
};

#endif // KIOSKWINDOW_H
