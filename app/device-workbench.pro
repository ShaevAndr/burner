QT += widgets network serialport
CONFIG += c++17

TARGET = device-workbench
TEMPLATE = app

SOURCES += \
    src/action_repository.cpp \
    src/base_device.cpp \
    src/catalog.cpp \
    src/device.cpp \
    src/devices/boc_v12_device.cpp \
    src/discovery.cpp \
    src/main.cpp \
    src/main_window.cpp \
    src/service_container.cpp \
    src/transport/unicorn_ascii_transport.cpp \
    src/workers.cpp \
    src/workflow.cpp \
    src/workflow_definition.cpp

HEADERS += \
    src/action_repository.h \
    src/base_device.h \
    src/catalog.h \
    src/device.h \
    src/device_transport.h \
    src/devices/boc_v12_device.h \
    src/discovery.h \
    src/main_window.h \
    src/models.h \
    src/service_container.h \
    src/transport/unicorn_ascii_transport.h \
    src/workers.h \
    src/workflow.h \
    src/workflow_definition.h
