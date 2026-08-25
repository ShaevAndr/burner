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

static bool deviceDescriptionContainsKeywords(const QString& description, const QStringList& keywords)
{
    if (keywords.isEmpty())
        return false;
    for (const QString& keyword : keywords)
    {
        if (keyword.trimmed().isEmpty()
            || !description.contains(keyword.trimmed(), Qt::CaseInsensitive))
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

static FirmwareFlashPlan buildFlashPlan(const DeviceIdentity& identity, const ActionSpec& action)
{
    FirmwareFlashPlan plan;
    plan.workflowId = action.workflow;
    plan.target = action.target;
    plan.artifact = identity.firmwareForTarget(action.target);
    plan.strategyId = QStringLiteral("page-flash");
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
    artifact.flashStrategy = map.value(QStringLiteral("flashStrategy"), artifact.flashStrategy).toString();
    artifact.flashParameters = map.value(QStringLiteral("flashParameters"), artifact.flashParameters).toMap();
    return artifact;
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

const WorkflowDefinition* WorkflowRepository::definitionForId(const QString& workflowId) const
{
    const auto definition = mDefinitions.constFind(workflowId);
    return definition == mDefinitions.constEnd() ? nullptr : &definition.value();
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

    if (mCallbacks.stageChanged)
        mCallbacks.stageChanged(step.op, step.label.isEmpty() ? step.op : step.label);

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
        restoreApplicationAfterFailure(device);
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
        if (mCallbacks.stageChanged)
            mCallbacks.stageChanged(QString(), stage);
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
            || mContext.transportError.contains(QStringLiteral("not ready"), Qt::CaseInsensitive)
            || mContext.transportError.contains(QStringLiteral("Device returned ASCII error"), Qt::CaseInsensitive);
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

void WorkflowExecution::restoreApplicationAfterFailure(DeviceBase& device)
{
    if (!mContext.applicationLoadingDisabled
        || (mAction.id != QStringLiteral("device.productionDate.update")
            && mAction.id != QStringLiteral("device.serialNumber.update")))
        return;

    const DeviceIdentity identity = device.identity();
    log(QStringLiteral("[%1] operation failed in bootloader; restoring main application")
        .arg(identity.typeHex()));

    QString loadError;
    QString loadRaw;
    if (!device.loadApplicationNoReply(&loadError, &loadRaw))
    {
        log(QStringLiteral("[%1] recovery load command failed: %2; checking application state")
            .arg(identity.typeHex(), loadError));
    }
    if (!loadRaw.isEmpty())
        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), loadRaw));

    DeviceIdentity expected = identity;
    expected.type = identity.applicationType;
    expected.version = identity.applicationVersion;
    expected.state = QStringLiteral("application");
    DeviceIdentity found;
    QString waitError;
    QString waitRaw;
    if (!device.waitForDeviceIdentity(expected, 30000, 500, &found, &waitError, &waitRaw))
    {
        if (!waitRaw.isEmpty())
            transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), waitRaw));
        log(QStringLiteral("[%1] main application recovery failed: %2")
            .arg(identity.typeHex(), waitError));
        return;
    }
    if (!waitRaw.isEmpty())
        transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), waitRaw));

    DeviceIdentity updated = identity;
    updated.id = !identity.uuid.isEmpty() ? identity.uuid : found.id;
    updated.endpoint = found.endpoint;
    updated.modbusAddress = found.modbusAddress;
    updated.type = found.type;
    updated.version = found.version;
    updated.description = found.description;
    if (!found.serialNumber.trimmed().isEmpty())
        updated.serialNumber = found.serialNumber;
    updated.state = QStringLiteral("application");
    device.updateIdentity(updated);
    mContext.applicationLoadingDisabled = false;
    log(QStringLiteral("[%1] main application restored after failed operation")
        .arg(updated.typeHex()));
}

