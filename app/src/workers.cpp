#include "workers.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr int kIdentityRefreshTimeoutMs = 60000;
constexpr int kIdentityRefreshPollIntervalMs = 500;
constexpr int kRunningProgressMaximum = 99;

QJsonObject extendedDescriptionObject(const QByteArray& data)
{
    const int jsonStart = data.indexOf('{');
    if (jsonStart < 0)
        return {};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data.mid(jsonStart), &parseError);
    return parseError.error == QJsonParseError::NoError && document.isObject()
        ? document.object()
        : QJsonObject();
}

QString identityDescriptionFromJson(const QJsonObject& root)
{
    const QJsonObject deviceInfo = root.value(QStringLiteral("DeviceInfo")).toObject();
    return deviceInfo.value(QStringLiteral("Description")).toString().trimmed();
}
}

DeviceDataWorker::DeviceDataWorker(quint64 requestId, std::shared_ptr<DeviceBase> device, QObject* parent) :
    QObject(parent),
    mRequestId(requestId),
    mDevice(std::move(device))
{
}

void DeviceDataWorker::run()
{
    DeviceIdentity identity;
    QStringList warnings;
    QStringList rawResponses;
    if (!mDevice)
    {
        warnings.append(QStringLiteral("Device is not available"));
        emit finished(mRequestId, identity, warnings, QString());
        return;
    }

    identity = mDevice->identity();
    emit progressChanged(mRequestId, 0, QStringLiteral("Расширенное описание JSON"));
    QByteArray extendedDescription;
    QString error;
    QString rawResponse;
    const bool extendedDescriptionRead = mDevice->readExtendedDescription(
        &extendedDescription,
        [this](int percent) {
            emit progressChanged(mRequestId, qBound(0, (percent * 70) / 100, 70),
                QStringLiteral("Расширенное описание JSON"));
        },
        &error,
        &rawResponse);
    if (extendedDescriptionRead)
        identity.descriptionJson = extendedDescription;
    else
        warnings.append(QStringLiteral("JSON description: %1").arg(error));
    if (!rawResponse.isEmpty())
        rawResponses.append(rawResponse);

    const QJsonObject descriptionObject = extendedDescriptionRead
        ? extendedDescriptionObject(extendedDescription)
        : QJsonObject();
    const QString jsonIdentityDescription = identityDescriptionFromJson(descriptionObject);
    emit progressChanged(mRequestId, 72, jsonIdentityDescription.isEmpty()
        ? QStringLiteral("Полное описание устройства")
        : QStringLiteral("Описание получено из JSON"));
    if (!jsonIdentityDescription.isEmpty())
    {
        identity.description = jsonIdentityDescription;
    }
    else
    {
        quint16 type = 0;
        quint16 version = 0;
        QString description;
        error.clear();
        rawResponse.clear();
        if (mDevice->readIdentityDescription(&type, &version, &description, &error, &rawResponse))
        {
            identity.type = type;
            identity.version = version;
            identity.description = description;
        }
        else
        {
            warnings.append(QStringLiteral("Full identity description: %1").arg(error));
        }
        if (!rawResponse.isEmpty())
            rawResponses.append(rawResponse);
    }

    emit progressChanged(mRequestId, 85, QStringLiteral("UUID устройства"));
    QString uuid;
    error.clear();
    rawResponse.clear();
    if (mDevice->readUuid(&uuid, &error, &rawResponse))
    {
        identity.uuid = uuid;
        identity.id = uuid;
    }
    else
    {
        warnings.append(QStringLiteral("UUID: %1").arg(error));
    }
    if (!rawResponse.isEmpty())
        rawResponses.append(rawResponse);
    emit progressChanged(mRequestId, 100, QStringLiteral("Данные устройства получены"));
    emit finished(mRequestId, identity, warnings, rawResponses.join(QLatin1Char('\n')));
}

WorkflowWorker::WorkflowWorker(WorkflowRepository* workflows,
    ActionSpec action,
    QVector<std::shared_ptr<DeviceBase>> devices,
    QVariantMap parameters,
    QObject* parent) :
    QObject(parent),
    mWorkflows(workflows),
    mAction(std::move(action)),
    mDevices(std::move(devices)),
    mParameters(std::move(parameters))
{
}

