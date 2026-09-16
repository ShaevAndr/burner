#include "base_device.h"
#include "transport/unicorn_ascii_transport.h"

#include <utility>

DeviceBase::DeviceBase(DeviceIdentity identity, std::shared_ptr<IDeviceTransport> transport,
    std::shared_ptr<const DeviceProfile> profile)
{
    mSession.identity = std::move(identity);
    mSession.profile = std::move(profile);
    mSession.transport = transport ? std::move(transport) : createUnicornAsciiTransport();
}

void DeviceBase::updateIdentity(DeviceIdentity identity)
{
    mSession.identity = std::move(identity);
}

void DeviceBase::updateProfile(std::shared_ptr<const DeviceProfile> profile)
{
    mSession.profile = std::move(profile);
}

quint16 DeviceBase::applicationType() const
{
    return mSession.profile ? mSession.profile->applicationType : mSession.identity.applicationType;
}

quint16 DeviceBase::bootloaderType() const
{
    return mSession.profile ? mSession.profile->bootloaderType : mSession.identity.bootloaderType;
}

QStringList DeviceBase::descriptionKeywords() const
{
    return mSession.profile ? mSession.profile->descriptionKeywords : mSession.identity.descriptionKeywords;
}

int DeviceBase::productionDateRegister() const
{
    return mSession.profile ? mSession.profile->productionDateRegister : mSession.identity.productionDateRegister;
}

int DeviceBase::serialNumberRegister() const
{
    return mSession.profile ? mSession.profile->serialNumberRegister : mSession.identity.serialNumberRegister;
}

const QVector<FirmwareVersionSpec>& DeviceBase::firmwareVersions() const
{
    return mSession.profile ? mSession.profile->firmwareVersions : mSession.identity.firmwareVersions;
}

const QVector<FirmwareArtifact>& DeviceBase::firmwareArtifacts() const
{
    return mSession.profile ? mSession.profile->firmwareArtifacts : mSession.identity.firmwareArtifacts;
}

const FirmwareVersionSpec* DeviceBase::firmwareVersionById(const QString& id) const
{
    for (const FirmwareVersionSpec& version : firmwareVersions())
    {
        if (version.id == id)
            return &version;
    }
    return nullptr;
}

const FirmwareTransitionSpec* DeviceBase::transitionTo(const QString& targetFirmwareId) const
{
    const auto& transitions = mSession.profile
        ? mSession.profile->firmwareTransitions : mSession.identity.firmwareTransitions;
    for (const FirmwareTransitionSpec& transition : transitions)
    {
        if (transition.from == mSession.identity.currentFirmwareId
            && transition.to == targetFirmwareId)
            return &transition;
    }
    return nullptr;
}

bool DeviceBase::isFirmwareTargetAllowed(const QString& targetFirmwareId) const
{
    if (!mSession.profile)
        return mSession.identity.isFirmwareTargetAllowed(targetFirmwareId);
    if (!firmwareVersionById(targetFirmwareId))
        return false;
    if (mSession.identity.currentFirmwareId.isEmpty())
        return mSession.profile->allowUnknownCurrentFirmware;
    const FirmwareTransitionSpec* transition = transitionTo(targetFirmwareId);
    return transition && transition->enabled;
}

bool DeviceBase::allowUnknownCurrentFirmware() const
{
    return mSession.profile ? mSession.profile->allowUnknownCurrentFirmware
        : mSession.identity.allowUnknownCurrentFirmware;
}

FirmwareArtifact DeviceBase::firmwareForTarget(const QString& target) const
{
    FirmwareArtifact first;
    for (const FirmwareArtifact& artifact : firmwareArtifacts())
    {
        if (artifact.target != target
            || !artifact.isAllowedFromFirmware(mSession.identity.currentFirmwareId))
            continue;
        if (first.relativePath.isEmpty())
            first = artifact;
        if (artifact.isDefault)
            return artifact;
    }
    return first;
}

QString DeviceBase::className() const
{
    return QStringLiteral("DeviceBase");
}

bool DeviceBase::reset(QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->resetDevice(mSession.identity, error, rawResponse);
}

