#include "workers.h"

#include <QElapsedTimer>
#include <QTimer>

#include <utility>

namespace
{
constexpr int kIdentityRefreshTimeoutMs = 60000;
constexpr int kIdentityRefreshPollIntervalMs = 500;
constexpr int kRunningProgressMaximum = 99;
}

UuidWorker::UuidWorker(quint64 requestId, std::shared_ptr<DeviceBase> device, QObject* parent) :
    QObject(parent),
    mRequestId(requestId),
    mDevice(std::move(device))
{
}

void UuidWorker::run()
{
    QString uuid;
    QString error;
    QString rawResponse;
    if (!mDevice)
        error = QStringLiteral("Device is not available");
    else
        mDevice->readUuid(&uuid, &error, &rawResponse);
    emit finished(mRequestId, uuid, error, rawResponse);
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
    WorkflowRunner runner(mWorkflows);
    QString failedOperation;
    QString failedStage;
    bool failureCaptured = false;
    connect(&runner, &WorkflowRunner::logMessage, this, &WorkflowWorker::logMessage);
    connect(&runner, &WorkflowRunner::transportLogMessage, this, &WorkflowWorker::transportLogMessage);
    connect(&runner, &WorkflowRunner::progressChanged, this, [this](int percent) {
        emit progressChanged(qBound(0, percent, kRunningProgressMaximum));
    });
    connect(&runner, &WorkflowRunner::stageChanged, this, &WorkflowWorker::stageChanged);
    connect(&runner, &WorkflowRunner::failureStage, this,
        [&failedOperation, &failedStage, &failureCaptured](const QString& operation, const QString& stage) {
            if (!failureCaptured)
            {
                failureCaptured = true;
                failedOperation = operation;
                failedStage = stage;
            }
        });

    const bool workflowSuccessful = runner.run(mAction, mDevices, mParameters);
    bool refreshSuccessful = true;
    QString refreshError;
    emit stageChanged(QStringLiteral("device.refreshIdentity"), QStringLiteral("refresh identity"));
    for (int deviceIndex = 0; deviceIndex < mDevices.size(); ++deviceIndex)
    {
        const std::shared_ptr<DeviceBase>& device = mDevices.at(deviceIndex);
        if (!device)
            continue;

        DeviceIdentity expected = device->identity();
        expected.type = 0;
        expected.version = 0;
        expected.serialNumber.clear();
        expected.state.clear();

        DeviceIdentity refreshed;
        QString error;
        QString rawResponse;
        if (!device->waitForDeviceIdentity(expected,
                kIdentityRefreshTimeoutMs,
                kIdentityRefreshPollIntervalMs,
                &refreshed,
                &error,
                &rawResponse))
        {
            refreshSuccessful = false;
            if (refreshError.isEmpty())
                refreshError = error;
            emit logMessage(QStringLiteral("[%1] refresh identity failed: %2")
                .arg(device->identity().typeHex(), error));
        }
        else
        {
            emit identityRefreshed(deviceIndex, refreshed);
            emit logMessage(QStringLiteral("[%1] device identity refreshed")
                .arg(refreshed.typeHex()));
        }
        if (!rawResponse.isEmpty())
            emit transportLogMessage(QStringLiteral("[%1] %2")
                .arg(device->identity().typeHex(), rawResponse));
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
