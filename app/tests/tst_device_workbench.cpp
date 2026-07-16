#include <QtTest>
#include <QCoreApplication>
#include <QDate>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSignalSpy>
#include <QTime>
#include <QUuid>

#include "../src/action_repository.h"
#include "../src/catalog.h"
#include "../src/transport/unicorn_ascii_transport.h"
#include "../src/workflow.h"

class FakeDeviceTransport : public IDeviceTransport
{
public:
    struct WriteCall
    {
        quint16 index = 0;
        qint32 value = 0;
    };

    struct ReadCall
    {
        quint16 index = 0;
    };

    bool writeRegister(const DeviceIdentity&, quint16 index, qint32 value, QString*, QString* rawResponse = nullptr) override
    {
        writes.append(WriteCall{index, value});
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :000000E20000000000000000\\r\nRX !000000E20000000000000000\\r");
        return true;
    }

    bool readRegister(const DeviceIdentity&, quint16 index, qint32* value, QString*, QString* rawResponse = nullptr) override
    {
        reads.append(ReadCall{index});
        if (value)
            *value = 1234;
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :000000EB0000000000\\r\nRX !000000EB00000000000004D2\\r");
        return true;
    }

    bool resetDevice(const DeviceIdentity&, QString*, QString* rawResponse = nullptr) override
    {
        resetCalls++;
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :01FA55AA1234B0\\r\nRX !01FA0000\\r");
        return true;
    }

    QVector<WriteCall> writes;
    QVector<ReadCall> reads;
    int resetCalls = 0;
};

static QString sourceConfigPath(const QString& relativePath)
{
    const QFileInfo sourceFile(QStringLiteral(__FILE__));
    const QStringList roots = {
        QDir::currentPath(),
        QDir::currentPath() + QStringLiteral("/app"),
        QDir::currentPath() + QStringLiteral("/app/tests"),
        QDir::currentPath() + QStringLiteral("/.."),
        QDir::currentPath() + QStringLiteral("/../.."),
        sourceFile.absoluteDir().absolutePath(),
        sourceFile.absoluteDir().filePath(QStringLiteral("..")),
        sourceFile.absoluteDir().filePath(QStringLiteral("../.."))
    };

    for (const QString& root : roots)
    {
        const QString candidate = QDir(root).filePath(relativePath);
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return QDir(sourceFile.absoluteDir()).filePath(relativePath);
}

static QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

class DeviceWorkbenchTest : public QObject
{
    Q_OBJECT

private slots:
    void catalogExposesBocV12Actions();
    void workflowEmitsProgressForTestFlash();
    void workflowEmitsProductionDateSequence();
    void workflowWritesProductionDateRegistersInOrder();
    void catalogDetectsDeviceState();
    void workflowWritesSerialNumberRegisterInBootloader();
    void unicornAsciiTransportFactoryCreatesTransport();
    void pingActionIsSingleDeviceOnly();
    void pingActionIsAvailableForUnknownDevices();
    void deviceReadIntDelegatesToTransport();
};

void DeviceWorkbenchTest::catalogExposesBocV12Actions()
{
    CatalogService catalog;
    ActionRepository actions;

    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));
    QVERIFY2(actions.load(sourceConfigPath(QStringLiteral("config/actions.json")), &error), qPrintable(error));

    DeviceIdentity device;
    device.type = 0x0A03;
    device.version = 0x0001;
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 15 2026 14:30:56)");

    device = catalog.enrich(device);
    QVERIFY(device.known);
    QCOMPARE(device.name, QStringLiteral("БОЦ-В-12"));
    QCOMPARE(device.expectedDescription, QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 15 2026 14:30:56)"));
    QCOMPARE(device.expectedDescriptionPattern, QStringLiteral("Блок обработки цифровой (БОЦ-В-12){boot} {year} {quarter} Зав.№{serial} (SW {sw})"));
    QVERIFY(!device.descriptionMismatch);

    DeviceIdentity variant = device;
    variant.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1972 III Зав.№915 (SW Aug 03 2027 08:11:12)");
    variant = catalog.enrich(variant);
    QVERIFY(variant.known);
    QVERIFY2(!variant.descriptionMismatch, "Year, quarter, and serial number must be ignored by the description matcher");

    DeviceIdentity bootloader = device;
    bootloader.type = 0x1001;
    bootloader.version = 0x0000;
    bootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) (Boot) 1970 I Зав.№902 (SW Jul 15 2026 14:30:56)");
    bootloader = catalog.enrich(bootloader);
    QVERIFY2(bootloader.known, "Bootloader identity should resolve to the same catalog entry");
    QCOMPARE(bootloader.catalogId, QStringLiteral("boc.v12"));

    DeviceIdentity legacy;
    legacy.type = 0x0031;
    legacy = catalog.enrich(legacy);
    QVERIFY(!legacy.known);

    const QVector<ActionSpec> available = actions.actionsForDevice(device);
    QCOMPARE(available.size(), 5);
    QCOMPARE(available.at(0).id, QStringLiteral("flash.application.write"));
    QCOMPARE(available.at(1).id, QStringLiteral("flash.bootloader.write"));
    QCOMPARE(available.at(2).id, QStringLiteral("device.productionDate.update"));
    QCOMPARE(available.at(3).id, QStringLiteral("device.serialNumber.update"));
    QCOMPARE(available.at(4).id, QStringLiteral("device.ping"));
}