void WorkflowWorker::run()
{
    struct DeviceRunResult
    {
        bool workflowSuccessful = true;
        bool refreshSuccessful = true;
        bool failureCaptured = false;
        QString failedOperation;
        QString failedStage;
        QString refreshError;
        bool hasRefreshedIdentity = false;
        DeviceIdentity refreshedIdentity;
    };

    const int deviceCount = mDevices.size();
    int activeDeviceCount = 0;
    for (const std::shared_ptr<DeviceBase>& device : mDevices)
    {
        if (device)
            ++activeDeviceCount;
    }

    std::vector<DeviceRunResult> results(static_cast<size_t>(deviceCount));
    std::vector<int> deviceProgress(static_cast<size_t>(deviceCount), 0);
    std::vector<std::thread> threads;
    const int workerCount = qMin(activeDeviceCount, MaxParallelDevices);
    threads.reserve(static_cast<size_t>(workerCount));
    QMutex progressMutex;
    QMutex signalMutex;

    const auto emitLog = [this, &signalMutex](const QString& message) {
        QMutexLocker locker(&signalMutex);
        emit logMessage(message);
    };
    const auto emitTransportLog = [this, &signalMutex](const QString& message) {
        QMutexLocker locker(&signalMutex);
        emit transportLogMessage(message);
    };
    const auto emitStage = [this, &signalMutex](const QString& operation, const QString& stage) {
        QMutexLocker locker(&signalMutex);
        emit stageChanged(operation, stage);
    };
    const auto updateProgress = [this, &deviceProgress, &progressMutex, &signalMutex,
                                    activeDeviceCount](int deviceIndex, int percent) {
        int aggregate = 0;
        {
            QMutexLocker locker(&progressMutex);
            deviceProgress.at(static_cast<size_t>(deviceIndex)) = qBound(0, percent, 100);
            int sum = 0;
            for (int value : deviceProgress)
                sum += value;
            if (activeDeviceCount > 0)
                aggregate = sum / activeDeviceCount;
        }
        QMutexLocker signalLocker(&signalMutex);
        emit progressChanged(qBound(0, aggregate, kRunningProgressMaximum));
    };

    if (activeDeviceCount > MaxParallelDevices)
    {
        emitLog(QStringLiteral(
            "Parallel operation limit is %1 device(s); %2 device(s) will wait in queue")
            .arg(MaxParallelDevices)
            .arg(activeDeviceCount - MaxParallelDevices));
    }

    std::atomic<int> nextDeviceIndex{0};
    const auto runNextDevices = [this, deviceCount, &nextDeviceIndex, &results,
                                    &emitLog, &emitTransportLog, &emitStage, &updateProgress]() {
        while (true)
        {
            const int deviceIndex = nextDeviceIndex.fetch_add(1);
            if (deviceIndex >= deviceCount)
                return;

            const std::shared_ptr<DeviceBase> device = mDevices.at(deviceIndex);
            if (!device)
                continue;

            DeviceRunResult& result = results.at(static_cast<size_t>(deviceIndex));
            WorkflowRunner runner(mWorkflows);
            connect(&runner, &WorkflowRunner::logMessage, &runner,
                [&emitLog](const QString& message) { emitLog(message); }, Qt::DirectConnection);
            connect(&runner, &WorkflowRunner::transportLogMessage, &runner,
                [&emitTransportLog](const QString& message) { emitTransportLog(message); }, Qt::DirectConnection);
            connect(&runner, &WorkflowRunner::progressChanged, &runner,
                [&updateProgress, deviceIndex](int percent) { updateProgress(deviceIndex, percent); },
                Qt::DirectConnection);
            connect(&runner, &WorkflowRunner::stageChanged, &runner,
                [&emitStage](const QString& operation, const QString& stage) {
                    emitStage(operation, stage);
                }, Qt::DirectConnection);
            connect(&runner, &WorkflowRunner::failureStage, &runner,
                [&result](const QString& operation, const QString& stage) {
                    if (!result.failureCaptured)
                    {
                        result.failureCaptured = true;
                        result.failedOperation = operation;
                        result.failedStage = stage;
                    }
                }, Qt::DirectConnection);

            result.workflowSuccessful = runner.run(mAction, {device}, mParameters);

            emitStage(QStringLiteral("device.refreshIdentity"), QStringLiteral("refresh identity"));
            DeviceIdentity expected = device->identity();
            expected.type = 0;
            expected.version = 0;
            expected.serialNumber.clear();
            expected.state.clear();

            QString error;
            QString rawResponse;
            if (!device->waitForDeviceIdentity(expected,
                    kIdentityRefreshTimeoutMs,
                    kIdentityRefreshPollIntervalMs,
                    &result.refreshedIdentity,
                    &error,
                    &rawResponse))
            {
                result.refreshSuccessful = false;
                result.refreshError = error;
                emitLog(QStringLiteral("[%1] refresh identity failed: %2")
                    .arg(device->identity().typeHex(), error));
            }
            else
            {
                // The direct TCP fallback may not return a serial number.
                // Preserve the known value, including one written by this workflow.
                if (result.refreshedIdentity.serialNumber.trimmed().isEmpty())
                    result.refreshedIdentity.serialNumber = device->identity().serialNumber;
                result.hasRefreshedIdentity = true;
                emitLog(QStringLiteral("[%1] device identity refreshed")
                    .arg(result.refreshedIdentity.typeHex()));
            }
            if (!rawResponse.isEmpty())
                emitTransportLog(QStringLiteral("[%1] %2")
                    .arg(device->identity().typeHex(), rawResponse));
            updateProgress(deviceIndex, 100);
        }
    };

    for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex)
    {
        threads.emplace_back(runNextDevices);
    }

    for (std::thread& thread : threads)
        thread.join();

    bool workflowSuccessful = true;
    bool refreshSuccessful = true;
    QString failedOperation;
    QString failedStage;
    QString refreshError;
    for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
    {
        const DeviceRunResult& result = results.at(static_cast<size_t>(deviceIndex));
        if (!mDevices.at(deviceIndex))
            continue;
        if (!result.workflowSuccessful && workflowSuccessful)
        {
            failedOperation = result.failedOperation;
            failedStage = result.failedStage;
        }
        workflowSuccessful = result.workflowSuccessful && workflowSuccessful;
        if (!result.refreshSuccessful && refreshSuccessful)
            refreshError = result.refreshError;
        refreshSuccessful = result.refreshSuccessful && refreshSuccessful;
        if (result.hasRefreshedIdentity)
            emit identityRefreshed(deviceIndex, result.refreshedIdentity);
    }

    const bool successful = workflowSuccessful && refreshSuccessful;
    if (successful)
    {
        emit stageChanged(QStringLiteral("workflow.complete"), QStringLiteral("done"));
        emit finished(true, QStringLiteral("workflow.complete"), QStringLiteral("done"));
    }
    else if (!workflowSuccessful)
    {
        emit stageChanged(
            failedOperation.isEmpty() ? QStringLiteral("workflow") : failedOperation,
            failedStage.isEmpty() ? QStringLiteral("workflow failed") : failedStage);
        emit finished(false,
            failedOperation.isEmpty() ? QStringLiteral("workflow") : failedOperation,
            failedStage.isEmpty() ? QStringLiteral("workflow failed") : failedStage);
    }
    else
    {
        emit stageChanged(QStringLiteral("device.refreshIdentity"),
            refreshError.isEmpty() ? QStringLiteral("refresh identity") : refreshError);
        emit finished(false, QStringLiteral("device.refreshIdentity"),
            refreshError.isEmpty() ? QStringLiteral("refresh identity") : refreshError);
    }
}

