#include "device.h"

DeviceBase::DeviceBase(DeviceIdentity identity) :
    mIdentity(std::move(identity))
{
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

std::unique_ptr<DeviceBase> DeviceFactory::create(const DeviceIdentity& identity) const
{
    if (identity.deviceClass == QStringLiteral("Ad042Device"))
        return std::make_unique<Ad042Device>(identity);
    if (identity.deviceClass == QStringLiteral("Ad052Device"))
        return std::make_unique<Ad052Device>(identity);
    if (identity.deviceClass == QStringLiteral("Ad021Device"))
        return std::make_unique<Ad021Device>(identity);
    return std::make_unique<DeviceBase>(identity);
}
