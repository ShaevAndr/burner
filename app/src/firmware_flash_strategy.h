#ifndef DEVICE_WORKBENCH_FIRMWARE_FLASH_STRATEGY_H
#define DEVICE_WORKBENCH_FIRMWARE_FLASH_STRATEGY_H

#include "base_device.h"
#include "models.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <memory>

struct FirmwareWritePlan
{
    int flashNum = 0;
    int pageSize = 0;
    QVector<int> pageNumbers;
    QVector<QByteArray> pages;
};

struct FirmwareFlashPlan
{
    QString workflowId;
    QString strategyId;
    QVariantMap strategyParameters;
    QString target;
    FirmwareArtifact artifact;
    QString fileName;
    QByteArray data;
    bool verifyAfterWrite = true;
    int flashNum = 0;
    int offset = 0;
    int pageSize = 2048;
    int beginPage = 0;
    int endPage = 0;
    std::shared_ptr<const FirmwareWritePlan> writePlan;
};

struct FirmwareFlashCallbacks
{
    std::function<void(const QString&)> log;
    std::function<void(const QString&)> transportLog;
    std::function<void(int)> progress;
    std::function<void()> processEvents;
    std::function<bool()> shouldCancel;
};

// Offline validation runs before any reset or flash write.
bool validateFirmwareImage(const FirmwareFlashPlan& plan, QString* error = nullptr);

class FirmwareFlashStrategy
{
public:
    virtual ~FirmwareFlashStrategy() = default;
    virtual QString id() const = 0;
    virtual bool prepare(DeviceBase& device,
        FirmwareFlashPlan& plan,
        const FirmwareFlashCallbacks& callbacks) const = 0;
    virtual bool flash(DeviceBase& device,
        const FirmwareFlashPlan& plan,
        const FirmwareFlashCallbacks& callbacks) const = 0;
};

class FirmwareFlashStrategyRegistry
{
public:
    static const FirmwareFlashStrategy* find(const QString& strategyId);
};

#endif // DEVICE_WORKBENCH_FIRMWARE_FLASH_STRATEGY_H
