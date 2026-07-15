#ifndef DEVICE_WORKBENCH_MODELS_H
#define DEVICE_WORKBENCH_MODELS_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct FirmwareArtifact
{
    QString target;
    QString version;
    QString relativePath;
    QString sha256;
};

struct DeviceIdentity
{
    QString id;
    QString name;
    QString protocol;
    QString channel;
    QString endpoint;
    QString serialNumber;
    quint16 type = 0;
    quint16 version = 0;
    QString description;

    QString catalogId;
    QString expectedDescription;
    QString deviceClass;
    QString status;
    bool known = false;
    bool descriptionMismatch = false;
    QStringList capabilities;
    QHash<QString, QString> flashWorkflows;
    QVector<FirmwareArtifact> firmwareArtifacts;

    QString typeHex() const
    {
        return QStringLiteral("0x%1").arg(type, 4, 16, QLatin1Char('0')).toUpper();
    }

    QString versionHex() const
    {
        return QStringLiteral("0x%1").arg(version, 4, 16, QLatin1Char('0')).toUpper();
    }

    FirmwareArtifact firmwareForTarget(const QString& target) const
    {
        for (const FirmwareArtifact& artifact : firmwareArtifacts)
        {
            if (artifact.target == target)
                return artifact;
        }
        return {};
    }
};

struct CatalogEntry
{
    QString id;
    QString protocol;
    quint16 type = 0;
    QString name;
    QString expectedDescription;
    QString deviceClass;
    QStringList capabilities;
    QHash<QString, QString> flashWorkflows;
    QVector<FirmwareArtifact> firmwareArtifacts;
};

struct ActionSpec
{
    QString id;
    QString title;
    QString workflow;
    QString target;
    QStringList requiredCapabilities;
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
