#include <QtTest>
#include <QCoreApplication>
#include <QDate>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTime>
#include <QUuid>

#include <algorithm>

#include "../src/action_repository.h"
#include "../src/catalog.h"
#include "../src/device.h"
#include "../src/firmware_access_policy.h"
#include "../src/transport/unicorn_ascii_transport.h"
#include "../src/workflow.h"
#include "../src/workers.h"

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

    struct FlashPageCall
    {
        int flashNum = 0;
        int pageNum = 0;
        QByteArray page;
    };

    bool writeRegister(const DeviceIdentity&, quint16 index, qint32 value, QString* error, QString* rawResponse = nullptr) override
    {
        ++writeAttempts;
        const QString configuredError = writeErrorsByAttempt.value(writeAttempts);
        if (!configuredError.isEmpty())
        {
            if (error)
                *error = configuredError;
            if (rawResponse)
                *rawResponse = QStringLiteral("TX :01E2...\\r\nRX ?01E20B89\\r");
            return false;
        }
        if (failWrites)
        {
            if (error)
                *error = QStringLiteral("regular write should not be used");
            return false;
        }
        writes.append(WriteCall{index, value});
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :000000E20000000000000000\\r\nRX !000000E20000000000000000\\r");
        return true;
    }

    bool writeRegisterNoReply(const DeviceIdentity&, quint16 index, qint32 value, QString*, QString* rawResponse = nullptr) override
    {
        if (noReplyWriteDelayMs > 0)
            QThread::msleep(static_cast<unsigned long>(noReplyWriteDelayMs));
        noReplyWrites.append(WriteCall{index, value});
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :000000E20000000000000000\\r\nRX <not expected>");
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

    bool readUuid(const DeviceIdentity& device, QString* uuid, QString*, QString* rawResponse = nullptr) override
    {
        uuidReadCalls++;
        if (uuid)
            *uuid = device.uuid.isEmpty() ? uuidValue : device.uuid;
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :010702\\r\nRX !0107410FC2413431384D123536353133584F99\\r");
        return true;
    }

    bool flashGetParams(const DeviceIdentity&, QVector<FlashMemoryParams>* params, QString*, QString* rawResponse = nullptr) override
    {
        if (params)
            *params = flashParams;
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :0745CC\\r\nRX !07450000000400000800\\r");
        return true;
    }

    bool flashWritePage(const DeviceIdentity&, int flashNum, int pageNum, const QByteArray& page, QString*, QString* rawResponse = nullptr) override
    {
        flashWrites.append(FlashPageCall{flashNum, pageNum, page});
        flashPages.insert(pageNum, page);
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :0743...\\r\nRX !0743...\\r");
        return true;
    }

    bool flashReadPage(const DeviceIdentity&, int flashNum, int pageNum, QByteArray* page, QString*, QString* rawResponse = nullptr) override
    {
        flashReads.append(FlashPageCall{flashNum, pageNum, QByteArray()});
        if (page)
            *page = flashPages.value(pageNum);
        if (rawResponse)
            *rawResponse = QStringLiteral("TX :0744...\\r\nRX !0744...\\r");
        return true;
    }

    bool waitForDeviceIdentity(const DeviceIdentity& expected, int, int, DeviceIdentity* identity, QString*, QString* rawResponse = nullptr) override
    {
        waitForIdentityCalls++;
        waitExpectedIdentities.append(expected);
        DeviceIdentity found = discoveredIdentity;
        if (waitForIdentityCalls <= discoveredIdentities.size())
            found = discoveredIdentities.at(waitForIdentityCalls - 1);
        if (found.uuid.isEmpty())
            found.uuid = expected.uuid;
        if (identity)
            *identity = found;
        if (rawResponse)
            *rawResponse = QStringLiteral("discovered %1 %2").arg(found.typeHex(), found.description);
        return waitForIdentityCalls <= waitForIdentityResults.size()
            ? waitForIdentityResults.at(waitForIdentityCalls - 1)
            : waitForIdentityResult;
    }

    QVector<WriteCall> writes;
    QVector<WriteCall> noReplyWrites;
    QVector<ReadCall> reads;
    QVector<FlashMemoryParams> flashParams;
    QVector<FlashPageCall> flashWrites;
    QVector<FlashPageCall> flashReads;
    QHash<int, QByteArray> flashPages;
    DeviceIdentity discoveredIdentity;
    QVector<DeviceIdentity> discoveredIdentities;
    QVector<bool> waitForIdentityResults;
    QVector<DeviceIdentity> waitExpectedIdentities;
    int resetCalls = 0;
    int writeAttempts = 0;
    int waitForIdentityCalls = 0;
    int uuidReadCalls = 0;
    QString uuidValue = QStringLiteral("410FC241-3431-384D-1235-36353133584F");
    bool failWrites = false;
    bool waitForIdentityResult = true;
    int noReplyWriteDelayMs = 0;
    QHash<int, QString> writeErrorsByAttempt;
};

class TransportReadThread : public QThread
{
public:
    DeviceIdentity identity;
    std::shared_ptr<IDeviceTransport> transport;
    bool ok = false;
    qint32 value = 0;
    QString error;
    QString raw;
    qint64 elapsedMs = 0;

protected:
    void run() override
    {
        QElapsedTimer timer;
        timer.start();
        ok = transport->readRegister(identity, 0, &value, &error, &raw);
        elapsedMs = timer.elapsed();
    }
};

class TransportWriteThread : public QThread
{
public:
    DeviceIdentity identity;
    std::shared_ptr<IDeviceTransport> transport;
    bool ok = false;
    QString error;
    QString raw;

protected:
    void run() override
    {
        ok = transport->writeRegister(identity, 9, 123, &error, &raw);
    }
};

class TransportUuidThread : public QThread
{
public:
    DeviceIdentity identity;
    std::shared_ptr<IDeviceTransport> transport;
    bool ok = false;
    QString uuid;
    QString error;
    QString raw;

protected:
    void run() override
    {
        ok = transport->readUuid(identity, &uuid, &error, &raw);
    }
};

class TransportIdentityDescriptionThread : public QThread
{
public:
    DeviceIdentity identity;
    std::shared_ptr<IDeviceTransport> transport;
    bool ok = false;
    quint16 type = 0;
    quint16 version = 0;
    QString description;
    QString error;

protected:
    void run() override
    {
        ok = transport->readIdentityDescription(
            identity, &type, &version, &description, &error);
    }
};

class TransportExtendedDescriptionThread : public QThread
{
public:
    DeviceIdentity identity;
    std::shared_ptr<IDeviceTransport> transport;
    bool ok = false;
    QByteArray description;
    QVector<int> progress;
    QString error;

protected:
    void run() override
    {
        ok = transport->readExtendedDescription(identity, &description,
            [this](int value) { progress.append(value); }, &error);
    }
};

