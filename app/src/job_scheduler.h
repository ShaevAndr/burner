#ifndef DEVICE_WORKBENCH_JOB_SCHEDULER_H
#define DEVICE_WORKBENCH_JOB_SCHEDULER_H

#include "base_device.h"

#include <QVector>
#include <functional>
#include <memory>

// Schedules independent device executions with a global physical-device lock.
// Workflow code remains sequential for one device.
class JobScheduler
{
public:
    explicit JobScheduler(int parallelLimit = 5);

    using Execution = std::function<void(int deviceIndex, const QString& jobId,
        const QString& executionId, bool hasPhysicalKey)>;
    QString run(const QVector<std::shared_ptr<DeviceBase>>& devices,
        const Execution& execution) const;

private:
    int mParallelLimit = 5;
};

#endif // DEVICE_WORKBENCH_JOB_SCHEDULER_H
