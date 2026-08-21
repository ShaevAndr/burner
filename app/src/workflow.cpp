#include "workflow.h"
#include "workflow_definition.h"

#include <QCoreApplication>

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
    const WorkflowDefinition* definition = mWorkflows ? mWorkflows->definitionFor(action) : nullptr;
    if (!definition)
    {
        emit logMessage(QStringLiteral("Workflow %1 is not defined").arg(action.workflow));
        return false;
    }

    emit logMessage(QStringLiteral("Starting %1 for %2 device(s)").arg(action.id).arg(devices.size()));

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
    callbacks.processEvents = []() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    };

    bool successful = true;
    for (const std::shared_ptr<DeviceBase>& device : devices)
    {
        if (!device)
            continue;

        WorkflowExecution workflow(*definition, action, parameters, callbacks);
        while (workflow.next(*device))
        {
        }
        successful = workflow.isSuccessful() && successful;
    }

    emit logMessage(successful
        ? QStringLiteral("Workflow %1 completed successfully").arg(action.id)
        : QStringLiteral("Workflow %1 failed").arg(action.id));
    return successful;
}
