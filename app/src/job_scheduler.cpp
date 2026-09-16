#include "job_scheduler.h"

#include <QSet>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
std::mutex deviceLock;
std::condition_variable deviceReleased;
QSet<QString> activeKeys;

class DeviceLease
{
public:
    explicit DeviceLease(QStringList keys) : mKeys(std::move(keys))
    {
        if (mKeys.isEmpty())
            return;
        std::unique_lock<std::mutex> lock(deviceLock);
        deviceReleased.wait(lock, [this]() {
            for (const QString& key : mKeys)
            {
                if (activeKeys.contains(key))
                    return false;
            }
            return true;
        });
        for (const QString& key : mKeys)
            activeKeys.insert(key);
    }

    ~DeviceLease()
    {
        if (mKeys.isEmpty())
            return;
        {
            std::lock_guard<std::mutex> lock(deviceLock);
            for (const QString& key : mKeys)
                activeKeys.remove(key);
        }
        deviceReleased.notify_all();
    }

private:
    QStringList mKeys;
};
}

JobScheduler::JobScheduler(int parallelLimit) : mParallelLimit(std::max(1, parallelLimit))
{
}

QString JobScheduler::run(const QVector<std::shared_ptr<DeviceBase>>& devices,
    const Execution& execution) const
{
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (devices.isEmpty() || !execution)
        return jobId;

    std::atomic<int> nextDevice{0};
    const int workerCount = std::min(mParallelLimit, devices.size());
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(workerCount));
    for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex)
    {
        workers.emplace_back([&]() {
            while (true)
            {
                const int index = nextDevice.fetch_add(1);
                if (index >= devices.size())
                    break;
                const auto& device = devices.at(index);
                if (!device)
                    continue;
                const QStringList keys = device->physicalKeys();
                DeviceLease lease(keys);
                execution(index, jobId,
                    QUuid::createUuid().toString(QUuid::WithoutBraces), !keys.isEmpty());
            }
        });
    }
    for (std::thread& worker : workers)
        worker.join();
    return jobId;
}
