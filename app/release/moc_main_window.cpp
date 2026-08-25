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
    QByteArrayData data[32];
    char stringdata0[429];
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
QT_MOC_LITERAL(6, 63, 20), // "onDeviceDataProgress"
QT_MOC_LITERAL(7, 84, 9), // "requestId"
QT_MOC_LITERAL(8, 94, 7), // "percent"
QT_MOC_LITERAL(9, 102, 5), // "stage"
QT_MOC_LITERAL(10, 108, 20), // "onDeviceDataFinished"
QT_MOC_LITERAL(11, 129, 8), // "identity"
QT_MOC_LITERAL(12, 138, 8), // "warnings"
QT_MOC_LITERAL(13, 147, 11), // "rawResponse"
QT_MOC_LITERAL(14, 159, 19), // "onDiscoveryFinished"
QT_MOC_LITERAL(15, 179, 14), // "updateLineMode"
QT_MOC_LITERAL(16, 194, 14), // "updateBulkMenu"
QT_MOC_LITERAL(17, 209, 23), // "runProductionDateUpdate"
QT_MOC_LITERAL(18, 233, 15), // "runActionForRow"
QT_MOC_LITERAL(19, 249, 3), // "row"
QT_MOC_LITERAL(20, 253, 8), // "actionId"
QT_MOC_LITERAL(21, 262, 13), // "executeAction"
QT_MOC_LITERAL(22, 276, 10), // "ActionSpec"
QT_MOC_LITERAL(23, 287, 6), // "action"
QT_MOC_LITERAL(24, 294, 37), // "QVector<std::shared_ptr<Devic..."
QT_MOC_LITERAL(25, 332, 7), // "devices"
QT_MOC_LITERAL(26, 340, 9), // "appendLog"
QT_MOC_LITERAL(27, 350, 7), // "message"
QT_MOC_LITERAL(28, 358, 18), // "appendTransportLog"
QT_MOC_LITERAL(29, 377, 18), // "onWorkflowProgress"
QT_MOC_LITERAL(30, 396, 22), // "onWorkflowStageChanged"
QT_MOC_LITERAL(31, 419, 9) // "operation"

    },
    "MainWindow\0startDiscovery\0\0onDeviceFound\0"
    "DeviceIdentity\0device\0onDeviceDataProgress\0"
    "requestId\0percent\0stage\0onDeviceDataFinished\0"
    "identity\0warnings\0rawResponse\0"
    "onDiscoveryFinished\0updateLineMode\0"
    "updateBulkMenu\0runProductionDateUpdate\0"
    "runActionForRow\0row\0actionId\0executeAction\0"
    "ActionSpec\0action\0"
    "QVector<std::shared_ptr<DeviceBase> >\0"
    "devices\0appendLog\0message\0appendTransportLog\0"
    "onWorkflowProgress\0onWorkflowStageChanged\0"
    "operation"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x08 /* Private */,
       3,    1,   85,    2, 0x08 /* Private */,
       6,    3,   88,    2, 0x08 /* Private */,
      10,    4,   95,    2, 0x08 /* Private */,
      14,    0,  104,    2, 0x08 /* Private */,
      15,    0,  105,    2, 0x08 /* Private */,
      16,    0,  106,    2, 0x08 /* Private */,
      17,    0,  107,    2, 0x08 /* Private */,
      18,    2,  108,    2, 0x08 /* Private */,
      21,    2,  113,    2, 0x08 /* Private */,
      26,    1,  118,    2, 0x08 /* Private */,
      28,    1,  121,    2, 0x08 /* Private */,
      29,    1,  124,    2, 0x08 /* Private */,
      30,    2,  127,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::Int, QMetaType::QString,    7,    8,    9,
    QMetaType::Void, QMetaType::ULongLong, 0x80000000 | 4, QMetaType::QStringList, QMetaType::QString,    7,   11,   12,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   19,   20,
    QMetaType::Void, 0x80000000 | 22, 0x80000000 | 24,   23,   25,
    QMetaType::Void, QMetaType::QString,   27,
    QMetaType::Void, QMetaType::QString,   27,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   31,    9,

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
        case 2: _t->onDeviceDataProgress((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 3: _t->onDeviceDataFinished((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< DeviceIdentity(*)>(_a[2])),(*reinterpret_cast< const QStringList(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4]))); break;
        case 4: _t->onDiscoveryFinished(); break;
        case 5: _t->updateLineMode(); break;
        case 6: _t->updateBulkMenu(); break;
        case 7: _t->runProductionDateUpdate(); break;
        case 8: _t->runActionForRow((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 9: _t->executeAction((*reinterpret_cast< const ActionSpec(*)>(_a[1])),(*reinterpret_cast< const QVector<std::shared_ptr<DeviceBase> >(*)>(_a[2]))); break;
        case 10: _t->appendLog((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 11: _t->appendTransportLog((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 12: _t->onWorkflowProgress((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 13: _t->onWorkflowStageChanged((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DeviceIdentity >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 1:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DeviceIdentity >(); break;
            }
            break;
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
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
