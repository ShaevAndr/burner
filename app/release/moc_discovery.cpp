/****************************************************************************
** Meta object code from reading C++ file 'discovery.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.12)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/discovery.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'discovery.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.12. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_IDiscoveryStrategy_t {
    QByteArrayData data[8];
    char stringdata0[82];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_IDiscoveryStrategy_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_IDiscoveryStrategy_t qt_meta_stringdata_IDiscoveryStrategy = {
    {
QT_MOC_LITERAL(0, 0, 18), // "IDiscoveryStrategy"
QT_MOC_LITERAL(1, 19, 11), // "deviceFound"
QT_MOC_LITERAL(2, 31, 0), // ""
QT_MOC_LITERAL(3, 32, 14), // "DeviceIdentity"
QT_MOC_LITERAL(4, 47, 6), // "device"
QT_MOC_LITERAL(5, 54, 10), // "logMessage"
QT_MOC_LITERAL(6, 65, 7), // "message"
QT_MOC_LITERAL(7, 73, 8) // "finished"

    },
    "IDiscoveryStrategy\0deviceFound\0\0"
    "DeviceIdentity\0device\0logMessage\0"
    "message\0finished"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_IDiscoveryStrategy[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   29,    2, 0x06 /* Public */,
       5,    1,   32,    2, 0x06 /* Public */,
       7,    0,   35,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void,

       0        // eod
};

void IDiscoveryStrategy::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<IDiscoveryStrategy *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->deviceFound((*reinterpret_cast< DeviceIdentity(*)>(_a[1]))); break;
        case 1: _t->logMessage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->finished(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DeviceIdentity >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (IDiscoveryStrategy::*)(DeviceIdentity );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&IDiscoveryStrategy::deviceFound)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (IDiscoveryStrategy::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&IDiscoveryStrategy::logMessage)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (IDiscoveryStrategy::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&IDiscoveryStrategy::finished)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject IDiscoveryStrategy::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_IDiscoveryStrategy.data,
    qt_meta_data_IDiscoveryStrategy,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *IDiscoveryStrategy::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *IDiscoveryStrategy::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_IDiscoveryStrategy.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int IDiscoveryStrategy::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void IDiscoveryStrategy::deviceFound(DeviceIdentity _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void IDiscoveryStrategy::logMessage(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void IDiscoveryStrategy::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
struct qt_meta_stringdata_UdpBroadcastDiscovery_t {
    QByteArrayData data[3];
    char stringdata0[44];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_UdpBroadcastDiscovery_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_UdpBroadcastDiscovery_t qt_meta_stringdata_UdpBroadcastDiscovery = {
    {
QT_MOC_LITERAL(0, 0, 21), // "UdpBroadcastDiscovery"
QT_MOC_LITERAL(1, 22, 20), // "readPendingDatagrams"
QT_MOC_LITERAL(2, 43, 0) // ""

    },
    "UdpBroadcastDiscovery\0readPendingDatagrams\0"
    ""
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_UdpBroadcastDiscovery[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   19,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void UdpBroadcastDiscovery::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<UdpBroadcastDiscovery *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->readPendingDatagrams(); break;
        default: ;
        }
    }
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject UdpBroadcastDiscovery::staticMetaObject = { {
    &IDiscoveryStrategy::staticMetaObject,
    qt_meta_stringdata_UdpBroadcastDiscovery.data,
    qt_meta_data_UdpBroadcastDiscovery,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *UdpBroadcastDiscovery::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *UdpBroadcastDiscovery::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_UdpBroadcastDiscovery.stringdata0))
        return static_cast<void*>(this);
    return IDiscoveryStrategy::qt_metacast(_clname);
}

int UdpBroadcastDiscovery::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = IDiscoveryStrategy::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 1;
    }
    return _id;
}
struct qt_meta_stringdata_Rs485Discovery_t {
    QByteArrayData data[1];
    char stringdata0[15];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Rs485Discovery_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Rs485Discovery_t qt_meta_stringdata_Rs485Discovery = {
    {
QT_MOC_LITERAL(0, 0, 14) // "Rs485Discovery"

    },
    "Rs485Discovery"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Rs485Discovery[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void Rs485Discovery::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject Rs485Discovery::staticMetaObject = { {
    &IDiscoveryStrategy::staticMetaObject,
    qt_meta_stringdata_Rs485Discovery.data,
    qt_meta_data_Rs485Discovery,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *Rs485Discovery::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Rs485Discovery::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Rs485Discovery.stringdata0))
        return static_cast<void*>(this);
    return IDiscoveryStrategy::qt_metacast(_clname);
}

int Rs485Discovery::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = IDiscoveryStrategy::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
