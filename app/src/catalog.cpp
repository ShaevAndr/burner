#include "catalog.h"
#include "firmware_flash_strategy.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <utility>

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

static bool descriptionContainsBoot(const QString& description)
{
    static const QRegularExpression bootPattern(
        QStringLiteral("\\(\\s*Boot\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    return bootPattern.match(description).hasMatch();
}

static bool descriptionContainsKeywords(const QString& description, const QStringList& keywords)
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

static FirmwareArtifact artifactFromObject(const QJsonObject& object,
    const QString& firmwareId = QString(),
    const QString& defaultTitle = QString(),
    const QString& defaultVersion = QString(),
    const QString& defaultTarget = QString())
{
    FirmwareArtifact artifact;
    artifact.firmwareId = firmwareId.isEmpty()
        ? object.value(QStringLiteral("id")).toString()
        : firmwareId;
    artifact.target = object.value(QStringLiteral("target")).toString(defaultTarget);
    artifact.title = object.value(QStringLiteral("title")).toString(defaultTitle);
    artifact.version = object.value(QStringLiteral("version")).toString(defaultVersion);
    artifact.relativePath = object.value(QStringLiteral("relativePath")).toString();
    artifact.sha256 = object.value(QStringLiteral("sha256")).toString();
    artifact.format = object.value(QStringLiteral("format")).toString();
    artifact.addressBase = parseAddress(object.value(QStringLiteral("addressBase")));
    artifact.isDefault = object.value(QStringLiteral("default")).toBool(false);
    artifact.flashNum = object.value(QStringLiteral("flashNum")).toInt(0);
    artifact.offset = object.value(QStringLiteral("offset")).toInt(0);
    artifact.pageSize = object.value(QStringLiteral("pageSize")).toInt(0);
    artifact.pagesCount = object.value(QStringLiteral("pagesCount")).toInt(0);
    artifact.flashStrategy = object.value(QStringLiteral("flashStrategy")).toString().trimmed();
    artifact.flashParameters = object.value(QStringLiteral("flashParameters")).toObject().toVariantMap();
    const QJsonArray allowedFromFirmwareIds = object.value(
        QStringLiteral("allowedFromFirmwareIds")).toArray();
    for (const QJsonValue& firmwareId : allowedFromFirmwareIds)
    {
        const QString id = firmwareId.toString().trimmed();
        if (!id.isEmpty())
            artifact.allowedFromFirmwareIds.append(id);
    }
    return artifact;
}

static bool parseFirmwareCatalog(const QJsonObject& object, FirmwareCatalog* catalog, QString* error)
{
    if (!catalog)
        return false;

    catalog->deviceId = object.value(QStringLiteral("deviceId")).toString().trimmed();
    catalog->allowUnknownCurrentFirmware = object.value(
        QStringLiteral("allowUnknownCurrentFirmware")).toBool(true);

    const QJsonArray artifacts = object.value(QStringLiteral("artifacts")).toArray();
    for (const QJsonValue& artifactValue : artifacts)
    {
        const FirmwareArtifact artifact = artifactFromObject(artifactValue.toObject());
        if (!artifact.target.isEmpty() && !artifact.relativePath.isEmpty())
            catalog->firmwareArtifacts.append(artifact);
    }

    QSet<QString> firmwareIds;
    const QJsonArray versions = object.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue& versionValue : versions)
    {
        const QJsonObject versionObject = versionValue.toObject();
        FirmwareVersionSpec firmwareVersion;
        firmwareVersion.id = versionObject.value(QStringLiteral("id")).toString().trimmed();
        firmwareVersion.title = versionObject.value(QStringLiteral("title")).toString(firmwareVersion.id);
        firmwareVersion.version = versionObject.value(QStringLiteral("version")).toString(firmwareVersion.id);
        firmwareVersion.descriptionRegex = versionObject.value(QStringLiteral("descriptionRegex")).toString();
        firmwareVersion.detectFromDescription = versionObject.value(QStringLiteral("detectFromDescription")).toBool(true);
        firmwareVersion.artifact = artifactFromObject(
            versionObject.value(QStringLiteral("artifact")).toObject(),
            firmwareVersion.id,
            firmwareVersion.title,
            firmwareVersion.version,
            QStringLiteral("application"));
        const QJsonObject installation = versionObject.value(QStringLiteral("installation")).toObject();
        firmwareVersion.installation.workflow = installation.value(QStringLiteral("workflow")).toString().trimmed();
        firmwareVersion.installation.strategy = installation.value(QStringLiteral("strategy")).toString().trimmed();
        firmwareVersion.installation.parameters = installation.value(QStringLiteral("parameters")).toObject().toVariantMap();

        const QRegularExpression matcher(firmwareVersion.descriptionRegex);
        if (firmwareVersion.id.isEmpty() || firmwareIds.contains(firmwareVersion.id))
        {
            if (error)
                *error = QStringLiteral("Firmware catalog %1 has an empty or duplicate firmware id '%2'")
                    .arg(catalog->deviceId, firmwareVersion.id);
            return false;
        }
        if (firmwareVersion.descriptionRegex.isEmpty() || !matcher.isValid())
        {
            if (error)
                *error = QStringLiteral("Firmware %1 has invalid descriptionRegex: %2")
                    .arg(firmwareVersion.id, matcher.errorString());
            return false;
        }
        if (!firmwareVersion.artifact.relativePath.isEmpty()
            && (firmwareVersion.installation.workflow.isEmpty()
                || firmwareVersion.installation.strategy.isEmpty()))
        {
            if (error)
                *error = QStringLiteral("Installable firmware %1 must define installation.workflow and installation.strategy")
                    .arg(firmwareVersion.id);
            return false;
        }
        if (!firmwareVersion.installation.strategy.isEmpty()
            && !FirmwareFlashStrategyRegistry::find(firmwareVersion.installation.strategy))
        {
            if (error)
                *error = QStringLiteral("Firmware %1 references unknown flash strategy '%2'")
                    .arg(firmwareVersion.id, firmwareVersion.installation.strategy);
            return false;
        }

        firmwareIds.insert(firmwareVersion.id);
        catalog->firmwareVersions.append(firmwareVersion);
        if (!firmwareVersion.artifact.relativePath.isEmpty())
            catalog->firmwareArtifacts.append(firmwareVersion.artifact);
    }

    QSet<QString> transitionKeys;
    const QJsonArray transitions = object.value(QStringLiteral("transitions")).toArray();
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
                *error = QStringLiteral("Firmware catalog %1 has invalid or duplicate transition %2 -> %3")
                    .arg(catalog->deviceId, transition.from, transition.to);
            return false;
        }
        transitionKeys.insert(transitionKey);

        if (transition.enabled)
        {
            bool targetHasArtifact = false;
            for (const FirmwareVersionSpec& firmwareVersion : catalog->firmwareVersions)
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

        catalog->firmwareTransitions.append(transition);
    }
    return true;
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

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        if (error)
            *error = QStringLiteral("Catalog %1 is not a JSON object").arg(fileName);
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 4)
    {
        if (error)
            *error = QStringLiteral("Catalog %1 must use schemaVersion 4").arg(fileName);
        return false;
    }

    QHash<quint16, CatalogEntry> loadedEntries;
    QVector<CatalogEntry> catalogEntries;
    QSet<QString> deviceIds;
    const QJsonArray devices = root.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& value : devices)
    {
        const QJsonObject object = value.toObject();
        CatalogEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString().trimmed();
        entry.protocol = object.value(QStringLiteral("protocol")).toString();
        entry.type = parseType(object.value(QStringLiteral("type")).toString());
        entry.version = parseType(object.value(QStringLiteral("version")).toString());
        entry.bootloaderType = parseType(object.value(QStringLiteral("bootloaderType")).toString());
        entry.bootloaderVersion = parseType(object.value(QStringLiteral("bootloaderVersion")).toString());
        const QJsonObject operationParameters = object.value(QStringLiteral("operationParameters")).toObject();
        entry.productionDateRegister = operationParameters.value(QStringLiteral("productionDateRegister")).toInt(-1);
        entry.serialNumberRegister = operationParameters.value(QStringLiteral("serialNumberRegister")).toInt(-1);
        entry.applicationLoadRegister = operationParameters.value(QStringLiteral("applicationLoadRegister")).toInt(0);
        entry.name = object.value(QStringLiteral("name")).toString();

        const QJsonArray keywords = object.value(QStringLiteral("descriptionKeywords")).toArray();
        for (const QJsonValue& keyword : keywords)
        {
            const QString text = keyword.toString().trimmed();
            if (!text.isEmpty())
                entry.descriptionKeywords.append(text);
        }
        const QJsonArray capabilities = object.value(QStringLiteral("capabilities")).toArray();
        for (const QJsonValue& capability : capabilities)
            entry.capabilities.append(capability.toString());

        if ((entry.capabilities.contains(QStringLiteral("device.productionDate.update"))
                && entry.productionDateRegister < 0)
            || (entry.capabilities.contains(QStringLiteral("device.serialNumber.update"))
                && entry.serialNumberRegister < 0)
            || entry.applicationLoadRegister < 0 || entry.applicationLoadRegister > 65535)
        {
            if (error)
                *error = QStringLiteral("Device %1 enables date/serial update without configured registers")
                    .arg(entry.id);
            return false;
        }
        if (entry.id.isEmpty() || deviceIds.contains(entry.id) || entry.descriptionKeywords.isEmpty())
        {
            if (error)
                *error = QStringLiteral("Device has an empty/duplicate id or no descriptionKeywords: %1").arg(entry.id);
            return false;
        }
        deviceIds.insert(entry.id);

        const auto addIdentity = [&](quint16 type) -> bool {
            if (type == 0)
                return true;
            if (loadedEntries.contains(type))
            {
                if (error)
                    *error = QStringLiteral("Duplicate device type %1")
                        .arg(type, 4, 16, QLatin1Char('0'));
                return false;
            }
            loadedEntries.insert(type, entry);
            return true;
        };
        if (!addIdentity(entry.type)
            || !addIdentity(entry.bootloaderType))
            return false;
        catalogEntries.append(entry);
    }

    QHash<QString, FirmwareCatalog> firmwareByDeviceId;
    const QJsonArray firmwareCatalogs = root.value(QStringLiteral("firmwareCatalogs")).toArray();
    for (const QJsonValue& value : firmwareCatalogs)
    {
        FirmwareCatalog firmwareCatalog;
        if (!parseFirmwareCatalog(value.toObject(), &firmwareCatalog, error))
            return false;
        if (!deviceIds.contains(firmwareCatalog.deviceId)
            || firmwareByDeviceId.contains(firmwareCatalog.deviceId))
        {
            if (error)
                *error = QStringLiteral("Firmware catalog references an unknown or duplicate deviceId: %1")
                    .arg(firmwareCatalog.deviceId);
            return false;
        }
        firmwareByDeviceId.insert(firmwareCatalog.deviceId, firmwareCatalog);
    }

    if (catalogEntries.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Catalog contains no devices");
        return false;
    }

    QHash<quint16, std::shared_ptr<const DeviceProfile>> profilesByType;
    QVector<std::shared_ptr<const DeviceProfile>> profiles;
    for (const CatalogEntry& entry : catalogEntries)
    {
        auto profile = std::make_shared<DeviceProfile>();
        profile->id = entry.id;
        profile->protocol = entry.protocol;
        profile->applicationType = entry.type;
        profile->applicationVersion = entry.version;
        profile->bootloaderType = entry.bootloaderType;
        profile->bootloaderVersion = entry.bootloaderVersion;
        profile->name = entry.name;
        profile->descriptionKeywords = entry.descriptionKeywords;
        profile->capabilities = entry.capabilities;
        profile->applicationLoadRegister = entry.applicationLoadRegister;
        profile->productionDateRegister = entry.productionDateRegister;
        profile->serialNumberRegister = entry.serialNumberRegister;
        const auto firmware = firmwareByDeviceId.constFind(entry.id);
        if (firmware != firmwareByDeviceId.constEnd())
        {
            profile->firmwareArtifacts = firmware->firmwareArtifacts;
            profile->firmwareVersions = firmware->firmwareVersions;
            profile->firmwareTransitions = firmware->firmwareTransitions;
            profile->allowUnknownCurrentFirmware = firmware->allowUnknownCurrentFirmware;
        }
        for (const FirmwareArtifact& artifact : profile->firmwareArtifacts)
        {
            if (artifact.target == QStringLiteral("bootloader")
                && !artifact.relativePath.isEmpty()
                && !profile->capabilities.contains(QStringLiteral("flash.bootloader.write")))
            {
                profile->capabilities.append(QStringLiteral("flash.bootloader.write"));
            }
        }
        const std::shared_ptr<const DeviceProfile> immutable = profile;
        if (entry.type)
            profilesByType.insert(entry.type, immutable);
        if (entry.bootloaderType)
            profilesByType.insert(entry.bootloaderType, immutable);
        profiles.append(immutable);
    }
    mProfilesByType = std::move(profilesByType);
    mProfiles = std::move(profiles);
    return true;
}

