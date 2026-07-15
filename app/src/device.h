#ifndef DEVICE_WORKBENCH_DEVICE_H
#define DEVICE_WORKBENCH_DEVICE_H

#include "models.h"

#include <memory>

struct FlashPlan
{
    QString workflowId;
    QString target;
    FirmwareArtifact artifact;
    int flashNum = 0;
    int pageSize = 2048;
    int beginPage = 0;
    int endPage = 0;
    QString notes;
};

class DeviceBase
{
public:
    explicit DeviceBase(DeviceIdentity identity);
    virtual ~DeviceBase() = default;

    const DeviceIdentity& identity() const { return mIdentity; }
    virtual QString className() const;
    virtual FlashPlan flashPlan(const ActionSpec& action) const;
    virtual QStringList beforeFlashWrite(const FlashPlan& plan) const;
    virtual QStringList afterFlashWrite(const FlashPlan& plan) const;

protected:
    DeviceIdentity mIdentity;
};

class Ad042Device : public DeviceBase
{
public:
    using DeviceBase::DeviceBase;
    QString className() const override;
    FlashPlan flashPlan(const ActionSpec& action) const override;
};

class Ad052Device : public DeviceBase
{
public:
    using DeviceBase::DeviceBase;
    QString className() const override;
};

class Ad021Device : public DeviceBase
{
public:
    using DeviceBase::DeviceBase;
    QString className() const override;
    FlashPlan flashPlan(const ActionSpec& action) const override;
};

class DeviceFactory
{
public:
    std::unique_ptr<DeviceBase> create(const DeviceIdentity& identity) const;
};

#endif // DEVICE_WORKBENCH_DEVICE_H
