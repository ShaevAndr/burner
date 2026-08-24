#ifndef DEVICE_WORKBENCH_BOC_V12_DEVICE_H
#define DEVICE_WORKBENCH_BOC_V12_DEVICE_H

#include "../base_device.h"

class BocV12Device : public DeviceBase
{
public:
    using DeviceBase::DeviceBase;
    QString className() const override;
    bool disableLoadApplication(QString* error = nullptr, QString* rawResponse = nullptr) const override;
};

#endif // DEVICE_WORKBENCH_BOC_V12_DEVICE_H