static QString sourceConfigPath(const QString& relativePath)
{
    const QFileInfo sourceFile(QStringLiteral(__FILE__));
    static const QString initialCurrentPath = QDir::currentPath();
    const QStringList roots = {
        initialCurrentPath,
        initialCurrentPath + QStringLiteral("/app"),
        initialCurrentPath + QStringLiteral("/app/tests"),
        initialCurrentPath + QStringLiteral("/.."),
        initialCurrentPath + QStringLiteral("/../.."),
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
    void embeddedPayloadIsAvailable();
    void catalogExposesBocV12Actions();
    void catalogRecognizesBocV6();
    void workflowEmitsProgressForTestFlash();
    void workflowEmitsProductionDateSequence();
    void workflowWritesProductionDateRegistersInOrder();
    void workflowRestoresApplicationAfterProductionDateFailure();
    void workflowSkipsProtectedSettingsWithoutFactoryKey();
    void workflowWorkerRunsDevicesInParallel();
    void catalogDetectsDeviceState();
    void workflowWritesSerialNumberRegisterInBootloader();
    void applicationLoadActionIsAvailableForBootloader();
    void workflowLoadsApplicationFromBootloaderWithoutWaitingForReply();
    void workflowWritesApplicationFlashPagesFromBootloader();
    void bootloaderWorkflowWritesAndVerifiesFromApplication();
    void workflowParsesIntelHexBeforeWriting();
    void workflowLoadsConfiguredBocV6Firmware();
    void workflowRejectsBootloaderWithDifferentUuid();
    void workflowExecutesAllowedFirmwareTransition();
    void knownDeviceAllowsEveryFirmwareWhenCurrentVersionIsUnknown();
    void workflowRejectsDisabledFirmwareTransition();
    void unicornAsciiTransportFactoryCreatesTransport();
    void pingActionIsSingleDeviceOnly();
    void pingActionIsAvailableForUnknownDevices();
    void deviceBaseWritesConfiguredServiceRegisters();
    void deviceReadIntDelegatesToTransport();
    void unicornAsciiReadRegisterReturnsAfterFirstValidResponse();
    void unicornAsciiPreservesDeviceErrorResponse();
    void unicornAsciiReadsUuid();
    void unicornAsciiReadsFullIdentityDescription();
    void unicornAsciiReadsExtendedDescription();
    void networkReadsDeviceDataBocV6();
    void networkChangeSerialNumberBocV6();
    void networkPipelineBocV6();
};

void DeviceWorkbenchTest::embeddedPayloadIsAvailable()
{
    CatalogService catalog;
    ActionRepository actions;
    WorkflowRepository workflows;
    QString error;

    QVERIFY2(catalog.load(QStringLiteral(":/config/device-catalog.json"), &error), qPrintable(error));
    QVERIFY2(actions.load(QStringLiteral(":/config/actions.json"), &error), qPrintable(error));
    QVERIFY2(workflows.load(QStringLiteral(":/config/workflows.json"), &error), qPrintable(error));

    const WorkflowDefinition* bootloaderWorkflow = workflows.definitionForId(
        QStringLiteral("firmware.bootloader.direct"));
    QVERIFY(bootloaderWorkflow);
    QStringList bootloaderOperations;
    for (const WorkflowStep& step : bootloaderWorkflow->steps)
        bootloaderOperations.append(step.op);
    QCOMPARE(bootloaderOperations, QStringList({
        QStringLiteral("flash.prepare"),
        QStringLiteral("flash.validateArtifact"),
        QStringLiteral("firmware.flash"),
        QStringLiteral("firmware.verify"),
        QStringLiteral("workflow.finish")
    }));

    DeviceIdentity identity;
    identity.type = 0x0A03;
    identity.version = 0x0001;
    identity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");
    identity = catalog.enrich(identity);
    QVERIFY(identity.known);

    const FirmwareArtifact artifact = identity.firmwareForTarget(QStringLiteral("application"));
    QVERIFY(!artifact.relativePath.isEmpty());
    QFile firmware(QStringLiteral(":/") + artifact.relativePath);
    QVERIFY2(firmware.open(QIODevice::ReadOnly), qPrintable(firmware.errorString()));
    const QByteArray data = firmware.readAll();
    QVERIFY(!data.isEmpty());
    QVERIFY2(sha256Hex(data).compare(artifact.sha256, Qt::CaseInsensitive) == 0,
        "Embedded firmware SHA-256 does not match the catalog");
}

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
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");

    device = catalog.enrich(device);
    QVERIFY(device.known);
    QCOMPARE(device.name, QStringLiteral("БОЦ-В-12"));
    QCOMPARE(device.descriptionKeywords, QStringList{QStringLiteral("БОЦ-В-12")});
    QCOMPARE(device.currentFirmwareId, QStringLiteral("sw-2026-07-16-09-24-19"));
    QCOMPARE(device.productionDateRegister, 9);
    QCOMPARE(device.serialNumberRegister, 10);
    QCOMPARE(device.firmwareVersions.size(), 3);
    QCOMPARE(device.firmwareTransitions.size(), 2);
    QVERIFY(!device.allowUnknownCurrentFirmware);
    QVERIFY2(device.capabilities.contains(QStringLiteral("flash.bootloader.write")),
        "A recognized model with a bootloader artifact must receive the flash action automatically");

    DeviceIdentity variant = device;
    variant.type = 0;
    variant.version = 0;
    variant.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1972 III Зав.№915 (SW Aug 03 2027 08:11:12)");
    variant = catalog.enrich(variant);
    QVERIFY2(variant.known, "Device must fall back to description keywords when type/version are unavailable");
    QCOMPARE(variant.catalogId, QStringLiteral("boc.v12"));

    DeviceIdentity bootloader = device;
    bootloader.type = 0x1001;
    bootloader.version = 0x0000;
    bootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) (Boot) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");
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
    QCOMPARE(available.at(0).workflow, QStringLiteral("firmware.application.standard"));
    QCOMPARE(available.at(1).id, QStringLiteral("flash.bootloader.write"));
    QCOMPARE(available.at(1).workflow, QStringLiteral("firmware.bootloader.direct"));
    QCOMPARE(available.at(2).id, QStringLiteral("device.productionDate.update"));
    QCOMPARE(available.at(3).id, QStringLiteral("device.serialNumber.update"));
    QCOMPARE(available.at(4).id, QStringLiteral("device.ping"));

    const FirmwareArtifact defaultApplication = device.firmwareForTarget(QStringLiteral("application"));
    QVERIFY(defaultApplication.isDefault);
    QCOMPARE(defaultApplication.relativePath, QStringLiteral("flash/boc-v12/BOCv12_ADCVibr_Digital20260831_1800.hex"));
    QCOMPARE(defaultApplication.flashNum, 0);

    const FirmwareArtifact bootloaderArtifact = device.firmwareForTarget(QStringLiteral("bootloader"));
    QCOMPARE(bootloaderArtifact.relativePath,
        QStringLiteral("flash/boc-v12/bootloader/BOCv12_GD32F470Z_BootLoader20260820_150326.hex"));
    QCOMPARE(bootloaderArtifact.sha256,
        QStringLiteral("35D88F87F3B0529CE606E778C0A4B12DDB0A1B31AF39C22C1DFC81571AD183B0"));
    QCOMPARE(bootloaderArtifact.flashStrategy, QStringLiteral("page-flash"));
    QCOMPARE(bootloaderArtifact.allowedFromFirmwareIds, QStringList({
        QStringLiteral("sw-2026-07-08-12-51-18"),
        QStringLiteral("sw-2026-07-16-09-24-19"),
        QStringLiteral("sw-2026-08-31-17-24-51")
    }));

    const QString latestFirmwareId = QStringLiteral("sw-2026-08-31-17-24-51");
    QVERIFY(device.isFirmwareTargetAllowed(latestFirmwareId));
    QVERIFY(!device.isFirmwareTargetAllowed(QStringLiteral("sw-2026-07-16-09-24-19")));

    DeviceIdentity july8 = device;
    july8.description = QStringLiteral(
        "Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 08 2026 12:51:18)");
    july8 = catalog.enrich(july8);
    QCOMPARE(july8.currentFirmwareId, QStringLiteral("sw-2026-07-08-12-51-18"));
    QVERIFY(july8.isFirmwareTargetAllowed(latestFirmwareId));

    DeviceIdentity latest = device;
    latest.description = QStringLiteral(
        "Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Aug 31 2026 17:24:51)");
    latest = catalog.enrich(latest);
    QCOMPARE(latest.currentFirmwareId, latestFirmwareId);
    QVERIFY(!latest.isFirmwareTargetAllowed(latestFirmwareId));

    DeviceIdentity unknownFirmware = device;
    unknownFirmware.description = QStringLiteral(
        "Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Sep 01 2026 00:00:00)");
    unknownFirmware = catalog.enrich(unknownFirmware);
    QVERIFY(unknownFirmware.currentFirmwareId.isEmpty());
    QVERIFY(!unknownFirmware.isFirmwareTargetAllowed(latestFirmwareId));
}

void DeviceWorkbenchTest::catalogRecognizesBocV6()
{
    CatalogService catalog;
    ActionRepository actions;
    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));
    QVERIFY2(actions.load(sourceConfigPath(QStringLiteral("config/actions.json")), &error), qPrintable(error));

    DeviceIdentity device;
    device.type = 0x0A02;
    device.version = 0x4321;
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    device.serialNumber = QStringLiteral("000");
    device.endpoint = QStringLiteral("192.168.1.90:2001");
    device = catalog.enrich(device);

    QVERIFY(device.known);
    QCOMPARE(device.version, quint16(0x4321));
    QCOMPARE(device.catalogId, QStringLiteral("boc.v6"));
    QCOMPARE(device.name, QStringLiteral("БОЦ-В-6"));
    QCOMPARE(device.deviceClass, QStringLiteral("DeviceBase"));
    QCOMPARE(device.descriptionKeywords, QStringList{QStringLiteral("БОЦ-В-6")});
    QCOMPARE(device.currentFirmwareId, QStringLiteral("sw-2026-08-31-10-01-24"));
    QCOMPARE(device.bootloaderType, quint16(0x1000));
    QCOMPARE(device.bootloaderVersion, quint16(0x0000));
    QCOMPARE(device.productionDateRegister, 9);
    QCOMPARE(device.serialNumberRegister, 10);
    QCOMPARE(device.firmwareVersions.size(), 2);
    QCOMPARE(device.firmwareTransitions.size(), 4);

    const QString newFirmwareId = QStringLiteral("sw-2026-08-31-10-01-24");
    const QString previousFirmwareId = QStringLiteral("sw-2026-07-21-11-59-00");
    const FirmwareTransitionSpec* transitionFromCurrent = device.transitionTo(newFirmwareId);
    QVERIFY(transitionFromCurrent);
    QVERIFY(transitionFromCurrent->enabled);
    const FirmwareVersionSpec* targetFirmware = device.firmwareVersionById(newFirmwareId);
    QVERIFY(targetFirmware);
    QCOMPARE(targetFirmware->installation.workflow, QStringLiteral("firmware.application.standard"));
    QCOMPARE(targetFirmware->installation.strategy, QStringLiteral("page-flash"));
    QVERIFY(targetFirmware->artifact.isDefault);

    const FirmwareTransitionSpec* rollback = device.transitionTo(previousFirmwareId);
    QVERIFY(rollback);
    QVERIFY(rollback->enabled);

    DeviceIdentity previousFirmware = device;
    previousFirmware.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Jul 21 2026 11:59:00)");
    previousFirmware = catalog.enrich(previousFirmware);
    QCOMPARE(previousFirmware.currentFirmwareId, previousFirmwareId);
    const FirmwareTransitionSpec* upgrade = previousFirmware.transitionTo(newFirmwareId);
    QVERIFY(upgrade);
    QVERIFY(upgrade->enabled);

    DeviceIdentity newFirmware = device;
    newFirmware.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    newFirmware = catalog.enrich(newFirmware);
    QCOMPARE(newFirmware.currentFirmwareId, newFirmwareId);

    DeviceIdentity bootloader;
    bootloader.type = 0x1000;
    bootloader.version = 0x1234;
    bootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) (Boot) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    bootloader = catalog.enrich(bootloader);
    QVERIFY2(bootloader.known, "Generic bootloader identity must be resolved from its description");
    QCOMPARE(bootloader.catalogId, QStringLiteral("boc.v6"));
    QCOMPARE(bootloader.state, QStringLiteral("bootloader"));

    DeviceIdentity bootloaderWithUnexpectedIdentity;
    bootloaderWithUnexpectedIdentity.type = 0x7777;
    bootloaderWithUnexpectedIdentity.version = 0x0002;
    bootloaderWithUnexpectedIdentity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) (Boot) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    bootloaderWithUnexpectedIdentity = catalog.enrich(bootloaderWithUnexpectedIdentity);
    QVERIFY2(bootloaderWithUnexpectedIdentity.known,
        "Bootloader with unexpected type/version must be resolved from its description");
    QCOMPARE(bootloaderWithUnexpectedIdentity.catalogId, QStringLiteral("boc.v6"));
    QCOMPARE(bootloaderWithUnexpectedIdentity.state, QStringLiteral("bootloader"));

    DeviceIdentity unrelatedBootloader;
    unrelatedBootloader.type = 0x7777;
    unrelatedBootloader.version = 0x0002;
    unrelatedBootloader.description = QStringLiteral("Неизвестное устройство (Boot) 1970 I Зав.№000 (SW Aug 20 2026 16:26:49)");
    unrelatedBootloader = catalog.enrich(unrelatedBootloader);
    QVERIFY2(!unrelatedBootloader.known, "Unrecognized identity and description keywords must remain unknown");

    const QVector<ActionSpec> available = actions.actionsForDevice(device);
    QCOMPARE(available.size(), 4);
    QCOMPARE(available.at(0).id, QStringLiteral("flash.application.write"));
    QCOMPARE(available.at(1).id, QStringLiteral("device.productionDate.update"));
    QCOMPARE(available.at(2).id, QStringLiteral("device.serialNumber.update"));
    QCOMPARE(available.at(3).id, QStringLiteral("device.ping"));
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
    device.capabilities = QStringList{
        QStringLiteral("identity.read"),
        QStringLiteral("flash.application.write"),
        QStringLiteral("flash.bootloader.write")
    };
    FirmwareArtifact applicationArtifact;
    applicationArtifact.target = QStringLiteral("application");
    applicationArtifact.version = QStringLiteral("test-1.0.0");
    applicationArtifact.relativePath = QStringLiteral("flash/boc-v12/application-test.bin");
    applicationArtifact.sha256 = applicationHash;
    applicationArtifact.flashStrategy = QStringLiteral("test-no-write");
    FirmwareArtifact bootloaderArtifact;
    bootloaderArtifact.target = QStringLiteral("bootloader");
    bootloaderArtifact.version = QStringLiteral("test-1.0.0");
    bootloaderArtifact.relativePath = QStringLiteral("flash/boc-v12/bootloader-test.bin");
    bootloaderArtifact.sha256 = bootloaderHash;
    bootloaderArtifact.flashStrategy = QStringLiteral("test-no-write");
    device.firmwareArtifacts = {applicationArtifact, bootloaderArtifact};

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

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    QSignalSpy stageSpy(&runner, &WorkflowRunner::stageChanged);
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
    QVERIFY2(!stageSpy.isEmpty(), "Expected structured workflow stage notifications");
    QCOMPARE(stageSpy.first().at(0).toString(), QStringLiteral("flash.prepare"));
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
    device.productionDateRegister = 9;
    device.serialNumberRegister = 10;

    ActionSpec action;
    action.id = QStringLiteral("device.productionDate.update");
    action.title = QStringLiteral("Обновить дату производства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->waitForIdentityResults = {false, true};
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    QSignalSpy progressSpy(&runner, &WorkflowRunner::progressChanged);

    runner.run(action, {deviceObject}, QVariantMap{
        {QStringLiteral("productionDate"), QDate(2026, 7, 16)},
        {QStringLiteral("factorySettingsKey"), qint32(0x12345678)}
    });

    bool sawBootloader = false;
    bool sawTimestampWrite = false;
    bool sawLoadApplication = false;
    int int0Writes = 0;
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
        if (message.contains(QStringLiteral("load main application")))
            sawLoadApplication = true;
    }

    QVERIFY(sawBootloader);
    QVERIFY(sawTimestampWrite);
    QVERIFY(sawLoadApplication);
    QCOMPARE(int0Writes, 1);
    QVERIFY(!progressSpy.isEmpty());
    QVERIFY(progressSpy.first().at(0).toInt() > 0);
    QCOMPARE(progressSpy.last().at(0).toInt(), 100);
    QCOMPARE(transport->writes.size(), 2);
    QCOMPARE(transport->waitForIdentityCalls, 2);
    QCOMPARE(transport->noReplyWrites.size(), 2);
    QCOMPARE(transport->noReplyWrites.at(0).index, quint16(0));
    QCOMPARE(transport->noReplyWrites.at(0).value, qint32(1));
    QCOMPARE(transport->noReplyWrites.at(1).index, quint16(0));
    QCOMPARE(transport->noReplyWrites.at(1).value, qint32(1));
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
    device.productionDateRegister = 9;
    device.serialNumberRegister = 10;

    ActionSpec action;
    action.id = QStringLiteral("device.productionDate.update");
    action.title = QStringLiteral("Обновить дату производства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->writeErrorsByAttempt.insert(2, QStringLiteral("Device returned ASCII error 0x0b"));
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);

    QVERIFY(runner.run(action, {deviceObject}, QVariantMap{
        {QStringLiteral("productionDate"), QDate(2026, 7, 16)},
        {QStringLiteral("factorySettingsKey"), qint32(0x12345678)}
    }));

    QCOMPARE(transport->writes.size(), 2);
    QCOMPARE(transport->writeAttempts, 3);
    QCOMPARE(transport->resetCalls, 1);
    QCOMPARE(transport->writes.at(0).index, quint16(0));
    QCOMPARE(transport->writes.at(0).value, qint32(0));
    QCOMPARE(transport->writes.at(1).index, quint16(9));
    QCOMPARE(transport->writes.at(1).value, qint32(QDateTime(QDate(2026, 7, 16), QTime(0, 0), Qt::LocalTime).toSecsSinceEpoch()));
    QCOMPARE(transport->noReplyWrites.size(), 1);
    QCOMPARE(transport->noReplyWrites.first().index, quint16(0));
    QCOMPARE(transport->noReplyWrites.first().value, qint32(1));
}

