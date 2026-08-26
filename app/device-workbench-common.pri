QT += widgets network serialport
CONFIG += c++17 release
CONFIG -= debug debug_and_release build_all

TEMPLATE = app

SOURCES += \
    src/action_repository.cpp \
    src/base_device.cpp \
    src/catalog.cpp \
    src/device.cpp \
    src/devices/boc_v12_device.cpp \
    src/discovery.cpp \
    src/firmware_flash_strategy.cpp \
    src/main.cpp \
    src/main_window.cpp \
    src/service_container.cpp \
    src/transport/unicorn_ascii_transport.cpp \
    src/workers.cpp \
    src/workflow.cpp \
    src/workflow_definition.cpp

HEADERS += \
    src/action_repository.h \
    src/app_edition.h \
    src/base_device.h \
    src/catalog.h \
    src/device.h \
    src/device_transport.h \
    src/devices/boc_v12_device.h \
    src/discovery.h \
    src/firmware_flash_strategy.h \
    src/main_window.h \
    src/models.h \
    src/service_container.h \
    src/transport/unicorn_ascii_transport.h \
    src/workers.h \
    src/workflow.h \
    src/workflow_definition.h

# Intermediate compiler output always stays in a shadow-build directory.
OBJECTS_DIR = $$clean_path($$OUT_PWD/obj)
MOC_DIR = $$clean_path($$OUT_PWD/moc)
RCC_DIR = $$clean_path($$OUT_PWD/rcc)
UI_DIR = $$clean_path($$OUT_PWD/ui)
