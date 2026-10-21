#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include "databasemanager.h"
#include "mainwindow.h"
#include "thememanager.h"
#include "touchhelper.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents);
    // Touchscreen + active pen (e.g. Latitude 7320 Detachable / PN7320A):
    // untracked touch becomes mouse clicks; pen already acts as a mouse.
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, true);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents, false);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    TouchHelper::installAppWide();
    app.setApplicationName("OpenEkklesia");
    app.setOrganizationName("OpenEkklesia");

    ThemeManager::instance().initialize();

    // Initialize Database. Passing an empty path lets DatabaseManager resolve
    // a stable, user-writable location (QStandardPaths::AppDataLocation) so
    // data survives regardless of where the binary is launched from.
    if (!DatabaseManager::instance().initialize(QString())) {
        QMessageBox::critical(nullptr, "Database Error",
                              "Failed to open or initialize the application database. OpenEkklesia will now exit.");
        return 1;
    }

    MainWindow mainWindow;

    // Require a successful sign-in before revealing the application. The
    // credentials (PINs) are validated against the `users` table, and the
    // resolved role drives role-based access restrictions across modules.
    if (!mainWindow.promptLogin()) {
        DatabaseManager::instance().close();
        return 0;
    }

    mainWindow.show();

    int result = app.exec();

    // Clean up
    DatabaseManager::instance().close();

    return result;
}