std::shared_ptr<const DeviceProfile> CatalogService::profileForDevice(const DeviceIdentity& device) const
{
    const auto direct = mProfilesByType.constFind(device.type);
    if (direct != mProfilesByType.constEnd())
        return direct.value();

    std::shared_ptr<const DeviceProfile> keywordMatch;
    for (const auto& profile : mProfiles)
    {
        if (!descriptionContainsKeywords(device.description, profile->descriptionKeywords))
            continue;
        if (keywordMatch)
            return {};
        keywordMatch = profile;
    }
    return keywordMatch;
}

CatalogMatch CatalogService::match(DeviceIdentity device) const
{
    const std::shared_ptr<const DeviceProfile> profile = profileForDevice(device);
    device.state = descriptionContainsBoot(device.description)
        ? QStringLiteral("bootloader")
        : QStringLiteral("application");
    device.currentFirmwareId.clear();
    device.firmwareDetectionError.clear();
    device.catalogId.clear();
    device.name.clear();
    device.descriptionKeywords.clear();
    device.capabilities.clear();
    device.applicationType = 0;
    device.applicationVersion = 0;
    device.bootloaderType = 0;
    device.bootloaderVersion = 0;
    device.productionDateRegister = -1;
    device.serialNumberRegister = -1;
    device.applicationLoadRegister = 0;
    device.firmwareArtifacts.clear();
    device.firmwareVersions.clear();
    device.firmwareTransitions.clear();
    device.allowUnknownCurrentFirmware = true;

    if (!profile)
    {
        device.known = false;
        device.status = QStringLiteral("неизвестно");
        return {std::move(device), {}};
    }

    device.known = true;

    QStringList matchedFirmwareIds;
    for (const FirmwareVersionSpec& firmware : profile->firmwareVersions)
    {
        if (!firmware.detectFromDescription)
            continue;
        const QRegularExpression matcher(firmware.descriptionRegex);
        if (matcher.isValid() && matcher.match(device.description.trimmed()).hasMatch())
            matchedFirmwareIds.append(firmware.id);
    }

    if (matchedFirmwareIds.size() == 1)
    {
        device.currentFirmwareId = matchedFirmwareIds.first();
        device.status = QStringLiteral("прошивка %1").arg(device.currentFirmwareId);
    }
    else if (matchedFirmwareIds.isEmpty() && !profile->firmwareVersions.isEmpty())
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
        device.status = QStringLiteral("опознано");
    }
    return {std::move(device), profile};
}