void DeviceWorkbenchTest::workflowRestoresApplicationAfterProductionDateFailure()
{
    DeviceIdentity device;
    device.id = QStringLiteral("boc.v6");
    device.type = 0x0A02;
    device.version = 0x0001;
    device.applicationType = 0x0A02;
    device.applicationVersion = 0x0001;
    device.known = true;
    device.catalogId = QStringLiteral("boc.v6");
    device.name = QStringLiteral("БОЦ-В-6");
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6)");
    device.endpoint = QStringLiteral("192.168.1.254:2001");
    device.modbusAddress = 1;
    device.productionDateRegister = 9;

    ActionSpec action;
    action.id = QStringLiteral("device.productionDate.update");
    action.title = QStringLiteral("Обновить дату производства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    for (int attempt = 2; attempt <= 5; ++attempt)
        transport->writeErrorsByAttempt.insert(attempt, QStringLiteral("Device returned ASCII error 0x0b"));
    transport->discoveredIdentity = device;
    transport->discoveredIdentity.state = QStringLiteral("application");

    DeviceFactory factory(transport);
    const auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError),
        qPrintable(workflowError));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);

    QVERIFY(!runner.run(action, {deviceObject}, QVariantMap{
        {QStringLiteral("productionDate"), QDate(2026, 8, 25)},
        {QStringLiteral("factorySettingsKey"), qint32(0x12345678)}
    }));
    QCOMPARE(transport->writeAttempts, 5);
    QCOMPARE(transport->noReplyWrites.size(), 1);
    QCOMPARE(transport->noReplyWrites.first().value, qint32(1));
    QCOMPARE(transport->waitForIdentityCalls, 1);
    QCOMPARE(deviceObject->identity().state, QStringLiteral("application"));

    bool sawRecovery = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.first().toString().contains(QStringLiteral("main application restored")))
            sawRecovery = true;
    }
    QVERIFY2(sawRecovery, "Failed service-data update must restore the main application");
}

void DeviceWorkbenchTest::workflowSkipsProtectedSettingsWithoutFactoryKey()
{
    DeviceIdentity identity;
    identity.type = 0x0A02;
    identity.version = 0x0001;
    identity.endpoint = QStringLiteral("192.168.1.254:2001");
    identity.productionDateRegister = 9;
    identity.serialNumberRegister = 10;

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    const std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    WorkflowRepository workflows;
    QString error;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error),
        qPrintable(error));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);

    ActionSpec productionDateAction;
    productionDateAction.id = QStringLiteral("device.productionDate.update");
    QVERIFY(runner.run(productionDateAction, {device},
        QVariantMap{{QStringLiteral("productionDate"), QDate(2026, 8, 25)}}));

    ActionSpec serialNumberAction;
    serialNumberAction.id = QStringLiteral("device.serialNumber.update");
    QVERIFY(runner.run(serialNumberAction, {device},
        QVariantMap{{QStringLiteral("serialNumber"), 11},
            {QStringLiteral("factorySettingsKey"), QString()}}));

    QCOMPARE(transport->resetCalls, 0);
    QCOMPARE(transport->writes.size(), 0);
    QCOMPARE(transport->noReplyWrites.size(), 0);
    QCOMPARE(transport->waitForIdentityCalls, 0);

    int skippedMessages = 0;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.first().toString().contains(
                QStringLiteral("factory settings key is empty")))
            ++skippedMessages;
    }
    QCOMPARE(skippedMessages, 2);
}

void DeviceWorkbenchTest::workflowWorkerRunsDevicesInParallel()
{
    WorkflowRepository workflows;
    QString error;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error),
        qPrintable(error));

    DeviceIdentity firstIdentity;
    firstIdentity.type = 0x1000;
    firstIdentity.version = 0;
    firstIdentity.applicationType = 0x0A02;
    firstIdentity.applicationVersion = 1;
    firstIdentity.state = QStringLiteral("bootloader");
    firstIdentity.endpoint = QStringLiteral("192.168.1.254:2001");
    firstIdentity.uuid = QStringLiteral("11111111-1111-1111-1111-111111111111");

    DeviceIdentity secondIdentity = firstIdentity;
    secondIdentity.endpoint = QStringLiteral("192.168.1.193:2001");
    secondIdentity.uuid = QStringLiteral("22222222-2222-2222-2222-222222222222");

    auto firstTransport = std::make_shared<FakeDeviceTransport>();
    auto secondTransport = std::make_shared<FakeDeviceTransport>();
    firstTransport->noReplyWriteDelayMs = 700;
    secondTransport->noReplyWriteDelayMs = 700;
    firstTransport->discoveredIdentity = firstIdentity;
    secondTransport->discoveredIdentity = secondIdentity;
    firstTransport->discoveredIdentity.type = firstIdentity.applicationType;
    secondTransport->discoveredIdentity.type = secondIdentity.applicationType;
    firstTransport->discoveredIdentity.version = firstIdentity.applicationVersion;
    secondTransport->discoveredIdentity.version = secondIdentity.applicationVersion;
    firstTransport->discoveredIdentity.state = QStringLiteral("application");
    secondTransport->discoveredIdentity.state = QStringLiteral("application");

    DeviceFactory firstFactory(firstTransport);
    DeviceFactory secondFactory(secondTransport);
    const std::shared_ptr<DeviceBase> firstDevice = firstFactory.create(firstIdentity);
    const std::shared_ptr<DeviceBase> secondDevice = secondFactory.create(secondIdentity);
    QVERIFY(firstDevice);
    QVERIFY(secondDevice);

    ActionSpec action;
    action.id = QStringLiteral("device.application.load");
    WorkflowWorker worker(&workflows, action, {firstDevice, secondDevice}, {});
    QSignalSpy finishedSpy(&worker, &WorkflowWorker::finished);
    QElapsedTimer timer;
    timer.start();

    worker.run();

    QVERIFY2(timer.elapsed() < 1200,
        qPrintable(QStringLiteral("Two 700 ms device operations took %1 ms").arg(timer.elapsed())));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.first().at(0).toBool());
    QCOMPARE(firstTransport->noReplyWrites.size(), 1);
    QCOMPARE(secondTransport->noReplyWrites.size(), 1);
}

