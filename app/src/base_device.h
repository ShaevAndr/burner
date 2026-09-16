#ifndef DEVICE_WORKBENCH_BASE_DEVICE_H
#define DEVICE_WORKBENCH_BASE_DEVICE_H

#include "models.h"
#include "device_session.h"

#include <QHash>
#include <QVariantMap>
#include <functional>
#include <memory>

using DeviceOperation = std::function<bool(const QVariantMap& arguments, QString* error, QString* rawResponse)>;

class DeviceBase
{
public:
    explicit DeviceBase(DeviceIdentity identity, std::shared_ptr<IDeviceTransport> transport = {},
        std::shared_ptr<const DeviceProfile> profile = {});
    virtual ~DeviceBase() = default;

    const DeviceIdentity& identity() const { return mSession.identity; }
    const std::shared_ptr<const DeviceProfile>& profile() const { return mSession.profile; }
    quint16 applicationType() const;
    quint16 bootloaderType() const;
    QStringList descriptionKeywords() const;
    int productionDateRegister() const;
    int serialNumberRegister() const;
    const QVector<FirmwareVersionSpec>& firmwareVersions() const;
    const QVector<FirmwareArtifact>& firmwareArtifacts() const;
    const FirmwareVersionSpec* firmwareVersionById(const QString& id) const;
    const FirmwareTransitionSpec* transitionTo(const QString& targetFirmwareId) const;
    bool isFirmwareTargetAllowed(const QString& targetFirmwareId) const;
    bool allowUnknownCurrentFirmware() const;
    FirmwareArtifact firmwareForTarget(const QString& target) const;
    QString physicalKey() const { return mSession.physicalKey(); }
    QStringList physicalKeys() const { return mSession.physicalKeys(); }
    void updateIdentity(DeviceIdentity identity);
    void updateProfile(std::shared_ptr<const DeviceProfile> profile);
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
    virtual bool readIdentityDescription(quint16* type, quint16* version, QString* description,
        QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool readExtendedDescription(QByteArray* description, const std::function<void(int)>& progress,
        QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool flashGetParams(QVector<FlashMemoryParams>* params, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool flashWritePage(int flashNum, int pageNum, const QByteArray& page, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool flashReadPage(int flashNum, int pageNum, QByteArray* page, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool waitForDeviceIdentity(const DeviceIdentity& expected, int timeoutMs, int pollIntervalMs, DeviceIdentity* identity, QString* error = nullptr, QString* rawResponse = nullptr) const;
    virtual bool waitForDeviceIdentity(int timeoutMs, int pollIntervalMs, DeviceIdentity* identity, QString* error = nullptr, QString* rawResponse = nullptr) const;

protected:
    DeviceSession mSession;
};

#endif // DEVICE_WORKBENCH_BASE_DEVICE_H
