#ifndef DEVICE_WORKBENCH_WORKERS_H
#define DEVICE_WORKBENCH_WORKERS_H

#include "base_device.h"
#include "workflow.h"

#include <QObject>
#include <QVector>
#include <QVariantMap>
#include <memory>

class WorkflowWorker : public QObject
{
    Q_OBJECT
public:
    WorkflowWorker(WorkflowRepository* workflows,
        ActionSpec action,
        QVector<std::shared_ptr<DeviceBase>> devices,
        QVariantMap parameters,
        QObject* parent = nullptr);

public slots:
    void run();

signals:
    void logMessage(QString message);
    void transportLogMessage(QString message);
    void progressChanged(int percent);
    void finished();

private:
    WorkflowRepository* mWorkflows = nullptr;
    ActionSpec mAction;
    QVector<std::shared_ptr<DeviceBase>> mDevices;
    QVariantMap mParameters;
};

class PingWorker : public QObject
{
    Q_OBJECT
public:
    explicit PingWorker(std::shared_ptr<DeviceBase> device, int intervalMs = 500, QObject* parent = nullptr);

public slots:
    void start();
    void stop();

signals:
    void pingLine(QString message);
    void transportLogMessage(QString message);
    void finished();

private slots:
    void pingOnce();

private:
    void finish();

    std::shared_ptr<DeviceBase> mDevice;
    int mIntervalMs = 500;
    bool mStopping = false;
    bool mFinished = false;
};

#endif // DEVICE_WORKBENCH_WORKERS_H