bool DeviceBase::loadApplication(QString* error, QString* rawResponse) const
{
    return writeInt(quint16(mSession.profile ? mSession.profile->applicationLoadRegister : mSession.identity.applicationLoadRegister), 1, error, rawResponse);
}

bool DeviceBase::loadApplicationNoReply(QString* error, QString* rawResponse) const
{
    return writeIntNoReply(quint16(mSession.profile ? mSession.profile->applicationLoadRegister : mSession.identity.applicationLoadRegister), 1, error, rawResponse);
}

bool DeviceBase::disableLoadApplication(QString* error, QString* rawResponse) const
{
    return writeInt(quint16(mSession.profile ? mSession.profile->applicationLoadRegister : mSession.identity.applicationLoadRegister), 0, error, rawResponse);
}

bool DeviceBase::writeProductionDate(qint32 timestamp, QString* error, QString* rawResponse) const
{
    if ((mSession.profile ? mSession.profile->productionDateRegister : mSession.identity.productionDateRegister) < 0)
    {
        if (error)
            *error = QStringLiteral("Production date register is not configured for %1").arg(className());
        return false;
    }
    return writeInt(quint16((mSession.profile ? mSession.profile->productionDateRegister : mSession.identity.productionDateRegister)), timestamp, error, rawResponse);
}

bool DeviceBase::writeSerialNumber(qint32 serialNumber, QString* error, QString* rawResponse) const
{
    if ((mSession.profile ? mSession.profile->serialNumberRegister : mSession.identity.serialNumberRegister) < 0)
    {
        if (error)
            *error = QStringLiteral("Serial number register is not configured for %1").arg(className());
        return false;
    }
    return writeInt(quint16((mSession.profile ? mSession.profile->serialNumberRegister : mSession.identity.serialNumberRegister)), serialNumber, error, rawResponse);
}

bool DeviceBase::writeInt(quint16 index, qint32 value, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->writeRegister(mSession.identity, index, value, error, rawResponse);
}

bool DeviceBase::writeIntNoReply(quint16 index, qint32 value, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->writeRegisterNoReply(mSession.identity, index, value, error, rawResponse);
}

bool DeviceBase::readInt(quint16 index, qint32* value, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->readRegister(mSession.identity, index, value, error, rawResponse);
}

bool DeviceBase::readUuid(QString* uuid, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->readUuid(mSession.identity, uuid, error, rawResponse);
}

bool DeviceBase::readIdentityDescription(quint16* type, quint16* version, QString* description,
    QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->readIdentityDescription(mSession.identity, type, version, description, error, rawResponse);
}

bool DeviceBase::readExtendedDescription(QByteArray* description,
    const std::function<void(int)>& progress,
    QString* error,
    QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->readExtendedDescription(mSession.identity, description, progress, error, rawResponse);
}

bool DeviceBase::flashGetParams(QVector<FlashMemoryParams>* params, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->flashGetParams(mSession.identity, params, error, rawResponse);
}

bool DeviceBase::flashWritePage(int flashNum, int pageNum, const QByteArray& page, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->flashWritePage(mSession.identity, flashNum, pageNum, page, error, rawResponse);
}

bool DeviceBase::flashReadPage(int flashNum, int pageNum, QByteArray* page, QString* error, QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->flashReadPage(mSession.identity, flashNum, pageNum, page, error, rawResponse);
}

bool DeviceBase::waitForDeviceIdentity(const DeviceIdentity& expected,
    int timeoutMs,
    int pollIntervalMs,
    DeviceIdentity* identity,
    QString* error,
    QString* rawResponse) const
{
    if (!mSession.transport)
    {
        if (error)
            *error = QStringLiteral("Device transport is not available");
        return false;
    }
    return mSession.transport->waitForDeviceIdentity(expected, timeoutMs, pollIntervalMs, identity, error, rawResponse);
}

bool DeviceBase::waitForDeviceIdentity(int timeoutMs, int pollIntervalMs, DeviceIdentity* identity, QString* error, QString* rawResponse) const
{
    return waitForDeviceIdentity(mSession.identity, timeoutMs, pollIntervalMs, identity, error, rawResponse);
}
