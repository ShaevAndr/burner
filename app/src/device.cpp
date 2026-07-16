#include "device.h"
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

FlashPlan DeviceBase::flashPlan(const ActionSpec& action) const
{
    FlashPlan plan;
    plan.workflowId = mIdentity.flashWorkflows.value(action.target, action.workflow);
    plan.target = action.target;
    plan.artifact = mIdentity.firmwareForTarget(action.target);
    plan.notes = QStringLiteral("Default flash.write plan for %1").arg(mIdentity.typeHex());
    return plan;
}

QStringList DeviceBase::beforeFlashWrite(const FlashPlan& plan) const
{
    return {
        QStringLiteral("lock device"),
        QStringLiteral("read identity before writing %1").arg(plan.target),
        QStringLiteral("prepare %1 flash area").arg(plan.target)
    };
}

QStringList DeviceBase::afterFlashWrite(const FlashPlan& plan) const
{
    return {
        QStringLiteral("verify %1 flash").arg(plan.target),
        QStringLiteral("refresh identity")
    };
}

int DeviceBase::productionDateRegisterIndex() const
{
    return -1;
}

int DeviceBase::serialNumberRegisterIndex() const
{
    return -1;
}

qint32 DeviceBase::productionDateExitValue() const
{
    return 1;
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

QString Ad042Device::className() const
{
    return QStringLiteral("Ad042Device");
}

FlashPlan Ad042Device::flashPlan(const ActionSpec& action) const
{
    FlashPlan plan = DeviceBase::flashPlan(action);
    plan.pageSize = 2048;
    plan.endPage = action.target == QStringLiteral("bootloader") ? 15 : 127;
    plan.notes = QStringLiteral("AD-042 uses 2048-byte block flash pages");
    return plan;
}

QString Ad052Device::className() const
{
    return QStringLiteral("Ad052Device");
}

QString Ad021Device::className() const
{
    return QStringLiteral("Ad021Device");
}

FlashPlan Ad021Device::flashPlan(const ActionSpec& action) const
{
    FlashPlan plan = DeviceBase::flashPlan(action);
    plan.pageSize = 1024;
    plan.endPage = action.target == QStringLiteral("bootloader") ? 7 : 63;
    plan.notes = QStringLiteral("AD-021 has smaller flash pages; bootloader target is constrained");
    return plan;
}

QString BocV12Device::className() const
{
    return QStringLiteral("BocV12Device");
}

int BocV12Device::productionDateRegisterIndex() const
{
    return 9;
}

int BocV12Device::serialNumberRegisterIndex() const
{
    return 10;
}

qint32 BocV12Device::productionDateExitValue() const
{
    return 1;
}

DeviceFactory::DeviceFactory(std::shared_ptr<IDeviceTransport> transport) :
    mTransport(std::move(transport))
{
}

std::shared_ptr<DeviceBase> DeviceFactory::create(const DeviceIdentity& identity) const
{
    const std::shared_ptr<IDeviceTransport> transport = mTransport ? mTransport : createUnicornAsciiTransport();
    if (identity.catalogId == QStringLiteral("boc.v12") || identity.deviceClass == QStringLiteral("BocV12Device"))
        return std::make_shared<BocV12Device>(identity, transport);
    if (identity.deviceClass == QStringLiteral("Ad042Device"))
        return std::make_shared<Ad042Device>(identity, transport);
    if (identity.deviceClass == QStringLiteral("Ad052Device"))
        return std::make_shared<Ad052Device>(identity, transport);
    if (identity.deviceClass == QStringLiteral("Ad021Device"))
        return std::make_shared<Ad021Device>(identity, transport);
    return std::make_shared<DeviceBase>(identity, transport);
}
