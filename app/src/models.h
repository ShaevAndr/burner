#ifndef DEVICE_WORKBENCH_MODELS_H
#define DEVICE_WORKBENCH_MODELS_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

struct FirmwareArtifact
{
    QString firmwareId;
    QString target;
    QString title;
    QString version;
    QString relativePath;
    QString sha256;
    QString format;
    quint32 addressBase = 0;
    bool isDefault = false;
    int flashNum = 0;
    int offset = 0;
    int pageSize = 0;
    int pagesCount = 0;
};

struct FirmwarePipelineStep
{
    QString type;
    QString skipIfState;
    QVariantMap arguments;
};

struct FirmwareVersionSpec
{
    QString id;
    QString title;
    QString version;
    QString descriptionRegex;
    bool detectFromDescription = true;
    FirmwareArtifact artifact;
};

struct FirmwareTransitionSpec
{
    QString from;
    QString to;
    bool enabled = false;
    QString reason;
    QVector<FirmwarePipelineStep> pipeline;
};

struct FlashMemoryParams
{
    int pagesCount = 0;
    int pageSize = 0;

    bool isValid() const
    {
        return pagesCount > 0 && pageSize > 0;
    }
};

struct DeviceIdentity
{
    QString id;
    QString name;
    QString protocol;
    QString channel;
    QString endpoint;
    QString serialNumber;
    QString uuid;
    int modbusAddress = -1;
    quint16 type = 0;
    quint16 version = 0;
    QString description;
    QString state = QStringLiteral("application");

    QString catalogId;
    QString expectedDescription;
    QString expectedDescriptionPattern;
    QString deviceClass;
    QString status;
    bool known = false;
    bool descriptionMismatch = false;
    QString currentFirmwareId;
    QString firmwareDetectionError;
    quint16 applicationType = 0;
    quint16 applicationVersion = 0;
    quint16 bootloaderType = 0;
    quint16 bootloaderVersion = 0;
    QStringList capabilities;
    QHash<QString, QString> flashWorkflows;
    QVector<FirmwareArtifact> firmwareArtifacts;
    QVector<FirmwareVersionSpec> firmwareVersions;
    QVector<FirmwareTransitionSpec> firmwareTransitions;

    QString typeHex() const
    {
        return QStringLiteral("0x%1").arg(type, 4, 16, QLatin1Char('0')).toUpper();
    }

    QString versionHex() const
    {
        return QStringLiteral("0x%1").arg(version, 4, 16, QLatin1Char('0')).toUpper();
    }

    bool isBootloader() const
    {
        return state == QStringLiteral("bootloader");
    }

    FirmwareArtifact firmwareForTarget(const QString& target) const
    {
        FirmwareArtifact first;
        for (const FirmwareArtifact& artifact : firmwareArtifacts)
        {
            if (artifact.target != target)
                continue;
            if (first.relativePath.isEmpty())
                first = artifact;
            if (artifact.isDefault)
                return artifact;
        }
        return first;
    }

    const FirmwareVersionSpec* firmwareVersionById(const QString& firmwareId) const
    {
        for (const FirmwareVersionSpec& firmware : firmwareVersions)
        {
            if (firmware.id == firmwareId)
                return &firmware;
        }
        return nullptr;
    }

    const FirmwareTransitionSpec* transitionTo(const QString& targetFirmwareId) const
    {
        for (const FirmwareTransitionSpec& transition : firmwareTransitions)
        {
            if (transition.from == currentFirmwareId && transition.to == targetFirmwareId)
                return &transition;
        }
        return nullptr;
    }
};

struct CatalogEntry
{
    QString id;
    QString protocol;
    quint16 type = 0;
    quint16 version = 0;
    quint16 bootloaderType = 0;
    quint16 bootloaderVersion = 0;
    QString name;
    QString expectedDescription;
    QString expectedDescriptionPattern;
    QString deviceClass;
    QStringList capabilities;
    QHash<QString, QString> flashWorkflows;
    QVector<FirmwareArtifact> firmwareArtifacts;
    QVector<FirmwareVersionSpec> firmwareVersions;
    QVector<FirmwareTransitionSpec> firmwareTransitions;
};

struct ActionSpec
{
    QString id;
    QString title;
    QString workflow;
    QString target;
    QString selection;
    QStringList requiredCapabilities;
    QStringList allowedStates;
};

struct DiscoverySettings
{
    QString lineMode;
    QString interfaceName;
    QString protocolId;
    QString serialPortName;
    int addressStart = 1;
    int addressEnd = 64;
    int timeoutMs = 2000;
};

#endif // DEVICE_WORKBENCH_MODELS_H
