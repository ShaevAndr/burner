#include "../src/app_edition.h"
#include "../src/firmware_access_policy.h"

#include <QtTest>

class ExternalEditionTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesFirmwareAndPingActions()
    {
        QVERIFY(!AppEdition::isInternal());
        QCOMPARE(AppEdition::id(), QStringLiteral("external"));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("flash.application.write")));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("flash.bootloader.write")));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("device.application.load")));
        QVERIFY(!AppEdition::allowsAction(QStringLiteral("device.productionDate.update")));
        QVERIFY(!AppEdition::allowsAction(QStringLiteral("device.serialNumber.update")));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("device.ping")));
        QVERIFY(!AppEdition::allowsCustomFirmware());
    }

    void restrictsBocV6ToJulyToAugustUpgrade()
    {
        FirmwareVersionSpec july;
        july.id = QStringLiteral("sw-2026-07-21-11-59-00");
        july.artifact.relativePath = QStringLiteral(
            "flash/boc-v6/BOCv6_ADCVibr_Digital20260721_1228.hex");

        FirmwareVersionSpec august;
        august.id = QStringLiteral("sw-2026-08-31-10-01-24");
        august.artifact.relativePath = QStringLiteral(
            "flash/boc-v6/BOCv6_ADCVibr_Digital20260831_1007.hex");

        DeviceIdentity identity;
        identity.catalogId = QStringLiteral("boc.v6");
        identity.known = true;
        identity.firmwareVersions = {july, august};

        for (const FirmwareVersionSpec& from : identity.firmwareVersions)
        {
            for (const FirmwareVersionSpec& to : identity.firmwareVersions)
                identity.firmwareTransitions.append({from.id, to.id, true, QString()});
        }

        identity.currentFirmwareId = july.id;
        QVERIFY(FirmwareAccessPolicy::isTargetAllowed(identity, august.id));
        QVERIFY(!FirmwareAccessPolicy::isTargetAllowed(identity, july.id));

        identity.currentFirmwareId = august.id;
        QVERIFY(!FirmwareAccessPolicy::isTargetAllowed(identity, july.id));
        QVERIFY(!FirmwareAccessPolicy::isTargetAllowed(identity, august.id));

        identity.currentFirmwareId.clear();
        QVERIFY(!FirmwareAccessPolicy::isTargetAllowed(identity, july.id));
        QVERIFY(!FirmwareAccessPolicy::isTargetAllowed(identity, august.id));

        identity.catalogId = QStringLiteral("boc.v12");
        identity.currentFirmwareId = july.id;
        QVERIFY(FirmwareAccessPolicy::isTargetAllowed(identity, august.id));
    }
};

QTEST_APPLESS_MAIN(ExternalEditionTest)
#include "tst_external_edition.moc"
