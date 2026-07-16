#ifndef DEVICE_WORKBENCH_DEVICE_H
#define DEVICE_WORKBENCH_DEVICE_H

#include "models.h"
#include "device_transport.h"

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
    explicit DeviceBase(DeviceIdentity identity, std::shared_ptr<IDeviceTransport> transport = {});
    virtual ~DeviceBase() = default;

    const DeviceIdentity& identity() const { return mIdentity; }
    void updateIdentity(DeviceIdentity identity);
    virtual QString className() const;
    virtual FlashPlan flashPlan(const ActionSpec& action) const;
    virtual QStringList beforeFlashWrite(const FlashPlan& plan) const;
    virtual QStringList afterFlashWrite(const FlashPlan& plan) const;
    virtual int productionDateRegisterIndex() const;
    virtual int serialNumberRegisterIndex() const;
    virtual qint32 productionDateExitValue() const;
    virtual bool reset(QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool writeInt(quint16 index, qint32 value, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool readInt(quint16 index, qint32* value, QString* error = nullptr, QString* rawResponse = nullptr) const;

protected:
    DeviceIdentity mIdentity;
    std::shared_ptr<IDeviceTransport> mTransport;
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

class BocV12Device : public DeviceBase
{
public:
    using DeviceBase::DeviceBase;
    QString className() const override;
    int productionDateRegisterIndex() const override;
    int serialNumberRegisterIndex() const override;
    qint32 productionDateExitValue() const override;
};

class DeviceFactory
{
public:
    explicit DeviceFactory(std::shared_ptr<IDeviceTransport> transport = {});

    std::shared_ptr<DeviceBase> create(const DeviceIdentity& identity) const;

private:
    std::shared_ptr<IDeviceTransport> mTransport;
};

#endif // DEVICE_WORKBENCH_DEVICE_H
