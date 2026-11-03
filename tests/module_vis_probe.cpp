// End-to-end probe for the "Settings -> Modules -> Save -> sidebar doesn't
// update" report. Logs visibility of every nav button at each stage.
#include "mainwindow.h"
#include "appsettings.h"
#include "settingswidget.h"
#include "database/databasemanager.h"
#include <QApplication>
#include <QStackedWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QSettings>
#include <QDebug>

static QList<QPushButton*> navButtons(QWidget* root) {
    // Sidebar nav buttons: children of the sidebar with the navButton style
    QList<QPushButton*> out;
    const auto all = root->findChildren<QPushButton*>();
    for (QPushButton* b : all)
        if (b->objectName() == "navButton" || b->property("isNav").toBool())
            out << b;
    return out;
}

static void dumpVis(QWidget* root, const char* stage) {
    // Primary check: the member buttons MainWindow toggles in
    // applyModuleVisibility. Find them via their text labels, which are stable.
    auto buttons = navButtons(root);
    QStringList states;
    for (QPushButton* b : buttons) {
        if (!b) continue;
        states << QString("%1=%2").arg(b->text().trimmed(), b->isVisibleTo(root->window()) ? "vis" : "HID");
    }
    qDebug() << stage << "->" << states.join(", ");
}

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName("OpenEkklesia");
    app.setOrganizationName("OpenEkklesia");
    if (!DatabaseManager::instance().initialize(QString())) return 1;

    // Stage 0: simulate the user's saved Minimal config BEFORE the window
    // constructs (like a restart after saving Minimal).
    {
        AppSettings s;
        s.setValue("modules/people", true);
        s.setValue("modules/groups", false);
        s.setValue("modules/graph", false);
        s.setValue("modules/analytics", false);
        s.setValue("modules/shepherd", false);
        s.setValue("modules/service", true);
        s.setValue("modules/planning", false);
        s.setValue("modules/facilities", false);
        s.setValue("modules/giving", false);
        s.setValue("modules/inventory", false);
        s.setValue("modules/vault", false);
        s.setValue("modules/kiosk", false);
        s.sync();
    }

    MainWindow mw;
    dumpVis(&mw, "STAGE 1: after ctor (Minimal in registry)");

    // Stage 2: user opens Settings and clicks "Minimal" preset then Save.
    SettingsWidget* sw = nullptr;
    QMetaObject::invokeMethod(&mw, "onSettingsClicked", Qt::DirectConnection);
    sw = mw.findChild<SettingsWidget*>();
    if (!sw) { qDebug() << "no settings widget"; return 1; }

    // Click the Minimal preset button by text.
    const auto btns = sw->findChildren<QPushButton*>();
    QPushButton* minimal = nullptr;
    for (QPushButton* b : btns)
        if (b->text().contains("Minimal")) { minimal = b; break; }
    if (minimal) {
        qDebug() << "STAGE 2: clicking Minimal preset";
        minimal->click();
    } else {
        qDebug() << "STAGE 2: Minimal button NOT FOUND";
    }

    // Stage 3: click "Save All Changes".
    QPushButton* save = nullptr;
    const auto allBtns = mw.findChildren<QPushButton*>();
    for (QPushButton* b : allBtns)
        if (b->text().trimmed() == "Save All Changes") { save = b; break; }
    if (save) {
        qDebug() << "STAGE 3: clicking Save All Changes";
        save->click();
    } else {
        qDebug() << "STAGE 3: Save button NOT FOUND";
    }

    dumpVis(&mw, "STAGE 4: after save (sidebar should have updated live)");

    // Stage 5: persistence check
    AppSettings s5;
    qDebug() << "STAGE 5 store: modules/graph =" << s5.value("modules/graph").toString()
             << "modules/giving =" << s5.value("modules/giving").toString();

    return 0;
}
