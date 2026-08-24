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

bool DeviceBase::disableLoadApplication(QString* error, QString* rawResponse) const
{
    return writeInt(0, 0, error, rawResponse);
}

bool DeviceBase::writeProductionDate(qint32 timestamp, QString* error, QString* rawResponse) const
{
    if (mIdentity.productionDateRegister < 0)
    {
        if (error)
            *error = QStringLiteral("Production date register is not configured for %1").arg(className());
        return false;
    }
    return writeInt(quint16(mIdentity.productionDateRegister), timestamp, error, rawResponse);
}

bool DeviceBase::writeSerialNumber(qint32 serialNumber, QString* error, QString* rawResponse) const
{
    if (mIdentity.serialNumberRegister < 0)
    {
        if (error)
            *error = QStringLiteral("Serial number register is not configured for %1").arg(className());
        return false;
    }
    return writeInt(quint16(mIdentity.serialNumberRegister), serialNumber, error, rawResponse);
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

bool DeviceBase::readUuid(QString* uuid, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->readUuid(mIdentity, uuid, error, rawResponse);
}

bool DeviceBase::flashGetParams(QVector<FlashMemoryParams>* params, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->flashGetParams(mIdentity, params, error, rawResponse);
}

bool DeviceBase::flashWritePage(int flashNum, int pageNum, const QByteArray& page, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->flashWritePage(mIdentity, flashNum, pageNum, page, error, rawResponse);
}

bool DeviceBase::flashReadPage(int flashNum, int pageNum, QByteArray* page, QString* error, QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->flashReadPage(mIdentity, flashNum, pageNum, page, error, rawResponse);
}

bool DeviceBase::waitForDeviceIdentity(const DeviceIdentity& expected,
    int timeoutMs,
    int pollIntervalMs,
    DeviceIdentity* identity,
    QString* error,
    QString* rawResponse) const
{
    if (!mTransport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mTransport->waitForDeviceIdentity(expected, timeoutMs, pollIntervalMs, identity, error, rawResponse);
}

bool DeviceBase::waitForDeviceIdentity(int timeoutMs, int pollIntervalMs, DeviceIdentity* identity, QString* error, QString* rawResponse) const
{
    return waitForDeviceIdentity(mIdentity, timeoutMs, pollIntervalMs, identity, error, rawResponse);
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
        },
        {
            QStringLiteral("flash.writePage"),
            [this](const QVariantMap& arguments, QString* error, QString* rawResponse) {
                return flashWritePage(arguments.value(QStringLiteral("flashNum")).toInt(),
                    arguments.value(QStringLiteral("pageNum")).toInt(),
                    arguments.value(QStringLiteral("page")).toByteArray(),
                    error,
                    rawResponse);
            }
        }
    };
}

DeviceOperation DeviceBase::operation(const QString& key) const
{
    return operations().value(key);
}
