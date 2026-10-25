#ifndef APPCURRENCY_H
#define APPCURRENCY_H

#include <QSettings>
#include "appsettings.h"
#include <QString>

// The currency symbol configured in Settings → Localization. Defaults to "$".
// Reads QSettings on every call so a changed setting applies immediately.
inline QString appCurrencySymbol() {
    return AppSettings().value("currencySymbol", "$").toString();
}

#endif // APPCURRENCY_H
