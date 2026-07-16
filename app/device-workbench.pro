QT += widgets network serialport
CONFIG += c++17

TARGET = device-workbench
TEMPLATE = app

SOURCES += \
    src/action_repository.cpp \
    src/catalog.cpp \
    src/device.cpp \
    src/discovery.cpp \
    src/main.cpp \
    src/main_window.cpp \
    src/service_container.cpp \
    src/transport/unicorn_ascii_transport.cpp \
    src/workflow.cpp

HEADERS += \
    src/action_repository.h \
    src/catalog.h \
    src/device.h \
    src/device_transport.h \
    src/discovery.h \
    src/main_window.h \
    src/models.h \
    src/service_container.h \
    src/transport/unicorn_ascii_transport.h \
    src/workflow.h