void DeviceWorkbenchTest::catalogDetectsDeviceState()
{
    CatalogService catalog;

    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));

    DeviceIdentity application;
    application.type = 0x0A03;
    application.version = 0x0001;
    application.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");
    application = catalog.enrich(application);
    QVERIFY(application.known);
    QCOMPARE(application.state, QStringLiteral("application"));

    DeviceIdentity bootloader;
    bootloader.type = 0x1001;
    bootloader.version = 0x0000;
    bootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) (Boot) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");
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
    device.productionDateRegister = 9;
    device.serialNumberRegister = 10;

    ActionSpec action;
    action.id = QStringLiteral("device.serialNumber.update");
    action.title = QStringLiteral("Изменить номер устройства");

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);

    runner.run(action, {deviceObject}, QVariantMap{
        {QStringLiteral("serialNumber"), 915},
        {QStringLiteral("factorySettingsKey"), qint32(0x12345678)}
    });

    QCOMPARE(transport->resetCalls, 0);
    QCOMPARE(transport->writes.size(), 2);
    QCOMPARE(transport->writes.at(0).index, quint16(0));
    QCOMPARE(transport->writes.at(0).value, qint32(0));
    QCOMPARE(transport->writes.at(1).index, quint16(10));
    QCOMPARE(transport->writes.at(1).value, qint32(915));
    QCOMPARE(transport->noReplyWrites.size(), 1);
    QCOMPARE(transport->noReplyWrites.first().index, quint16(0));
    QCOMPARE(transport->noReplyWrites.first().value, qint32(1));
    QCOMPARE(deviceObject->identity().serialNumber, QStringLiteral("915"));
}

void DeviceWorkbenchTest::applicationLoadActionIsAvailableForBootloader()
{
    CatalogService catalog;
    ActionRepository actions;

    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));
    QVERIFY2(actions.load(sourceConfigPath(QStringLiteral("config/actions.json")), &error), qPrintable(error));

    DeviceIdentity bootloader;
    bootloader.type = 0x1001;
    bootloader.version = 0x0000;
    bootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) (Boot) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");
    bootloader = catalog.enrich(bootloader);
    QVERIFY(bootloader.known);
    QCOMPARE(bootloader.state, QStringLiteral("bootloader"));

    bool hasLoadApplication = false;
    bool hasApplicationFlash = false;
    bool hasBootloaderFlash = false;
    for (const ActionSpec& action : actions.actionsForDevice(bootloader))
    {
        hasLoadApplication = hasLoadApplication || action.id == QStringLiteral("device.application.load");
        hasApplicationFlash = hasApplicationFlash || action.id == QStringLiteral("flash.application.write");
        hasBootloaderFlash = hasBootloaderFlash || action.id == QStringLiteral("flash.bootloader.write");
    }
    QVERIFY2(hasApplicationFlash, "Bootloader devices must expose application flash action");
    QVERIFY2(hasLoadApplication, "Bootloader devices must expose load application action");
    QVERIFY2(!hasBootloaderFlash,
        "Bootloader flash must only be available from the main application");

    DeviceIdentity bocV6Bootloader;
    bocV6Bootloader.type = 0x1000;
    bocV6Bootloader.version = 0x0000;
    bocV6Bootloader.description = QStringLiteral(
        "Блок обработки цифровой (БОЦ-В-6) (Boot) 1970 I Зав.№902 (SW Aug 31 2026 10:01:24)");
    bocV6Bootloader = catalog.enrich(bocV6Bootloader);
    QVERIFY(bocV6Bootloader.known);
    QCOMPARE(bocV6Bootloader.state, QStringLiteral("bootloader"));

    hasLoadApplication = false;
    for (const ActionSpec& action : actions.actionsForDevice(bocV6Bootloader))
        hasLoadApplication = hasLoadApplication || action.id == QStringLiteral("device.application.load");
    QVERIFY2(hasLoadApplication, "BOC-V-6 bootloader must expose load application action");
}

void DeviceWorkbenchTest::workflowLoadsApplicationFromBootloaderWithoutWaitingForReply()
{
    DeviceIdentity device;
    device.id = QStringLiteral("boc.v12");
    device.type = 0x1001;
    device.version = 0x0000;
    device.known = true;
    device.catalogId = QStringLiteral("boc.v12");
    device.deviceClass = QStringLiteral("BocV12Device");
    device.name = QStringLiteral("БОЦ-В-12");
    device.state = QStringLiteral("bootloader");
    device.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) (Boot)");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("192.168.1.245:2001");
    device.modbusAddress = 7;
    ActionSpec action;
    action.id = QStringLiteral("device.application.load");
    action.title = QStringLiteral("Load application");
    action.workflow = QStringLiteral("device.application.load");

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->failWrites = true;
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);

    QVERIFY(runner.run(action, {deviceObject}));

    QCOMPARE(transport->writes.size(), 0);
    QCOMPARE(transport->noReplyWrites.size(), 1);
    QCOMPARE(transport->noReplyWrites.at(0).index, quint16(0));
    QCOMPARE(transport->noReplyWrites.at(0).value, qint32(1));
    QCOMPARE(transport->waitForIdentityCalls, 1);

    bool sawFinishedLog = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.at(0).toString().contains(QStringLiteral("workflow finished")))
        {
            sawFinishedLog = true;
            break;
        }
    }
    QVERIFY2(sawFinishedLog, "Workflow must finish after application identity is confirmed");
}

void DeviceWorkbenchTest::workflowWritesApplicationFlashPagesFromBootloader()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(QDir(tempDir.path()).mkpath(QStringLiteral("flash/boc-v12")));

    QByteArray firmware;
    firmware.resize(3000);
    for (int i = 0; i < firmware.size(); ++i)
        firmware[i] = char(i & 0xFF);

    const QString firmwarePath = QDir(tempDir.path()).filePath(QStringLiteral("flash/boc-v12/application.bin"));
    QFile firmwareFile(firmwarePath);
    QVERIFY(firmwareFile.open(QIODevice::WriteOnly));
    QCOMPARE(firmwareFile.write(firmware), qint64(firmware.size()));
    firmwareFile.close();

    FirmwareArtifact artifact;
    artifact.target = QStringLiteral("application");
    artifact.version = QStringLiteral("test");
    artifact.relativePath = QStringLiteral("flash/boc-v12/application.bin");
    artifact.sha256 = sha256Hex(firmware);
    artifact.isDefault = true;
    artifact.flashNum = 0;

    DeviceIdentity device;
    device.id = QStringLiteral("boc.v12");
    device.type = 0x1001;
    device.version = 0x0000;
    device.known = true;
    device.catalogId = QStringLiteral("boc.v12");
    device.deviceClass = QStringLiteral("BocV12Device");
    device.name = QStringLiteral("БОЦ-В-12");
    device.state = QStringLiteral("bootloader");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("192.168.1.245:2001");
    device.modbusAddress = 7;
    device.firmwareArtifacts = {artifact};

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.title = QStringLiteral("Write application");
    action.workflow = QStringLiteral("flash.write");
    action.target = QStringLiteral("application");

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->flashParams = {FlashMemoryParams{4, 2048}};
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);
    QVERIFY(deviceObject);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);

    const QString previousCurrentPath = QDir::currentPath();
    QDir::setCurrent(tempDir.path());
    runner.run(action, {deviceObject});
    QDir::setCurrent(previousCurrentPath);

    QCOMPARE(transport->flashWrites.size(), 2);
    QCOMPARE(transport->flashWrites.at(0).flashNum, 0);
    QCOMPARE(transport->flashWrites.at(0).pageNum, 0);
    QCOMPARE(transport->flashWrites.at(0).page.size(), 2048);
    QCOMPARE(transport->flashWrites.at(0).page.left(2048), firmware.left(2048));
    QCOMPARE(transport->flashWrites.at(1).pageNum, 1);
    QCOMPARE(transport->flashWrites.at(1).page.left(952), firmware.mid(2048));
    QCOMPARE(quint8(transport->flashWrites.at(1).page.at(952)), quint8(0xFF));
    QCOMPARE(transport->flashReads.size(), 2);
}

