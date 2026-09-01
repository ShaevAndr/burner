#include "service_container.h"

ServiceContainer::ServiceContainer(QObject* parent) :
    QObject(parent),
    mWorkflow(&mWorkflows, this),
    mUdpDiscovery(this),
    mRs485Discovery(this)
{
}

bool ServiceContainer::loadConfig(QString* error)
{
    QString catalogError;
    QString actionsError;
    QString workflowsError;
    const bool catalogOk = mCatalog.load(configPath(QStringLiteral("device-catalog.json")), &catalogError);
    const bool actionsOk = mActions.load(configPath(QStringLiteral("actions.json")), &actionsError);
    const bool workflowsOk = mWorkflows.load(configPath(QStringLiteral("workflows.json")), &workflowsError);

    if (!catalogOk || !actionsOk || !workflowsOk)
    {
        if (error)
            *error = QStringList{catalogError, actionsError, workflowsError}.join(QStringLiteral("\n"));
        return false;
    }
    return true;
}

bool ServiceContainer::reloadWorkflows(QString* error)
{
    return mWorkflows.load(configPath(QStringLiteral("workflows.json")), error);
}

QString ServiceContainer::configPath(const QString& fileName) const
{
    return QStringLiteral(":/config/") + fileName;
}
