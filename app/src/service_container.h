#ifndef DEVICE_WORKBENCH_SERVICE_CONTAINER_H
#define DEVICE_WORKBENCH_SERVICE_CONTAINER_H

#include "action_repository.h"
#include "catalog.h"
#include "discovery.h"
#include "workflow.h"
#include "workflow_definition.h"

#include <QObject>

class ServiceContainer : public QObject
{
    Q_OBJECT
public:
    explicit ServiceContainer(QObject* parent = nullptr);

    bool loadConfig(QString* error = nullptr);
    bool reloadWorkflows(QString* error = nullptr);

    CatalogService& catalog() { return mCatalog; }
    ActionRepository& actions() { return mActions; }
    WorkflowRepository& workflows() { return mWorkflows; }
    WorkflowRunner& workflow() { return mWorkflow; }
    UdpBroadcastDiscovery& udpDiscovery() { return mUdpDiscovery; }
    Rs485Discovery& rs485Discovery() { return mRs485Discovery; }

private:
    QString configPath(const QString& fileName) const;

    CatalogService mCatalog;
    ActionRepository mActions;
    WorkflowRepository mWorkflows;
    WorkflowRunner mWorkflow;
    UdpBroadcastDiscovery mUdpDiscovery;
    Rs485Discovery mRs485Discovery;
};

#endif // DEVICE_WORKBENCH_SERVICE_CONTAINER_H
