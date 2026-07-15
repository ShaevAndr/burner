#ifndef DEVICE_WORKBENCH_CATALOG_H
#define DEVICE_WORKBENCH_CATALOG_H

#include "models.h"

#include <QHash>
#include <QString>

class CatalogService
{
public:
    bool load(const QString& fileName, QString* error = nullptr);
    DeviceIdentity enrich(DeviceIdentity device) const;
    bool isLoaded() const { return !mEntriesByType.isEmpty(); }

private:
    QHash<quint16, CatalogEntry> mEntriesByType;
};

#endif // DEVICE_WORKBENCH_CATALOG_H
