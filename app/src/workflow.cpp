#include "workflow.h"
#include "app_edition.h"
#include "workflow_definition.h"

#include <QCoreApplication>
#include <utility>

WorkflowRunner::WorkflowRunner(WorkflowRepository* workflows, QObject* parent) :
    QObject(parent),
    mWorkflows(workflows)
{
}

void WorkflowRunner::setWorkflowRepository(WorkflowRepository* workflows)
{
    mWorkflows = workflows;
}

void WorkflowRunner::setCancellationToken(std::shared_ptr<std::atomic_bool> token)
{
    mCancelToken = std::move(token);
}

bool WorkflowRunner::run(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters)
{
    mLastError = {};
    emit logMessage(QStringLiteral("Starting %1 for %2 device(s)").arg(action.id).arg(devices.size()));

    if (!AppEdition::allowsAction(action.id))
    {
        mLastError.code = QStringLiteral("POLICY_ACTION_DENIED");
        mLastError.category = QStringLiteral("policy");
        mLastError.operationId = action.id;
        mLastError.safeMessage = QStringLiteral("Действие недоступно в этой редакции");
        emit logMessage(QStringLiteral("Action %1 is not available in the %2 edition")
            .arg(action.id, AppEdition::id()));
        emit failureStage(QStringLiteral("edition.action"), action.id);
        return false;
    }

    QString currentOperation;
    QString currentStage;
    WorkflowCallbacks callbacks;
    callbacks.logMessage = [this](const QString& message) {
        emit logMessage(message);
    };
    callbacks.transportLogMessage = [this](const QString& message) {
        emit transportLogMessage(message);
    };
    callbacks.progressChanged = [this](int percent) {
        emit progressChanged(percent);
    };
    callbacks.stageChanged = [this, &currentOperation, &currentStage](const QString& operation, const QString& stage) {
        currentOperation = operation;
        currentStage = stage;
        emit stageChanged(operation, stage);
    };
    callbacks.stepCompleted = [this](const QString& operation) {
        emit stepCompleted(operation);
    };
    callbacks.recoveryEvent = [this](const QString& state) {
        emit recoveryEvent(state);
    };
    callbacks.processEvents = []() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    };
    callbacks.shouldCancel = [this]() {
        return mCancelToken && mCancelToken->load();
    };

    bool successful = true;
    for (const std::shared_ptr<DeviceBase>& device : devices)
    {
        if (!device)
            continue;

        QString workflowId = action.workflow;
        const QString targetFirmwareId = parameters.value(QStringLiteral("targetFirmwareId")).toString();
        if (!targetFirmwareId.isEmpty())
        {
            const FirmwareVersionSpec* targetFirmware = device->firmwareVersionById(targetFirmwareId);
            if (targetFirmware && !targetFirmware->installation.workflow.isEmpty())
                workflowId = targetFirmware->installation.workflow;
        }
        std::shared_ptr<const WorkflowDefinition> definition;
        if (mWorkflows)
            definition = workflowId.isEmpty()
                ? mWorkflows->snapshotFor(action)
                : mWorkflows->snapshotForId(workflowId);
        if (!definition)
        {
            mLastError.code = QStringLiteral("CONFIG_UNKNOWN_WORKFLOW");
            mLastError.category = QStringLiteral("configuration");
            mLastError.operationId = workflowId;
            mLastError.safeMessage = QStringLiteral("Сценарий действия не найден");
            emit logMessage(QStringLiteral("Workflow %1 is not defined").arg(workflowId));
            emit failureStage(QStringLiteral("workflow.definition"), workflowId);
            successful = false;
            continue;
        }

        emit definitionSelected(definition->id, definition->version);

        WorkflowExecution workflow(*definition, action, parameters, callbacks);
        while (workflow.next(*device))
        {
        }
        if (!workflow.isSuccessful())
        {
            mLastError = workflow.error();
            emit failureStage(currentOperation, currentStage);
        }
        successful = workflow.isSuccessful() && successful;
    }

    emit logMessage(successful
        ? QStringLiteral("Workflow %1 completed successfully").arg(action.id)
        : QStringLiteral("Workflow %1 failed").arg(action.id));
    return successful;
}
