#ifndef DEVICE_WORKBENCH_CATALOG_H
#define DEVICE_WORKBENCH_CATALOG_H

#include "models.h"

#include <QHash>
#include <QString>
#include <QVector>

class CatalogService
{
public:
    bool load(const QString& fileName, QString* error = nullptr);
    DeviceIdentity enrich(DeviceIdentity device) const;
    bool isLoaded() const { return !mEntries.isEmpty(); }

private:
    const CatalogEntry* entryForDevice(const DeviceIdentity& device) const;
    QHash<quint16, CatalogEntry> mEntriesByType;
    QVector<CatalogEntry> mEntries;
    QHash<QString, FirmwareCatalog> mFirmwareByDeviceId;
};

#endif // DEVICE_WORKBENCH_CATALOG_H
