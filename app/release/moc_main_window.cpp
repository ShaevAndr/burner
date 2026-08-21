/****************************************************************************
** Meta object code from reading C++ file 'main_window.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.12)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/main_window.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QVector>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'main_window.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.12. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[27];
    char stringdata0[336];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 14), // "startDiscovery"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 13), // "onDeviceFound"
QT_MOC_LITERAL(4, 41, 14), // "DeviceIdentity"
QT_MOC_LITERAL(5, 56, 6), // "device"
QT_MOC_LITERAL(6, 63, 18), // "onUuidReadFinished"
QT_MOC_LITERAL(7, 82, 9), // "requestId"
QT_MOC_LITERAL(8, 92, 4), // "uuid"
QT_MOC_LITERAL(9, 97, 5), // "error"
QT_MOC_LITERAL(10, 103, 11), // "rawResponse"
QT_MOC_LITERAL(11, 115, 19), // "onDiscoveryFinished"
QT_MOC_LITERAL(12, 135, 14), // "updateLineMode"
QT_MOC_LITERAL(13, 150, 14), // "updateBulkMenu"
QT_MOC_LITERAL(14, 165, 15), // "runActionForRow"
QT_MOC_LITERAL(15, 181, 3), // "row"
QT_MOC_LITERAL(16, 185, 8), // "actionId"
QT_MOC_LITERAL(17, 194, 13), // "executeAction"
QT_MOC_LITERAL(18, 208, 10), // "ActionSpec"
QT_MOC_LITERAL(19, 219, 6), // "action"
QT_MOC_LITERAL(20, 226, 37), // "QVector<std::shared_ptr<Devic..."
QT_MOC_LITERAL(21, 264, 7), // "devices"
QT_MOC_LITERAL(22, 272, 9), // "appendLog"
QT_MOC_LITERAL(23, 282, 7), // "message"
QT_MOC_LITERAL(24, 290, 18), // "appendTransportLog"
QT_MOC_LITERAL(25, 309, 18), // "onWorkflowProgress"
QT_MOC_LITERAL(26, 328, 7) // "percent"

    },
    "MainWindow\0startDiscovery\0\0onDeviceFound\0"
    "DeviceIdentity\0device\0onUuidReadFinished\0"
    "requestId\0uuid\0error\0rawResponse\0"
    "onDiscoveryFinished\0updateLineMode\0"
    "updateBulkMenu\0runActionForRow\0row\0"
    "actionId\0executeAction\0ActionSpec\0"
    "action\0QVector<std::shared_ptr<DeviceBase> >\0"
    "devices\0appendLog\0message\0appendTransportLog\0"
    "onWorkflowProgress\0percent"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   69,    2, 0x08 /* Private */,
       3,    1,   70,    2, 0x08 /* Private */,
       6,    4,   73,    2, 0x08 /* Private */,
      11,    0,   82,    2, 0x08 /* Private */,
      12,    0,   83,    2, 0x08 /* Private */,
      13,    0,   84,    2, 0x08 /* Private */,
      14,    2,   85,    2, 0x08 /* Private */,
      17,    2,   90,    2, 0x08 /* Private */,
      22,    1,   95,    2, 0x08 /* Private */,
      24,    1,   98,    2, 0x08 /* Private */,
      25,    1,  101,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString, QMetaType::QString, QMetaType::QString,    7,    8,    9,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   15,   16,
    QMetaType::Void, 0x80000000 | 18, 0x80000000 | 20,   19,   21,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void, QMetaType::Int,   26,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->startDiscovery(); break;
        case 1: _t->onDeviceFound((*reinterpret_cast< DeviceIdentity(*)>(_a[1]))); break;
        case 2: _t->onUuidReadFinished((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4]))); break;
        case 3: _t->onDiscoveryFinished(); break;
        case 4: _t->updateLineMode(); break;
        case 5: _t->updateBulkMenu(); break;
        case 6: _t->runActionForRow((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 7: _t->executeAction((*reinterpret_cast< const ActionSpec(*)>(_a[1])),(*reinterpret_cast< const QVector<std::shared_ptr<DeviceBase> >(*)>(_a[2]))); break;
        case 8: _t->appendLog((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 9: _t->appendTransportLog((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 10: _t->onWorkflowProgress((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    &QMainWindow::staticMetaObject,
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 11;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
