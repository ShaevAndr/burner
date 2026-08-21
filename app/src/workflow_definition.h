#ifndef DEVICE_WORKBENCH_WORKFLOW_DEFINITION_H
#define DEVICE_WORKBENCH_WORKFLOW_DEFINITION_H

#include "base_device.h"
#include "models.h"

#include <QHash>
#include <QVariantMap>
#include <QVector>
#include <functional>

struct WorkflowStep
{
    QString op;
    QString label;
    QString message;
    QString skipIfState;
    QString skippedLog;
    QVariantMap arguments;
    int retryAttempts = 1;
};

struct WorkflowDefinition
{
    QString id;
    QVector<WorkflowStep> steps;
};

struct WorkflowCallbacks
{
    std::function<void(const QString&)> logMessage;
    std::function<void(const QString&)> transportLogMessage;
    std::function<void(int)> progressChanged;
    std::function<void()> processEvents;
};

struct WorkflowFlashPlan
{
    QString workflowId;
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
    QVector<QByteArray> expectedPages;
};

struct WorkflowContext
{
    qint64 productionTimestamp = 0;
    int serialNumber = 0;
    WorkflowFlashPlan flashPlan;
    QString transportError;
    QString transportRaw;
};

class WorkflowRepository
{
public:
    bool load(const QString& fileName, QString* error = nullptr);
    const WorkflowDefinition* definitionFor(const ActionSpec& action) const;
    bool isEmpty() const { return mDefinitions.isEmpty(); }

private:
    QHash<QString, WorkflowDefinition> mDefinitions;
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
    bool runOperationWithRetry(const DeviceIdentity& identity,
        const WorkflowStep& step,
        const DeviceOperation& operation,
        const QVariantMap& arguments);
    bool isRuntimeStep(const QString& operation) const;
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
    int mNextStep = 0;
    bool mFinished = false;
    bool mSuccessful = true;
};

#endif // DEVICE_WORKBENCH_WORKFLOW_DEFINITION_H
