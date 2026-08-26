#include "../src/app_edition.h"

#include <QtTest>

class ExternalEditionTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesFirmwareActionsOnly()
    {
        QVERIFY(!AppEdition::isInternal());
        QCOMPARE(AppEdition::id(), QStringLiteral("external"));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("flash.application.write")));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("flash.bootloader.write")));
        QVERIFY(AppEdition::allowsAction(QStringLiteral("device.application.load")));
        QVERIFY(!AppEdition::allowsAction(QStringLiteral("device.productionDate.update")));
        QVERIFY(!AppEdition::allowsAction(QStringLiteral("device.serialNumber.update")));
        QVERIFY(!AppEdition::allowsAction(QStringLiteral("device.ping")));
    }
};

QTEST_APPLESS_MAIN(ExternalEditionTest)
#include "tst_external_edition.moc"
