/****************************************************************************
** Meta object code from reading C++ file 'workers.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.12)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/workers.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'workers.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.12. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_DeviceDataWorker_t {
    QByteArrayData data[12];
    char stringdata0[116];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_DeviceDataWorker_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_DeviceDataWorker_t qt_meta_stringdata_DeviceDataWorker = {
    {
QT_MOC_LITERAL(0, 0, 16), // "DeviceDataWorker"
QT_MOC_LITERAL(1, 17, 15), // "progressChanged"
QT_MOC_LITERAL(2, 33, 0), // ""
QT_MOC_LITERAL(3, 34, 9), // "requestId"
QT_MOC_LITERAL(4, 44, 7), // "percent"
QT_MOC_LITERAL(5, 52, 5), // "stage"
QT_MOC_LITERAL(6, 58, 8), // "finished"
QT_MOC_LITERAL(7, 67, 14), // "DeviceIdentity"
QT_MOC_LITERAL(8, 82, 8), // "identity"
QT_MOC_LITERAL(9, 91, 8), // "warnings"
QT_MOC_LITERAL(10, 100, 11), // "rawResponse"
QT_MOC_LITERAL(11, 112, 3) // "run"

    },
    "DeviceDataWorker\0progressChanged\0\0"
    "requestId\0percent\0stage\0finished\0"
    "DeviceIdentity\0identity\0warnings\0"
    "rawResponse\0run"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_DeviceDataWorker[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    3,   29,    2, 0x06 /* Public */,
       6,    4,   36,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      11,    0,   45,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::ULongLong, QMetaType::Int, QMetaType::QString,    3,    4,    5,
    QMetaType::Void, QMetaType::ULongLong, 0x80000000 | 7, QMetaType::QStringList, QMetaType::QString,    3,    8,    9,   10,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void DeviceDataWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DeviceDataWorker *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->progressChanged((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< QString(*)>(_a[3]))); break;
        case 1: _t->finished((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< DeviceIdentity(*)>(_a[2])),(*reinterpret_cast< QStringList(*)>(_a[3])),(*reinterpret_cast< QString(*)>(_a[4]))); break;
        case 2: _t->run(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 1:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DeviceIdentity >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DeviceDataWorker::*)(quint64 , int , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DeviceDataWorker::progressChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DeviceDataWorker::*)(quint64 , DeviceIdentity , QStringList , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DeviceDataWorker::finished)) {
                *result = 1;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject DeviceDataWorker::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_DeviceDataWorker.data,
    qt_meta_data_DeviceDataWorker,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *DeviceDataWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DeviceDataWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DeviceDataWorker.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DeviceDataWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void DeviceDataWorker::progressChanged(quint64 _t1, int _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void DeviceDataWorker::finished(quint64 _t1, DeviceIdentity _t2, QStringList _t3, QString _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)), const_cast<void*>(reinterpret_cast<const void*>(&_t4)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
struct qt_meta_stringdata_WorkflowWorker_t {
    QByteArrayData data[18];
    char stringdata0[201];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_WorkflowWorker_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_WorkflowWorker_t qt_meta_stringdata_WorkflowWorker = {
    {
QT_MOC_LITERAL(0, 0, 14), // "WorkflowWorker"
QT_MOC_LITERAL(1, 15, 10), // "logMessage"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 7), // "message"
QT_MOC_LITERAL(4, 35, 19), // "transportLogMessage"
QT_MOC_LITERAL(5, 55, 15), // "progressChanged"
QT_MOC_LITERAL(6, 71, 7), // "percent"
QT_MOC_LITERAL(7, 79, 12), // "stageChanged"
QT_MOC_LITERAL(8, 92, 9), // "operation"
QT_MOC_LITERAL(9, 102, 5), // "stage"
QT_MOC_LITERAL(10, 108, 17), // "identityRefreshed"
QT_MOC_LITERAL(11, 126, 11), // "deviceIndex"
QT_MOC_LITERAL(12, 138, 14), // "DeviceIdentity"
QT_MOC_LITERAL(13, 153, 8), // "identity"
QT_MOC_LITERAL(14, 162, 8), // "finished"
QT_MOC_LITERAL(15, 171, 10), // "successful"
QT_MOC_LITERAL(16, 182, 14), // "stageOperation"
QT_MOC_LITERAL(17, 197, 3) // "run"

    },
    "WorkflowWorker\0logMessage\0\0message\0"
    "transportLogMessage\0progressChanged\0"
    "percent\0stageChanged\0operation\0stage\0"
    "identityRefreshed\0deviceIndex\0"
    "DeviceIdentity\0identity\0finished\0"
    "successful\0stageOperation\0run"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_WorkflowWorker[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x06 /* Public */,
       4,    1,   52,    2, 0x06 /* Public */,
       5,    1,   55,    2, 0x06 /* Public */,
       7,    2,   58,    2, 0x06 /* Public */,
      10,    2,   63,    2, 0x06 /* Public */,
      14,    3,   68,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      17,    0,   75,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    8,    9,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 12,   11,   13,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString, QMetaType::QString,   15,   16,    9,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void WorkflowWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<WorkflowWorker *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->logMessage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 1: _t->transportLogMessage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->progressChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->stageChanged((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2]))); break;
        case 4: _t->identityRefreshed((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< DeviceIdentity(*)>(_a[2]))); break;
        case 5: _t->finished((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2])),(*reinterpret_cast< QString(*)>(_a[3]))); break;
        case 6: _t->run(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 1:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DeviceIdentity >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (WorkflowWorker::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WorkflowWorker::logMessage)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (WorkflowWorker::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WorkflowWorker::transportLogMessage)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (WorkflowWorker::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WorkflowWorker::progressChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (WorkflowWorker::*)(QString , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WorkflowWorker::stageChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (WorkflowWorker::*)(int , DeviceIdentity );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WorkflowWorker::identityRefreshed)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (WorkflowWorker::*)(bool , QString , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WorkflowWorker::finished)) {
                *result = 5;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject WorkflowWorker::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_WorkflowWorker.data,
    qt_meta_data_WorkflowWorker,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *WorkflowWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WorkflowWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_WorkflowWorker.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int WorkflowWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void WorkflowWorker::logMessage(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void WorkflowWorker::transportLogMessage(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void WorkflowWorker::progressChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void WorkflowWorker::stageChanged(QString _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void WorkflowWorker::identityRefreshed(int _t1, DeviceIdentity _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void WorkflowWorker::finished(bool _t1, QString _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}
struct qt_meta_stringdata_PingWorker_t {
    QByteArrayData data[9];
    char stringdata0[78];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_PingWorker_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_PingWorker_t qt_meta_stringdata_PingWorker = {
    {
QT_MOC_LITERAL(0, 0, 10), // "PingWorker"
QT_MOC_LITERAL(1, 11, 8), // "pingLine"
QT_MOC_LITERAL(2, 20, 0), // ""
QT_MOC_LITERAL(3, 21, 7), // "message"
QT_MOC_LITERAL(4, 29, 19), // "transportLogMessage"
QT_MOC_LITERAL(5, 49, 8), // "finished"
QT_MOC_LITERAL(6, 58, 5), // "start"
QT_MOC_LITERAL(7, 64, 4), // "stop"
QT_MOC_LITERAL(8, 69, 8) // "pingOnce"

    },
    "PingWorker\0pingLine\0\0message\0"
    "transportLogMessage\0finished\0start\0"
    "stop\0pingOnce"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PingWorker[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   44,    2, 0x06 /* Public */,
       4,    1,   47,    2, 0x06 /* Public */,
       5,    0,   50,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       6,    0,   51,    2, 0x0a /* Public */,
       7,    0,   52,    2, 0x0a /* Public */,
       8,    0,   53,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void PingWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<PingWorker *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->pingLine((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 1: _t->transportLogMessage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->finished(); break;
        case 3: _t->start(); break;
        case 4: _t->stop(); break;
        case 5: _t->pingOnce(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (PingWorker::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PingWorker::pingLine)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (PingWorker::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PingWorker::transportLogMessage)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (PingWorker::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PingWorker::finished)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject PingWorker::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_PingWorker.data,
    qt_meta_data_PingWorker,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *PingWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PingWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PingWorker.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int PingWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void PingWorker::pingLine(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void PingWorker::transportLogMessage(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void PingWorker::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
