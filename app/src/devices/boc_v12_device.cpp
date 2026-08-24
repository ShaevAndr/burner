#include "boc_v12_device.h"

QString BocV12Device::className() const
{
    return QStringLiteral("BocV12Device");
}

bool BocV12Device::disableLoadApplication(QString* error, QString* rawResponse) const
{
    return writeInt(0, 0, error, rawResponse);
}
