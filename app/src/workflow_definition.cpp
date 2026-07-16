#include "workflow_definition.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTime>
#include <utility>

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

static WorkflowFlashPlan buildFlashPlan(const DeviceIdentity& identity, const ActionSpec& action)
{
    WorkflowFlashPlan plan;
    plan.workflowId = identity.flashWorkflows.value(action.target, action.workflow);
    plan.target = action.target;
    plan.artifact = identity.firmwareForTarget(action.target);
    return plan;
}

static QVariantMap jsonObjectToVariantMap(const QJsonObject& object)
{
    QVariantMap result;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        result.insert(it.key(), it.value().toVariant());
    return result;
}

bool WorkflowRepository::load(const QString& fileName, QString* error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("Cannot open workflows %1: %2").arg(fileName, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
            *error = QStringLiteral("Workflows %1 is not a valid JSON object: %2").arg(fileName, parseError.errorString());
        return false;
    }

    QHash<QString, WorkflowDefinition> loaded;
    const QJsonArray workflows = doc.object().value(QStringLiteral("workflows")).toArray();
    for (const QJsonValue& workflowValue : workflows)
    {
        const QJsonObject workflowObject = workflowValue.toObject();
        WorkflowDefinition definition;
        definition.id = workflowObject.value(QStringLiteral("id")).toString();
        if (definition.id.isEmpty())
            continue;

        const QJsonArray steps = workflowObject.value(QStringLiteral("steps")).toArray();
        for (const QJsonValue& stepValue : steps)
        {
            const QJsonObject stepObject = stepValue.toObject();
            WorkflowStep step;
            step.op = stepObject.value(QStringLiteral("op")).toString();
            step.label = stepObject.value(QStringLiteral("label")).toString(step.op);
            step.message = stepObject.value(QStringLiteral("message")).toString();
            step.skipIfState = stepObject.value(QStringLiteral("skipIfState")).toString();
            step.skippedLog = stepObject.value(QStringLiteral("skippedLog")).toString();
            step.retryAttempts = stepObject.value(QStringLiteral("retry")).toInt(1);

            step.arguments = jsonObjectToVariantMap(stepObject);
            definition.steps.append(step);
        }

        loaded.insert(definition.id, definition);
    }

    if (loaded.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Workflows %1 does not define any workflows").arg(fileName);
        return false;
    }

    mDefinitions = loaded;
    return true;
}

const WorkflowDefinition* WorkflowRepository::definitionFor(const ActionSpec& action) const
{
    auto byWorkflow = mDefinitions.constFind(action.workflow);
    if (byWorkflow != mDefinitions.constEnd())
        return &byWorkflow.value();

    auto byAction = mDefinitions.constFind(action.id);
    if (byAction != mDefinitions.constEnd())
        return &byAction.value();

    const QHash<QString, QString> legacyActionWorkflows = {
        {QStringLiteral("device.productionDate.update"), QStringLiteral("device.production-date.update")},
        {QStringLiteral("device.serialNumber.update"), QStringLiteral("device.serial-number.update")},
        {QStringLiteral("flash.application.write"), QStringLiteral("flash.write")},
        {QStringLiteral("flash.bootloader.write"), QStringLiteral("flash.write")}
    };
    const QString mappedWorkflow = legacyActionWorkflows.value(action.id);
    if (!mappedWorkflow.isEmpty())
    {
        auto mapped = mDefinitions.constFind(mappedWorkflow);
        if (mapped != mDefinitions.constEnd())
            return &mapped.value();
    }

    return nullptr;
}

WorkflowExecution::WorkflowExecution(const WorkflowDefinition& definition,
    const ActionSpec& action,
    QVariantMap parameters,
    WorkflowCallbacks callbacks) :
    mDefinition(definition),
    mAction(action),
    mParameters(std::move(parameters)),
    mCallbacks(std::move(callbacks))
{
}

bool WorkflowExecution::next(DeviceBase& device)
{
    if (mFinished)
        return false;

    if (mNextStep >= mDefinition.steps.size())
    {
        mFinished = true;
        return false;
    }

    const WorkflowStep step = mDefinition.steps.at(mNextStep);
    ++mNextStep;

    const DeviceIdentity& identity = device.identity();
    if (!step.skipIfState.isEmpty() && identity.state == step.skipIfState)
    {
        if (!step.skippedLog.isEmpty())
            log(formatMessage(identity, step.skippedLog, step.arguments));
    }
    else if (!executeRuntimeStep(device, step) && !executeDeviceStep(device, step))
    {
        mSuccessful = false;
        mFinished = true;
        return false;
    }

    progress(completedProgressPercent());

    if (mNextStep >= mDefinition.steps.size())
        mFinished = true;

    return !mFinished;
}

void WorkflowExecution::log(const QString& message) const
{
    if (mCallbacks.logMessage)
        mCallbacks.logMessage(message);
}

void WorkflowExecution::transportLog(const QString& message) const
{
    if (mCallbacks.transportLogMessage)
        mCallbacks.transportLogMessage(message);
}

void WorkflowExecution::progress(int percent) const
{
    if (percent >= 0 && mCallbacks.progressChanged)
        mCallbacks.progressChanged(percent);
}