void DeviceWorkbenchTest::bootloaderWorkflowWritesAndVerifiesFromApplication()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(QDir(tempDir.path()).mkpath(QStringLiteral("flash/boc-v12/bootloader")));

    const QByteArray firmware(3000, char(0x5A));
    const QString firmwarePath = QDir(tempDir.path()).filePath(
        QStringLiteral("flash/boc-v12/bootloader/bootloader.bin"));
    QFile firmwareFile(firmwarePath);
    QVERIFY(firmwareFile.open(QIODevice::WriteOnly));
    QCOMPARE(firmwareFile.write(firmware), qint64(firmware.size()));
    firmwareFile.close();

    FirmwareArtifact artifact;
    artifact.target = QStringLiteral("bootloader");
    artifact.relativePath = QStringLiteral("flash/boc-v12/bootloader/bootloader.bin");
    artifact.sha256 = sha256Hex(firmware);
    artifact.isDefault = true;
    artifact.flashNum = 0;
    artifact.flashStrategy = QStringLiteral("page-flash");
    artifact.allowedFromFirmwareIds = QStringList{QStringLiteral("application.allowed")};

    DeviceIdentity identity;
    identity.id = QStringLiteral("boc.v12");
    identity.type = 0x0A03;
    identity.version = 0x0001;
    identity.known = true;
    identity.catalogId = QStringLiteral("boc.v12");
    identity.name = QStringLiteral("БОЦ-В-12");
    identity.state = QStringLiteral("application");
    identity.currentFirmwareId = QStringLiteral("application.allowed");
    identity.endpoint = QStringLiteral("192.168.1.245:2001");
    identity.firmwareArtifacts = {artifact};

    ActionSpec action;
    action.id = QStringLiteral("flash.bootloader.write");
    action.title = QStringLiteral("Прошить bootloader");
    action.workflow = QStringLiteral("firmware.bootloader.direct");
    action.target = QStringLiteral("bootloader");

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->flashParams = {FlashMemoryParams{4, 2048}};
    DeviceFactory factory(transport);
    const std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    WorkflowRepository workflows;
    QString workflowError;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")),
        &workflowError), qPrintable(workflowError));
    WorkflowRunner runner(&workflows);
    QStringList workflowLogs;
    connect(&runner, &WorkflowRunner::logMessage, this,
        [&workflowLogs](const QString& message) { workflowLogs.append(message); });

    QVariantMap artifactMap;
    artifactMap.insert(QStringLiteral("target"), artifact.target);
    artifactMap.insert(QStringLiteral("relativePath"), artifact.relativePath);
    artifactMap.insert(QStringLiteral("sha256"), artifact.sha256);
    artifactMap.insert(QStringLiteral("flashNum"), artifact.flashNum);
    artifactMap.insert(QStringLiteral("flashStrategy"), artifact.flashStrategy);
    artifactMap.insert(QStringLiteral("allowedFromFirmwareIds"),
        artifact.allowedFromFirmwareIds);
    QVariantMap parameters;
    parameters.insert(QStringLiteral("artifact"), artifactMap);

    const QString previousCurrentPath = QDir::currentPath();
    QDir::setCurrent(tempDir.path());
    const bool successful = runner.run(action, {device}, parameters);
    QDir::setCurrent(previousCurrentPath);

    QVERIFY(successful);
    QCOMPARE(transport->resetCalls, 0);
    QCOMPARE(transport->writes.size(), 0);
    QCOMPARE(transport->noReplyWrites.size(), 0);
    QCOMPARE(transport->flashWrites.size(), 2);
    QCOMPARE(transport->flashReads.size(), 2);
    QCOMPARE(transport->flashWrites.at(0).flashNum, 0);
    QCOMPARE(transport->flashWrites.at(1).flashNum, 0);

    DeviceIdentity blockedIdentity = device->identity();
    blockedIdentity.currentFirmwareId = QStringLiteral("application.blocked");
    device->updateIdentity(blockedIdentity);
    const int writesBeforeBlockedRun = transport->flashWrites.size();
    const int readsBeforeBlockedRun = transport->flashReads.size();
    QVERIFY(!runner.run(action, {device}, parameters));
    QCOMPARE(transport->flashWrites.size(), writesBeforeBlockedRun);
    QCOMPARE(transport->flashReads.size(), readsBeforeBlockedRun);
    QVERIFY(std::any_of(workflowLogs.cbegin(), workflowLogs.cend(),
        [](const QString& message) {
            return message.contains(QStringLiteral("is not allowed from current application"));
        }));
}

void DeviceWorkbenchTest::workflowParsesIntelHexBeforeWriting()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QByteArray hex = QByteArrayLiteral(
        ":020000040804EE\n"
        ":0400000001020304F2\n"
        ":0410000005060708D2\n"
        ":020000042000DA\n"
        ":0400040000000000F8\n"
        ":00000001FF\n");
    const QString firmwarePath = QDir(tempDir.path()).filePath(QStringLiteral("firmware.hex"));
    QFile firmwareFile(firmwarePath);
    QVERIFY(firmwareFile.open(QIODevice::WriteOnly));
    QCOMPARE(firmwareFile.write(hex), qint64(hex.size()));
    firmwareFile.close();

    FirmwareArtifact artifact;
    artifact.target = QStringLiteral("application");
    artifact.relativePath = firmwarePath;
    artifact.sha256 = sha256Hex(hex);
    artifact.format = QStringLiteral("intelHex");
    artifact.addressBase = 0x08040000;
    artifact.flashNum = 0;

    DeviceIdentity device;
    device.id = QStringLiteral("boc.v6");
    device.type = 0x0A02;
    device.version = 0x0001;
    device.known = true;
    device.state = QStringLiteral("bootloader");
    device.endpoint = QStringLiteral("192.168.1.90:2001");
    device.modbusAddress = 1;
    device.firmwareArtifacts = {artifact};

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.workflow = QStringLiteral("flash.write");
    action.target = QStringLiteral("application");

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->flashParams = {FlashMemoryParams{4, 2048}};
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(device);

    WorkflowRepository workflows;
    QString error;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error), qPrintable(error));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    runner.run(action, {deviceObject});

    QCOMPARE(transport->flashWrites.size(), 2);
    QCOMPARE(transport->flashWrites.at(0).pageNum, 0);
    QCOMPARE(transport->flashWrites.at(0).page.left(4), QByteArray::fromHex("01020304"));
    QCOMPARE(quint8(transport->flashWrites.at(0).page.at(4)), quint8(0xFF));
    QCOMPARE(transport->flashWrites.at(1).pageNum, 2);
    QCOMPARE(transport->flashWrites.at(1).page.left(4), QByteArray::fromHex("05060708"));
    QCOMPARE(transport->flashReads.size(), 2);
    QCOMPARE(transport->flashReads.at(0).pageNum, 0);
    QCOMPARE(transport->flashReads.at(1).pageNum, 2);

    bool sawIgnoredRam = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.first().toString().contains(QStringLiteral("ignored 4 out-of-range bytes")))
            sawIgnoredRam = true;
    }
    QVERIFY(sawIgnoredRam);
}

void DeviceWorkbenchTest::workflowLoadsConfiguredBocV6Firmware()
{
    CatalogService catalog;
    QString error;
    const QString catalogPath = sourceConfigPath(QStringLiteral("config/device-catalog.json"));
    QVERIFY2(catalog.load(catalogPath, &error), qPrintable(error));

    DeviceIdentity identity;
    identity.id = QStringLiteral("192.168.1.90:2001");
    identity.endpoint = identity.id;
    identity.type = 0x0A02;
    identity.version = 0x0001;
    identity.modbusAddress = 1;
    identity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    identity = catalog.enrich(identity);

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->flashParams = {FlashMemoryParams{512, 64}, FlashMemoryParams{480, 4096}};
    DeviceIdentity discoveredBootloader;
    discoveredBootloader.id = QStringLiteral("192.168.1.90:2002");
    discoveredBootloader.endpoint = discoveredBootloader.id;
    discoveredBootloader.type = 0x7777;
    discoveredBootloader.version = 0x0002;
    discoveredBootloader.modbusAddress = 1;
    discoveredBootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) (Boot) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");

    DeviceIdentity discoveredApplication;
    discoveredApplication.id = identity.id;
    discoveredApplication.endpoint = identity.endpoint;
    discoveredApplication.type = 0x0A02;
    discoveredApplication.version = 0x4321;
    discoveredApplication.modbusAddress = 1;
    discoveredApplication.description = QStringLiteral(
        "Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    transport->discoveredIdentities = {
        discoveredBootloader,
        discoveredApplication,
        discoveredApplication
    };
    transport->waitForIdentityResults = {true, false, true};

    DeviceFactory factory(transport);
    auto deviceObject = factory.create(identity);
    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.workflow = QStringLiteral("unused.action.fallback");
    action.target = QStringLiteral("application");

    WorkflowRepository workflows;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error), qPrintable(error));
    WorkflowRunner runner(&workflows);
    const QString previousCurrentPath = QDir::currentPath();
    QDir::setCurrent(QFileInfo(catalogPath).absoluteDir().absoluteFilePath(QStringLiteral("..")));
    runner.run(action, {deviceObject}, QVariantMap{
        {QStringLiteral("targetFirmwareId"), QStringLiteral("sw-2026-08-31-10-01-24")}
    });
    QDir::setCurrent(previousCurrentPath);

    QCOMPARE(transport->resetCalls, 1);
    QCOMPARE(transport->writes.size(), 1);
    QCOMPARE(transport->writes.at(0).index, quint16(0));
    QCOMPARE(transport->writes.at(0).value, qint32(0));
    QCOMPARE(transport->reads.size(), 0);
    QCOMPARE(transport->flashWrites.size(), 98);
    QCOMPARE(transport->flashReads.size(), 98);
    QCOMPARE(transport->flashWrites.first().flashNum, 1);
    QCOMPARE(transport->flashWrites.first().pageNum, 0);
    QCOMPARE(transport->flashWrites.at(80).pageNum, 80);
    QCOMPARE(transport->flashWrites.at(81).pageNum, 192);
    QCOMPARE(transport->flashWrites.last().pageNum, 208);
    QCOMPARE(transport->flashWrites.first().page.left(4), QByteArray::fromHex("40060420"));
    QCOMPARE(quint8(transport->flashWrites.last().page.at(772)), quint8(0xFF));
    QCOMPARE(transport->flashReads.at(80).pageNum, 80);
    QCOMPARE(transport->flashReads.at(81).pageNum, 192);
    QCOMPARE(transport->flashReads.last().pageNum, 208);
    QCOMPARE(transport->waitForIdentityCalls, 3);
    QCOMPARE(transport->waitExpectedIdentities.at(0).type, quint16(0x0000));
    QCOMPARE(transport->waitExpectedIdentities.at(0).version, quint16(0x0000));
    QCOMPARE(transport->waitExpectedIdentities.at(0).state, QStringLiteral("bootloader"));
    QCOMPARE(transport->waitExpectedIdentities.at(1).type, quint16(0x0A02));
    QCOMPARE(transport->waitExpectedIdentities.at(1).version, quint16(0x0000));
    QCOMPARE(transport->waitExpectedIdentities.at(2).type, quint16(0x0A02));
    QCOMPARE(transport->waitExpectedIdentities.at(2).version, quint16(0x0000));
    QCOMPARE(transport->noReplyWrites.size(), 2);
    QCOMPARE(transport->noReplyWrites.at(0).index, quint16(0));
    QCOMPARE(transport->noReplyWrites.at(0).value, qint32(1));
    QCOMPARE(transport->noReplyWrites.at(1).index, quint16(0));
    QCOMPARE(transport->noReplyWrites.at(1).value, qint32(1));
    QCOMPARE(deviceObject->identity().currentFirmwareId, QStringLiteral("sw-2026-08-31-10-01-24"));
    QCOMPARE(deviceObject->identity().state, QStringLiteral("application"));
    QCOMPARE(deviceObject->identity().uuid, transport->uuidValue);
    QCOMPARE(transport->uuidReadCalls, 1);
}

