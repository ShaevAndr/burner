#ifndef DEVICE_WORKBENCH_DEVICE_TRANSPORT_H
#define DEVICE_WORKBENCH_DEVICE_TRANSPORT_H

#include "models.h"

#include <QString>

class IDeviceTransport
{
public:
    virtual ~IDeviceTransport() = default;
    virtual bool writeRegister(const DeviceIdentity& device, quint16 index, qint32 value, QString* error, QString* rawResponse = nullptr) = 0;
    virtual bool readRegister(const DeviceIdentity& device, quint16 index, qint32* value, QString* error, QString* rawResponse = nullptr) = 0;
    virtual bool resetDevice(const DeviceIdentity& device, QString* error, QString* rawResponse = nullptr) = 0;
};

#endif // DEVICE_WORKBENCH_DEVICE_TRANSPORT_H
