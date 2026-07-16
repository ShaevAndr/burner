#ifndef DEVICE_WORKBENCH_DEVICE_H
#define DEVICE_WORKBENCH_DEVICE_H

#include "base_device.h"

#include <memory>

class DeviceFactory
{
public:
    explicit DeviceFactory(std::shared_ptr<IDeviceTransport> transport = {});

    std::shared_ptr<DeviceBase> create(const DeviceIdentity& identity) const;

private:
    std::shared_ptr<IDeviceTransport> mTransport;
};

#endif // DEVICE_WORKBENCH_DEVICE_H
