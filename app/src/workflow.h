#ifndef DEVICE_WORKBENCH_WORKFLOW_H
#define DEVICE_WORKBENCH_WORKFLOW_H

#include "base_device.h"
#include "device_transport.h"
#include "workflow_definition.h"

#include <QObject>
#include <QVariantMap>
#include <atomic>
#include <memory>

class WorkflowRunner : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowRunner(WorkflowRepository* workflows = nullptr, QObject* parent = nullptr);
    void setWorkflowRepository(WorkflowRepository* workflows);
    void setCancellationToken(std::shared_ptr<std::atomic_bool> token);
    const OperationError& lastError() const { return mLastError; }

    bool run(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters = {});

signals:
    void logMessage(QString message);
    void transportLogMessage(QString message);
    void progressChanged(int percent);
    void stageChanged(QString operation, QString stage);
    void stepCompleted(QString operation);
    void failureStage(QString operation, QString stage);
    void definitionSelected(QString workflowId, QString version);

private:
    WorkflowRepository* mWorkflows = nullptr;
    std::shared_ptr<std::atomic_bool> mCancelToken;
    OperationError mLastError;
};

#endif // DEVICE_WORKBENCH_WORKFLOW_H
