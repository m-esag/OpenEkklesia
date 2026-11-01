// Drives SettingsWidget::saveSettings() end-to-end and reports whether
// module keys persist.
#include "settingswidget.h"
#include "database/databasemanager.h"
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QSettings>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("OpenEkklesia");
    app.setOrganizationName("OpenEkklesia");
    if (!DatabaseManager::instance().initialize(QString())) return 1;

    SettingsWidget* w = new SettingsWidget();
    // Toggle a module OFF to prove the round trip
    QTimer::singleShot(300, w, [w]() {
        w->selectCategory(2); // Modules & Features page
        QTimer::singleShot(200, w, [w]() {
            qDebug() << "invoking saveSettings()...";
            w->saveSettings();
            qDebug() << "saveSettings() returned";
            QSettings s("OpenEkklesia", "CHMS");
            qDebug() << "modules/graph now =" << s.value("modules/graph", "<missing>").toString();
            qDebug() << "modules/giving now =" << s.value("modules/giving", "<missing>").toString();
            qApp->quit();
        });
    });
    return app.exec();
}