PingWorker::PingWorker(std::shared_ptr<DeviceBase> device, int intervalMs, QObject* parent) :
    QObject(parent),
    mDevice(std::move(device)),
    mIntervalMs(qMax(1, intervalMs))
{
}

void PingWorker::start()
{
    mStopping = false;
    pingOnce();
}

void PingWorker::stop()
{
    mStopping = true;
    if (!mFinished)
        finish();
}

void PingWorker::pingOnce()
{
    if (mStopping || !mDevice)
    {
        finish();
        return;
    }

    QElapsedTimer timer;
    timer.start();

    qint32 value = 0;
    QString error;
    QString raw;
    const DeviceIdentity current = mDevice->identity();
    if (mDevice->readInt(0, &value, &error, &raw))
        emit pingLine(QStringLiteral("%1 int[0] = %2").arg(current.typeHex()).arg(value));
    else
        emit pingLine(QStringLiteral("%1 read int[0] failed: %2").arg(current.typeHex(), error));

    if (!raw.isEmpty())
        emit transportLogMessage(QStringLiteral("%1 %2").arg(current.typeHex(), raw));

    if (mStopping)
    {
        finish();
        return;
    }

    const int delayMs = qMax(0, mIntervalMs - int(timer.elapsed()));
    QTimer::singleShot(delayMs, this, &PingWorker::pingOnce);
}

void PingWorker::finish()
{
    if (mFinished)
        return;
    mFinished = true;
    emit finished();
}
