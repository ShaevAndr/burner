#ifndef DEVICE_WORKBENCH_WORKFLOW_H
#define DEVICE_WORKBENCH_WORKFLOW_H

#include "device.h"

#include <QObject>

class WorkflowRunner : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowRunner(QObject* parent = nullptr);

    void run(const ActionSpec& action, const QVector<DeviceIdentity>& devices);

signals:
    void logMessage(QString message);
};

#endif // DEVICE_WORKBENCH_WORKFLOW_H
