#ifndef DEVICE_WORKBENCH_UNICORN_ASCII_TRANSPORT_H
#define DEVICE_WORKBENCH_UNICORN_ASCII_TRANSPORT_H

#include "../device_transport.h"

#include <memory>

std::shared_ptr<IDeviceTransport> createUnicornAsciiTransport();

#endif // DEVICE_WORKBENCH_UNICORN_ASCII_TRANSPORT_H
