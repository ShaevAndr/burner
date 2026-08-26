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
    return isInternal()
        ? QStringLiteral("Device Workbench — внутренняя версия")
        : QStringLiteral("Device Workbench — прошивка");
}

inline bool allowsAction(const QString& actionId)
{
    if (isInternal())
        return true;

    return actionId.startsWith(QStringLiteral("flash."))
        || actionId == QStringLiteral("device.application.load");
}
}

#endif // DEVICE_WORKBENCH_APP_EDITION_H
