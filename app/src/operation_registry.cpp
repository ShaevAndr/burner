#include "operation_registry.h"

#include <QtGlobal>
#include <QSet>
#include <cmath>
#include <utility>

namespace
{
OperationArgument integer(qint64 minimum = 0,
    qint64 maximum = std::numeric_limits<qint64>::max(), bool required = false)
{
    return {OperationArgument::Type::Integer, required, minimum, maximum};
}

OperationArgument string(bool required = false)
{
    return {OperationArgument::Type::String, required, 0, 0};
}

}

bool OperationContract::validate(const QVariantMap& step, QString* error) const
{
    static const QSet<QString> commonFields = {
        QStringLiteral("op"), QStringLiteral("label"), QStringLiteral("message"),
        QStringLiteral("skipIfState"), QStringLiteral("skippedLog"), QStringLiteral("retry")
    };
    for (auto it = step.constBegin(); it != step.constEnd(); ++it)
    {
        if (commonFields.contains(it.key()))
            continue;
        const auto rule = arguments.constFind(it.key());
        if (rule == arguments.constEnd())
        {
            if (error)
                *error = QStringLiteral("Unknown argument '%1' for operation %2").arg(it.key(), id);
            return false;
        }

        const QVariant& value = it.value();
        bool valid = false;
        switch (rule->type)
        {
        case OperationArgument::Type::Integer:
        {
            const bool numericType = value.type() == QVariant::Int
                || value.type() == QVariant::UInt
                || value.type() == QVariant::LongLong
                || value.type() == QVariant::ULongLong
                || value.type() == QVariant::Double;
            bool converted = false;
            const double number = value.toDouble(&converted);
            valid = numericType && converted && std::isfinite(number)
                && std::floor(number) == number
                && number >= rule->minimum && number <= rule->maximum;
            break;
        }
        case OperationArgument::Type::String:
            valid = value.type() == QVariant::String && !value.toString().trimmed().isEmpty();
            break;
        case OperationArgument::Type::Boolean:
            valid = value.type() == QVariant::Bool;
            break;
        }
        if (!valid)
        {
            if (error)
                *error = QStringLiteral("Invalid argument '%1' for operation %2").arg(it.key(), id);
            return false;
        }
    }

    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it)
    {
        if (it->required && !step.contains(it.key()))
        {
            if (error)
                *error = QStringLiteral("Missing argument '%1' for operation %2").arg(it.key(), id);
            return false;
        }
    }
    if ((id == QStringLiteral("device.writeProductionDate")
            || id == QStringLiteral("device.writeSerialNumber")
            || id == QStringLiteral("device.verifyRegister"))
        && !step.contains(QStringLiteral("value"))
        && !step.contains(QStringLiteral("valueFrom")))
    {
        if (error)
            *error = QStringLiteral("Operation %1 requires value or valueFrom").arg(id);
        return false;
    }
    if (step.contains(QStringLiteral("valueFrom")))
    {
        const QString source = step.value(QStringLiteral("valueFrom")).toString();
        if (source != QStringLiteral("productionDate") && source != QStringLiteral("serialNumber"))
        {
            if (error)
                *error = QStringLiteral("Unknown valueFrom '%1' for operation %2").arg(source, id);
            return false;
        }
    }
    if (id == QStringLiteral("device.verifyRegister"))
    {
        const QString registerName = step.value(QStringLiteral("register")).toString();
        if ((registerName != QStringLiteral("productionDate")
                && registerName != QStringLiteral("serialNumber"))
            || (step.contains(QStringLiteral("valueFrom"))
                && step.value(QStringLiteral("valueFrom")).toString() != registerName))
        {
            if (error)
                *error = QStringLiteral("Invalid register or valueFrom for operation %1").arg(id);
            return false;
        }
    }
    return true;
}

const OperationRegistry& OperationRegistry::instance()
{
    static const OperationRegistry registry;
    return registry;
}

const OperationContract* OperationRegistry::find(const QString& id) const
{
    const auto it = mOperations.constFind(id);
    return it == mOperations.constEnd() ? nullptr : &it.value();
}

void OperationRegistry::add(OperationContract contract)
{
    Q_ASSERT(!mOperations.contains(contract.id));
    mOperations.insert(contract.id, std::move(contract));
}

