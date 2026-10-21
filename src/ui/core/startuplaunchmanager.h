#ifndef STARTUPLAUNCHMANAGER_H
#define STARTUPLAUNCHMANAGER_H

#include <QString>
#include <QSettings>

namespace StartupLaunchManager {

bool supported();
bool isEnabled();
void setEnabled(bool enabled);

} // namespace StartupLaunchManager

#endif // STARTUPLAUNCHMANAGER_H