void WorkflowExecution::processEvents() const
{
    if (mCallbacks.processEvents)
        mCallbacks.processEvents();
}

int WorkflowExecution::completedProgressPercent() const
{
    if (mDefinition.steps.isEmpty())
        return 100;
    const int percent = (mNextStep * 100) / mDefinition.steps.size();
    return percent > 100 ? 100 : percent;
}

void WorkflowExecution::reportProgressStage(const DeviceIdentity& identity, const QString& stage) const
{
    if (!stage.isEmpty())
    {
        log(QStringLiteral("[%1] progress %2% - %3")
            .arg(identity.typeHex())
            .arg(completedProgressPercent())
            .arg(stage));
    }
    processEvents();
}

void WorkflowExecution::emitTransportLog(const DeviceIdentity& identity)
{
    if (!mContext.transportRaw.isEmpty())
        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), mContext.transportRaw));
}

bool WorkflowExecution::runOperationWithRetry(const DeviceIdentity& identity,
    const WorkflowStep& step,
    const DeviceOperation& operation,
    const QVariantMap& arguments)
{
    const int attempts = qMax(1, step.retryAttempts);
    for (int attempt = 1; attempt <= attempts; ++attempt)
    {
        mContext.transportRaw.clear();
        mContext.transportError.clear();
        if (operation(arguments, &mContext.transportError, &mContext.transportRaw))
        {
            emitTransportLog(identity);
            return true;
        }

        emitTransportLog(identity);

        const bool retryable = mContext.transportError.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
            || mContext.transportError.contains(QStringLiteral("timeout"), Qt::CaseInsensitive)
            || mContext.transportError.contains(QStringLiteral("not ready"), Qt::CaseInsensitive);
        if (!retryable || attempt == attempts)
            break;

        log(QStringLiteral("[%1] %2 not ready, retry %3/%4")
            .arg(identity.typeHex(), step.label)
            .arg(attempt + 1)
            .arg(attempts));
        sleepWithEvents(500);
    }
    return false;
}

