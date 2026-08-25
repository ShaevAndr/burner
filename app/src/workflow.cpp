#include "workflow.h"
#include "workflow_definition.h"

#include <QCoreApplication>

namespace
{
bool requiresFactorySettingsKey(const QString& actionId)
{
    return actionId == QStringLiteral("device.productionDate.update")
        || actionId == QStringLiteral("device.serialNumber.update");
}

bool hasFactorySettingsKey(const QVariantMap& parameters)
{
    const auto key = parameters.constFind(QStringLiteral("factorySettingsKey"));
    if (key == parameters.constEnd() || !key.value().isValid() || key.value().isNull())
        return false;
    if (key.value().type() == QVariant::String)
        return !key.value().toString().trimmed().isEmpty();
    if (key.value().type() == QVariant::ByteArray)
        return !key.value().toByteArray().trimmed().isEmpty();
    return true;
}
}

WorkflowRunner::WorkflowRunner(WorkflowRepository* workflows, QObject* parent) :
    QObject(parent),
    mWorkflows(workflows)
{
}

void WorkflowRunner::setWorkflowRepository(WorkflowRepository* workflows)
{
    mWorkflows = workflows;
}

bool WorkflowRunner::run(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters)
{
    emit logMessage(QStringLiteral("Starting %1 for %2 device(s)").arg(action.id).arg(devices.size()));

    if (requiresFactorySettingsKey(action.id) && !hasFactorySettingsKey(parameters))
    {
        emit logMessage(QStringLiteral(
            "Skipped %1 for %2 device(s): factory settings key is empty; protected settings were not changed")
            .arg(action.id).arg(devices.size()));
        emit progressChanged(100);
        return true;
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
    callbacks.processEvents = []() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
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
            const FirmwareVersionSpec* targetFirmware = device->identity().firmwareVersionById(targetFirmwareId);
            if (targetFirmware && !targetFirmware->installation.workflow.isEmpty())
                workflowId = targetFirmware->installation.workflow;
        }
        const WorkflowDefinition* definition = nullptr;
        if (mWorkflows)
            definition = workflowId.isEmpty()
                ? mWorkflows->definitionFor(action)
                : mWorkflows->definitionForId(workflowId);
        if (!definition)
        {
            emit logMessage(QStringLiteral("Workflow %1 is not defined").arg(workflowId));
            emit failureStage(QStringLiteral("workflow.definition"), workflowId);
            successful = false;
            continue;
        }

        WorkflowExecution workflow(*definition, action, parameters, callbacks);
        while (workflow.next(*device))
        {
        }
        if (!workflow.isSuccessful())
            emit failureStage(currentOperation, currentStage);
        successful = workflow.isSuccessful() && successful;
    }

    emit logMessage(successful
        ? QStringLiteral("Workflow %1 completed successfully").arg(action.id)
        : QStringLiteral("Workflow %1 failed").arg(action.id));
    return successful;
}
