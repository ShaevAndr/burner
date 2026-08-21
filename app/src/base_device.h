#ifndef DEVICE_WORKBENCH_BASE_DEVICE_H
#define DEVICE_WORKBENCH_BASE_DEVICE_H

#include "models.h"
#include "device_transport.h"

#include <QHash>
#include <QVariantMap>
#include <functional>
#include <memory>

using DeviceOperation = std::function<bool(const QVariantMap& arguments, QString* error, QString* rawResponse)>;

class DeviceBase
{
public:
    explicit DeviceBase(DeviceIdentity identity, std::shared_ptr<IDeviceTransport> transport = {});
    virtual ~DeviceBase() = default;

    const DeviceIdentity& identity() const { return mIdentity; }
    void updateIdentity(DeviceIdentity identity);
    virtual QString className() const;
    virtual bool reset(QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool loadApplication(QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool loadApplicationNoReply(QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool disableLoadApplication(QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool writeProductionDate(qint32 timestamp, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool writeSerialNumber(qint32 serialNumber, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool writeInt(quint16 index, qint32 value, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool writeIntNoReply(quint16 index, qint32 value, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool readInt(quint16 index, qint32* value, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool readUuid(QString* uuid, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool flashGetParams(QVector<FlashMemoryParams>* params, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool flashWritePage(int flashNum, int pageNum, const QByteArray& page, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool flashReadPage(int flashNum, int pageNum, QByteArray* page, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool waitForDeviceIdentity(const DeviceIdentity& expected, int timeoutMs, int pollIntervalMs, DeviceIdentity* identity, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool waitForDeviceIdentity(int timeoutMs, int pollIntervalMs, DeviceIdentity* identity, QString* error = nullptr, QString* rawResponse = nullptr) const;
    QHash<QString, DeviceOperation> operations() const;
    DeviceOperation operation(const QString& key) const;

protected:
    DeviceIdentity mIdentity;
    std::shared_ptr<IDeviceTransport> mTransport;
};

#endif // DEVICE_WORKBENCH_BASE_DEVICE_H
