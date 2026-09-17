#include "execution_journal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStringList>
#include <utility>

namespace
{
QMutex journalMutex;

bool appendLocked(const QString& path, QJsonObject event, QString* error)
{
    static const QStringList allowedFields = {
        QStringLiteral("jobId"), QStringLiteral("executionId"),
        QStringLiteral("uuid"), QStringLiteral("endpoint"),
        QStringLiteral("workflowId"), QStringLiteral("workflowVersion"),
        QStringLiteral("operationId"), QStringLiteral("completedOperationId"),
        QStringLiteral("inFlightOperationId"),
        QStringLiteral("recoveryState"),
        QStringLiteral("event"),
        QStringLiteral("errorCode"), QStringLiteral("flashMayHaveStarted")
    };
    QJsonObject safeEvent;
    for (const QString& field : allowedFields)
    {
        if (event.contains(field))
            safeEvent.insert(field, event.value(field));
    }
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
    {
        if (error)
            *error = QStringLiteral("Cannot create journal directory %1").arg(info.absolutePath());
        return false;
    }
    safeEvent.insert(QStringLiteral("timeUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.size() > 0)
    {
        file.seek(file.size() - 1);
        const QByteArray lastByte = file.read(1);
        if (lastByte != "\n" && file.write("\n", 1) != 1)
        {
            if (error)
                *error = file.errorString();
            return false;
        }
    }
    const QByteArray line = QJsonDocument(safeEvent).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size() || !file.flush())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
}

ExecutionJournal::ExecutionJournal(QString filePath) :
    mFilePath(filePath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("execution-journal.jsonl"))
        : std::move(filePath))
{
}

bool ExecutionJournal::append(QJsonObject event, QString* error) const
{
    QMutexLocker locker(&journalMutex);
    return appendLocked(mFilePath, std::move(event), error);
}

QVector<QJsonObject> ExecutionJournal::recoverInterrupted(QString* error) const
{
    QMutexLocker locker(&journalMutex);
    QVector<QJsonObject> interrupted;
    QFile file(mFilePath);
    if (!file.exists())
        return interrupted;
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return interrupted;
    }

    QHash<QString, QJsonObject> lastEvent;
    QStringList order;
    while (!file.atEnd())
    {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readLine());
        if (!doc.isObject())
            continue; // A torn final line must not hide earlier valid events.
        const QJsonObject event = doc.object();
        const QString executionId = event.value(QStringLiteral("executionId")).toString();
        if (executionId.isEmpty())
            continue;
        if (!lastEvent.contains(executionId))
            order.append(executionId);
        lastEvent.insert(executionId, event);
    }
    file.close();

    for (const QString& executionId : order)
    {
        QJsonObject event = lastEvent.value(executionId);
        const QString state = event.value(QStringLiteral("event")).toString();
        if (state != QStringLiteral("started") && state != QStringLiteral("stage")
            && state != QStringLiteral("stepCompleted")
            && state != QStringLiteral("recoveryStarted")
            && state != QStringLiteral("recoverySucceeded")
            && state != QStringLiteral("recoveryFailed"))
            continue;
        event.insert(QStringLiteral("inFlightOperationId"),
            state == QStringLiteral("stage") || state == QStringLiteral("recoveryStarted")
                ? event.value(QStringLiteral("operationId")).toString() : QString());
        if (state.startsWith(QStringLiteral("recovery")))
            event.insert(QStringLiteral("recoveryState"), state);
        event.insert(QStringLiteral("event"), QStringLiteral("interrupted"));
        event.insert(QStringLiteral("errorCode"), QStringLiteral("EXECUTION_INTERRUPTED"));
        if (!appendLocked(mFilePath, event, error))
            break;
        interrupted.append(event);
    }
    return interrupted;
}
