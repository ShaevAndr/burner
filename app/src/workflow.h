#ifndef DEVICE_WORKBENCH_WORKFLOW_H
#define DEVICE_WORKBENCH_WORKFLOW_H

#include "base_device.h"
#include "device_transport.h"
#include "workflow_definition.h"

#include <QObject>
#include <QVariantMap>
#include <memory>

class WorkflowRunner : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowRunner(WorkflowRepository* workflows = nullptr, QObject* parent = nullptr);
    void setWorkflowRepository(WorkflowRepository* workflows);

    void run(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters = {});

signals:
    void logMessage(QString message);
    void transportLogMessage(QString message);
    void progressChanged(int percent);

private:
    WorkflowRepository* mWorkflows = nullptr;
};

#endif // DEVICE_WORKBENCH_WORKFLOW_H
