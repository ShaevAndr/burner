TEMPLATE = app
TARGET = obnovi-boc-cli
DESTDIR = $$clean_path($$PWD/../../releases/tools)

CONFIG += console c++17 release
CONFIG -= app_bundle qt debug
QT -= core gui

QMAKE_TARGET_PRODUCT = "обнови-БОЦ CLI"
QMAKE_TARGET_DESCRIPTION = "Менеджер прошивок и сборок обнови-БОЦ"

SOURCES += main.cpp

win32-g++ {
    QMAKE_CXXFLAGS += -Wall -Wextra
    QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
    LIBS += -lshell32 -lgdi32 -lole32
}
