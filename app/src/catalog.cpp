#include "catalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

static quint16 parseType(const QString& raw)
{
    bool ok = false;
    const QString trimmed = raw.trimmed();
    const int base = trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10;
    const uint value = trimmed.toUInt(&ok, base);
    return ok ? quint16(value & 0xFFFF) : 0;
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

    const bool matchesBootloaderIdentity = entry->bootloaderType != 0
        && device.type == entry->bootloaderType
        && device.version == entry->bootloaderVersion;
    return (matchesBootloaderIdentity && hasBootMarker)
        ? QStringLiteral("bootloader")
        : QStringLiteral("application");
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

    mEntriesByTypeVersion.clear();
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
            artifact.target = firmware.value(QStringLiteral("target")).toString();
            artifact.version = firmware.value(QStringLiteral("version")).toString();
            artifact.relativePath = firmware.value(QStringLiteral("relativePath")).toString();
            artifact.sha256 = firmware.value(QStringLiteral("sha256")).toString();
            if (!artifact.target.isEmpty() && !artifact.relativePath.isEmpty())
                entry.firmwareArtifacts.append(artifact);
        }

        if (entry.type != 0)
            mEntriesByTypeVersion.insert(catalogKey(entry.type, entry.version), entry);
        if (entry.bootloaderType != 0)
            mEntriesByTypeVersion.insert(catalogKey(entry.bootloaderType, entry.bootloaderVersion), entry);
    }
    return true;
}

const CatalogEntry* CatalogService::entryForDevice(const DeviceIdentity& device) const
{
    const auto direct = mEntriesByTypeVersion.constFind(catalogKey(device.type, device.version));
    if (direct != mEntriesByTypeVersion.constEnd())
        return &direct.value();

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
    const QRegularExpression pattern = descriptionPatternToRegex(entry->expectedDescriptionPattern);
    const bool matchesPattern = pattern.isValid() && pattern.match(device.description.trimmed()).hasMatch();
    device.descriptionMismatch = !matchesPattern;
    device.status = device.descriptionMismatch ? QStringLiteral("можно обновить") : QStringLiteral("актуально");
    return device;
}
