#include "catalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

static quint16 parseType(const QString& raw)
{
    bool ok = false;
    const QString trimmed = raw.trimmed();
    const int base = trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10;
    const uint value = trimmed.toUInt(&ok, base);
    return ok ? quint16(value & 0xFFFF) : 0;
}

static quint32 parseAddress(const QJsonValue& value)
{
    if (value.isDouble())
        return quint32(value.toDouble());

    bool ok = false;
    const QString raw = value.toString().trimmed();
    const int base = raw.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10;
    const quint32 address = raw.toUInt(&ok, base);
    return ok ? address : 0;
}

static quint32 catalogKey(quint16 type, quint16 version)
{
    return (quint32(type) << 16) | quint32(version);
}

static QRegularExpression descriptionPatternToRegex(const QString& pattern)
{
    QString regex = QRegularExpression::escape(pattern.trimmed());
    regex.replace(QStringLiteral("\\{boot\\}"), QStringLiteral("(?: \\(Boot\\))?"));
    regex.replace(QStringLiteral("\\{year\\}"), QStringLiteral("\\d{4}"));
    regex.replace(QStringLiteral("\\{quarter\\}"), QStringLiteral("[IVX]+"));
    regex.replace(QStringLiteral("\\{serial\\}"), QStringLiteral("\\d+"));
    regex.replace(QStringLiteral("\\{sw\\}"), QStringLiteral(".+"));
    return QRegularExpression(QStringLiteral("^%1$").arg(regex));
}

static bool descriptionContainsBoot(const QString& description)
{
    static const QRegularExpression bootPattern(QStringLiteral("\\(\\s*Boot\\s*\\)"), QRegularExpression::CaseInsensitiveOption);
    return bootPattern.match(description).hasMatch();
}

static QString deviceStateFor(const DeviceIdentity& device, const CatalogEntry* entry)
{
    const bool hasBootMarker = descriptionContainsBoot(device.description);
    if (!entry)
        return hasBootMarker ? QStringLiteral("bootloader") : QStringLiteral("application");
    return hasBootMarker ? QStringLiteral("bootloader") : QStringLiteral("application");
}

