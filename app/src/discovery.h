#ifndef DEVICE_WORKBENCH_DISCOVERY_H
#define DEVICE_WORKBENCH_DISCOVERY_H

#include "models.h"

#include <QNetworkInterface>
#include <QObject>
#include <QSerialPortInfo>
#include <QTimer>
#include <QUdpSocket>

class IDiscoveryStrategy : public QObject
{
    Q_OBJECT
public:
    explicit IDiscoveryStrategy(QObject* parent = nullptr) : QObject(parent) {}
    virtual void start(const DiscoverySettings& settings) = 0;
    virtual void cancel() = 0;

signals:
    void deviceFound(DeviceIdentity device);
    void logMessage(QString message);
    void finished();
};

class UdpBroadcastDiscovery : public IDiscoveryStrategy
{
    Q_OBJECT
public:
    explicit UdpBroadcastDiscovery(QObject* parent = nullptr);
    void start(const DiscoverySettings& settings) override;
    void cancel() override;

private slots:
    void readPendingDatagrams();

private:
    void sendSearchDatagram(const DiscoverySettings& settings);
    void parseDatagram(const QByteArray& datagram, const QHostAddress& sender);

    QUdpSocket mSocket;
    QTimer mFinishTimer;
};

class Rs485Discovery : public IDiscoveryStrategy
{
    Q_OBJECT
public:
    explicit Rs485Discovery(QObject* parent = nullptr);
    void start(const DiscoverySettings& settings) override;
    void cancel() override;
};

QStringList availableSerialPorts();
QStringList availableNetworkInterfaces();

#endif // DEVICE_WORKBENCH_DISCOVERY_H