void DeviceWorkbenchTest::workflowEmitsProgressForTestFlash()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QDir root(tempDir.path());
    QVERIFY(root.mkpath(QStringLiteral("flash/boc-v12")));

    const QString applicationPath = root.filePath(QStringLiteral("flash/boc-v12/application-test.bin"));
    const QString bootloaderPath = root.filePath(QStringLiteral("flash/boc-v12/bootloader-test.bin"));

    {
        QFile applicationFile(applicationPath);
        QVERIFY(applicationFile.open(QIODevice::WriteOnly));
        applicationFile.write("application-test", 16);
    }

    {
        QFile bootloaderFile(bootloaderPath);
        QVERIFY(bootloaderFile.open(QIODevice::WriteOnly));
        bootloaderFile.write("bootloader-test", 15);
    }

    const QString applicationHash = sha256Hex(QByteArray("application-test", 16));
    const QString bootloaderHash = sha256Hex(QByteArray("bootloader-test", 15));

    DeviceIdentity device;
    device.id = QStringLiteral("boc.v12");
    device.type = 0x0A03;
    device.version = 0x0001;
    device.known = true;
    device.name = QStringLiteral("БОЦ-В-12");
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12)");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("192.168.1.245:2001");
    device.flashWorkflows.insert(QStringLiteral("application"), QStringLiteral("flash.test"));
    device.flashWorkflows.insert(QStringLiteral("bootloader"), QStringLiteral("flash.test"));
    device.capabilities = QStringList{
        QStringLiteral("identity.read"),
        QStringLiteral("flash.application.write"),
        QStringLiteral("flash.bootloader.write")
    };
    device.firmwareArtifacts = {
        FirmwareArtifact{QStringLiteral("application"), QStringLiteral("test-1.0.0"), QStringLiteral("flash/boc-v12/application-test.bin"), applicationHash},
        FirmwareArtifact{QStringLiteral("bootloader"), QStringLiteral("test-1.0.0"), QStringLiteral("flash/boc-v12/bootloader-test.bin"), bootloaderHash}
    };

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.title = QStringLiteral("Write application");
    action.workflow = QStringLiteral("flash.write");
    action.target = QStringLiteral("application");

    const QString previousCurrentPath = QDir::currentPath();
    QDir::setCurrent(tempDir.path());

    DeviceFactory factory;
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRunner runner;
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    runner.run(action, {deviceObject});

    QDir::setCurrent(previousCurrentPath);

    bool sawProgress = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (row.isEmpty())
            continue;
        const QString message = row.at(0).toString();
        if (message.contains(QStringLiteral("progress"), Qt::CaseInsensitive))
        {
            sawProgress = true;
            break;
        }
    }

    QVERIFY2(sawProgress, "Expected simulated flash progress log messages");
}

