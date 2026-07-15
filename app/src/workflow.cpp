#include "workflow.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

static QString resolveArtifactPath(const QString& relativePath)
{
    const QStringList roots = {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath(),
        QDir::currentPath() + QStringLiteral("/app")
    };

    for (const QString& root : roots)
    {
        const QString candidate = QDir(root).filePath(relativePath);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(relativePath);
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

WorkflowRunner::WorkflowRunner(QObject* parent) :
    QObject(parent)
{
}

void WorkflowRunner::run(const ActionSpec& action, const QVector<DeviceIdentity>& devices)
{
    DeviceFactory factory;
    emit logMessage(QStringLiteral("Starting %1 for %2 device(s)").arg(action.id).arg(devices.size()));

    for (const DeviceIdentity& identity : devices)
    {
        std::unique_ptr<DeviceBase> device = factory.create(identity);
        const FlashPlan plan = device->flashPlan(action);
        emit logMessage(QStringLiteral("[%1] %2 uses %3 target=%4 pageSize=%5 pages=%6-%7")
            .arg(identity.typeHex(), device->className(), plan.workflowId, plan.target)
            .arg(plan.pageSize)
            .arg(plan.beginPage)
            .arg(plan.endPage));
        if (!plan.artifact.relativePath.isEmpty())
        {
            emit logMessage(QStringLiteral("[%1] firmware %2 version=%3 sha256=%4")
                .arg(identity.typeHex(), plan.artifact.relativePath, plan.artifact.version, plan.artifact.sha256));
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
        }

        for (const QString& step : device->beforeFlashWrite(plan))
            emit logMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), step));

        emit logMessage(QStringLiteral("[%1] writeFlash is intentionally not sent to hardware in MVP")
            .arg(identity.typeHex()));

        for (const QString& step : device->afterFlashWrite(plan))
            emit logMessage(QStringLiteral("[%1] %2").arg(identity.typeHex(), step));
    }

    emit logMessage(QStringLiteral("Workflow %1 finished").arg(action.id));
}