bool WorkflowExecution::isRuntimeStep(const QString& operation) const
{
    static const QSet<QString> runtimeOperations = {
        QStringLiteral("context.productionDate"),
        QStringLiteral("context.serialNumber"),
        QStringLiteral("sleep"),
        QStringLiteral("device.connect"),
        QStringLiteral("device.ensureUuid"),
        QStringLiteral("firmware.validateTransition"),
        QStringLiteral("device.enterBootloader"),
        QStringLiteral("device.disableApplicationLoad"),
        QStringLiteral("device.captureServiceData"),
        QStringLiteral("firmware.validateArtifact"),
        QStringLiteral("firmware.flash"),
        QStringLiteral("firmware.verify"),
        QStringLiteral("device.restoreServiceData"),
        QStringLiteral("device.waitForApplication"),
        QStringLiteral("firmware.verifyInstalledVersion"),
        QStringLiteral("firmware.complete"),
        QStringLiteral("flash.prepare"),
        QStringLiteral("flash.validateArtifact"),
        QStringLiteral("flash.preflight"),
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
        if (identity.isBootloader())
            mContext.applicationLoadingDisabled = true;
        const QDate productionDate = mParameters.value(QStringLiteral("productionDate")).toDate();
        const QDate effectiveDate = productionDate.isValid() ? productionDate : QDate::currentDate();
        mContext.productionTimestamp = QDateTime(effectiveDate, QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch();
        log(QStringLiteral("[%1] preparing production date update for %2")
            .arg(identity.typeHex(), effectiveDate.toString(QStringLiteral("dd.MM.yyyy"))));
        return true;
    }

    if (step.op == QStringLiteral("context.serialNumber"))
    {
        if (identity.isBootloader())
            mContext.applicationLoadingDisabled = true;
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

    if (step.op == QStringLiteral("device.ensureUuid"))
        return ensureDeviceUuid(device);

    if (step.op == QStringLiteral("firmware.validateTransition"))
    {
        mContext.targetFirmwareId = mParameters.value(QStringLiteral("targetFirmwareId")).toString();
        const FirmwareVersionSpec* target = identity.firmwareVersionById(mContext.targetFirmwareId);
        if (!target)
        {
            log(QStringLiteral("[%1] firmware target %2 is not configured for this device")
                .arg(identity.typeHex(), mContext.targetFirmwareId));
            return false;
        }

        const bool unknownCurrentFirmware = identity.known && identity.currentFirmwareId.isEmpty();
        const FirmwareTransitionSpec* transition = unknownCurrentFirmware
            ? nullptr
            : identity.transitionTo(mContext.targetFirmwareId);
        if (!unknownCurrentFirmware && (!transition || !transition->enabled))
        {
            const QString reason = transition && !transition->reason.isEmpty()
                ? transition->reason
                : QStringLiteral("transition is not configured or disabled");
            log(QStringLiteral("[%1] firmware transition %2 -> %3 denied: %4")
                .arg(identity.typeHex(), identity.currentFirmwareId, mContext.targetFirmwareId, reason));
            return false;
        }
        if (target->installation.strategy.isEmpty())
        {
            log(QStringLiteral("[%1] firmware %2 has no flash strategy")
                .arg(identity.typeHex(), target->id));
            return false;
        }
        if (!FirmwareFlashStrategyRegistry::find(target->installation.strategy))
        {
            log(QStringLiteral("[%1] firmware %2 uses unknown flash strategy '%3'")
                .arg(identity.typeHex(), target->id, target->installation.strategy));
            return false;
        }

        mContext.targetFirmware = *target;
        mContext.transitionValidated = true;
        mContext.flashPlan = buildFlashPlan(identity, mAction);
        mContext.flashPlan.workflowId = target->installation.workflow;
        mContext.flashPlan.strategyId = target->installation.strategy;
        mContext.flashPlan.strategyParameters = target->installation.parameters;
        mContext.flashPlan.artifact = target->artifact;
        mContext.flashPlan.target = target->artifact.target;
        mContext.flashPlan.flashNum = target->artifact.flashNum;
        mContext.flashPlan.offset = target->artifact.offset;
        mContext.flashPlan.verifyAfterWrite = false;
        if (target->artifact.pageSize > 0)
            mContext.flashPlan.pageSize = target->artifact.pageSize;
        if (target->artifact.pagesCount > 0)
            mContext.flashPlan.endPage = target->artifact.pagesCount - 1;

        if (unknownCurrentFirmware)
        {
            log(QStringLiteral("[%1] current firmware is unknown; target %2 selected from device catalog via %3, strategy=%4")
                .arg(identity.typeHex(), target->id,
                    target->installation.workflow, target->installation.strategy));
        }
        else
        {
            log(QStringLiteral("[%1] firmware transition %2 -> %3 via %4, strategy=%5")
                .arg(identity.typeHex(), identity.currentFirmwareId, target->id,
                    target->installation.workflow, target->installation.strategy));
        }
        reportProgressStage(identity, QStringLiteral("queued"));
        return true;
    }

    if (step.op == QStringLiteral("device.enterBootloader"))
    {
        if (!mContext.transitionValidated)
        {
            log(QStringLiteral("[%1] enter bootloader requested before transition validation")
                .arg(identity.typeHex()));
            return false;
        }
        if (identity.state == QStringLiteral("bootloader"))
        {
            log(QStringLiteral("[%1] device is already in bootloader mode").arg(identity.typeHex()));
            return true;
        }
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

        DeviceIdentity expected = identity;
        expected.type = 0;
        expected.version = 0;
        expected.state = QStringLiteral("bootloader");
        DeviceIdentity found;
        const int timeoutMs = step.arguments.value(QStringLiteral("timeoutMs"), 15000).toInt();
        const int pollIntervalMs = step.arguments.value(QStringLiteral("pollIntervalMs"), 500).toInt();
        log(QStringLiteral("[%1] waiting for bootloader (expected %2 %3, description fallback enabled)")
            .arg(identity.typeHex(),
                QStringLiteral("0X%1").arg(identity.bootloaderType, 4, 16, QLatin1Char('0')).toUpper(),
                QStringLiteral("0X%1").arg(identity.bootloaderVersion, 4, 16, QLatin1Char('0')).toUpper()));
        if (!device.waitForDeviceIdentity(expected, timeoutMs, pollIntervalMs, &found, &error, &raw))
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
        const bool descriptionMatches = deviceDescriptionContainsKeywords(found.description, identity.descriptionKeywords);
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
        log(QStringLiteral("[%1] bootloader connected at %2").arg(found.typeHex(), found.endpoint));
        sleepWithEvents(step.arguments.value(QStringLiteral("settleMs"), 250).toInt());
        return true;
    }

    if (step.op == QStringLiteral("device.disableApplicationLoad"))
        return disableApplicationLoading(device);

    if (step.op == QStringLiteral("device.captureServiceData"))
    {
        struct ServiceValue
        {
            int registerIndex;
            QString label;
            qint32* value;
            bool* available;
        };
        const ServiceValue values[] = {
            {identity.productionDateRegister, QStringLiteral("production date"),
                &mContext.preservedProductionDate, &mContext.hasPreservedProductionDate},
            {identity.serialNumberRegister, QStringLiteral("serial number"),
                &mContext.preservedSerialNumber, &mContext.hasPreservedSerialNumber}
        };
        for (const ServiceValue& service : values)
        {
            if (service.registerIndex < 0)
                continue;
            QString error;
            QString raw;
            qint32 value = 0;
            if (!device.readInt(quint16(service.registerIndex), &value, &error, &raw))
            {
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                log(QStringLiteral("[%1] cannot preserve %2 from int[%3]: %4")
                    .arg(identity.typeHex(), service.label)
                    .arg(service.registerIndex)
                    .arg(error));
                return false;
            }
            if (!raw.isEmpty())
                transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
            *service.value = value;
            *service.available = true;
            log(QStringLiteral("[%1] preserved %2 from int[%3]")
                .arg(identity.typeHex(), service.label)
                .arg(service.registerIndex));
        }
        return true;
    }

    if (step.op == QStringLiteral("firmware.validateArtifact"))
    {
        if (!mContext.transitionValidated)
        {
            log(QStringLiteral("[%1] artifact validation requested before transition validation")
                .arg(identity.typeHex()));
            return false;
        }
        WorkflowStep validationStep;
        validationStep.op = QStringLiteral("flash.validateArtifact");
        return executeRuntimeStep(device, validationStep);
    }

    if (step.op == QStringLiteral("firmware.flash"))
    {
        if (mContext.flashPlan.data.isEmpty())
        {
            log(QStringLiteral("[%1] flash requested before artifact validation").arg(identity.typeHex()));
            return false;
        }
        const FirmwareFlashStrategy* strategy = FirmwareFlashStrategyRegistry::find(mContext.flashPlan.strategyId);
        if (!strategy)
        {
            log(QStringLiteral("[%1] unknown flash strategy '%2'")
                .arg(identity.typeHex(), mContext.flashPlan.strategyId));
            return false;
        }
        const FirmwareFlashCallbacks callbacks = {
            [this](const QString& message) { log(message); },
            [this](const QString& message) { transportLog(message); },
            [this](int value) { progress(value); },
            [this]() { processEvents(); }
        };
        mContext.flashWritten = strategy->flash(device, mContext.flashPlan, callbacks);
        return mContext.flashWritten;
    }

    if (step.op == QStringLiteral("firmware.verify"))
    {
        if (!mContext.flashWritten)
        {
            log(QStringLiteral("[%1] firmware verify requested before flash")
                .arg(identity.typeHex()));
            return false;
        }
        if (mContext.flashPlan.strategyId == QStringLiteral("test-no-write"))
            return true;
        return verifyFlashPages(device);
    }

    if (step.op == QStringLiteral("device.restoreServiceData"))
    {
        struct ServiceValue
        {
            int registerIndex;
            QString label;
            qint32 value;
            bool available;
        };
        const ServiceValue values[] = {
            {identity.productionDateRegister, QStringLiteral("production date"),
                mContext.preservedProductionDate, mContext.hasPreservedProductionDate},
            {identity.serialNumberRegister, QStringLiteral("serial number"),
                mContext.preservedSerialNumber, mContext.hasPreservedSerialNumber}
        };
        for (const ServiceValue& service : values)
        {
            if (!service.available || service.registerIndex < 0)
                continue;
            QString error;
            QString raw;
            bool restored = false;
            for (int attempt = 1; attempt <= 4; ++attempt)
            {
                error.clear();
                raw.clear();
                if (device.writeInt(quint16(service.registerIndex), service.value, &error, &raw))
                {
                    restored = true;
                    break;
                }
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                if (attempt < 4)
                    sleepWithEvents(500);
            }
            if (!raw.isEmpty())
                transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
            if (!restored)
            {
                log(QStringLiteral("[%1] cannot restore %2 to int[%3]: %4")
                    .arg(identity.typeHex(), service.label)
                    .arg(service.registerIndex)
                    .arg(error));
                return false;
            }
            log(QStringLiteral("[%1] restored %2 to int[%3]")
                .arg(identity.typeHex(), service.label)
                .arg(service.registerIndex));
        }
        return true;
    }

    if (step.op == QStringLiteral("device.waitForApplication"))
    {
        DeviceIdentity expected = identity;
        expected.type = identity.applicationType;
        expected.version = identity.applicationVersion;
        expected.state = QStringLiteral("application");
        const int timeoutMs = step.arguments.value(QStringLiteral("timeoutMs"), 15000).toInt();
        const int pollIntervalMs = step.arguments.value(QStringLiteral("pollIntervalMs"), 1000).toInt();
        const int retryLoadAttempts = qMax(0,
            step.arguments.value(QStringLiteral("retryLoadAttempts"), 0).toInt());
        const int retryDelayMs = qMax(0,
            step.arguments.value(QStringLiteral("retryDelayMs"), 1000).toInt());

        for (int waitAttempt = 0; waitAttempt <= retryLoadAttempts; ++waitAttempt)
        {
            QString error;
            QString raw;
            if (device.waitForDeviceIdentity(expected, timeoutMs, pollIntervalMs,
                    &mContext.reappearedIdentity, &error, &raw))
            {
                if (!raw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

                const DeviceIdentity found = mContext.reappearedIdentity;
                DeviceIdentity updated = identity;
                updated.id = !identity.uuid.isEmpty() ? identity.uuid : found.id;
                updated.endpoint = found.endpoint;
                updated.modbusAddress = found.modbusAddress;
                updated.type = found.type;
                updated.version = found.version;
                updated.description = found.description;
                if (!found.serialNumber.trimmed().isEmpty())
                    updated.serialNumber = found.serialNumber;
                updated.state = QStringLiteral("application");
                device.updateIdentity(updated);
                mContext.applicationLoadingDisabled = false;
                return true;
            }
            if (!raw.isEmpty())
                transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
            log(QStringLiteral("[%1] waitForApplication attempt %2 failed: %3")
                .arg(identity.typeHex()).arg(waitAttempt + 1).arg(error));

            if (waitAttempt >= retryLoadAttempts)
                return false;

            if (retryDelayMs > 0)
                sleepWithEvents(retryDelayMs);
            log(QStringLiteral("[%1] application is still unavailable; retry load application (%2/%3)")
                .arg(identity.typeHex()).arg(waitAttempt + 2).arg(retryLoadAttempts + 1));

            QString loadError;
            QString loadRaw;
            if (!device.loadApplicationNoReply(&loadError, &loadRaw))
            {
                if (!loadRaw.isEmpty())
                    transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), loadRaw));
                log(QStringLiteral("[%1] retry load application failed: %2")
                    .arg(identity.typeHex(), loadError));
                return false;
            }
            if (!loadRaw.isEmpty())
                transportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), loadRaw));
        }
        return false;
    }

    if (step.op == QStringLiteral("firmware.verifyInstalledVersion"))
    {
        const DeviceIdentity& found = mContext.reappearedIdentity;
        const QRegularExpression targetMatcher(mContext.targetFirmware.descriptionRegex);
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
        updated.currentFirmwareId = mContext.targetFirmwareId;
        updated.firmwareDetectionError.clear();
        updated.status = QStringLiteral("прошивка %1").arg(mContext.targetFirmwareId);
        device.updateIdentity(updated);
        log(QStringLiteral("[%1] application %2 is running")
            .arg(found.typeHex(), mContext.targetFirmwareId));
        return true;
    }

    if (step.op == QStringLiteral("firmware.complete"))
    {
        reportProgressStage(identity, QStringLiteral("done"));
        return true;
    }


    if (step.op == QStringLiteral("flash.prepare"))
    {
        mContext.flashPlan = buildFlashPlan(identity, mAction);
        mContext.flashPlan.artifact = artifactFromVariant(mParameters.value(QStringLiteral("artifact")).toMap(), mContext.flashPlan.artifact);
        if (!mContext.flashPlan.artifact.flashStrategy.isEmpty())
            mContext.flashPlan.strategyId = mContext.flashPlan.artifact.flashStrategy;
        mContext.flashPlan.strategyParameters = mContext.flashPlan.artifact.flashParameters;
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

    // A direct identity refresh may not contain a serial number (for example,
    // when UDP discovery is unavailable and the device is reached over TCP).
    // Keep the in-memory identity in sync as soon as the write succeeds so the
    // refresh can retain the actual requested number instead of losing it.
    if (step.op == QStringLiteral("device.writeSerialNumber"))
    {
        DeviceIdentity updated = device.identity();
        updated.serialNumber = QString::number(arguments.value(QStringLiteral("value")).toInt());
        device.updateIdentity(updated);
    }
    else if (step.op == QStringLiteral("device.reset")
        && (mAction.id == QStringLiteral("device.productionDate.update")
            || mAction.id == QStringLiteral("device.serialNumber.update")))
    {
        mContext.applicationLoadingDisabled = true;
    }
    else if (step.op == QStringLiteral("device.disableLoadApplication"))
    {
        mContext.applicationLoadingDisabled = true;
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
