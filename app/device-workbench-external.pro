TARGET = device-workbench-external
DEFINES += DEVICE_WORKBENCH_EXTERNAL
DESTDIR = $$clean_path($$PWD/../releases/external)
QMAKE_TARGET_PRODUCT = "обнови-БОЦ"

include(device-workbench-common.pri)
