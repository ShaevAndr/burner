#ifndef DEVICE_WORKBENCH_DEVICE_TRANSPORT_H
#define DEVICE_WORKBENCH_DEVICE_TRANSPORT_H

#include "models.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class IDeviceTransport
{
public:
    virtual ~IDeviceTransport() = default;
    virtual bool writeRegister(const DeviceIdentity& device, quint16 index, qint32 value, QString* error, QString* rawResponse = nullptr) = 0;
    virtual bool writeRegisterNoReply(const DeviceIdentity& device, quint16 index, qint32 value, QString* error, QString* rawResponse = nullptr)
    {
        return writeRegister(device, index, value, error, rawResponse);
    }
    virtual bool readRegister(const DeviceIdentity& device, quint16 index, qint32* value, QString* error, QString* rawResponse = nullptr) = 0;
    virtual bool readUuid(const DeviceIdentity&, QString* uuid, QString* error, QString* = nullptr)
    {
        if (uuid)
            uuid->clear();
        if (error)
            *error = QStringLiteral("UUID read is not supported by this transport");
        return false;
    }
    virtual bool resetDevice(const DeviceIdentity& device, QString* error, QString* rawResponse = nullptr) = 0;
    virtual bool flashGetParams(const DeviceIdentity&, QVector<FlashMemoryParams>* params, QString* error, QString* = nullptr)
    {
        if (params)
            params->clear();
        if (error)
            *error = QStringLiteral("Flash params are not supported by this transport");
        return false;
    }
    virtual bool flashWritePage(const DeviceIdentity&, int, int, const QByteArray&, QString* error, QString* = nullptr)
    {
        if (error)
            *error = QStringLiteral("Flash write is not supported by this transport");
        return false;
    }
    virtual bool flashReadPage(const DeviceIdentity&, int, int, QByteArray*, QString* error, QString* = nullptr)
    {
        if (error)
            *error = QStringLiteral("Flash read is not supported by this transport");
        return false;
    }
    virtual bool waitForDeviceIdentity(const DeviceIdentity&, int, int, DeviceIdentity*, QString* error, QString* = nullptr)
    {
        if (error)
            *error = QStringLiteral("Device discovery is not supported by this transport");
        return false;
    }
};

#endif // DEVICE_WORKBENCH_DEVICE_TRANSPORT_H
