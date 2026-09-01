#ifndef DEVICE_WORKBENCH_APP_EDITION_H
#define DEVICE_WORKBENCH_APP_EDITION_H

#include <QString>

namespace AppEdition
{
inline bool isInternal()
{
#ifdef DEVICE_WORKBENCH_EXTERNAL
    return false;
#else
    return true;
#endif
}

inline QString id()
{
    return isInternal() ? QStringLiteral("internal") : QStringLiteral("external");
}

inline QString displayName()
{
    return QStringLiteral("обнови-БОЦ");
}

inline QString applicationId()
{
    return isInternal()
        ? QStringLiteral("DeviceWorkbenchInternal")
        : QStringLiteral("DeviceWorkbenchExternal");
}

inline bool allowsAction(const QString& actionId)
{
    if (isInternal())
        return true;

    return actionId.startsWith(QStringLiteral("flash."))
        || actionId == QStringLiteral("device.application.load")
        || actionId == QStringLiteral("device.ping");
}

inline bool allowsCustomFirmware()
{
    return isInternal();
}
}

#endif // DEVICE_WORKBENCH_APP_EDITION_H