DeviceIdentity CatalogService::enrich(DeviceIdentity device) const
{
    CatalogMatch matched = match(std::move(device));
    if (!matched.profile)
    {
        matched.identity.name = QStringLiteral("Unknown device");
        return matched.identity;
    }

    // Compatibility for callers still working with identity-only catalog data.
    // Runtime sessions use match() and retain the immutable profile separately.
    DeviceIdentity& enriched = matched.identity;
    enriched.catalogId = matched.profile->id;
    enriched.name = matched.profile->name;
    enriched.descriptionKeywords = matched.profile->descriptionKeywords;
    enriched.capabilities = matched.profile->capabilities;
    enriched.applicationType = matched.profile->applicationType;
    enriched.applicationVersion = matched.profile->applicationVersion;
    enriched.bootloaderType = matched.profile->bootloaderType;
    enriched.bootloaderVersion = matched.profile->bootloaderVersion;
    enriched.productionDateRegister = matched.profile->productionDateRegister;
    enriched.serialNumberRegister = matched.profile->serialNumberRegister;
    enriched.applicationLoadRegister = matched.profile->applicationLoadRegister;
    enriched.firmwareArtifacts = matched.profile->firmwareArtifacts;
    enriched.firmwareVersions = matched.profile->firmwareVersions;
    enriched.firmwareTransitions = matched.profile->firmwareTransitions;
    enriched.allowUnknownCurrentFirmware = matched.profile->allowUnknownCurrentFirmware;
    return enriched;
}
