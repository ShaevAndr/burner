#ifndef DEVICE_WORKBENCH_CATALOG_H
#define DEVICE_WORKBENCH_CATALOG_H

#include "models.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <memory>

struct CatalogMatch
{
    DeviceIdentity identity;
    std::shared_ptr<const DeviceProfile> profile;
};

class CatalogService
{
public:
    bool load(const QString& fileName, QString* error = nullptr);
    CatalogMatch match(DeviceIdentity device) const;
    DeviceIdentity enrich(DeviceIdentity device) const;
    std::shared_ptr<const DeviceProfile> profileForDevice(const DeviceIdentity& device) const;
    const QVector<std::shared_ptr<const DeviceProfile>>& profiles() const { return mProfiles; }
    bool isLoaded() const { return !mProfiles.isEmpty(); }

private:
    QHash<quint16, std::shared_ptr<const DeviceProfile>> mProfilesByType;
    QVector<std::shared_ptr<const DeviceProfile>> mProfiles;
};

#endif // DEVICE_WORKBENCH_CATALOG_H
