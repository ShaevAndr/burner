#include "workflow_definition.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTime>
#include <algorithm>
#include <utility>

static QString resolveArtifactPath(const QString& relativePath)
{
    if (QFileInfo(relativePath).isAbsolute())
        return relativePath;

    QStringList roots;
    if (QCoreApplication::instance())
    {
        roots.append(QCoreApplication::applicationDirPath());
        roots.append(QCoreApplication::applicationDirPath() + QStringLiteral("/.."));
        roots.append(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    }
    roots.append({
        QDir::currentPath(),
        QDir::currentPath() + QStringLiteral("/app"),
        QDir::currentPath() + QStringLiteral("/../app")
    });

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

static QRegularExpression deviceDescriptionPatternToRegex(const QString& pattern)
{
    QString regex = QRegularExpression::escape(pattern.trimmed());
    regex.replace(QStringLiteral("\\{boot\\}"), QStringLiteral("(?: \\(Boot\\))?"));
    regex.replace(QStringLiteral("\\{year\\}"), QStringLiteral("\\d{4}"));
    regex.replace(QStringLiteral("\\{quarter\\}"), QStringLiteral("[IVX]+"));
    regex.replace(QStringLiteral("\\{serial\\}"), QStringLiteral("\\d+"));
    regex.replace(QStringLiteral("\\{sw\\}"), QStringLiteral(".+"));
    return QRegularExpression(QStringLiteral("^%1$").arg(regex),
        QRegularExpression::CaseInsensitiveOption);
}

struct IntelHexImage
{
    QByteArray data;
    int ignoredBytes = 0;
};

static bool parseIntelHex(const QByteArray& fileData,
    quint32 addressBase,
    int maxImageSize,
    IntelHexImage* image,
    QString* error)
{
    if (!image || maxImageSize <= 0)
    {
        if (error)
            *error = QStringLiteral("Invalid Intel HEX target range");
        return false;
    }

    image->data.clear();
    image->ignoredBytes = 0;
    quint32 upperAddress = 0;
    bool eofSeen = false;
    const QList<QByteArray> lines = fileData.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const QByteArray line = lines.at(lineIndex).trimmed();
        if (line.isEmpty())
            continue;
        if (!line.startsWith(':') || ((line.size() - 1) % 2) != 0)
        {
            if (error)
                *error = QStringLiteral("Bad Intel HEX record at line %1").arg(lineIndex + 1);
            return false;
        }

        const QByteArray encoded = line.mid(1);
        for (char character : encoded)
        {
            const bool isHex = (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f')
                || (character >= 'A' && character <= 'F');
            if (!isHex)
            {
                if (error)
                    *error = QStringLiteral("Bad Intel HEX character at line %1").arg(lineIndex + 1);
                return false;
            }
        }

        const QByteArray record = QByteArray::fromHex(encoded);
        if (record.size() < 5 || record.size() != quint8(record.at(0)) + 5)
        {
            if (error)
                *error = QStringLiteral("Bad Intel HEX length at line %1").arg(lineIndex + 1);
            return false;
        }

        quint8 checksum = 0;
        for (char byte : record)
            checksum = quint8(checksum + quint8(byte));
        if (checksum != 0)
        {
            if (error)
                *error = QStringLiteral("Bad Intel HEX checksum at line %1").arg(lineIndex + 1);
            return false;
        }

        const int byteCount = quint8(record.at(0));
        const quint16 offset = quint16((quint16(quint8(record.at(1))) << 8) | quint8(record.at(2)));
        const quint8 type = quint8(record.at(3));
        if (type == 0x00)
        {
            const quint64 absoluteAddress = quint64(upperAddress) + offset;
            const quint64 rangeBegin = addressBase;
            const quint64 rangeEnd = rangeBegin + quint64(maxImageSize);
            const quint64 recordEnd = absoluteAddress + quint64(byteCount);
            if (absoluteAddress >= rangeBegin && recordEnd <= rangeEnd)
            {
                const int relativeAddress = int(absoluteAddress - rangeBegin);
                const int requiredSize = relativeAddress + byteCount;
                if (requiredSize > image->data.size())
                    image->data.append(QByteArray(requiredSize - image->data.size(), char(0xFF)));
                std::copy(record.constBegin() + 4,
                    record.constBegin() + 4 + byteCount,
                    image->data.begin() + relativeAddress);
            }
            else if (recordEnd <= rangeBegin || absoluteAddress >= rangeEnd)
            {
                image->ignoredBytes += byteCount;
            }
            else
            {
                if (error)
                    *error = QStringLiteral("Intel HEX record crosses flash boundary at line %1")
                        .arg(lineIndex + 1);
                return false;
            }
        }
        else if (type == 0x01)
        {
            eofSeen = true;
            break;
        }
        else if (type == 0x04 && byteCount == 2)
        {
            upperAddress = quint32((quint32(quint8(record.at(4))) << 8)
                | quint8(record.at(5))) << 16;
        }
        else if (type != 0x05)
        {
            if (error)
                *error = QStringLiteral("Unsupported Intel HEX record type %1 at line %2")
                    .arg(type)
                    .arg(lineIndex + 1);
            return false;
        }
    }

    if (!eofSeen || image->data.isEmpty())
    {
        if (error)
            *error = !eofSeen
                ? QStringLiteral("Intel HEX end-of-file record is missing")
                : QStringLiteral("Intel HEX has no data in the configured flash range");
        return false;
    }
    return true;
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
    plan.flashNum = plan.artifact.flashNum;
    plan.offset = plan.artifact.offset;
    if (plan.artifact.pageSize > 0)
        plan.pageSize = plan.artifact.pageSize;
    if (plan.artifact.pagesCount > 0)
        plan.endPage = plan.artifact.pagesCount - 1;
    return plan;
}

static FirmwareArtifact artifactFromVariant(const QVariantMap& map, const FirmwareArtifact& fallback)
{
    if (map.isEmpty())
        return fallback;

    FirmwareArtifact artifact = fallback;
    artifact.firmwareId = map.value(QStringLiteral("firmwareId"), artifact.firmwareId).toString();
    artifact.target = map.value(QStringLiteral("target"), artifact.target).toString();
    artifact.title = map.value(QStringLiteral("title"), artifact.title).toString();
    artifact.version = map.value(QStringLiteral("version"), artifact.version).toString();
    artifact.relativePath = map.value(QStringLiteral("relativePath"), artifact.relativePath).toString();
    artifact.sha256 = map.value(QStringLiteral("sha256"), artifact.sha256).toString();
    artifact.format = map.value(QStringLiteral("format"), artifact.format).toString();
    artifact.addressBase = map.value(QStringLiteral("addressBase"), artifact.addressBase).toUInt();
    artifact.isDefault = map.value(QStringLiteral("default"), artifact.isDefault).toBool();
    artifact.flashNum = map.value(QStringLiteral("flashNum"), artifact.flashNum).toInt();
    artifact.offset = map.value(QStringLiteral("offset"), artifact.offset).toInt();
    artifact.pageSize = map.value(QStringLiteral("pageSize"), artifact.pageSize).toInt();
    artifact.pagesCount = map.value(QStringLiteral("pagesCount"), artifact.pagesCount).toInt();
    return artifact;
}

static QVariantMap artifactToVariant(const FirmwareArtifact& artifact)
{
    QVariantMap map;
    map.insert(QStringLiteral("firmwareId"), artifact.firmwareId);
    map.insert(QStringLiteral("target"), artifact.target);
    map.insert(QStringLiteral("title"), artifact.title);
    map.insert(QStringLiteral("version"), artifact.version);
    map.insert(QStringLiteral("relativePath"), artifact.relativePath);
    map.insert(QStringLiteral("sha256"), artifact.sha256);
    map.insert(QStringLiteral("format"), artifact.format);
    map.insert(QStringLiteral("addressBase"), artifact.addressBase);
    map.insert(QStringLiteral("default"), artifact.isDefault);
    map.insert(QStringLiteral("flashNum"), artifact.flashNum);
    map.insert(QStringLiteral("offset"), artifact.offset);
    map.insert(QStringLiteral("pageSize"), artifact.pageSize);
    map.insert(QStringLiteral("pagesCount"), artifact.pagesCount);
    return map;
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
    else if ((isRuntimeStep(step.op) && !executeRuntimeStep(device, step))
        || (!isRuntimeStep(step.op) && !executeDeviceStep(device, step)))
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

bool WorkflowExecution::ensureDeviceUuid(DeviceBase& device)
{
    if (!device.identity().uuid.isEmpty())
        return true;

    QString uuid;
    QString error;
    QString raw;
    if (!device.readUuid(&uuid, &error, &raw))
    {
        if (!raw.isEmpty())
            transportLog(QStringLiteral("[%1] %2").arg(device.identity().typeHex(), raw));
        log(QStringLiteral("[%1] UUID read before reset failed: %2")
            .arg(device.identity().typeHex(), error));
        return false;
    }
    if (!raw.isEmpty())
        transportLog(QStringLiteral("[%1] %2").arg(device.identity().typeHex(), raw));

    DeviceIdentity updated = device.identity();
    updated.uuid = uuid;
    device.updateIdentity(updated);
    log(QStringLiteral("[%1] device UUID %2").arg(updated.typeHex(), updated.uuid));
    return true;
}

bool WorkflowExecution::disableApplicationLoading(DeviceBase& device)
{
    const DeviceIdentity identity = device.identity();
    const QString stage = QStringLiteral("disable application loading");
    reportProgressStage(identity, stage);
    log(QStringLiteral("[%1] %2 (int[0] = 0)").arg(identity.typeHex(), stage));

    QString error;
    QString raw;
    if (!device.disableLoadApplication(&error, &raw))
    {
        if (!raw.isEmpty())
            transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
        log(QStringLiteral("[%1] disable application loading failed: %2")
            .arg(identity.typeHex(), error));
        return false;
    }
    if (!raw.isEmpty())
        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
    return true;
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

bool WorkflowExecution::isRuntimeStep(const QString& operation) const
{
    static const QSet<QString> runtimeOperations = {
        QStringLiteral("context.productionDate"),
        QStringLiteral("context.serialNumber"),
        QStringLiteral("sleep"),
        QStringLiteral("device.connect"),
        QStringLiteral("firmware.executeTransition"),
        QStringLiteral("flash.prepare"),
        QStringLiteral("flash.validateArtifact"),
        QStringLiteral("flash.preflight"),
        QStringLiteral("flash.simulateWrite"),
        QStringLiteral("flash.complete"),
        QStringLiteral("workflow.finish"),
        QStringLiteral("log")
    };
    return runtimeOperations.contains(operation);
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

    if (step.op == QStringLiteral("firmware.executeTransition"))
    {
        if (!ensureDeviceUuid(device))
            return false;

        const QString targetFirmwareId = mParameters.value(QStringLiteral("targetFirmwareId")).toString();
        if (identity.currentFirmwareId.isEmpty())
        {
            log(QStringLiteral("[%1] firmware transition denied: %2")
                .arg(identity.typeHex(), identity.firmwareDetectionError.isEmpty()
                    ? QStringLiteral("current firmware is unknown")
                    : identity.firmwareDetectionError));
            return false;
        }

        const FirmwareTransitionSpec* transitionSpec = identity.transitionTo(targetFirmwareId);
        const FirmwareVersionSpec* targetFirmwareSpec = identity.firmwareVersionById(targetFirmwareId);
        if (!transitionSpec || !transitionSpec->enabled || !targetFirmwareSpec)
        {
            const QString reason = transitionSpec && !transitionSpec->reason.isEmpty()
                ? transitionSpec->reason
                : QStringLiteral("transition is not configured or disabled");
            log(QStringLiteral("[%1] firmware transition %2 -> %3 denied: %4")
                .arg(identity.typeHex(), identity.currentFirmwareId, targetFirmwareId, reason));
            return false;
        }

        const FirmwareTransitionSpec transition = *transitionSpec;
        const FirmwareVersionSpec targetFirmware = *targetFirmwareSpec;

        log(QStringLiteral("[%1] firmware transition %2 -> %3, %4 pipeline step(s)")
            .arg(identity.typeHex(), transition.from, transition.to)
            .arg(transition.pipeline.size()));
        mParameters.insert(QStringLiteral("artifact"), artifactToVariant(targetFirmware.artifact));
        mParameters.insert(QStringLiteral("verifyAfterWrite"), false);
        mParameters.insert(QStringLiteral("bootloaderPrepared"), false);

        bool flashed = false;
        for (int pipelineIndex = 0; pipelineIndex < transition.pipeline.size(); ++pipelineIndex)
        {
            const FirmwarePipelineStep& pipelineStep = transition.pipeline.at(pipelineIndex);
            log(QStringLiteral("[%1] pipeline %2/%3: %4")
                .arg(identity.typeHex())
                .arg(pipelineIndex + 1)
                .arg(transition.pipeline.size())
                .arg(pipelineStep.type));

            if (!pipelineStep.skipIfState.isEmpty() && identity.state == pipelineStep.skipIfState)
            {
                log(QStringLiteral("[%1] pipeline step %2 skipped for state %3")
                    .arg(identity.typeHex(), pipelineStep.type, identity.state));
                continue;
            }

            const bool loadBootloaderStep = pipelineStep.type == QStringLiteral("loadBootloader");
            if (loadBootloaderStep && identity.state == QStringLiteral("bootloader"))
            {
                log(QStringLiteral("[%1] device is already in bootloader mode").arg(identity.typeHex()));
                if (!disableApplicationLoading(device))
                    return false;
                mParameters.insert(QStringLiteral("bootloaderPrepared"), true);
                continue;
            }

            if (pipelineStep.type == QStringLiteral("reset") || loadBootloaderStep)
            {
                if (!ensureDeviceUuid(device))
                    return false;

                QString error;
                QString raw;
                if (!device.reset(&error, &raw))
                {
                    if (!raw.isEmpty())
                        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                    log(QStringLiteral("[%1] reset failed: %2").arg(identity.typeHex(), error));
                    return false;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

                if (identity.bootloaderType == 0)
                {
                    log(QStringLiteral("[%1] bootloader identity is not configured").arg(identity.typeHex()));
                    return false;
                }

                DeviceIdentity expectedBootloader = identity;
                // Do not filter discovery by the configured bootloader identity. Some
                // bootloaders expose a different or incomplete type/version and must
                // be recognized from the device description instead.
                expectedBootloader.type = 0;
                expectedBootloader.version = 0;
                expectedBootloader.state = QStringLiteral("bootloader");

                DeviceIdentity found;
                const int timeoutMs = pipelineStep.arguments.value(QStringLiteral("timeoutMs"), 15000).toInt();
                const int pollIntervalMs = pipelineStep.arguments.value(QStringLiteral("pollIntervalMs"), 500).toInt();
                raw.clear();
                error.clear();
                log(QStringLiteral("[%1] waiting for bootloader (expected %2 %3, description fallback enabled)")
                    .arg(identity.typeHex(),
                        QStringLiteral("0X%1").arg(identity.bootloaderType, 4, 16, QLatin1Char('0')).toUpper(),
                        QStringLiteral("0X%1").arg(identity.bootloaderVersion, 4, 16, QLatin1Char('0')).toUpper()));
                if (!device.waitForDeviceIdentity(expectedBootloader, timeoutMs, pollIntervalMs, &found, &error, &raw))
                {
                    if (!raw.isEmpty())
                        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                    log(QStringLiteral("[%1] waitForBootloader failed: %2").arg(identity.typeHex(), error));
                    return false;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

                static const QRegularExpression bootMarker(
                    QStringLiteral("\\(\\s*Boot\\s*\\)"),
                    QRegularExpression::CaseInsensitiveOption);
                const bool configuredIdentityMatches = found.type == identity.bootloaderType
                    && found.version == identity.bootloaderVersion;
                const QRegularExpression descriptionPattern =
                    deviceDescriptionPatternToRegex(identity.expectedDescriptionPattern);
                const bool descriptionMatches = !identity.expectedDescriptionPattern.trimmed().isEmpty()
                    && descriptionPattern.isValid()
                    && descriptionPattern.match(found.description.trimmed()).hasMatch();
                const bool hasBootMarker = bootMarker.match(found.description).hasMatch();
                const bool uuidMatches = !identity.uuid.isEmpty()
                    && found.uuid.compare(identity.uuid, Qt::CaseInsensitive) == 0;
                if (!uuidMatches || !hasBootMarker || (!configuredIdentityMatches && !descriptionMatches))
                {
                    log(QStringLiteral("[%1] unexpected bootloader identity: %2 %3 UUID=%4 '%5'")
                        .arg(identity.typeHex(), found.typeHex(), found.versionHex(), found.uuid, found.description));
                    return false;
                }
                if (!configuredIdentityMatches)
                {
                    log(QStringLiteral("[%1] bootloader identified by description: %2 %3 '%4'")
                        .arg(identity.typeHex(), found.typeHex(), found.versionHex(), found.description));
                }

                DeviceIdentity updated = identity;
                updated.id = !identity.uuid.isEmpty() ? identity.uuid : found.id;
                updated.endpoint = found.endpoint;
                updated.modbusAddress = found.modbusAddress;
                updated.type = found.type;
                updated.version = found.version;
                updated.description = found.description;
                updated.serialNumber = found.serialNumber;
                updated.state = QStringLiteral("bootloader");
                device.updateIdentity(updated);
                log(QStringLiteral("[%1] bootloader connected at %2")
                    .arg(found.typeHex(), found.endpoint));
                if (loadBootloaderStep)
                {
                    if (!disableApplicationLoading(device))
                        return false;
                    mParameters.insert(QStringLiteral("bootloaderPrepared"), true);
                }
                sleepWithEvents(pipelineStep.arguments.value(QStringLiteral("settleMs"), 250).toInt());
                continue;
            }

            if (pipelineStep.type == QStringLiteral("flash"))
            {
                WorkflowStep flashStep;
                flashStep.op = QStringLiteral("flash.prepare");
                if (!executeRuntimeStep(device, flashStep))
                    return false;
                flashStep.op = QStringLiteral("flash.validateArtifact");
                if (!executeRuntimeStep(device, flashStep))
                    return false;
                flashStep.op = QStringLiteral("flash.preflight");
                if (!executeRuntimeStep(device, flashStep))
                    return false;
                flashStep.op = QStringLiteral("flash.simulateWrite");
                if (!executeRuntimeStep(device, flashStep))
                    return false;
                flashed = true;
                continue;
            }

            if (pipelineStep.type == QStringLiteral("verify"))
            {
                if (!flashed || !verifyFlashPages(device))
                    return false;
                continue;
            }

            if (pipelineStep.type == QStringLiteral("restart"))
            {
                QString error;
                QString raw;
                if (!device.loadApplicationNoReply(&error, &raw))
                {
                    if (!raw.isEmpty())
                        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                    log(QStringLiteral("[%1] application restart failed: %2").arg(identity.typeHex(), error));
                    return false;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                continue;
            }

            if (pipelineStep.type == QStringLiteral("waitForApplication"))
            {
                DeviceIdentity found;
                DeviceIdentity expectedApplication = identity;
                expectedApplication.type = identity.applicationType;
                expectedApplication.version = identity.applicationVersion;
                expectedApplication.state = QStringLiteral("application");
                QString error;
                QString raw;
                const int timeoutMs = pipelineStep.arguments.value(QStringLiteral("timeoutMs"), 15000).toInt();
                const int pollIntervalMs = pipelineStep.arguments.value(QStringLiteral("pollIntervalMs"), 1000).toInt();
                if (!device.waitForDeviceIdentity(expectedApplication, timeoutMs, pollIntervalMs, &found, &error, &raw))
                {
                    if (!raw.isEmpty())
                        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                    log(QStringLiteral("[%1] waitForApplication failed: %2").arg(identity.typeHex(), error));
                    return false;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

                const QRegularExpression targetMatcher(targetFirmware.descriptionRegex);
                const bool identityMatches = found.type == identity.applicationType
                    && found.version == identity.applicationVersion;
                const bool uuidMatches = !identity.uuid.isEmpty()
                    && found.uuid.compare(identity.uuid, Qt::CaseInsensitive) == 0;
                const bool descriptionMatches = targetMatcher.isValid()
                    && targetMatcher.match(found.description.trimmed()).hasMatch();
                if (!uuidMatches || !identityMatches || !descriptionMatches)
                {
                    log(QStringLiteral("[%1] application appeared with unexpected identity, UUID or firmware: %2 %3 UUID=%4 '%5'")
                        .arg(identity.typeHex(), found.typeHex(), found.versionHex(), found.uuid, found.description));
                    return false;
                }

                DeviceIdentity updated = identity;
                updated.id = !identity.uuid.isEmpty() ? identity.uuid : found.id;
                updated.endpoint = found.endpoint;
                updated.modbusAddress = found.modbusAddress;
                updated.type = found.type;
                updated.version = found.version;
                updated.description = found.description;
                updated.serialNumber = found.serialNumber;
                updated.state = QStringLiteral("application");
                updated.currentFirmwareId = targetFirmwareId;
                updated.firmwareDetectionError.clear();
                updated.status = QStringLiteral("прошивка %1").arg(targetFirmwareId);
                device.updateIdentity(updated);
                log(QStringLiteral("[%1] application %2 is running")
                    .arg(found.typeHex(), targetFirmwareId));
                continue;
            }
        }
        return true;
    }

    if (step.op == QStringLiteral("flash.prepare"))
    {
        mContext.flashPlan = buildFlashPlan(identity, mAction);
        mContext.flashPlan.artifact = artifactFromVariant(mParameters.value(QStringLiteral("artifact")).toMap(), mContext.flashPlan.artifact);
        mContext.flashPlan.flashNum = mContext.flashPlan.artifact.flashNum;
        mContext.flashPlan.offset = mContext.flashPlan.artifact.offset;
        mContext.flashPlan.verifyAfterWrite = mParameters.value(QStringLiteral("verifyAfterWrite"), true).toBool();
        if (mContext.flashPlan.artifact.pageSize > 0)
            mContext.flashPlan.pageSize = mContext.flashPlan.artifact.pageSize;
        if (mContext.flashPlan.artifact.pagesCount > 0)
            mContext.flashPlan.endPage = mContext.flashPlan.artifact.pagesCount - 1;
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
            mContext.flashPlan.fileName = fileName;
            QString hashError;
            const QString actualHash = sha256File(fileName, &hashError);
            if (actualHash.isEmpty())
            {
                log(QStringLiteral("[%1] firmware file read failed: %2")
                    .arg(identity.typeHex(), hashError));
                return false;
            }
            else if (actualHash.compare(mContext.flashPlan.artifact.sha256, Qt::CaseInsensitive) == 0)
            {
                log(QStringLiteral("[%1] firmware hash OK").arg(identity.typeHex()));
            }
            else if (mContext.flashPlan.artifact.sha256.trimmed().isEmpty())
            {
                log(QStringLiteral("[%1] firmware hash calculated %2").arg(identity.typeHex(), actualHash));
            }
            else
            {
                log(QStringLiteral("[%1] firmware hash mismatch actual=%2")
                    .arg(identity.typeHex(), actualHash));
                return false;
            }

            QFile file(fileName);
            if (!file.open(QIODevice::ReadOnly))
            {
                log(QStringLiteral("[%1] firmware file open failed: %2")
                    .arg(identity.typeHex(), file.errorString()));
                return false;
            }
            mContext.flashPlan.data = file.readAll();
            if (mContext.flashPlan.data.isEmpty())
            {
                log(QStringLiteral("[%1] firmware file is empty").arg(identity.typeHex()));
                return false;
            }
        }
        else
        {
            log(QStringLiteral("[%1] no firmware artifact configured for target=%2")
                .arg(identity.typeHex(), mContext.flashPlan.target));
            reportProgressStage(identity, QStringLiteral("no firmware artifact configured"));
            return false;
        }
        return true;
    }

    if (step.op == QStringLiteral("flash.preflight"))
    {
        if (!mParameters.value(QStringLiteral("bootloaderPrepared"), false).toBool())
        {
            if (!disableApplicationLoading(device))
                return false;
            mParameters.insert(QStringLiteral("bootloaderPrepared"), true);
        }
        else
        {
            log(QStringLiteral("[%1] application loading is already disabled")
                .arg(identity.typeHex()));
        }

        const QStringList flashPreflightSteps = {
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
            QVector<FlashMemoryParams> params;
            QString error;
            QString raw;
            bool paramsLoaded = false;
            const int paramsAttempts = 4;
            for (int attempt = 1; attempt <= paramsAttempts; ++attempt)
            {
                error.clear();
                raw.clear();
                if (device.flashGetParams(&params, &error, &raw))
                {
                    paramsLoaded = true;
                    break;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                if (attempt < paramsAttempts)
                {
                    log(QStringLiteral("[%1] flashGetParams retry %2/%3 after: %4")
                        .arg(identity.typeHex())
                        .arg(attempt + 1)
                        .arg(paramsAttempts)
                        .arg(error));
                    sleepWithEvents(500);
                }
            }
            if (!paramsLoaded)
            {
                log(QStringLiteral("[%1] flashGetParams failed after %2 attempt(s): %3")
                    .arg(identity.typeHex())
                    .arg(paramsAttempts)
                    .arg(error));
                return false;
            }
            if (!raw.isEmpty())
                transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

            const int flashNum = mContext.flashPlan.flashNum;
            if (flashNum < 0 || flashNum >= params.size() || !params.at(flashNum).isValid())
            {
                log(QStringLiteral("[%1] flash #%2 is not available").arg(identity.typeHex()).arg(flashNum));
                return false;
            }

            mContext.flashPlan.pageSize = params.at(flashNum).pageSize;
            const int pagesCount = params.at(flashNum).pagesCount;
            const bool isIntelHex = mContext.flashPlan.artifact.format.compare(QStringLiteral("intelHex"), Qt::CaseInsensitive) == 0
                || mContext.flashPlan.fileName.endsWith(QStringLiteral(".hex"), Qt::CaseInsensitive)
                || mContext.flashPlan.fileName.endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive);
            if (isIntelHex)
            {
                const int maxImageSize = pagesCount * mContext.flashPlan.pageSize - mContext.flashPlan.offset;
                IntelHexImage image;
                if (!parseIntelHex(mContext.flashPlan.data,
                        mContext.flashPlan.artifact.addressBase,
                        maxImageSize,
                        &image,
                        &error))
                {
                    log(QStringLiteral("[%1] Intel HEX parse failed: %2").arg(identity.typeHex(), error));
                    return false;
                }
                mContext.flashPlan.data = image.data;
                log(QStringLiteral("[%1] Intel HEX loaded %2 flash bytes from base 0x%3; ignored %4 out-of-range bytes")
                    .arg(identity.typeHex())
                    .arg(image.data.size())
                    .arg(mContext.flashPlan.artifact.addressBase, 8, 16, QLatin1Char('0'))
                    .arg(image.ignoredBytes));
            }
            const int firstPage = qMax(0, mContext.flashPlan.offset / mContext.flashPlan.pageSize);
            const int firstPageOffset = qMax(0, mContext.flashPlan.offset % mContext.flashPlan.pageSize);
            const int pagesToWrite = (firstPageOffset + mContext.flashPlan.data.size() + mContext.flashPlan.pageSize - 1) / mContext.flashPlan.pageSize;
            if (pagesToWrite <= 0 || firstPage + pagesToWrite > pagesCount)
            {
                log(QStringLiteral("[%1] firmware does not fit flash #%2: size=%3 pageSize=%4 pages=%5")
                    .arg(identity.typeHex())
                    .arg(flashNum)
                    .arg(mContext.flashPlan.data.size())
                    .arg(mContext.flashPlan.pageSize)
                    .arg(pagesCount));
                return false;
            }

            mContext.flashPlan.firstWrittenPage = firstPage;
            mContext.flashPlan.expectedPages.clear();

            log(QStringLiteral("[%1] writing %2 bytes to flash #%3, pages %4-%5")
                .arg(identity.typeHex())
                .arg(mContext.flashPlan.data.size())
                .arg(flashNum)
                .arg(firstPage)
                .arg(firstPage + pagesToWrite - 1));

            for (int pageIndex = 0; pageIndex < pagesToWrite; ++pageIndex)
            {
                QByteArray page(mContext.flashPlan.pageSize, char(0xFF));
                const int targetOffset = pageIndex == 0 ? firstPageOffset : 0;
                const int sourceOffset = pageIndex * mContext.flashPlan.pageSize - firstPageOffset;
                const int normalizedSourceOffset = qMax(0, sourceOffset);
                const int chunkSize = qMin(mContext.flashPlan.pageSize - targetOffset, mContext.flashPlan.data.size() - normalizedSourceOffset);
                std::copy(mContext.flashPlan.data.constBegin() + normalizedSourceOffset,
                    mContext.flashPlan.data.constBegin() + normalizedSourceOffset + chunkSize,
                    page.begin() + targetOffset);
                mContext.flashPlan.expectedPages.append(page);

                raw.clear();
                if (!device.flashWritePage(flashNum, firstPage + pageIndex, page, &error, &raw))
                {
                    if (!raw.isEmpty())
                        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                    log(QStringLiteral("[%1] flash page %2 write failed: %3")
                        .arg(identity.typeHex())
                        .arg(firstPage + pageIndex)
                        .arg(error));
                    return false;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

                const int percent = ((pageIndex + 1) * 80) / pagesToWrite;
                progress(percent);
                log(QStringLiteral("[%1] flash #%2 wrote page %3 of %4")
                    .arg(identity.typeHex())
                    .arg(flashNum)
                    .arg(pageIndex + 1)
                    .arg(pagesToWrite));
                processEvents();
            }

            if (mContext.flashPlan.verifyAfterWrite)
                return verifyFlashPages(device);
            else
            {
                progress(100);
            }
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

bool WorkflowExecution::verifyFlashPages(DeviceBase& device)
{
    const DeviceIdentity& identity = device.identity();
    if (mContext.flashPlan.expectedPages.isEmpty())
    {
        log(QStringLiteral("[%1] flash verify requested before any pages were written")
            .arg(identity.typeHex()));
        return false;
    }

    for (int pageIndex = 0; pageIndex < mContext.flashPlan.expectedPages.size(); ++pageIndex)
    {
        QByteArray actual;
        QString error;
        QString raw;
        const int pageNum = mContext.flashPlan.firstWrittenPage + pageIndex;
        if (!device.flashReadPage(mContext.flashPlan.flashNum, pageNum, &actual, &error, &raw))
        {
            if (!raw.isEmpty())
                transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
            log(QStringLiteral("[%1] flash page %2 verify read failed: %3")
                .arg(identity.typeHex())
                .arg(pageNum)
                .arg(error));
            return false;
        }
        if (!raw.isEmpty())
            transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
        if (actual != mContext.flashPlan.expectedPages.at(pageIndex))
        {
            log(QStringLiteral("[%1] flash page %2 verify mismatch")
                .arg(identity.typeHex())
                .arg(pageNum));
            return false;
        }

        progress(80 + ((pageIndex + 1) * 20) / mContext.flashPlan.expectedPages.size());
        processEvents();
    }
    log(QStringLiteral("[%1] flash verify OK").arg(identity.typeHex()));
    return true;
}

bool WorkflowExecution::executeDeviceStep(DeviceBase& device, const WorkflowStep& step)
{
    if (step.op == QStringLiteral("device.reset") && !ensureDeviceUuid(device))
        return false;

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