void DeviceWorkbenchTest::workflowEmitsProductionDateSequence()
{
    DeviceIdentity device;
    device.id = QStringLiteral("boc.v12");
    device.type = 0x0A03;
    device.version = 0x0001;
    device.known = true;
    device.catalogId = QStringLiteral("boc.v12");
    device.name = QStringLiteral("БОЦ-В-12");
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12)");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("192.168.1.245:2001");
    device.modbusAddress = 7;

    ActionSpec action;
    action.id = QStringLiteral("device.productionDate.update");
    action.title = QStringLiteral("Обновить дату производства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRunner runner;
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    QSignalSpy progressSpy(&runner, &WorkflowRunner::progressChanged);

    runner.run(action, {deviceObject}, QVariantMap{{QStringLiteral("productionDate"), QDate(2026, 7, 16)}});

    bool sawBootloader = false;
    bool sawTimestampWrite = false;
    int int0Writes = 0;
    int int1Writes = 0;
    for (const QList<QVariant>& row : logSpy)
    {
        if (row.isEmpty())
            continue;
        const QString message = row.at(0).toString();
        if (message.contains(QStringLiteral("bootloader identity connected")))
            sawBootloader = true;
        if (message.contains(QStringLiteral("int[9] =")))
            sawTimestampWrite = true;
        if (message.contains(QStringLiteral("int[0] = 0")))
            ++int0Writes;
        if (message.contains(QStringLiteral("int[0] = 1")))
            ++int1Writes;
    }

    QVERIFY(sawBootloader);
    QVERIFY(sawTimestampWrite);
    QCOMPARE(int0Writes, 1);
    QCOMPARE(int1Writes, 1);
    QVERIFY(progressSpy.count() >= 5);
    QCOMPARE(transport->writes.size(), 3);
}

void DeviceWorkbenchTest::workflowWritesProductionDateRegistersInOrder()
{
    DeviceIdentity device;
    device.id = QStringLiteral("boc.v12");
    device.type = 0x0A03;
    device.version = 0x0001;
    device.known = true;
    device.catalogId = QStringLiteral("boc.v12");
    device.name = QStringLiteral("БОЦ-В-12");
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12)");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("192.168.1.245:2001");
    device.modbusAddress = 7;

    ActionSpec action;
    action.id = QStringLiteral("device.productionDate.update");
    action.title = QStringLiteral("Обновить дату производства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRunner runner;

    runner.run(action, {deviceObject}, QVariantMap{{QStringLiteral("productionDate"), QDate(2026, 7, 16)}});

    QCOMPARE(transport->writes.size(), 3);
    QCOMPARE(transport->resetCalls, 1);
    QCOMPARE(transport->writes.at(0).index, quint16(0));
    QCOMPARE(transport->writes.at(0).value, qint32(0));
    QCOMPARE(transport->writes.at(1).index, quint16(9));
    QCOMPARE(transport->writes.at(1).value, qint32(QDateTime(QDate(2026, 7, 16), QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch()));
    QCOMPARE(transport->writes.at(2).index, quint16(0));
    QCOMPARE(transport->writes.at(2).value, qint32(1));
}

void DeviceWorkbenchTest::catalogDetectsDeviceState()
{
    CatalogService catalog;

    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));

    DeviceIdentity application;
    application.type = 0x0A03;
    application.version = 0x0001;
    application.description = QStringLiteral("Р‘Р»РѕРє РѕР±СЂР°Р±РѕС‚РєРё С†РёС„СЂРѕРІРѕР№ (Р‘РћР¦-Р’-12) 1970 I Р—Р°РІ.в„–902 (SW Jul 15 2026 14:30:56)");
    application = catalog.enrich(application);
    QVERIFY(application.known);
    QCOMPARE(application.state, QStringLiteral("application"));

    DeviceIdentity bootloader;
    bootloader.type = 0x1001;
    bootloader.version = 0x0000;
    bootloader.description = QStringLiteral("Р‘Р»РѕРє РѕР±СЂР°Р±РѕС‚РєРё С†РёС„СЂРѕРІРѕР№ (Р‘РћР¦-Р’-12) (Boot) 1970 I Р—Р°РІ.в„–902 (SW Jul 15 2026 14:30:56)");
    bootloader = catalog.enrich(bootloader);
    QVERIFY(bootloader.known);
    QCOMPARE(bootloader.state, QStringLiteral("bootloader"));
}

