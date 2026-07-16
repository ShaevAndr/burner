#ifndef DEVICE_WORKBENCH_WORKFLOW_H
#define DEVICE_WORKBENCH_WORKFLOW_H

#include "device.h"
#include "device_transport.h"

#include <QObject>
#include <QVariantMap>
#include <memory>

class WorkflowRunner : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowRunner(QObject* parent = nullptr);

    void run(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters = {});

signals:
    void logMessage(QString message);
    void transportLogMessage(QString message);
    void progressChanged(int percent);

};

#endif // DEVICE_WORKBENCH_WORKFLOW_H
