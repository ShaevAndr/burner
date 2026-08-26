TARGET = device-workbench-internal
DEFINES += DEVICE_WORKBENCH_INTERNAL
DESTDIR = $$clean_path($$PWD/../releases/internal)
QMAKE_TARGET_PRODUCT = "Device Workbench Internal"

include(device-workbench-common.pri)