bool WorkflowExecution::executeRuntimeStep(DeviceBase& device, const WorkflowStep& step)
{
    const DeviceIdentity& identity = device.identity();

    if (step.op == QStringLiteral("context.productionDate"))
    {
        const QDate productionDate = mParameters.value(QStringLiteral("productionDate")).toDate();
        const QDate effectiveDate = productionDate.isValid() ? productionDate : QDate::currentDate();
        mContext.productionTimestamp = QDateTime(effectiveDate, QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch();
        log(QStringLiteral("[%1] preparing production date update for %2")
            .arg(identity.typeHex(), effectiveDate.toString(QStringLiteral("dd.MM.yyyy"))));
        return true;
    }

    if (step.op == QStringLiteral("context.serialNumber"))
    {
        bool serialOk = false;
        mContext.serialNumber = mParameters.value(QStringLiteral("serialNumber")).toInt(&serialOk);
        if (!serialOk || mContext.serialNumber < 0)
        {
            log(QStringLiteral("[%1] invalid serial number").arg(identity.typeHex()));
            return false;
        }
        log(QStringLiteral("[%1] preparing serial number update to %2")
            .arg(identity.typeHex())
            .arg(mContext.serialNumber));
        return true;
    }

    if (step.op == QStringLiteral("sleep"))
    {
        if (!step.message.isEmpty())
            log(formatMessage(identity, step.message, step.arguments));
        sleepWithEvents(step.arguments.value(QStringLiteral("ms")).toInt());
        return true;
    }

    if (step.op == QStringLiteral("device.connect"))
    {
        if (!step.message.isEmpty())
            log(formatMessage(identity, step.message, step.arguments));
        sleepWithEvents(step.arguments.value(QStringLiteral("ms"), 0).toInt());
        const QString state = step.arguments.value(QStringLiteral("state"), QStringLiteral("device")).toString();
        if (state == QStringLiteral("bootloader"))
            log(QStringLiteral("[%1] bootloader identity connected").arg(identity.typeHex()));
        else
            log(QStringLiteral("[%1] device connected").arg(identity.typeHex()));
        sleepWithEvents(step.arguments.value(QStringLiteral("settleMs"), 0).toInt());
        return true;
    }

    if (step.op == QStringLiteral("flash.prepare"))
    {
        mContext.flashPlan = buildFlashPlan(identity, mAction);
        log(QStringLiteral("[%1] %2 uses %3 target=%4 pageSize=%5 pages=%6-%7")
            .arg(identity.typeHex(), device.className(), mContext.flashPlan.workflowId, mContext.flashPlan.target)
            .arg(mContext.flashPlan.pageSize)
            .arg(mContext.flashPlan.beginPage)
            .arg(mContext.flashPlan.endPage));
        reportProgressStage(identity, QStringLiteral("queued"));
        return true;
    }

    if (step.op == QStringLiteral("flash.validateArtifact"))
    {
        if (!mContext.flashPlan.artifact.relativePath.isEmpty())
        {
            log(QStringLiteral("[%1] firmware %2 version=%3 sha256=%4")
                .arg(identity.typeHex(), mContext.flashPlan.artifact.relativePath, mContext.flashPlan.artifact.version, mContext.flashPlan.artifact.sha256));
            reportProgressStage(identity, QStringLiteral("validating firmware artifact"));
            const QString fileName = resolveArtifactPath(mContext.flashPlan.artifact.relativePath);
            QString hashError;
            const QString actualHash = sha256File(fileName, &hashError);
            if (actualHash.isEmpty())
            {
                log(QStringLiteral("[%1] firmware file read failed: %2")
                    .arg(identity.typeHex(), hashError));
            }
            else if (actualHash.compare(mContext.flashPlan.artifact.sha256, Qt::CaseInsensitive) == 0)
            {
                log(QStringLiteral("[%1] firmware hash OK").arg(identity.typeHex()));
            }
            else
            {
                log(QStringLiteral("[%1] firmware hash mismatch actual=%2")
                    .arg(identity.typeHex(), actualHash));
            }
        }
        else
        {
            log(QStringLiteral("[%1] no firmware artifact configured for target=%2")
                .arg(identity.typeHex(), mContext.flashPlan.target));
            reportProgressStage(identity, QStringLiteral("no firmware artifact configured"));
        }
        return true;
    }

    if (step.op == QStringLiteral("flash.preflight"))
    {
        const QStringList flashPreflightSteps = {
            QStringLiteral("lock device"),
            QStringLiteral("read identity before writing %1").arg(mContext.flashPlan.target),
            QStringLiteral("prepare %1 flash area").arg(mContext.flashPlan.target)
        };
        for (const QString& flashStep : flashPreflightSteps)
        {
            reportProgressStage(identity, flashStep);
            log(QStringLiteral("[%1] %2").arg(identity.typeHex(), flashStep));
        }
        return true;
    }

    if (step.op == QStringLiteral("flash.simulateWrite"))
    {
        if (mContext.flashPlan.workflowId == QStringLiteral("flash.test"))
        {
            reportProgressStage(identity, QStringLiteral("simulating write"));
            log(QStringLiteral("[%1] writeFlash is intentionally not sent to hardware in MVP")
                .arg(identity.typeHex()));
            reportProgressStage(identity, QStringLiteral("verifying"));
        }
        else
        {
            log(QStringLiteral("[%1] writeFlash is intentionally not sent to hardware in MVP")
                .arg(identity.typeHex()));
            reportProgressStage(identity, QStringLiteral("stub completed"));
        }
        return true;
    }

    if (step.op == QStringLiteral("flash.complete"))
    {
        const QStringList flashCompletionSteps = {
            QStringLiteral("verify %1 flash").arg(mContext.flashPlan.target),
            QStringLiteral("refresh identity")
        };
        for (const QString& flashStep : flashCompletionSteps)
        {
            reportProgressStage(identity, flashStep);
            log(QStringLiteral("[%1] %2").arg(identity.typeHex(), flashStep));
        }
        return true;
    }

    if (step.op == QStringLiteral("workflow.finish"))
    {
        reportProgressStage(identity, QStringLiteral("done"));
        return true;
    }

    if (step.op == QStringLiteral("log"))
    {
        log(formatMessage(identity, step.message, step.arguments));
        return true;
    }

    return false;
}

bool WorkflowExecution::executeDeviceStep(DeviceBase& device, const WorkflowStep& step)
{
    const DeviceIdentity& identity = device.identity();
    const DeviceOperation operation = device.operation(step.op);
    if (!operation)
    {
        log(QStringLiteral("[%1] operation %2 is not supported by %3")
            .arg(identity.typeHex(), step.op, device.className()));
        return false;
    }

    const QVariantMap arguments = resolveArguments(step);
    if (!step.message.isEmpty())
        log(formatMessage(identity, step.message, arguments));

    if (!runOperationWithRetry(identity, step, operation, arguments))
    {
        log(QStringLiteral("[%1] %2 failed: %3")
            .arg(identity.typeHex(), step.label, mContext.transportError));
        return false;
    }

    return true;
}

QVariantMap WorkflowExecution::resolveArguments(const WorkflowStep& step) const
{
    QVariantMap arguments = step.arguments;
    const QString valueFrom = arguments.value(QStringLiteral("valueFrom")).toString();
    if (valueFrom == QStringLiteral("productionDate"))
        arguments.insert(QStringLiteral("value"), mContext.productionTimestamp);
    else if (valueFrom == QStringLiteral("serialNumber"))
        arguments.insert(QStringLiteral("value"), mContext.serialNumber);
    return arguments;
}

QString WorkflowExecution::formatMessage(const DeviceIdentity& identity, const QString& message, const QVariantMap& arguments) const
{
    QString formatted = message;
    formatted.replace(QStringLiteral("{type}"), identity.typeHex());
    formatted.replace(QStringLiteral("{state}"), arguments.value(QStringLiteral("state")).toString());
    formatted.replace(QStringLiteral("{value}"), arguments.value(QStringLiteral("value")).toString());
    return QStringLiteral("[%1] %2").arg(identity.typeHex(), formatted);
}