bool CatalogService::load(const QString& fileName, QString* error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("Cannot open catalog %1: %2").arg(fileName, file.errorString());
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        if (error)
            *error = QStringLiteral("Catalog %1 is not a JSON object").arg(fileName);
        return false;
    }

    QHash<quint32, CatalogEntry> loadedEntries;
    QVector<CatalogEntry> catalogEntries;
    const QJsonArray devices = doc.object().value(QStringLiteral("deviceTypes")).toArray();
    for (const QJsonValue& value : devices)
    {
        const QJsonObject obj = value.toObject();
        CatalogEntry entry;
        entry.id = obj.value(QStringLiteral("id")).toString();
        entry.protocol = obj.value(QStringLiteral("protocol")).toString();
        entry.type = parseType(obj.value(QStringLiteral("type")).toString());
        entry.version = parseType(obj.value(QStringLiteral("version")).toString());
        entry.bootloaderType = parseType(obj.value(QStringLiteral("bootloaderType")).toString());
        entry.bootloaderVersion = parseType(obj.value(QStringLiteral("bootloaderVersion")).toString());
        entry.name = obj.value(QStringLiteral("name")).toString();
        entry.expectedDescription = obj.value(QStringLiteral("expectedDescription")).toString(entry.name);
        entry.expectedDescriptionPattern = obj.value(QStringLiteral("expectedDescriptionPattern")).toString(entry.expectedDescription);
        entry.deviceClass = obj.value(QStringLiteral("deviceClass")).toString(QStringLiteral("DeviceBase"));

        const QJsonArray capabilities = obj.value(QStringLiteral("capabilities")).toArray();
        for (const QJsonValue& cap : capabilities)
            entry.capabilities.append(cap.toString());

        const QJsonObject workflows = obj.value(QStringLiteral("flashWorkflows")).toObject();
        for (auto it = workflows.begin(); it != workflows.end(); ++it)
            entry.flashWorkflows.insert(it.key(), it.value().toString());

        const QJsonArray firmwareFiles = obj.value(QStringLiteral("firmwareFiles")).toArray();
        for (const QJsonValue& firmwareValue : firmwareFiles)
        {
            const QJsonObject firmware = firmwareValue.toObject();
            FirmwareArtifact artifact;
            artifact.firmwareId = firmware.value(QStringLiteral("id")).toString();
            artifact.target = firmware.value(QStringLiteral("target")).toString();
            artifact.title = firmware.value(QStringLiteral("title")).toString();
            artifact.version = firmware.value(QStringLiteral("version")).toString();
            artifact.relativePath = firmware.value(QStringLiteral("relativePath")).toString();
            artifact.sha256 = firmware.value(QStringLiteral("sha256")).toString();
            artifact.format = firmware.value(QStringLiteral("format")).toString();
            artifact.addressBase = parseAddress(firmware.value(QStringLiteral("addressBase")));
            artifact.isDefault = firmware.value(QStringLiteral("default")).toBool(false);
            artifact.flashNum = firmware.value(QStringLiteral("flashNum")).toInt(0);
            artifact.offset = firmware.value(QStringLiteral("offset")).toInt(0);
            artifact.pageSize = firmware.value(QStringLiteral("pageSize")).toInt(0);
            artifact.pagesCount = firmware.value(QStringLiteral("pagesCount")).toInt(0);
            if (!artifact.target.isEmpty() && !artifact.relativePath.isEmpty())
                entry.firmwareArtifacts.append(artifact);
        }

        const QJsonObject graph = obj.value(QStringLiteral("firmwareGraph")).toObject();
        QSet<QString> firmwareIds;
        const QJsonArray versions = graph.value(QStringLiteral("versions")).toArray();
        for (const QJsonValue& versionValue : versions)
        {
            const QJsonObject versionObject = versionValue.toObject();
            FirmwareVersionSpec firmwareVersion;
            firmwareVersion.id = versionObject.value(QStringLiteral("id")).toString().trimmed();
            firmwareVersion.title = versionObject.value(QStringLiteral("title")).toString(firmwareVersion.id);
            firmwareVersion.version = versionObject.value(QStringLiteral("version")).toString(firmwareVersion.id);
            firmwareVersion.descriptionRegex = versionObject.value(QStringLiteral("descriptionRegex")).toString();
            firmwareVersion.detectFromDescription = versionObject.value(QStringLiteral("detectFromDescription")).toBool(true);

            const QJsonObject artifactObject = versionObject.value(QStringLiteral("artifact")).toObject();
            firmwareVersion.artifact.firmwareId = firmwareVersion.id;
            firmwareVersion.artifact.target = artifactObject.value(QStringLiteral("target")).toString(QStringLiteral("application"));
            firmwareVersion.artifact.title = firmwareVersion.title;
            firmwareVersion.artifact.version = firmwareVersion.version;
            firmwareVersion.artifact.relativePath = artifactObject.value(QStringLiteral("relativePath")).toString();
            firmwareVersion.artifact.sha256 = artifactObject.value(QStringLiteral("sha256")).toString();
            firmwareVersion.artifact.format = artifactObject.value(QStringLiteral("format")).toString();
            firmwareVersion.artifact.addressBase = parseAddress(artifactObject.value(QStringLiteral("addressBase")));
            firmwareVersion.artifact.isDefault = artifactObject.value(QStringLiteral("default")).toBool(false);
            firmwareVersion.artifact.flashNum = artifactObject.value(QStringLiteral("flashNum")).toInt(0);
            firmwareVersion.artifact.offset = artifactObject.value(QStringLiteral("offset")).toInt(0);
            firmwareVersion.artifact.pageSize = artifactObject.value(QStringLiteral("pageSize")).toInt(0);
            firmwareVersion.artifact.pagesCount = artifactObject.value(QStringLiteral("pagesCount")).toInt(0);

            const QRegularExpression matcher(firmwareVersion.descriptionRegex);
            if (firmwareVersion.id.isEmpty() || firmwareIds.contains(firmwareVersion.id))
            {
                if (error)
                    *error = QStringLiteral("Device %1 has an empty or duplicate firmware id '%2'").arg(entry.id, firmwareVersion.id);
                return false;
            }
            if (firmwareVersion.descriptionRegex.isEmpty() || !matcher.isValid())
            {
                if (error)
                    *error = QStringLiteral("Firmware %1 has invalid descriptionRegex: %2")
                        .arg(firmwareVersion.id, matcher.errorString());
                return false;
            }
            firmwareIds.insert(firmwareVersion.id);
            entry.firmwareVersions.append(firmwareVersion);
            if (!firmwareVersion.artifact.relativePath.isEmpty())
                entry.firmwareArtifacts.append(firmwareVersion.artifact);
        }

        const QSet<QString> supportedPipelineSteps = {
            QStringLiteral("reset"),
            QStringLiteral("loadBootloader"),
            QStringLiteral("flash"),
            QStringLiteral("verify"),
            QStringLiteral("restart"),
            QStringLiteral("waitForApplication")
        };
        const QJsonArray transitions = graph.value(QStringLiteral("transitions")).toArray();
        QSet<QString> transitionKeys;
        for (const QJsonValue& transitionValue : transitions)
        {
            const QJsonObject transitionObject = transitionValue.toObject();
            FirmwareTransitionSpec transition;
            transition.from = transitionObject.value(QStringLiteral("from")).toString().trimmed();
            transition.to = transitionObject.value(QStringLiteral("to")).toString().trimmed();
            transition.enabled = transitionObject.value(QStringLiteral("enabled")).toBool(false);
            transition.reason = transitionObject.value(QStringLiteral("reason")).toString();

            const QString transitionKey = transition.from + QLatin1Char('\n') + transition.to;
            if (!firmwareIds.contains(transition.from) || !firmwareIds.contains(transition.to)
                || transitionKeys.contains(transitionKey))
            {
                if (error)
                    *error = QStringLiteral("Device %1 has invalid or duplicate firmware transition %2 -> %3")
                        .arg(entry.id, transition.from, transition.to);
                return false;
            }
            transitionKeys.insert(transitionKey);

            if (transition.enabled)
            {
                bool targetHasArtifact = false;
                for (const FirmwareVersionSpec& firmwareVersion : entry.firmwareVersions)
                {
                    if (firmwareVersion.id == transition.to)
                    {
                        targetHasArtifact = !firmwareVersion.artifact.relativePath.isEmpty();
                        break;
                    }
                }
                if (!targetHasArtifact)
                {
                    if (error)
                        *error = QStringLiteral("Enabled transition %1 -> %2 has no target artifact")
                            .arg(transition.from, transition.to);
                    return false;
                }
            }

            bool sawFlash = false;
            const QJsonArray pipeline = transitionObject.value(QStringLiteral("pipeline")).toArray();
            for (const QJsonValue& stepValue : pipeline)
            {
                const QJsonObject stepObject = stepValue.toObject();
                FirmwarePipelineStep step;
                step.type = stepObject.value(QStringLiteral("type")).toString();
                step.skipIfState = stepObject.value(QStringLiteral("skipIfState")).toString();
                step.arguments = stepObject.toVariantMap();
                step.arguments.remove(QStringLiteral("type"));
                step.arguments.remove(QStringLiteral("skipIfState"));
                if (!supportedPipelineSteps.contains(step.type))
                {
                    if (error)
                        *error = QStringLiteral("Transition %1 -> %2 has unsupported pipeline step '%3'")
                            .arg(transition.from, transition.to, step.type);
                    return false;
                }
                if (step.type == QStringLiteral("flash"))
                    sawFlash = true;
                if (step.type == QStringLiteral("verify") && !sawFlash)
                {
                    if (error)
                        *error = QStringLiteral("Transition %1 -> %2 verifies before flash")
                            .arg(transition.from, transition.to);
                    return false;
                }
                transition.pipeline.append(step);
            }
            if (transition.pipeline.isEmpty() || !sawFlash)
            {
                if (error)
                    *error = QStringLiteral("Transition %1 -> %2 must contain a flash step")
                        .arg(transition.from, transition.to);
                return false;
            }
            entry.firmwareTransitions.append(transition);
        }

        if (entry.type != 0)
            loadedEntries.insert(catalogKey(entry.type, entry.version), entry);
        if (entry.bootloaderType != 0)
            loadedEntries.insert(catalogKey(entry.bootloaderType, entry.bootloaderVersion), entry);
        catalogEntries.append(entry);
    }
    mEntriesByTypeVersion = std::move(loadedEntries);
    mEntries = std::move(catalogEntries);
    return true;
}

