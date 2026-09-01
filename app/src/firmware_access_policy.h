#ifndef DEVICE_WORKBENCH_FIRMWARE_ACCESS_POLICY_H
#define DEVICE_WORKBENCH_FIRMWARE_ACCESS_POLICY_H

#include "app_edition.h"
#include "models.h"

#include <QFileInfo>

namespace FirmwareAccessPolicy
{
inline QString artifactFileName(const FirmwareVersionSpec* firmware)
{
    return firmware ? QFileInfo(firmware->artifact.relativePath).fileName() : QString();
}

inline bool isRestrictedExternalBocV6(const DeviceIdentity& identity)
{
    return !AppEdition::isInternal() && identity.catalogId == QStringLiteral("boc.v6");
}

inline bool isTargetAllowed(const DeviceIdentity& identity, const QString& targetFirmwareId)
{
    if (!identity.isFirmwareTargetAllowed(targetFirmwareId))
        return false;

    if (!isRestrictedExternalBocV6(identity))
        return true;

    const FirmwareVersionSpec* current = identity.firmwareVersionById(identity.currentFirmwareId);
    const FirmwareVersionSpec* target = identity.firmwareVersionById(targetFirmwareId);
    return artifactFileName(current).compare(
               QStringLiteral("BOCv6_ADCVibr_Digital20260721_1228.hex"),
               Qt::CaseInsensitive) == 0
        && artifactFileName(target).compare(
               QStringLiteral("BOCv6_ADCVibr_Digital20260831_1007.hex"),
               Qt::CaseInsensitive) == 0;
}
}

#endif // DEVICE_WORKBENCH_FIRMWARE_ACCESS_POLICY_H
