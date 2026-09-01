QT += core testlib network
CONFIG += console c++17 testcase
TEMPLATE = app
TARGET = device-workbench-tests

RESOURCES += ../embedded_resources.qrc

SOURCES += \
    tst_device_workbench.cpp \
    ../src/action_repository.cpp \
    ../src/base_device.cpp \
    ../src/catalog.cpp \
    ../src/device.cpp \
    ../src/firmware_flash_strategy.cpp \
    ../src/devices/boc_v12_device.cpp \
    ../src/transport/unicorn_ascii_transport.cpp \
    ../src/workers.cpp \
    ../src/workflow.cpp \
    ../src/workflow_definition.cpp

HEADERS += \
    ../src/action_repository.h \
    ../src/app_edition.h \
    ../src/base_device.h \
    ../src/catalog.h \
    ../src/device.h \
    ../src/device_transport.h \
    ../src/firmware_flash_strategy.h \
    ../src/devices/boc_v12_device.h \
    ../src/models.h \
    ../src/transport/unicorn_ascii_transport.h \
    ../src/workers.h \
    ../src/workflow.h \
    ../src/workflow_definition.h