void DeviceWorkbenchTest::workflowRejectsBootloaderWithDifferentUuid()
{
    CatalogService catalog;
    QString error;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error), qPrintable(error));

    DeviceIdentity identity;
    identity.id = QStringLiteral("192.168.1.90:2001");
    identity.endpoint = identity.id;
    identity.type = 0x0A02;
    identity.version = 0x0001;
    identity.modbusAddress = 1;
    identity.serialNumber = QStringLiteral("000");
    identity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    identity = catalog.enrich(identity);

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->discoveredIdentity.endpoint = QStringLiteral("192.168.1.90:2001");
    transport->discoveredIdentity.id = transport->discoveredIdentity.endpoint;
    transport->discoveredIdentity.type = 0x1000;
    transport->discoveredIdentity.version = 0x0000;
    transport->discoveredIdentity.modbusAddress = 1;
    transport->discoveredIdentity.serialNumber = QStringLiteral("000");
    transport->discoveredIdentity.uuid = QStringLiteral("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF");
    transport->discoveredIdentity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) (Boot) 1970 I Зав.№000 (SW Jul 20 20");

    DeviceFactory factory(transport);
    const std::shared_ptr<DeviceBase> device = factory.create(identity);
    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.workflow = QStringLiteral("firmware.application.standard");
    action.target = QStringLiteral("application");

    WorkflowRepository workflows;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error), qPrintable(error));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    runner.run(action, {device}, QVariantMap{
        {QStringLiteral("targetFirmwareId"), QStringLiteral("sw-2026-08-31-10-01-24")}
    });

    QCOMPARE(transport->resetCalls, 1);
    QCOMPARE(transport->flashWrites.size(), 0);
    bool sawUuidRejection = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.first().toString().contains(QStringLiteral("unexpected bootloader identity"))
            && row.first().toString().contains(QStringLiteral("FFFFFFFF-FFFF")))
            sawUuidRejection = true;
    }
    QVERIFY2(sawUuidRejection, "A bootloader with a different UUID must be rejected before flashing");
}

void DeviceWorkbenchTest::workflowExecutesAllowedFirmwareTransition()
{
    CatalogService catalog;
    QString error;
    const QString catalogPath = sourceConfigPath(QStringLiteral("config/device-catalog.json"));
    QVERIFY2(catalog.load(catalogPath, &error), qPrintable(error));

    DeviceIdentity bootloader;
    bootloader.id = QStringLiteral("192.168.1.90:2002");
    bootloader.endpoint = bootloader.id;
    bootloader.channel = QStringLiteral("UDP");
    bootloader.type = 0x1000;
    bootloader.version = 0x0000;
    bootloader.modbusAddress = 1;
    bootloader.serialNumber = QStringLiteral("000");
    bootloader.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) (Boot) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    bootloader = catalog.enrich(bootloader);
    QCOMPARE(bootloader.state, QStringLiteral("bootloader"));
    QCOMPARE(bootloader.currentFirmwareId, QStringLiteral("sw-2026-08-31-10-01-24"));

    auto transport = std::make_shared<FakeDeviceTransport>();
    transport->flashParams = {FlashMemoryParams{512, 64}, FlashMemoryParams{480, 4096}};
    transport->discoveredIdentity.id = QStringLiteral("192.168.1.90:2001");
    transport->discoveredIdentity.endpoint = transport->discoveredIdentity.id;
    transport->discoveredIdentity.type = 0x0A02;
    transport->discoveredIdentity.version = 0x0001;
    transport->discoveredIdentity.modbusAddress = 1;
    transport->discoveredIdentity.serialNumber = QStringLiteral("000");
    transport->discoveredIdentity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Jul 21 2026 11:59:00)");

    DeviceFactory factory(transport);
    auto deviceObject = factory.create(bootloader);
    QVERIFY(deviceObject);

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.title = QStringLiteral("Write application");
    action.workflow = QStringLiteral("firmware.application.standard");
    action.target = QStringLiteral("application");

    WorkflowRepository workflows;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error), qPrintable(error));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);

    const QString previousCurrentPath = QDir::currentPath();
    QDir::setCurrent(QFileInfo(catalogPath).absoluteDir().absoluteFilePath(QStringLiteral("..")));
    QVERIFY(runner.run(action, {deviceObject}, QVariantMap{{QStringLiteral("targetFirmwareId"), QStringLiteral("sw-2026-07-21-11-59-00")}}));
    QDir::setCurrent(previousCurrentPath);

    QCOMPARE(transport->resetCalls, 0);
    QCOMPARE(transport->writes.size(), 1);
    QCOMPARE(transport->writes.at(0).index, quint16(0));
    QCOMPARE(transport->writes.at(0).value, qint32(0));
    QCOMPARE(transport->reads.size(), 0);
    QCOMPARE(transport->flashWrites.size(), 97);
    QCOMPARE(transport->flashReads.size(), 97);
    QCOMPARE(transport->noReplyWrites.size(), 1);
    QCOMPARE(transport->noReplyWrites.first().index, quint16(0));
    QCOMPARE(transport->noReplyWrites.first().value, qint32(1));
    QCOMPARE(transport->waitForIdentityCalls, 1);
    QCOMPARE(deviceObject->identity().state, QStringLiteral("application"));
    QCOMPARE(deviceObject->identity().currentFirmwareId, QStringLiteral("sw-2026-07-21-11-59-00"));

    bool sawTransition = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.first().toString().contains(QStringLiteral("firmware transition sw-2026-08-31-10-01-24 -> sw-2026-07-21-11-59-00")))
            sawTransition = true;
    }
    QVERIFY(sawTransition);
}

void DeviceWorkbenchTest::knownDeviceAllowsEveryFirmwareWhenCurrentVersionIsUnknown()
{
    FirmwareVersionSpec first;
    first.id = QStringLiteral("1.0.0");
    first.artifact.firmwareId = first.id;
    first.artifact.target = QStringLiteral("application");
    first.artifact.relativePath = QStringLiteral(
        "flash/boc-v6/BOCv6_ADCVibr_Digital20260721_1228.hex");
    first.installation.workflow = QStringLiteral("firmware.application.standard");
    first.installation.strategy = QStringLiteral("page-flash");

    FirmwareVersionSpec second = first;
    second.id = QStringLiteral("2.0.0");
    second.artifact.firmwareId = second.id;
    second.artifact.relativePath = QStringLiteral(
        "flash/boc-v6/BOCv6_ADCVibr_Digital20260831_1007.hex");

    DeviceIdentity identity;
    identity.id = QStringLiteral("known-device");
    identity.endpoint = QStringLiteral("127.0.0.1:2001");
    identity.type = 0x0A03;
    identity.version = 0x0001;
    identity.applicationType = identity.type;
    identity.applicationVersion = identity.version;
    identity.bootloaderType = 0x1001;
    identity.bootloaderVersion = 0;
    identity.known = true;
    identity.catalogId = QStringLiteral("boc.v6");
    identity.state = QStringLiteral("application");
    identity.uuid = QStringLiteral("410FC241-3431-384D-1235-36353133584F");
    identity.firmwareVersions = {first, second};

    QVERIFY(identity.currentFirmwareId.isEmpty());
    QVERIFY(identity.isFirmwareTargetAllowed(first.id));
    QVERIFY(identity.isFirmwareTargetAllowed(second.id));
    QVERIFY(FirmwareAccessPolicy::isTargetAllowed(identity, first.id));
    QVERIFY(FirmwareAccessPolicy::isTargetAllowed(identity, second.id));

    DeviceIdentity unknownDevice = identity;
    unknownDevice.known = false;
    QVERIFY(!unknownDevice.isFirmwareTargetAllowed(second.id));

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    const std::shared_ptr<DeviceBase> device = factory.create(identity);

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.workflow = QStringLiteral("firmware.application.standard");
    action.target = QStringLiteral("application");

    WorkflowRepository workflows;
    QString error;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error), qPrintable(error));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);

    // The fake device intentionally fails later while reconnecting to its
    // bootloader. Reaching reset proves that unknown current firmware no longer
    // blocks target validation.
    QVERIFY(!runner.run(action, {device}, QVariantMap{{QStringLiteral("targetFirmwareId"), second.id}}));
    QCOMPARE(transport->resetCalls, 1);

    bool sawUnknownVersionSelection = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty()
            && row.first().toString().contains(QStringLiteral("current firmware is unknown; target 2.0.0 selected")))
        {
            sawUnknownVersionSelection = true;
        }
    }
    QVERIFY(sawUnknownVersionSelection);
}

void DeviceWorkbenchTest::workflowRejectsDisabledFirmwareTransition()
{
    FirmwareVersionSpec from;
    from.id = QStringLiteral("1.0.0");
    from.descriptionRegex = QStringLiteral("1.0.0$");
    FirmwareVersionSpec to;
    to.id = QStringLiteral("2.0.0");
    to.descriptionRegex = QStringLiteral("2.0.0$");
    to.artifact.firmwareId = to.id;
    to.artifact.target = QStringLiteral("application");
    to.artifact.relativePath = QStringLiteral("unused.bin");
    to.installation.workflow = QStringLiteral("firmware.application.standard");
    to.installation.strategy = QStringLiteral("page-flash");

    FirmwareTransitionSpec transition;
    transition.from = from.id;
    transition.to = to.id;
    transition.enabled = false;
    transition.reason = QStringLiteral("downgrade is disabled");

    DeviceIdentity identity;
    identity.id = QStringLiteral("test-device");
    identity.endpoint = QStringLiteral("127.0.0.1:2001");
    identity.type = 0x0A03;
    identity.version = 0x0001;
    identity.known = true;
    identity.currentFirmwareId = from.id;
    identity.firmwareVersions = {from, to};
    identity.firmwareTransitions = {transition};

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    auto deviceObject = factory.create(identity);

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.workflow = QStringLiteral("firmware.application.standard");
    action.target = QStringLiteral("application");

    WorkflowRepository workflows;
    QString error;
    QVERIFY2(workflows.load(sourceConfigPath(QStringLiteral("config/workflows.json")), &error), qPrintable(error));
    WorkflowRunner runner(&workflows);
    QSignalSpy logSpy(&runner, &WorkflowRunner::logMessage);
    QVERIFY(!runner.run(action, {deviceObject}, QVariantMap{{QStringLiteral("targetFirmwareId"), to.id}}));

    QCOMPARE(transport->resetCalls, 0);
    QCOMPARE(transport->flashWrites.size(), 0);
    bool sawDenied = false;
    bool sawUnsupportedFallback = false;
    for (const QList<QVariant>& row : logSpy)
    {
        if (!row.isEmpty() && row.first().toString().contains(QStringLiteral("downgrade is disabled")))
            sawDenied = true;
        if (!row.isEmpty() && row.first().toString().contains(QStringLiteral("is not supported by")))
            sawUnsupportedFallback = true;
    }
    QVERIFY(sawDenied);
    QVERIFY2(!sawUnsupportedFallback, "A failed runtime operation must not fall through to DeviceBase operations");
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
    identity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-12) 1970 I Зав.№902 (SW Jul 16 2026 09:24:19)");
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

