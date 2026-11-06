#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <shobjidl.h>
#endif
#include "databasemanager.h"
#include "mainwindow.h"
#include "setupwizard.h"
#include "thememanager.h"
#include "touchhelper.h"
#include "appsettings.h"
#include "apptranslator.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents);
    // Touchscreen + active pen (e.g. Latitude 7320 Detachable / PN7320A):
    // untracked touch becomes mouse clicks; pen already acts as a mouse.
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, true);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents, false);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#if defined(Q_OS_WIN)
    SetCurrentProcessExplicitAppUserModelID(L"OpenEkklesia.ChMS");

    // MSYS/Git-Bash environments export POSIX-style HOME/TMP/TEMP ("/home/...",
    // "/tmp"). Qt resolves QSettings, temp files and the SQLite journal/wal
    // sidecars through these, and a non-drive path silently breaks every
    // write: QSettings reports AccessError and the database opens read-only.
    // Restore real Windows paths before anything touches the filesystem.
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty()) {
        for (const char* var : {"TMP", "TEMP", "TMPDIR", "HOME"}) {
            const QString val = qEnvironmentVariable(var);
            if (!val.isEmpty() && !val.contains(':')) {
                qputenv(var, var == QString("HOME")
                                  ? QFile::encodeName(userProfile)
                                  : QFile::encodeName(userProfile + QStringLiteral("/AppData/Local/Temp")));
            }
        }
    }
#endif
    QApplication app(argc, argv);
    TouchHelper::installAppWide();
    app.setApplicationName("OpenEkklesia");
    app.setOrganizationName("OpenEkklesia");

#if defined(Q_OS_WIN)
    // Normalize the working directory to the executable's folder. When the
    // app is launched from a terminal sitting in a junction/symlinked path
    // (e.g. a workspace alias like C:\home\manuel), QSettings writes fail
    // with AccessError and QSQLITE opens the database read-only — settings
    // and all module saves silently no-op. Launching from Explorer is
    // unaffected, but a normalized CWD makes every launch path equal.
    QDir::setCurrent(QCoreApplication::applicationDirPath());
#endif

    // Resolve the UI language before any UI is constructed (theme engine,
    // first-run wizard, main window) so the first paint is already localized.
    // No live retranslation in this phase: a language change made in Settings
    // applies on restart (the first-run wizard retranslates itself live).
    AppTranslator::instance().setLanguage(
        AppSettings().value("ui/language", "en").toString());

    ThemeManager::instance().initialize();

    // Initialize Database. Passing an empty path lets DatabaseManager resolve
    // a stable, user-writable location (QStandardPaths::AppDataLocation) so
    // data survives regardless of where the binary is launched from.
    if (!DatabaseManager::instance().initialize(QString())) {
        // Show the REAL failure reason, not just the fact of failure: the
        // generic dialog once turned a missing SQLite driver into days of
        // remote diagnosis ("can not load requested driver qsqlite" never
        // reached the user's screen).
        QString detail;
        {
            const QSqlDatabase db = DatabaseManager::instance().database();
            if (!db.isValid()) {
                detail = "No usable Qt SQLite driver is installed in this build.";
            } else if (!db.isOpen()) {
                const QSqlError err = db.lastError();
                detail = err.isValid()
                             ? err.text().trimmed()
                             : "The driver loaded but the connection did not open.";
            } else {
                detail = "The connection opened but schema setup failed. "
                         "See the diagnostic log for details.";
            }
            detail += QString("\n\nDatabase location attempted:\n%1")
                          .arg(db.isValid() && !db.databaseName().isEmpty()
                                   ? db.databaseName()
                                   : "(unavailable)");
        }
        QMessageBox::critical(nullptr, "Database Error",
                              QString("Failed to open or initialize the application "
                                      "database.\n\n%1\n\nOpenEkklesia will now exit.")
                                  .arg(detail.toHtmlEscaped()));
        return 1;
    }

    // First run: collect the church profile before anything else. The details
    // print on giving statements (tax receipts), so ask for them up front
    // instead of shipping placeholder data that a quick "Save All Changes"
    // would bake into real receipts. Skippable; re-shown never.
    {
        AppSettings settings;
        if (!settings.contains("profileComplete")) {
            SetupWizard wizard;
            wizard.exec();
        }
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
