#include "boc_v12_device.h"

QString BocV12Device::className() const
{
    return QStringLiteral("BocV12Device");
}

bool BocV12Device::disableLoadApplication(QString* error, QString* rawResponse) const
{
    return writeInt(0, 0, error, rawResponse);
}

bool BocV12Device::writeProductionDate(qint32 timestamp, QString* error, QString* rawResponse) const
{
    return writeInt(9, timestamp, error, rawResponse);
}

bool BocV12Device::writeSerialNumber(qint32 serialNumber, QString* error, QString* rawResponse) const
{
    return writeInt(10, serialNumber, error, rawResponse);
}