void DeviceWorkbenchTest::workflowWritesSerialNumberRegisterInBootloader()
{
    DeviceIdentity device;
    device.id = QStringLiteral("boc.v12");
    device.type = 0x1001;
    device.version = 0x0000;
    device.known = true;
    device.catalogId = QStringLiteral("boc.v12");
    device.deviceClass = QStringLiteral("BocV12Device");
    device.name = QStringLiteral("Р‘РћР¦-Р’-12");
    device.state = QStringLiteral("bootloader");
    device.description = QStringLiteral("Р‘Р»РѕРє РѕР±СЂР°Р±РѕС‚РєРё С†РёС„СЂРѕРІРѕР№ (Р‘РћР¦-Р’-12) (Boot)");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("192.168.1.245:2001");
    device.modbusAddress = 7;

    ActionSpec action;
    action.id = QStringLiteral("device.serialNumber.update");
    action.title = QStringLiteral("Изменить номер устройства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRunner runner;

    runner.run(action, {deviceObject}, QVariantMap{{QStringLiteral("serialNumber"), 915}});

    QCOMPARE(transport->resetCalls, 0);
    QCOMPARE(transport->writes.size(), 3);
    QCOMPARE(transport->writes.at(0).index, quint16(0));
    QCOMPARE(transport->writes.at(0).value, qint32(0));
    QCOMPARE(transport->writes.at(1).index, quint16(10));
    QCOMPARE(transport->writes.at(1).value, qint32(915));
    QCOMPARE(transport->writes.at(2).index, quint16(0));
    QCOMPARE(transport->writes.at(2).value, qint32(1));
}

void DeviceWorkbenchTest::unicornAsciiTransportFactoryCreatesTransport()
{
    std::shared_ptr<IDeviceTransport> transport = createUnicornAsciiTransport();
    QVERIFY(transport);
}

void DeviceWorkbenchTest::pingActionIsSingleDeviceOnly()
{
    CatalogService catalog;
    ActionRepository actions;

    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));
    QVERIFY2(actions.load(sourceConfigPath(QStringLiteral("config/actions.json")), &error), qPrintable(error));

    DeviceIdentity identity;
    identity.type = 0x0A03;
    identity.version = 0x0001;
    identity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 15 2026 14:30:56)");
    identity = catalog.enrich(identity);

    const QVector<ActionSpec> rowActions = actions.actionsForDevice(identity);
    bool hasPing = false;
    for (const ActionSpec& action : rowActions)
        hasPing = hasPing || action.id == QStringLiteral("device.ping");
    QVERIFY(hasPing);

    DeviceFactory factory(std::make_shared<FakeDeviceTransport>());
    std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    const QVector<ActionSpec> bulkActions = actions.commonActions({device});
    for (const ActionSpec& action : bulkActions)
        QVERIFY2(action.id != QStringLiteral("device.ping"), "Ping must be unavailable in bulk actions");
}

void DeviceWorkbenchTest::pingActionIsAvailableForUnknownDevices()
{
    ActionRepository actions;

    QString error;
    QVERIFY2(actions.load(sourceConfigPath(QStringLiteral("config/actions.json")), &error), qPrintable(error));

    DeviceIdentity identity;
    identity.id = QStringLiteral("unknown");
    identity.type = 0x0F0C;
    identity.version = 0x0004;
    identity.known = false;

    const QVector<ActionSpec> rowActions = actions.actionsForDevice(identity);
    bool hasPing = false;
    for (const ActionSpec& action : rowActions)
        hasPing = hasPing || action.id == QStringLiteral("device.ping");
    QVERIFY2(hasPing, "Unknown devices must still expose per-device ping");

    DeviceFactory factory(std::make_shared<FakeDeviceTransport>());
    std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    const QVector<ActionSpec> bulkActions = actions.commonActions({device});
    for (const ActionSpec& action : bulkActions)
        QVERIFY2(action.id != QStringLiteral("device.ping"), "Unknown-device ping must stay unavailable in bulk actions");
}

void DeviceWorkbenchTest::deviceReadIntDelegatesToTransport()
{
    DeviceIdentity identity;
    identity.type = 0x0A03;
    identity.version = 0x0001;
    identity.known = true;
    identity.catalogId = QStringLiteral("boc.v12");
    identity.deviceClass = QStringLiteral("BocV12Device");
    identity.modbusAddress = 7;
    identity.endpoint = QStringLiteral("192.168.1.245:2001");

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    qint32 value = 0;
    QString error;
    QString raw;
    QVERIFY(device->readInt(0, &value, &error, &raw));
    QCOMPARE(value, qint32(1234));
    QCOMPARE(transport->reads.size(), 1);
    QCOMPARE(transport->reads.at(0).index, quint16(0));
}

QTEST_APPLESS_MAIN(DeviceWorkbenchTest)

#include "tst_device_workbench.moc"
