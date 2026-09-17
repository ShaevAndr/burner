#ifndef DEVICE_WORKBENCH_WORKFLOW_DEFINITION_H
#define DEVICE_WORKBENCH_WORKFLOW_DEFINITION_H

#include "base_device.h"
#include "firmware_flash_strategy.h"
#include "models.h"
#include "operation_registry.h"

#include <QHash>
#include <QReadWriteLock>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <memory>

struct WorkflowStep
{
    QString op;
    QString label;
    QString message;
    QString skipIfState;
    QString skippedLog;
    QVariantMap arguments;
    int retryAttempts = 1;
    const OperationContract* contract = nullptr;
};

enum class RecoveryKind { None, LoadApplication, ManualBootloader };

struct RecoveryPlan
{
    RecoveryKind kind = RecoveryKind::None;
    int timeoutMs = 30000;
    int pollIntervalMs = 500;
};

struct WorkflowDefinition
{
    QString id;
    QString version;
    RecoveryPlan recovery;
    QVector<WorkflowStep> steps;
};

struct WorkflowCallbacks
{
    std::function<void(const QString&)> logMessage;
    std::function<void(const QString&)> transportLogMessage;
    std::function<void(int)> progressChanged;
    std::function<void(const QString&, const QString&)> stageChanged;
    std::function<void(const QString&)> stepCompleted;
    std::function<void()> processEvents;
    std::function<bool()> shouldCancel;
};

struct WorkflowContext
{
    qint64 productionTimestamp = 0;
    int serialNumber = 0;
    FirmwareFlashPlan flashPlan;
    FirmwareVersionSpec targetFirmware;
    QString targetFirmwareId;
    DeviceIdentity reappearedIdentity;
    qint32 preservedProductionDate = 0;
    qint32 preservedSerialNumber = 0;
    bool hasPreservedProductionDate = false;
    bool hasPreservedSerialNumber = false;
    bool transitionValidated = false;
    bool flashWritten = false;
    bool applicationLoadingDisabled = false;
    QString transportError;
    QString transportRaw;
};

class WorkflowRepository
{
public:
    bool load(const QString& fileName, QString* error = nullptr);
    std::shared_ptr<const WorkflowDefinition> snapshotFor(const ActionSpec& action) const;
    std::shared_ptr<const WorkflowDefinition> snapshotForId(const QString& workflowId) const;
    void replaceWith(const WorkflowRepository& other);
    bool isEmpty() const;

private:
    mutable QReadWriteLock mLock;
    QHash<QString, std::shared_ptr<const WorkflowDefinition>> mDefinitions;
};

class WorkflowExecution
{
public:
    WorkflowExecution(const WorkflowDefinition& definition,
        const ActionSpec& action,
        QVariantMap parameters,
        WorkflowCallbacks callbacks);

    bool next(DeviceBase& device);
    bool isFinished() const { return mFinished; }
    bool isSuccessful() const { return mSuccessful; }
    const OperationError& error() const { return mError; }

private:
    void log(const QString& message) const;
    void transportLog(const QString& message) const;
    void progress(int percent) const;
    void processEvents() const;
    int completedProgressPercent() const;
    void reportProgressStage(const DeviceIdentity& identity, const QString& stage) const;
    void emitTransportLog(const DeviceIdentity& identity);
    bool ensureDeviceUuid(DeviceBase& device);
    bool disableApplicationLoading(DeviceBase& device);
    void restoreApplicationAfterFailure(DeviceBase& device);
    bool runOperationWithRetry(const DeviceIdentity& identity,
        const WorkflowStep& step,
        const DeviceOperation& operation,
        const QVariantMap& arguments);
    bool executeRuntimeStep(DeviceBase& device, const WorkflowStep& step);
    bool executeDeviceStep(DeviceBase& device, const WorkflowStep& step);
    bool verifyFlashPages(DeviceBase& device);
    QVariantMap resolveArguments(const WorkflowStep& step) const;
    QString formatMessage(const DeviceIdentity& identity, const QString& message, const QVariantMap& arguments) const;

    const WorkflowDefinition& mDefinition;
    ActionSpec mAction;
    QVariantMap mParameters;
    WorkflowCallbacks mCallbacks;
    WorkflowContext mContext;
    OperationError mError;
    int mNextStep = 0;
    bool mFinished = false;
    bool mSuccessful = true;
};

#endif // DEVICE_WORKBENCH_WORKFLOW_DEFINITION_H
