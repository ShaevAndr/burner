QT += core testlib network
CONFIG += console c++17 testcase
TEMPLATE = app
TARGET = device-workbench-tests

SOURCES += \
    tst_device_workbench.cpp \
    ../src/action_repository.cpp \
    ../src/catalog.cpp \
    ../src/device.cpp \
    ../src/transport/unicorn_ascii_transport.cpp \
    ../src/workflow.cpp

HEADERS += \
    ../src/action_repository.h \
    ../src/catalog.h \
    ../src/device.h \
    ../src/device_transport.h \
    ../src/models.h \
    ../src/transport/unicorn_ascii_transport.h \
    ../src/workflow.h
