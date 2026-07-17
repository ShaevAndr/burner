#include "workers.h"

#include <QElapsedTimer>
#include <QTimer>

#include <utility>

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
    connect(&runner, &WorkflowRunner::logMessage, this, &WorkflowWorker::logMessage);
    connect(&runner, &WorkflowRunner::transportLogMessage, this, &WorkflowWorker::transportLogMessage);
    connect(&runner, &WorkflowRunner::progressChanged, this, &WorkflowWorker::progressChanged);

    runner.run(mAction, mDevices, mParameters);
    emit finished();
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
