#include "catalog.h"
#include "firmware_flash_strategy.h"

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
    return artifact;
}

static bool parseFirmwareCatalog(const QJsonObject& object, FirmwareCatalog* catalog, QString* error)
{
    if (!catalog)
        return false;

    catalog->deviceId = object.value(QStringLiteral("deviceId")).toString().trimmed();

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

    QHash<quint32, CatalogEntry> loadedEntries;
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
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.deviceClass = object.value(QStringLiteral("deviceClass")).toString(QStringLiteral("DeviceBase"));

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
                && entry.serialNumberRegister < 0))
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

        const auto addIdentity = [&](quint16 type, quint16 version) -> bool {
            if (type == 0)
                return true;
            const quint32 key = catalogKey(type, version);
            if (loadedEntries.contains(key))
            {
                if (error)
                    *error = QStringLiteral("Duplicate device identity %1/%2")
                        .arg(type, 4, 16, QLatin1Char('0'))
                        .arg(version, 4, 16, QLatin1Char('0'));
                return false;
            }
            loadedEntries.insert(key, entry);
            return true;
        };
        if (!addIdentity(entry.type, entry.version)
            || !addIdentity(entry.bootloaderType, entry.bootloaderVersion))
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

    mEntriesByTypeVersion = std::move(loadedEntries);
    mEntries = std::move(catalogEntries);
    mFirmwareByDeviceId = std::move(firmwareByDeviceId);
    return true;
}

const CatalogEntry* CatalogService::entryForDevice(const DeviceIdentity& device) const
{
    const auto direct = mEntriesByTypeVersion.constFind(catalogKey(device.type, device.version));
    if (direct != mEntriesByTypeVersion.constEnd())
        return &direct.value();

    const CatalogEntry* keywordMatch = nullptr;
    for (const CatalogEntry& entry : mEntries)
    {
        if (!descriptionContainsKeywords(device.description, entry.descriptionKeywords))
            continue;
        if (keywordMatch)
            return nullptr;
        keywordMatch = &entry;
    }
    return keywordMatch;
}

DeviceIdentity CatalogService::enrich(DeviceIdentity device) const
{
    const CatalogEntry* entry = entryForDevice(device);
    device.state = descriptionContainsBoot(device.description)
        ? QStringLiteral("bootloader")
        : QStringLiteral("application");
    device.currentFirmwareId.clear();
    device.firmwareDetectionError.clear();
    device.firmwareArtifacts.clear();
    device.firmwareVersions.clear();
    device.firmwareTransitions.clear();

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
    device.descriptionKeywords = entry->descriptionKeywords;
    device.deviceClass = entry->deviceClass;
    device.capabilities = entry->capabilities;
    device.applicationType = entry->type;
    device.applicationVersion = entry->version;
    device.bootloaderType = entry->bootloaderType;
    device.bootloaderVersion = entry->bootloaderVersion;
    device.productionDateRegister = entry->productionDateRegister;
    device.serialNumberRegister = entry->serialNumberRegister;

    const auto firmwareIt = mFirmwareByDeviceId.constFind(entry->id);
    if (firmwareIt == mFirmwareByDeviceId.constEnd())
    {
        device.status = QStringLiteral("опознано");
        return device;
    }

    device.firmwareArtifacts = firmwareIt->firmwareArtifacts;
    device.firmwareVersions = firmwareIt->firmwareVersions;
    device.firmwareTransitions = firmwareIt->firmwareTransitions;

    QStringList matchedFirmwareIds;
    for (const FirmwareVersionSpec& firmware : device.firmwareVersions)
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
    else if (matchedFirmwareIds.isEmpty() && !device.firmwareVersions.isEmpty())
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
    return device;
}