OperationRegistry::OperationRegistry()
{
    const auto runtime = [this](const QString& id,
        QHash<QString, OperationArgument> arguments = {},
        OperationSideEffect effect = OperationSideEffect::None) {
        OperationContract contract;
        contract.id = id;
        contract.kind = OperationKind::Runtime;
        contract.arguments = std::move(arguments);
        contract.sideEffect = effect;
        contract.idempotency = effect == OperationSideEffect::Destructive
            ? OperationIdempotency::Unsafe : OperationIdempotency::Safe;
        add(std::move(contract));
    };
    const auto device = [this](const QString& id, RegisteredDeviceHandler handler,
        QHash<QString, OperationArgument> arguments = {},
        OperationSideEffect effect = OperationSideEffect::None,
        OperationIdempotency idempotency = OperationIdempotency::Safe) {
        OperationContract contract;
        contract.id = id;
        contract.kind = OperationKind::Device;
        contract.arguments = std::move(arguments);
        contract.sideEffect = effect;
        contract.idempotency = idempotency;
        contract.handler = std::move(handler);
        add(std::move(contract));
    };

    runtime(QStringLiteral("context.productionDate"));
    runtime(QStringLiteral("context.serialNumber"));
    runtime(QStringLiteral("sleep"), {{QStringLiteral("ms"), integer(0, 60000, true)}});
    runtime(QStringLiteral("device.connect"), {
        {QStringLiteral("state"), string(true)},
        {QStringLiteral("ms"), integer(0, 60000)},
        {QStringLiteral("settleMs"), integer(0, 60000)}
    });
    runtime(QStringLiteral("device.ensureUuid"));
    runtime(QStringLiteral("firmware.validateTransition"));
    runtime(QStringLiteral("device.enterBootloader"), {
        {QStringLiteral("timeoutMs"), integer(1, 120000)},
        {QStringLiteral("pollIntervalMs"), integer(1, 10000)},
        {QStringLiteral("settleMs"), integer(0, 60000)}
    }, OperationSideEffect::Reversible);
    runtime(QStringLiteral("device.disableApplicationLoad"), {}, OperationSideEffect::Reversible);
    runtime(QStringLiteral("device.captureServiceData"));
    runtime(QStringLiteral("firmware.validateArtifact"));
    runtime(QStringLiteral("firmware.flash"), {}, OperationSideEffect::Destructive);
    runtime(QStringLiteral("firmware.verify"));
    runtime(QStringLiteral("device.restoreServiceData"), {}, OperationSideEffect::Reversible);
    runtime(QStringLiteral("device.waitForApplication"), {
        {QStringLiteral("timeoutMs"), integer(1, 120000)},
        {QStringLiteral("pollIntervalMs"), integer(1, 10000)},
        {QStringLiteral("retryLoadAttempts"), integer(0, 10)},
        {QStringLiteral("retryDelayMs"), integer(0, 60000)}
    });
    runtime(QStringLiteral("firmware.verifyInstalledVersion"));
    runtime(QStringLiteral("firmware.complete"));
    runtime(QStringLiteral("flash.prepare"));
    runtime(QStringLiteral("flash.validateArtifact"));
    runtime(QStringLiteral("flash.buildPagePlan"));
    runtime(QStringLiteral("flash.preflight"));
    runtime(QStringLiteral("flash.complete"));
    runtime(QStringLiteral("workflow.finish"));
    runtime(QStringLiteral("log"));

    device(QStringLiteral("device.reset"),
        [](DeviceBase& item, const QVariantMap&, QString* error, QString* raw) {
            return item.reset(error, raw);
        }, {}, OperationSideEffect::Reversible, OperationIdempotency::Conditional);
    device(QStringLiteral("device.loadApplication"),
        [](DeviceBase& item, const QVariantMap&, QString* error, QString* raw) {
            return item.loadApplication(error, raw);
        }, {}, OperationSideEffect::Reversible, OperationIdempotency::Conditional);
    device(QStringLiteral("device.loadApplicationNoReply"),
        [](DeviceBase& item, const QVariantMap&, QString* error, QString* raw) {
            return item.loadApplicationNoReply(error, raw);
        }, {}, OperationSideEffect::Reversible, OperationIdempotency::Conditional);
    device(QStringLiteral("device.disableLoadApplication"),
        [](DeviceBase& item, const QVariantMap&, QString* error, QString* raw) {
            return item.disableLoadApplication(error, raw);
        }, {}, OperationSideEffect::Reversible);
    device(QStringLiteral("device.writeProductionDate"),
        [](DeviceBase& item, const QVariantMap& args, QString* error, QString* raw) {
            return item.writeProductionDate(args.value(QStringLiteral("value")).toInt(), error, raw);
        }, {{QStringLiteral("value"), integer(std::numeric_limits<qint32>::min(),
                    std::numeric_limits<qint32>::max())},
            {QStringLiteral("valueFrom"), string()}}, OperationSideEffect::Destructive,
            OperationIdempotency::Conditional);
    device(QStringLiteral("device.writeSerialNumber"),
        [](DeviceBase& item, const QVariantMap& args, QString* error, QString* raw) {
            return item.writeSerialNumber(args.value(QStringLiteral("value")).toInt(), error, raw);
        }, {{QStringLiteral("value"), integer(std::numeric_limits<qint32>::min(),
                    std::numeric_limits<qint32>::max())},
            {QStringLiteral("valueFrom"), string()}}, OperationSideEffect::Destructive,
            OperationIdempotency::Conditional);
    device(QStringLiteral("device.verifyRegister"),
        [](DeviceBase& item, const QVariantMap& args, QString* error, QString* raw) {
            const QString registerName = args.value(QStringLiteral("register")).toString();
            const int registerIndex = registerName == QStringLiteral("productionDate")
                ? item.productionDateRegister() : item.serialNumberRegister();
            if (registerIndex < 0)
            {
                if (error)
                    *error = QStringLiteral("Register %1 is not configured").arg(registerName);
                return false;
            }
            qint32 actual = 0;
            if (!item.readInt(quint16(registerIndex), &actual, error, raw))
                return false;
            const qint32 expected = args.value(QStringLiteral("value")).toInt();
            if (actual != expected)
            {
                if (error)
                    *error = QStringLiteral("Register %1 readback mismatch: expected %2, got %3")
                        .arg(registerName).arg(expected).arg(actual);
                return false;
            }
            return true;
        }, {{QStringLiteral("register"), string(true)},
            {QStringLiteral("value"), integer(std::numeric_limits<qint32>::min(),
                std::numeric_limits<qint32>::max())},
            {QStringLiteral("valueFrom"), string()}}, OperationSideEffect::None);
    device(QStringLiteral("device.ping"),
        [](DeviceBase& item, const QVariantMap& args, QString* error, QString* raw) {
            qint32 value = 0;
            return item.readInt(quint16(args.value(QStringLiteral("index"), 0).toUInt()),
                &value, error, raw);
        }, {{QStringLiteral("index"), integer(0, 65535)}});
}
