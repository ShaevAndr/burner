#include "catalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static quint16 parseType(const QString& raw)
{
    bool ok = false;
    const QString trimmed = raw.trimmed();
    const int base = trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10;
    const uint value = trimmed.toUInt(&ok, base);
    return ok ? quint16(value & 0xFFFF) : 0;
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

    mEntriesByType.clear();
    const QJsonArray devices = doc.object().value(QStringLiteral("deviceTypes")).toArray();
    for (const QJsonValue& value : devices)
    {
        const QJsonObject obj = value.toObject();
        CatalogEntry entry;
        entry.id = obj.value(QStringLiteral("id")).toString();
        entry.protocol = obj.value(QStringLiteral("protocol")).toString();
        entry.type = parseType(obj.value(QStringLiteral("type")).toString());
        entry.name = obj.value(QStringLiteral("name")).toString();
        entry.expectedDescription = obj.value(QStringLiteral("expectedDescription")).toString(entry.name);
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
            mEntriesByType.insert(entry.type, entry);
    }
    return true;
}

DeviceIdentity CatalogService::enrich(DeviceIdentity device) const
{
    if (!mEntriesByType.contains(device.type))
    {
        device.known = false;
        device.name = QStringLiteral("Unknown device");
        device.status = QStringLiteral("неизвестно");
        return device;
    }

    const CatalogEntry entry = mEntriesByType.value(device.type);
    device.known = true;
    device.catalogId = entry.id;
    device.name = entry.name;
    device.expectedDescription = entry.expectedDescription;
    device.deviceClass = entry.deviceClass;
    device.capabilities = entry.capabilities;
    device.flashWorkflows = entry.flashWorkflows;
    device.firmwareArtifacts = entry.firmwareArtifacts;
    device.descriptionMismatch = (device.description.trimmed() != entry.expectedDescription.trimmed());
    device.status = device.descriptionMismatch ? QStringLiteral("можно обновить") : QStringLiteral("актуально");
    return device;
}
