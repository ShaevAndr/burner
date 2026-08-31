#ifndef DEVICE_WORKBENCH_FIRMWARE_FLASH_STRATEGY_H
#define DEVICE_WORKBENCH_FIRMWARE_FLASH_STRATEGY_H

#include "base_device.h"
#include "models.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <functional>

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
    int firstWrittenPage = 0;
    QVector<int> expectedPageNumbers;
    QVector<QByteArray> expectedPages;
};

struct FirmwareFlashCallbacks
{
    std::function<void(const QString&)> log;
    std::function<void(const QString&)> transportLog;
    std::function<void(int)> progress;
    std::function<void()> processEvents;
};

class FirmwareFlashStrategy
{
public:
    virtual ~FirmwareFlashStrategy() = default;
    virtual QString id() const = 0;
    virtual bool flash(DeviceBase& device,
        FirmwareFlashPlan& plan,
        const FirmwareFlashCallbacks& callbacks) const = 0;
};

class FirmwareFlashStrategyRegistry
{
public:
    static const FirmwareFlashStrategy* find(const QString& strategyId);
};

#endif // DEVICE_WORKBENCH_FIRMWARE_FLASH_STRATEGY_H