const CatalogEntry* CatalogService::entryForDevice(const DeviceIdentity& device) const
{
    const auto direct = mEntriesByTypeVersion.constFind(catalogKey(device.type, device.version));
    if (!descriptionContainsBoot(device.description))
        return direct != mEntriesByTypeVersion.constEnd() ? &direct.value() : nullptr;

    if (direct != mEntriesByTypeVersion.constEnd())
    {
        const QRegularExpression directPattern = descriptionPatternToRegex(direct->expectedDescriptionPattern);
        if (directPattern.isValid() && directPattern.match(device.description.trimmed()).hasMatch())
            return &direct.value();
    }

    for (const CatalogEntry& entry : mEntries)
    {
        const QRegularExpression pattern = descriptionPatternToRegex(entry.expectedDescriptionPattern);
        if (pattern.isValid() && pattern.match(device.description.trimmed()).hasMatch())
            return &entry;
    }

    return nullptr;
}

DeviceIdentity CatalogService::enrich(DeviceIdentity device) const
{
    const CatalogEntry* entry = entryForDevice(device);
    device.state = deviceStateFor(device, entry);
    if (!entry)
    {
        device.known = false;
        device.name = QStringLiteral("Unknown device");
        device.status = QStringLiteral("неизвестно");
        return device;
    }

    device.known = true;
    device.catalogId = entry->id;
    device.name = entry->name;
    device.expectedDescription = entry->expectedDescription;
    device.expectedDescriptionPattern = entry->expectedDescriptionPattern;
    device.deviceClass = entry->deviceClass;
    device.capabilities = entry->capabilities;
    device.flashWorkflows = entry->flashWorkflows;
    device.firmwareArtifacts = entry->firmwareArtifacts;
    device.firmwareVersions = entry->firmwareVersions;
    device.firmwareTransitions = entry->firmwareTransitions;
    device.applicationType = entry->type;
    device.applicationVersion = entry->version;
    device.bootloaderType = entry->bootloaderType;
    device.bootloaderVersion = entry->bootloaderVersion;
    const QRegularExpression pattern = descriptionPatternToRegex(entry->expectedDescriptionPattern);
    const bool matchesPattern = pattern.isValid() && pattern.match(device.description.trimmed()).hasMatch();
    device.descriptionMismatch = !matchesPattern;
    QStringList matchedFirmwareIds;
    for (const FirmwareVersionSpec& firmware : entry->firmwareVersions)
    {
        if (!firmware.detectFromDescription)
            continue;
        const QRegularExpression firmwareMatcher(firmware.descriptionRegex);
        if (firmwareMatcher.isValid() && firmwareMatcher.match(device.description.trimmed()).hasMatch())
            matchedFirmwareIds.append(firmware.id);
    }
    if (matchedFirmwareIds.size() == 1)
    {
        device.currentFirmwareId = matchedFirmwareIds.first();
        device.status = QStringLiteral("прошивка %1").arg(device.currentFirmwareId);
    }
    else if (matchedFirmwareIds.isEmpty() && !entry->firmwareVersions.isEmpty())
    {
        device.firmwareDetectionError = QStringLiteral("версия прошивки не определена");
        device.status = device.firmwareDetectionError;
    }
    else if (matchedFirmwareIds.size() > 1)
    {
        device.firmwareDetectionError = QStringLiteral("неоднозначная версия прошивки: %1")
            .arg(matchedFirmwareIds.join(QStringLiteral(", ")));
        device.status = device.firmwareDetectionError;
    }
    else
    {
        device.status = device.descriptionMismatch ? QStringLiteral("можно обновить") : QStringLiteral("актуально");
    }
    return device;
}
