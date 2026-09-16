#ifndef DEVICE_WORKBENCH_ACTION_REPOSITORY_H
#define DEVICE_WORKBENCH_ACTION_REPOSITORY_H

#include "models.h"

#include <QVector>
#include <memory>

class DeviceBase;

class ActionRepository
{
public:
    bool load(const QString& fileName, QString* error = nullptr);
    bool validateAgainstProfiles(const QVector<std::shared_ptr<const DeviceProfile>>& profiles,
        QString* error = nullptr) const;
    QVector<ActionSpec> actionsForDevice(const DeviceIdentity& device) const;
    QVector<ActionSpec> actionsForDevice(const DeviceBase& device) const;
    QVector<ActionSpec> commonActions(const QVector<std::shared_ptr<DeviceBase>>& devices) const;
    const QVector<ActionSpec>& allActions() const { return mActions; }

private:
    bool isActionAllowed(const ActionSpec& action, const DeviceIdentity& device) const;
    bool isActionAllowed(const ActionSpec& action, const DeviceBase& device) const;

    QVector<ActionSpec> mActions;
};

#endif // DEVICE_WORKBENCH_ACTION_REPOSITORY_H
