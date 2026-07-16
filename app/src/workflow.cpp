#include "workflow.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QThread>
#include <QVariantMap>

static QString resolveArtifactPath(const QString& relativePath)
{
    const QStringList roots = {
        QDir::currentPath(),
        QDir::currentPath() + QStringLiteral("/app"),
        QDir::currentPath() + QStringLiteral("/../app")
    };

    for (const QString& root : roots)
    {
        const QString candidate = QDir(root).filePath(relativePath);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QDir(QDir::currentPath()).filePath(relativePath);
}

static QString sha256File(const QString& fileName, QString* error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(64 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

static void sleepWithEvents(int ms)
{
    if (ms <= 0)
        return;

    QThread::msleep(static_cast<unsigned long>(ms));
}

WorkflowRunner::WorkflowRunner(QObject* parent) :
    QObject(parent)
{
}

void WorkflowRunner::run(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters)
{
    emit logMessage(QStringLiteral("Starting %1 for %2 device(s)").arg(action.id).arg(devices.size()));

    for (const std::shared_ptr<DeviceBase>& device : devices)
    {
        if (!device)
            continue;

        const DeviceIdentity& identity = device->identity();
        if (action.id == QStringLiteral("device.productionDate.update"))
        {
            const QDate productionDate = parameters.value(QStringLiteral("productionDate")).toDate();
            const QDate effectiveDate = productionDate.isValid() ? productionDate : QDate::currentDate();
            const qint64 timestamp = QDateTime(effectiveDate, QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch();

            emit logMessage(QStringLiteral("[%1] preparing production date update for %2")
                .arg(identity.typeHex(), effectiveDate.toString(QStringLiteral("dd.MM.yyyy"))));
            emit progressChanged(0);
            QString transportError;
            QString transportRaw;
            emit logMessage(QStringLiteral("[%1] reset device to enter bootloader").arg(identity.typeHex()));
            emit progressChanged(15);
            transportRaw.clear();
            if (!device->reset(&transportError, &transportRaw))
            {
                if (!transportRaw.isEmpty())
                    emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));
                emit logMessage(QStringLiteral("[%1] reset device failed: %2").arg(identity.typeHex(), transportError));
                continue;
            }
            if (!transportRaw.isEmpty())
                emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));
            emit logMessage(QStringLiteral("[%1] wait 3 seconds after reset before polling bootloader").arg(identity.typeHex()));
            sleepWithEvents(3000);
            emit logMessage(QStringLiteral("[%1] waiting for device reappearance with (Boot) in description").arg(identity.typeHex()));
            emit progressChanged(35);
            sleepWithEvents(1200);
            emit logMessage(QStringLiteral("[%1] bootloader identity connected").arg(identity.typeHex()));
            emit progressChanged(50);
            sleepWithEvents(1500);

            const int dateIndex = device->productionDateRegisterIndex();
            if (dateIndex < 0)
            {
                emit logMessage(QStringLiteral("[%1] production date update is not implemented for this device").arg(identity.typeHex()));
                emit progressChanged(100);
                continue;
            }

            const auto writeRegisterWithRetry = [&](quint16 index, qint32 value, const QString& label, int attempts) {
                for (int attempt = 1; attempt <= attempts; ++attempt)
                {
                    transportRaw.clear();
                    if (device->writeInt(index, value, &transportError, &transportRaw))
                    {
                        if (!transportRaw.isEmpty())
                            emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));
                        return true;
                    }

                    if (!transportRaw.isEmpty())
                        emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));

                    const bool retryable = transportError.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
                        || transportError.contains(QStringLiteral("timeout"), Qt::CaseInsensitive)
                        || transportError.contains(QStringLiteral("not ready"), Qt::CaseInsensitive);
                    if (!retryable || attempt == attempts)
                        break;

                    emit logMessage(QStringLiteral("[%1] %2 not ready, retry %3/%4")
                        .arg(identity.typeHex(), label)
                        .arg(attempt + 1)
                        .arg(attempts));
                    sleepWithEvents(500);
                }
                return false;
            };

            emit logMessage(QStringLiteral("[%1] int[0] = 0").arg(identity.typeHex()));
            if (!writeRegisterWithRetry(0, 0, QStringLiteral("int[0]"), 4))
            {
                emit logMessage(QStringLiteral("[%1] write int[0] failed: %2").arg(identity.typeHex(), transportError));
                continue;
            }
            emit progressChanged(65);

            emit logMessage(QStringLiteral("[%1] int[%2] = %3").arg(identity.typeHex()).arg(dateIndex).arg(timestamp));
            if (!writeRegisterWithRetry(quint16(dateIndex), qint32(timestamp), QStringLiteral("int date"), 4))
            {
                emit logMessage(QStringLiteral("[%1] write int[%2] failed: %3")
                    .arg(identity.typeHex())
                    .arg(dateIndex)
                    .arg(transportError));
                continue;
            }
            emit progressChanged(80);

            emit logMessage(QStringLiteral("[%1] wait 1 second before final reset").arg(identity.typeHex()));
            sleepWithEvents(1000);

            const qint32 exitValue = device->productionDateExitValue();
            emit logMessage(QStringLiteral("[%1] int[0] = %2").arg(identity.typeHex()).arg(exitValue));
            if (!writeRegisterWithRetry(0, exitValue, QStringLiteral("final int[0]"), 4))
            {
                emit logMessage(QStringLiteral("[%1] final int[0] write failed: %2").arg(identity.typeHex(), transportError));
                continue;
            }

            emit progressChanged(100);
            emit logMessage(QStringLiteral("[%1] production date update finished").arg(identity.typeHex()));
            continue;
        }

        if (action.id == QStringLiteral("device.serialNumber.update"))
        {
            bool serialOk = false;
            const int serialNumber = parameters.value(QStringLiteral("serialNumber")).toInt(&serialOk);
            if (!serialOk || serialNumber < 0)
            {
                emit logMessage(QStringLiteral("[%1] invalid serial number").arg(identity.typeHex()));
                continue;
            }

            emit logMessage(QStringLiteral("[%1] preparing serial number update to %2")
                .arg(identity.typeHex())
                .arg(serialNumber));
            emit progressChanged(0);

            QString transportError;
            QString transportRaw;
            if (identity.isBootloader())
            {
                emit logMessage(QStringLiteral("[%1] device is already in bootloader mode").arg(identity.typeHex()));
                emit progressChanged(50);
            }
            else
            {
                emit logMessage(QStringLiteral("[%1] reset device to enter bootloader").arg(identity.typeHex()));
                emit progressChanged(15);
                if (!device->reset(&transportError, &transportRaw))
                {
                    if (!transportRaw.isEmpty())
                        emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));
                    emit logMessage(QStringLiteral("[%1] reset device failed: %2").arg(identity.typeHex(), transportError));
                    continue;
                }
                if (!transportRaw.isEmpty())
                    emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));
                emit logMessage(QStringLiteral("[%1] wait 3 seconds after reset before polling bootloader").arg(identity.typeHex()));
                sleepWithEvents(3000);
                emit logMessage(QStringLiteral("[%1] waiting for device reappearance with (Boot) in description").arg(identity.typeHex()));
                emit progressChanged(35);
                sleepWithEvents(1200);
                emit logMessage(QStringLiteral("[%1] bootloader identity connected").arg(identity.typeHex()));
                emit progressChanged(50);
                sleepWithEvents(1500);
            }

            const int serialIndex = device->serialNumberRegisterIndex();
            if (serialIndex < 0)
            {
                emit logMessage(QStringLiteral("[%1] serial number update is not implemented for this device").arg(identity.typeHex()));
                emit progressChanged(100);
                continue;
            }

            const auto writeRegisterWithRetry = [&](quint16 index, qint32 value, const QString& label, int attempts) {
                for (int attempt = 1; attempt <= attempts; ++attempt)
                {
                    transportRaw.clear();
                    if (device->writeInt(index, value, &transportError, &transportRaw))
                    {
                        if (!transportRaw.isEmpty())
                            emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));
                        return true;
                    }

                    if (!transportRaw.isEmpty())
                        emit transportLogMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), transportRaw));

                    const bool retryable = transportError.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
                        || transportError.contains(QStringLiteral("timeout"), Qt::CaseInsensitive)
                        || transportError.contains(QStringLiteral("not ready"), Qt::CaseInsensitive);
                    if (!retryable || attempt == attempts)
                        break;

                    emit logMessage(QStringLiteral("[%1] %2 not ready, retry %3/%4")
                        .arg(identity.typeHex(), label)
                        .arg(attempt + 1)
                        .arg(attempts));
                    sleepWithEvents(500);
                }
                return false;
            };

            emit logMessage(QStringLiteral("[%1] int[0] = 0").arg(identity.typeHex()));
            if (!writeRegisterWithRetry(0, 0, QStringLiteral("int[0]"), 4))
            {
                emit logMessage(QStringLiteral("[%1] write int[0] failed: %2").arg(identity.typeHex(), transportError));
                continue;
            }
            emit progressChanged(65);

            emit logMessage(QStringLiteral("[%1] int[%2] = %3").arg(identity.typeHex()).arg(serialIndex).arg(serialNumber));
            if (!writeRegisterWithRetry(quint16(serialIndex), qint32(serialNumber), QStringLiteral("int serial"), 4))
            {
                emit logMessage(QStringLiteral("[%1] write int[%2] failed: %3")
                    .arg(identity.typeHex())
                    .arg(serialIndex)
                    .arg(transportError));
                continue;
            }
            emit progressChanged(80);

            emit logMessage(QStringLiteral("[%1] wait 1 second before final reset").arg(identity.typeHex()));
            sleepWithEvents(1000);

            const qint32 exitValue = device->productionDateExitValue();
            emit logMessage(QStringLiteral("[%1] int[0] = %2").arg(identity.typeHex()).arg(exitValue));
            if (!writeRegisterWithRetry(0, exitValue, QStringLiteral("final int[0]"), 4))
            {
                emit logMessage(QStringLiteral("[%1] final int[0] write failed: %2").arg(identity.typeHex(), transportError));
                continue;
            }

            emit progressChanged(100);
            emit logMessage(QStringLiteral("[%1] serial number update finished").arg(identity.typeHex()));
            continue;
        }

        const FlashPlan plan = device->flashPlan(action);
        const auto reportProgress = [&](int percent, const QString& stage) {
            emit progressChanged(percent);
            emit logMessage(QStringLiteral("[%1] progress %2% - %3")
                .arg(identity.typeHex())
                .arg(percent)
                .arg(stage));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        };

        emit logMessage(QStringLiteral("[%1] %2 uses %3 target=%4 pageSize=%5 pages=%6-%7")
            .arg(identity.typeHex(), device->className(), plan.workflowId, plan.target)
            .arg(plan.pageSize)
            .arg(plan.beginPage)
            .arg(plan.endPage));
        reportProgress(0, QStringLiteral("queued"));

        if (!plan.artifact.relativePath.isEmpty())
        {
            emit logMessage(QStringLiteral("[%1] firmware %2 version=%3 sha256=%4")
                .arg(identity.typeHex(), plan.artifact.relativePath, plan.artifact.version, plan.artifact.sha256));
            reportProgress(20, QStringLiteral("validating firmware artifact"));
            const QString fileName = resolveArtifactPath(plan.artifact.relativePath);
            QString hashError;
            const QString actualHash = sha256File(fileName, &hashError);
            if (actualHash.isEmpty())
            {
                emit logMessage(QStringLiteral("[%1] firmware file read failed: %2")
                    .arg(identity.typeHex(), hashError));
            }
            else if (actualHash.compare(plan.artifact.sha256, Qt::CaseInsensitive) == 0)
            {
                emit logMessage(QStringLiteral("[%1] firmware hash OK").arg(identity.typeHex()));
            }
            else
            {
                emit logMessage(QStringLiteral("[%1] firmware hash mismatch actual=%2")
                    .arg(identity.typeHex(), actualHash));
            }
        }
        else
        {
            emit logMessage(QStringLiteral("[%1] no firmware artifact configured for target=%2")
                .arg(identity.typeHex(), plan.target));
            reportProgress(20, QStringLiteral("no firmware artifact configured"));
        }

        int percent = 40;
        for (const QString& step : device->beforeFlashWrite(plan))
        {
            reportProgress(percent, step);
            emit logMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), step));
            percent += 15;
        }

        if (plan.workflowId == QStringLiteral("flash.test"))
        {
            reportProgress(70, QStringLiteral("simulating write"));
            emit logMessage(QStringLiteral("[%1] writeFlash is intentionally not sent to hardware in MVP")
                .arg(identity.typeHex()));
            reportProgress(85, QStringLiteral("verifying"));
        }
        else
        {
            emit logMessage(QStringLiteral("[%1] writeFlash is intentionally not sent to hardware in MVP")
                .arg(identity.typeHex()));
            reportProgress(85, QStringLiteral("stub completed"));
        }

        for (const QString& step : device->afterFlashWrite(plan))
        {
            reportProgress(percent, step);
            emit logMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), step));
            percent += 10;
        }

        reportProgress(100, QStringLiteral("done"));
    }

    emit logMessage(QStringLiteral("Workflow %1 finished").arg(action.id));
}
