#include "device.h"
#include "devices/boc_v12_device.h"
#include "transport/unicorn_ascii_transport.h"

#include <utility>

DeviceFactory::DeviceFactory(std::shared_ptr<IDeviceTransport> transport) :
    mTransport(std::move(transport))
{
}

std::shared_ptr<DeviceBase> DeviceFactory::create(const DeviceIdentity& identity) const
{
    const std::shared_ptr<IDeviceTransport> transport = mTransport ? mTransport : createUnicornAsciiTransport();
    if (identity.catalogId == QStringLiteral("boc.v12") || identity.deviceClass == QStringLiteral("BocV12Device"))
        return std::make_shared<BocV12Device>(identity, transport);
    return std::make_shared<DeviceBase>(identity, transport);
}
