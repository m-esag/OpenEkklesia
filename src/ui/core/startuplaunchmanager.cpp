#include "startuplaunchmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace StartupLaunchManager {

bool supported() {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool isEnabled() {
#if defined(Q_OS_WIN)
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    return run.contains("OpenEkklesia");
#elif defined(Q_OS_MAC)
    const QString plist = QDir::homePath() + "/Library/LaunchAgents/org.openekklesia.OpenEkklesia.plist";
    return QFile::exists(plist);
#elif defined(Q_OS_LINUX)
    const QString desktop = QDir::homePath() + "/.config/autostart/openekklesia.desktop";
    return QFile::exists(desktop);
#else
    return false;
#endif
}

void setEnabled(bool enabled) {
    if (!supported())
        return;

    const QString appPath = QCoreApplication::applicationFilePath();

#if defined(Q_OS_WIN)
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (enabled) {
        const QString command = "\"" + QDir::toNativeSeparators(appPath) + "\"";
        run.setValue("OpenEkklesia", command);
    } else {
        run.remove("OpenEkklesia");
    }
#elif defined(Q_OS_MAC)
    const QString dir = QDir::homePath() + "/Library/LaunchAgents";
    const QString plist = dir + "/org.openekklesia.OpenEkklesia.plist";
    if (enabled) {
        QDir().mkpath(dir);
        QFile file(plist);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream stream(&file);
            stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            stream << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                      "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
            stream << "<plist version=\"1.0\"><dict>\n";
            stream << "  <key>Label</key><string>org.openekklesia.OpenEkklesia</string>\n";
            stream << "  <key>ProgramArguments</key>\n";
            stream << "  <array><string>" << appPath << "</string></array>\n";
            stream << "  <key>RunAtLoad</key><true/>\n";
            stream << "</dict></plist>\n";
            file.close();
        }
    } else {
        QFile::remove(plist);
    }
#elif defined(Q_OS_LINUX)
    const QString dir = QDir::homePath() + "/.config/autostart";
    const QString desktop = dir + "/openekklesia.desktop";
    if (enabled) {
        QDir().mkpath(dir);
        QFile file(desktop);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream stream(&file);
            stream << "[Desktop Entry]\n";
            stream << "Type=Application\n";
            stream << "Name=OpenEkklesia\n";
            stream << "Exec=\"" << appPath << "\" %U\n";
            stream << "X-GNOME-Autostart-enabled=true\n";
            file.close();
        }
    } else {
        QFile::remove(desktop);
    }
#endif
}

} // namespace StartupLaunchManager