#ifndef DEVICE_WORKBENCH_OPERATION_REGISTRY_H
#define DEVICE_WORKBENCH_OPERATION_REGISTRY_H

#include "base_device.h"

#include <QHash>
#include <QVariantMap>
#include <functional>
#include <limits>

enum class OperationKind { Runtime, Device };
enum class OperationSideEffect { None, Reversible, Destructive };
enum class OperationIdempotency { Safe, Conditional, Unsafe };

struct OperationError
{
    QString code;
    QString category;
    QString operationId;
    QString safeMessage;
    QString technicalDetails;
    bool retryable = false;

    bool isValid() const { return !code.isEmpty(); }
};

struct OperationArgument
{
    enum class Type { Integer, String, Boolean };
    Type type = Type::Integer;
    bool required = false;
    qint64 minimum = std::numeric_limits<qint64>::min();
    qint64 maximum = std::numeric_limits<qint64>::max();
};

using RegisteredDeviceHandler = std::function<bool(DeviceBase&, const QVariantMap&,
    QString*, QString*)>;

struct OperationContract
{
    QString id;
    OperationKind kind = OperationKind::Runtime;
    OperationSideEffect sideEffect = OperationSideEffect::None;
    OperationIdempotency idempotency = OperationIdempotency::Safe;
    QHash<QString, OperationArgument> arguments;
    RegisteredDeviceHandler handler;

    bool validate(const QVariantMap& step, QString* error = nullptr) const;
};

class OperationRegistry
{
public:
    static const OperationRegistry& instance();
    const OperationContract* find(const QString& id) const;

private:
    OperationRegistry();
    void add(OperationContract contract);
    QHash<QString, OperationContract> mOperations;
};

#endif // DEVICE_WORKBENCH_OPERATION_REGISTRY_H
