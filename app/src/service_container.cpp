#include "service_container.h"
#include "firmware_flash_strategy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <utility>

namespace
{
bool validateConfiguration(const CatalogService& catalog,
    const ActionRepository& actions,
    const WorkflowRepository& workflows,
    QString* error)
{
    if (!actions.validateAgainstProfiles(catalog.profiles(), error))
        return false;
    for (const ActionSpec& action : actions.allActions())
    {
        if (!workflows.snapshotFor(action))
        {
            if (error)
                *error = QStringLiteral("Action %1 references unknown workflow %2")
                    .arg(action.id, action.workflow);
            return false;
        }
    }

    QSet<QString> checkedArtifacts;
    for (const auto& profile : catalog.profiles())
    {
        if (!profile)
            continue;
        QVector<FirmwareArtifact> artifacts = profile->firmwareArtifacts;
        for (const FirmwareVersionSpec& version : profile->firmwareVersions)
        {
            if (!version.installation.workflow.isEmpty()
                && !workflows.snapshotForId(version.installation.workflow))
            {
                if (error)
                    *error = QStringLiteral("Firmware %1 references unknown workflow %2")
                        .arg(version.id, version.installation.workflow);
                return false;
            }
            if (!version.installation.strategy.isEmpty()
                && !FirmwareFlashStrategyRegistry::find(version.installation.strategy))
            {
                if (error)
                    *error = QStringLiteral("Firmware %1 references unknown flash strategy %2")
                        .arg(version.id, version.installation.strategy);
                return false;
            }
            if (!version.artifact.relativePath.isEmpty())
                artifacts.append(version.artifact);
        }
        for (const FirmwareArtifact& artifact : artifacts)
        {
            if (artifact.relativePath.isEmpty()
                || artifact.relativePath.startsWith(QStringLiteral(":/"))
                || QFileInfo(artifact.relativePath).isAbsolute()
                || QDir::cleanPath(artifact.relativePath).startsWith(QStringLiteral("..")))
            {
                if (error)
                    *error = QStringLiteral("Firmware path is invalid: %1").arg(artifact.relativePath);
                return false;
            }
            const QString artifactKey = artifact.relativePath + QLatin1Char('\n')
                + artifact.sha256 + QLatin1Char('\n') + artifact.format;
            if (checkedArtifacts.contains(artifactKey))
                continue;
            checkedArtifacts.insert(artifactKey);
            const QString path = QStringLiteral(":/") + QDir::cleanPath(artifact.relativePath);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                if (error)
                    *error = QStringLiteral("Firmware file is unavailable: %1").arg(path);
                return false;
            }
            FirmwareFlashPlan plan;
            plan.artifact = artifact;
            plan.fileName = path;
            plan.data = file.readAll();
            const QString actualHash = QString::fromLatin1(
                QCryptographicHash::hash(plan.data, QCryptographicHash::Sha256).toHex());
            if (artifact.sha256.isEmpty()
                || actualHash.compare(artifact.sha256, Qt::CaseInsensitive) != 0)
            {
                if (error)
                    *error = QStringLiteral("Firmware SHA-256 mismatch: %1").arg(path);
                return false;
            }
            QString formatError;
            if (!validateFirmwareImage(plan, &formatError))
            {
                if (error)
                    *error = QStringLiteral("Firmware format invalid in %1: %2")
                        .arg(path, formatError);
                return false;
            }
        }
    }
    return true;
}
}

ServiceContainer::ServiceContainer(QObject* parent) :
    QObject(parent),
    mWorkflow(&mWorkflows, this),
    mUdpDiscovery(this),
    mRs485Discovery(this)
{
}

bool ServiceContainer::loadConfig(QString* error)
{
    CatalogService catalog;
    ActionRepository actions;
    WorkflowRepository workflows;
    if (!catalog.load(configPath(QStringLiteral("device-catalog.json")), error)
        || !actions.load(configPath(QStringLiteral("actions.json")), error)
        || !workflows.load(configPath(QStringLiteral("workflows.json")), error)
        || !validateConfiguration(catalog, actions, workflows, error))
        return false;

    mCatalog = std::move(catalog);
    mActions = std::move(actions);
    mWorkflows.replaceWith(workflows);
    return true;
}

bool ServiceContainer::reloadWorkflows(QString* error)
{
    WorkflowRepository workflows;
    if (!workflows.load(configPath(QStringLiteral("workflows.json")), error)
        || !validateConfiguration(mCatalog, mActions, workflows, error))
        return false;
    mWorkflows.replaceWith(workflows);
    return true;
}

QString ServiceContainer::configPath(const QString& fileName) const
{
    return QStringLiteral(":/config/") + fileName;
}
