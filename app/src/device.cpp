#include "device.h"
#include "transport/unicorn_ascii_transport.h"

#include <utility>

DeviceFactory::DeviceFactory(std::shared_ptr<IDeviceTransport> transport) :
    mTransport(std::move(transport))
{
}

std::shared_ptr<DeviceBase> DeviceFactory::create(const DeviceIdentity& identity,
    std::shared_ptr<const DeviceProfile> profile) const
{
    const std::shared_ptr<IDeviceTransport> transport = mTransport ? mTransport : createUnicornAsciiTransport();
    return std::make_shared<DeviceBase>(identity, transport, std::move(profile));
}