void DeviceWorkbenchTest::deviceBaseWritesConfiguredServiceRegisters()
{
    DeviceIdentity identity;
    identity.type = 0x0A02;
    identity.version = 0x0001;
    identity.productionDateRegister = 9;
    identity.serialNumberRegister = 10;

    auto transport = std::make_shared<FakeDeviceTransport>();
    DeviceFactory factory(transport);
    const std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    QString error;
    QVERIFY2(device->writeProductionDate(1786838400, &error), qPrintable(error));
    QVERIFY2(device->writeSerialNumber(915, &error), qPrintable(error));
    QCOMPARE(transport->writes.size(), 2);
    QCOMPARE(transport->writes.at(0).index, quint16(9));
    QCOMPARE(transport->writes.at(0).value, qint32(1786838400));
    QCOMPARE(transport->writes.at(1).index, quint16(10));
    QCOMPARE(transport->writes.at(1).value, qint32(915));
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

void DeviceWorkbenchTest::unicornAsciiReadRegisterReturnsAfterFirstValidResponse()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    DeviceIdentity identity;
    identity.modbusAddress = 7;
    identity.endpoint = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());

    TransportReadThread thread;
    thread.identity = identity;
    thread.transport = createUnicornAsciiTransport();
    thread.start();

    QVERIFY2(server.waitForNewConnection(1000), "Expected transport to connect to test TCP server");
    QTcpSocket* client = server.nextPendingConnection();
    QVERIFY(client);
    QVERIFY2(client->waitForReadyRead(1000), "Expected transport to send ReadInt request");
    client->readAll();

    const QByteArray response("!07EB00000000000004D200\r");
    QCOMPARE(client->write(response), qint64(response.size()));
    QVERIFY(client->waitForBytesWritten(1000));

    QVERIFY2(thread.wait(4000), "Transport read did not finish");
    QVERIFY2(thread.ok, qPrintable(thread.error));
    QCOMPARE(thread.value, qint32(1234));
    QVERIFY2(thread.elapsedMs < 1000, qPrintable(QStringLiteral("Read took %1 ms").arg(thread.elapsedMs)));
}

void DeviceWorkbenchTest::unicornAsciiPreservesDeviceErrorResponse()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    DeviceIdentity identity;
    identity.modbusAddress = 1;
    identity.endpoint = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());

    TransportWriteThread thread;
    thread.identity = identity;
    thread.transport = createUnicornAsciiTransport();
    thread.start();

    QVERIFY2(server.waitForNewConnection(1000), "Expected write transport connection");
    QTcpSocket* client = server.nextPendingConnection();
    QVERIFY(client);
    QVERIFY2(client->waitForReadyRead(1000), "Expected WriteInt request");
    const QByteArray request = client->readAll();
    QVERIFY(request.startsWith(":01E2"));
    QVERIFY(request.endsWith('\r'));

    const QByteArray response("?01E20B89\r");
    QCOMPARE(client->write(response), qint64(response.size()));
    QVERIFY(client->waitForBytesWritten(1000));

    QVERIFY2(thread.wait(4000), "Transport write did not finish");
    QVERIFY(!thread.ok);
    QCOMPARE(thread.error, QStringLiteral("Device returned ASCII error 0x0b"));
    QVERIFY(thread.raw.contains(QStringLiteral("RX ?01E20B89")));
}

void DeviceWorkbenchTest::unicornAsciiReadsUuid()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    DeviceIdentity identity;
    identity.modbusAddress = 1;
    identity.endpoint = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());

    TransportUuidThread thread;
    thread.identity = identity;
    thread.transport = createUnicornAsciiTransport();
    thread.start();

    QVERIFY2(server.waitForNewConnection(1000), "Expected UUID transport connection");
    QTcpSocket* client = server.nextPendingConnection();
    QVERIFY(client);
    QVERIFY2(client->waitForReadyRead(1000), "Expected UUID request");
    QCOMPARE(client->readAll(), QByteArray(":010702\r"));

    const QByteArray response("!0107410FC2413431384D123536353133584F99\r");
    QCOMPARE(client->write(response), qint64(response.size()));
    QVERIFY(client->waitForBytesWritten(1000));

    QVERIFY2(thread.wait(4000), "UUID read did not finish");
    QVERIFY2(thread.ok, qPrintable(thread.error));
    QCOMPARE(thread.uuid, QStringLiteral("410FC241-3431-384D-1235-36353133584F"));
}

void DeviceWorkbenchTest::unicornAsciiReadsFullIdentityDescription()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    DeviceIdentity identity;
    identity.modbusAddress = 1;
    identity.endpoint = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());
    TransportIdentityDescriptionThread thread;
    thread.identity = identity;
    thread.transport = createUnicornAsciiTransport();
    thread.start();

    QVERIFY2(server.waitForNewConnection(1000), "Expected identity-description connection");
    QTcpSocket* client = server.nextPendingConnection();
    QVERIFY(client);
    QVERIFY2(client->waitForReadyRead(1000), "Expected 0xFF request");
    QCOMPARE(client->readAll(), QByteArray(":01FF27\r"));

    const QString expectedDescription = QStringLiteral(
        "Блок обработки цифровой (БОЦ-В-6) 2020 III Зав.№777 (SW Aug 20 2026 16:26:49)");
    QByteArray body = QByteArray::fromHex("0A020001") + expectedDescription.toUtf8();
    const QByteArray response = QByteArray("!01FF") + body.toHex().toUpper() + QByteArray("00\r");
    QCOMPARE(client->write(response), qint64(response.size()));
    QVERIFY(client->waitForBytesWritten(1000));

    QVERIFY2(thread.wait(4000), "Identity-description read did not finish");
    QVERIFY2(thread.ok, qPrintable(thread.error));
    QCOMPARE(thread.type, quint16(0x0A02));
    QCOMPARE(thread.version, quint16(0x0001));
    QCOMPARE(thread.description, expectedDescription);
}

void DeviceWorkbenchTest::unicornAsciiReadsExtendedDescription()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    DeviceIdentity identity;
    identity.modbusAddress = 1;
    identity.endpoint = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());
    TransportExtendedDescriptionThread thread;
    thread.identity = identity;
    thread.transport = createUnicornAsciiTransport();
    thread.start();

    QVERIFY2(server.waitForNewConnection(1000), "Expected extended-description connection");
    QTcpSocket* client = server.nextPendingConnection();
    QVERIFY(client);
    QVERIFY2(client->waitForReadyRead(1000), "Expected 0xEE request");
    QCOMPARE(client->readAll(), QByteArray(":01EE000000000000020027\r"));

    const QByteArray json("JSON\n{\"DeviceInfo\":{\"Description\":\"Full device description\"}}");
    QByteArray body;
    body.append(QByteArray::fromHex("00000000"));
    const QByteArray sizeHex = QByteArray::number(json.size(), 16).rightJustified(8, '0').toUpper();
    body.append(QByteArray::fromHex(sizeHex));
    body.append(QByteArray::fromHex(sizeHex));
    body.append(json);
    body.append(QByteArray::fromHex("00000000"));
    const QByteArray response = QByteArray("!01EE") + body.toHex().toUpper() + QByteArray("00\r");
    QCOMPARE(client->write(response), qint64(response.size()));
    QVERIFY(client->waitForBytesWritten(1000));

    QVERIFY2(thread.wait(4000), "Extended-description read did not finish");
    QVERIFY2(thread.ok, qPrintable(thread.error));
    QCOMPARE(thread.description, json);
    QVERIFY(!thread.progress.isEmpty());
    QCOMPARE(thread.progress.first(), 0);
    QCOMPARE(thread.progress.last(), 100);
}

