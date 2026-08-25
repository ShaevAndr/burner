#include "workers.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

#include <utility>

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
            // The transport's direct TCP fallback can identify the device by
            // type and UUID but has no serial field. Do not erase the known
            // serial (including a number just written by this workflow).
            if (refreshed.serialNumber.trimmed().isEmpty())
                refreshed.serialNumber = device->identity().serialNumber;
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
