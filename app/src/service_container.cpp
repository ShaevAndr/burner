#include "service_container.h"

#include <QCoreApplication>
#include <QDir>

ServiceContainer::ServiceContainer(QObject* parent) :
    QObject(parent),
    mWorkflow(this),
    mUdpDiscovery(this),
    mRs485Discovery(this)
{
}

bool ServiceContainer::loadConfig(QString* error)
{
    QString catalogError;
    QString actionsError;
    const bool catalogOk = mCatalog.load(configPath(QStringLiteral("device-catalog.json")), &catalogError);
    const bool actionsOk = mActions.load(configPath(QStringLiteral("actions.json")), &actionsError);

    if (!catalogOk || !actionsOk)
    {
        if (error)
            *error = QStringList{catalogError, actionsError}.join(QStringLiteral("\n"));
        return false;
    }
    return true;
}

QString ServiceContainer::configPath(const QString& fileName) const
{
    const QStringList roots = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/config"),
        QDir::currentPath() + QStringLiteral("/config"),
        QDir::currentPath() + QStringLiteral("/../config"),
        QDir::currentPath() + QStringLiteral("/app/config")
    };

    for (const QString& root : roots)
    {
        const QString candidate = QDir(root).filePath(fileName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/") + fileName);
}
