#include "base_device.h"
#include "transport/unicorn_ascii_transport.h"

#include <utility>

DeviceBase::DeviceBase(DeviceIdentity identity, std::shared_ptr<IDeviceTransport> transport) :
    mIdentity(std::move(identity)),
    mTransport(transport ? std::move(transport) : createUnicornAsciiTransport())
{
}

void DeviceBase::updateIdentity(DeviceIdentity identity)
{
    mIdentity = std::move(identity);
}

QString DeviceBase::className() const
{
    return QStringLiteral("DeviceBase");
}

bool DeviceBase::reset(QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->resetDevice(mIdentity, error, rawResponse);
}

bool DeviceBase::loadApplication(QString* error, QString* rawResponse) const
{
    return writeInt(0, 1, error, rawResponse);
}

bool DeviceBase::loadApplicationNoReply(QString* error, QString* rawResponse) const
{
    return writeIntNoReply(0, 1, error, rawResponse);
}

bool DeviceBase::disableLoadApplication(QString* error, QString*) const
{
    if (error)
        *error = QStringLiteral("Disable load application is not supported for %1").arg(className());
    return false;
}

bool DeviceBase::writeProductionDate(qint32, QString* error, QString*) const
{
    if (error)
        *error = QStringLiteral("Production date update is not supported for %1").arg(className());
    return false;
}

bool DeviceBase::writeSerialNumber(qint32, QString* error, QString*) const
{
    if (error)
        *error = QStringLiteral("Serial number update is not supported for %1").arg(className());
    return false;
}

bool DeviceBase::writeInt(quint16 index, qint32 value, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->writeRegister(mIdentity, index, value, error, rawResponse);
}

bool DeviceBase::writeIntNoReply(quint16 index, qint32 value, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->writeRegisterNoReply(mIdentity, index, value, error, rawResponse);
}

bool DeviceBase::readInt(quint16 index, qint32* value, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->readRegister(mIdentity, index, value, error, rawResponse);
}

QHash<QString, DeviceOperation> DeviceBase::operations() const
{
    return {
        {
            QStringLiteral("device.reset"),
            [this](const QVariantMap&, QString* error, QString* rawResponse) {
                return reset(error, rawResponse);
            }
        },
        {
            QStringLiteral("device.loadApplication"),
            [this](const QVariantMap&, QString* error, QString* rawResponse) {
                return loadApplication(error, rawResponse);
            }
        },
        {
            QStringLiteral("device.loadApplicationNoReply"),
            [this](const QVariantMap&, QString* error, QString* rawResponse) {
                return loadApplicationNoReply(error, rawResponse);
            }
        },
        {
            QStringLiteral("device.disableLoadApplication"),
            [this](const QVariantMap&, QString* error, QString* rawResponse) {
                return disableLoadApplication(error, rawResponse);
            }
        },
        {
            QStringLiteral("device.writeProductionDate"),
            [this](const QVariantMap& arguments, QString* error, QString* rawResponse) {
                return writeProductionDate(arguments.value(QStringLiteral("value")).toInt(), error, rawResponse);
            }
        },
        {
            QStringLiteral("device.writeSerialNumber"),
            [this](const QVariantMap& arguments, QString* error, QString* rawResponse) {
                return writeSerialNumber(arguments.value(QStringLiteral("value")).toInt(), error, rawResponse);
            }
        },
        {
            QStringLiteral("device.ping"),
            [this](const QVariantMap& arguments, QString* error, QString* rawResponse) {
                const quint16 index = quint16(arguments.value(QStringLiteral("index"), 0).toUInt());
                qint32 value = 0;
                return readInt(index, &value, error, rawResponse);
            }
        }
    };
}

DeviceOperation DeviceBase::operation(const QString& key) const
{
    return operations().value(key);
}
