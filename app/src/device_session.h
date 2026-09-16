#ifndef DEVICE_WORKBENCH_DEVICE_SESSION_H
#define DEVICE_WORKBENCH_DEVICE_SESSION_H

#include "device_transport.h"
#include "models.h"

#include <memory>

// A runtime connection to one physical device. The profile is a snapshot of
// the catalog version used when the session was created.
struct DeviceSession
{
    DeviceIdentity identity;
    std::shared_ptr<const DeviceProfile> profile;
    std::shared_ptr<IDeviceTransport> transport;

    QString physicalKey() const
    {
        if (!identity.uuid.trimmed().isEmpty())
            return QStringLiteral("uuid:") + identity.uuid.trimmed().toLower();
        if (identity.endpoint.trimmed().isEmpty())
            return {};
        return QStringLiteral("endpoint:") + identity.protocol.toLower()
            + QLatin1Char('|') + identity.channel.toLower()
            + QLatin1Char('|') + identity.endpoint.trimmed().toLower();
    }

    QStringList physicalKeys() const
    {
        QStringList keys;
        if (!identity.uuid.trimmed().isEmpty())
            keys.append(QStringLiteral("uuid:") + identity.uuid.trimmed().toLower());
        if (!identity.endpoint.trimmed().isEmpty())
            keys.append(QStringLiteral("endpoint:") + identity.protocol.toLower()
                + QLatin1Char('|') + identity.channel.toLower()
                + QLatin1Char('|') + identity.endpoint.trimmed().toLower());
        return keys;
    }
};

#endif // DEVICE_WORKBENCH_DEVICE_SESSION_H