void DeviceWorkbenchTest::networkReadsDeviceDataBocV6()
{
    const QString endpoint = QString::fromLocal8Bit(
        qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_ENDPOINT")).trimmed();
    if (endpoint.isEmpty())
        QSKIP("Set DEVICE_WORKBENCH_NETWORK_BOCV6_ENDPOINT to run the read-only device-data test");

    DeviceIdentity identity;
    identity.endpoint = endpoint;
    identity.modbusAddress = 1;
    const auto transport = createUnicornAsciiTransport();

    QByteArray extendedDescription;
    QVector<int> progress;
    QString error;
    QVERIFY2(transport->readExtendedDescription(identity, &extendedDescription,
        [&progress](int value) { progress.append(value); }, &error), qPrintable(error));
    QVERIFY(extendedDescription.startsWith("JSON"));
    QVERIFY(extendedDescription.size() > 1000);
    QVERIFY(!progress.isEmpty());
    QCOMPARE(progress.first(), 0);
    QCOMPARE(progress.last(), 100);

    quint16 type = 0;
    quint16 version = 0;
    QString description;
    QVERIFY2(transport->readIdentityDescription(
        identity, &type, &version, &description, &error), qPrintable(error));
    QCOMPARE(type, quint16(0x0A02));
    QCOMPARE(version, quint16(0x0001));
    QVERIFY2(description.endsWith(QLatin1Char(')')), qPrintable(description));

    CatalogService catalog;
    QVERIFY2(catalog.load(sourceConfigPath(QStringLiteral("config/device-catalog.json")), &error),
        qPrintable(error));
    identity.type = type;
    identity.version = version;
    identity.description = description;
    identity.descriptionJson = extendedDescription;
    identity = catalog.enrich(identity);
    QVERIFY(identity.known);
    QVERIFY2(!identity.currentFirmwareId.isEmpty(), qPrintable(description));

    QString uuid;
    QVERIFY2(transport->readUuid(identity, &uuid, &error), qPrintable(error));
    DeviceIdentity staleExpected = identity;
    staleExpected.uuid = uuid;
    staleExpected.serialNumber = QStringLiteral("stale-before-firmware-update");
    DeviceIdentity rediscovered;
    QVERIFY2(transport->waitForDeviceIdentity(staleExpected, 5000, 500,
        &rediscovered, &error), qPrintable(error));
    QCOMPARE(rediscovered.uuid, uuid);
    QVERIFY(rediscovered.serialNumber != staleExpected.serialNumber);
}

void DeviceWorkbenchTest::networkChangeSerialNumberBocV6()
{
    const QString endpoint = QString::fromLocal8Bit(
        qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_ENDPOINT")).trimmed();
    const QString expectedText = QString::fromLocal8Bit(
        qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_SERIAL_EXPECTED")).trimmed();
    const QString targetText = QString::fromLocal8Bit(
        qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_SERIAL_TARGET")).trimmed();
    const QString factorySettingsKey = QString::fromLocal8Bit(
        qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_FACTORY_SETTINGS_KEY")).trimmed();
    const bool verifyOnly = qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_SERIAL_VERIFY_ONLY") == "1";
    if (endpoint.isEmpty() || expectedText.isEmpty() || targetText.isEmpty())
        QSKIP("Set endpoint, expected serial and target serial to run the destructive serial-number test");
    if (!verifyOnly && factorySettingsKey.isEmpty())
        QSKIP("Set DEVICE_WORKBENCH_NETWORK_BOCV6_FACTORY_SETTINGS_KEY for a protected write");

    bool expectedOk = false;
    bool targetOk = false;
    const int expectedSerial = expectedText.toInt(&expectedOk);
    const int targetSerial = targetText.toInt(&targetOk);
    QVERIFY(expectedOk);
    QVERIFY(targetOk);
    QVERIFY(targetSerial >= 0 && targetSerial <= 999999);

    const QString catalogPath = sourceConfigPath(QStringLiteral("release/config/device-catalog.json"));
    const QString workflowsPath = sourceConfigPath(QStringLiteral("release/config/workflows.json"));
    CatalogService catalog;
    QString error;
    QVERIFY2(catalog.load(catalogPath, &error), qPrintable(error));

    DeviceIdentity expected;
    expected.id = endpoint;
    expected.endpoint = endpoint;
    expected.channel = QStringLiteral("UDP");
    expected.protocol = QStringLiteral("unicorn-ascii");

    const std::shared_ptr<IDeviceTransport> transport = createUnicornAsciiTransport();
    DeviceIdentity discovered;
    QString raw;
    QVERIFY2(transport->waitForDeviceIdentity(expected, 5000, 500, &discovered, &error, &raw),
        qPrintable(QStringLiteral("Initial discovery failed: %1\n%2").arg(error, raw)));
    discovered = catalog.enrich(discovered);
    QVERIFY2(discovered.known && discovered.catalogId == QStringLiteral("boc.v6"),
        qPrintable(QStringLiteral("Unexpected device: %1 %2 '%3'")
            .arg(discovered.typeHex(), discovered.versionHex(), discovered.description)));

    bool currentSerialOk = false;
    const int currentSerial = discovered.serialNumber.toInt(&currentSerialOk);
    QVERIFY2(currentSerialOk && currentSerial == expectedSerial,
        qPrintable(QStringLiteral("Serial precondition failed: expected %1, actual '%2'; no write performed")
            .arg(expectedSerial).arg(discovered.serialNumber)));
    qInfo().noquote() << QStringLiteral("BOC-V-6 before update: UUID=%1 serial=%2 endpoint=%3")
        .arg(discovered.uuid, discovered.serialNumber, discovered.endpoint);

    if (verifyOnly)
    {
        DeviceIdentity refreshExpected = discovered;
        refreshExpected.type = 0;
        refreshExpected.version = 0;
        refreshExpected.serialNumber.clear();
        refreshExpected.state.clear();
        const QString staleEndpoint = QString::fromLocal8Bit(
            qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_STALE_ENDPOINT")).trimmed();
        if (!staleEndpoint.isEmpty())
            refreshExpected.endpoint = staleEndpoint;
        DeviceIdentity refreshed;
        error.clear();
        raw.clear();
        QVERIFY2(transport->waitForDeviceIdentity(refreshExpected, 5000, 500,
                &refreshed, &error, &raw),
            qPrintable(QStringLiteral("Read-only refresh failed: %1\n%2").arg(error, raw)));
        bool refreshedSerialOk = false;
        const int refreshedSerial = refreshed.serialNumber.toInt(&refreshedSerialOk);
        QVERIFY(refreshedSerialOk);
        QCOMPARE(refreshedSerial, expectedSerial);
        QCOMPARE(refreshed.uuid, discovered.uuid);
        qInfo().noquote() << QStringLiteral("Read-only refresh confirmed serial=%1 UUID=%2")
            .arg(refreshed.serialNumber, refreshed.uuid);
        return;
    }

    DeviceFactory factory(transport);
    const std::shared_ptr<DeviceBase> device = factory.create(discovered);
    QVERIFY(device);
    WorkflowRepository workflows;
    QVERIFY2(workflows.load(workflowsPath, &error), qPrintable(error));

    ActionSpec action;
    action.id = QStringLiteral("device.serialNumber.update");
    action.title = QStringLiteral("Изменить номер устройства");
    action.workflow = QStringLiteral("device.serial-number.update");
    WorkflowRunner runner(&workflows);
    QObject::connect(&runner, &WorkflowRunner::logMessage, [](const QString& message) {
        qInfo().noquote() << message;
    });
    QObject::connect(&runner, &WorkflowRunner::transportLogMessage, [](const QString& message) {
        qInfo().noquote() << message;
    });

    QVERIFY2(runner.run(action, {device}, QVariantMap{
        {QStringLiteral("serialNumber"), targetSerial},
        {QStringLiteral("factorySettingsKey"), factorySettingsKey}
    }), "Serial-number workflow failed before identity refresh");

    DeviceIdentity refreshExpected = device->identity();
    refreshExpected.type = 0;
    refreshExpected.version = 0;
    refreshExpected.serialNumber.clear();
    refreshExpected.state.clear();
    DeviceIdentity refreshed;
    QElapsedTimer reappearanceTimer;
    reappearanceTimer.start();
    error.clear();
    raw.clear();
    QVERIFY2(transport->waitForDeviceIdentity(refreshExpected, 30000, 500,
            &refreshed, &error, &raw),
        qPrintable(QStringLiteral("Refresh failed after %1 ms: %2\n%3")
            .arg(reappearanceTimer.elapsed()).arg(error, raw)));
    const qint64 reappearanceMs = reappearanceTimer.elapsed();
    refreshed = catalog.enrich(refreshed);
    bool refreshedSerialOk = false;
    const int refreshedSerial = refreshed.serialNumber.toInt(&refreshedSerialOk);
    qInfo().noquote() << QStringLiteral("BOC-V-6 after update: UUID=%1 serial=%2 reappearedIn=%3ms")
        .arg(refreshed.uuid, refreshed.serialNumber).arg(reappearanceMs);
    QVERIFY2(refreshedSerialOk && refreshedSerial == targetSerial,
        qPrintable(QStringLiteral("Expected refreshed serial %1, actual '%2'")
            .arg(targetSerial).arg(refreshed.serialNumber)));
}

void DeviceWorkbenchTest::networkPipelineBocV6()
{
    const QString endpoint = QString::fromLocal8Bit(qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_ENDPOINT")).trimmed();
    if (endpoint.isEmpty())
        QSKIP("Set DEVICE_WORKBENCH_NETWORK_BOCV6_ENDPOINT to run the destructive network firmware test");
    const QString configuredTarget = QString::fromLocal8Bit(qgetenv("DEVICE_WORKBENCH_NETWORK_BOCV6_TARGET")).trimmed();
    const QString targetFirmwareId = configuredTarget.isEmpty()
        ? QStringLiteral("sw-2026-08-31-10-01-24")
        : configuredTarget;

    const QString catalogPath = sourceConfigPath(QStringLiteral("release/config/device-catalog.json"));
    const QString workflowsPath = sourceConfigPath(QStringLiteral("release/config/workflows.json"));
    QVERIFY2(QFileInfo::exists(catalogPath), qPrintable(catalogPath));
    QVERIFY2(QFileInfo::exists(workflowsPath), qPrintable(workflowsPath));

    CatalogService catalog;
    QString error;
    QVERIFY2(catalog.load(catalogPath, &error), qPrintable(error));

    DeviceIdentity identity;
    identity.id = endpoint;
    identity.endpoint = endpoint;
    identity.channel = QStringLiteral("UDP");
    identity.protocol = QStringLiteral("unicorn-ascii");
    identity.type = 0x0A02;
    identity.version = 0x0001;
    identity.modbusAddress = 1;
    identity.serialNumber = QStringLiteral("000");
    identity.description = QStringLiteral("Блок обработки цифровой (БОЦ-В-6) 1970 I Зав.№000 (SW Aug 31 2026 10:01:24)");
    identity = catalog.enrich(identity);
    QVERIFY2(identity.known, "The network BOC-V-6 identity was not recognized by the release catalog");

    WorkflowRepository workflows;
    QVERIFY2(workflows.load(workflowsPath, &error), qPrintable(error));
    DeviceFactory factory;
    const std::shared_ptr<DeviceBase> device = factory.create(identity);
    QVERIFY(device);

    ActionSpec action;
    action.id = QStringLiteral("flash.application.write");
    action.workflow = QStringLiteral("firmware.application.standard");
    action.target = QStringLiteral("application");

    WorkflowRunner runner(&workflows);
    QObject::connect(&runner, &WorkflowRunner::logMessage, [](const QString& message) {
        qInfo().noquote() << message;
    });
    QObject::connect(&runner, &WorkflowRunner::transportLogMessage, [](const QString& message) {
        qInfo().noquote() << message;
    });
    QObject::connect(&runner, &WorkflowRunner::progressChanged, [](int progress) {
        qInfo().noquote() << QStringLiteral("progress %1%").arg(progress);
    });

    const QString previousCurrentPath = QDir::currentPath();
    QDir::setCurrent(QFileInfo(catalogPath).absoluteDir().absoluteFilePath(QStringLiteral("..")));
    runner.run(action, {device}, QVariantMap{
        {QStringLiteral("targetFirmwareId"), targetFirmwareId}
    });
    QDir::setCurrent(previousCurrentPath);

    QCOMPARE(device->identity().state, QStringLiteral("application"));
    QCOMPARE(device->identity().currentFirmwareId, targetFirmwareId);
    QCOMPARE(device->identity().uuid, QStringLiteral("410FC241-3431-384D-1235-36353133584F"));
}

QTEST_APPLESS_MAIN(DeviceWorkbenchTest)

#include "tst_device_workbench.moc"
