QT += core testlib
CONFIG += console c++17 testcase release
CONFIG -= debug debug_and_release build_all
TEMPLATE = app
TARGET = external-edition-tests
DEFINES += DEVICE_WORKBENCH_EXTERNAL

SOURCES += tst_external_edition.cpp
HEADERS += ../src/app_edition.h
