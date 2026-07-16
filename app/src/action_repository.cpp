#include "action_repository.h"
#include "device.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

bool ActionRepository::load(const QString& fileName, QString* error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("Cannot open actions %1: %2").arg(fileName, file.errorString());
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        if (error)
            *error = QStringLiteral("Actions %1 is not a JSON object").arg(fileName);
        return false;
    }

    mActions.clear();
    const QJsonArray actions = doc.object().value(QStringLiteral("actions")).toArray();
    for (const QJsonValue& value : actions)
    {
        const QJsonObject obj = value.toObject();
        ActionSpec action;
        action.id = obj.value(QStringLiteral("id")).toString();
        action.title = obj.value(QStringLiteral("title")).toString(action.id);
        action.workflow = obj.value(QStringLiteral("workflow")).toString();
        action.selection = obj.value(QStringLiteral("selection")).toString(QStringLiteral("many"));

        const QJsonObject when = obj.value(QStringLiteral("when")).toObject();
        const QJsonArray capabilities = when.value(QStringLiteral("capabilitiesAll")).toArray();
        for (const QJsonValue& cap : capabilities)
            action.requiredCapabilities.append(cap.toString());

        const QJsonArray inputs = obj.value(QStringLiteral("inputs")).toArray();
        for (const QJsonValue& inputValue : inputs)
        {
            const QJsonObject input = inputValue.toObject();
            if (input.value(QStringLiteral("name")).toString() == QStringLiteral("artifact"))
                action.target = input.value(QStringLiteral("target")).toString();
        }

        if (!action.id.isEmpty())
            mActions.append(action);
    }
    return true;
}

QVector<ActionSpec> ActionRepository::actionsForDevice(const DeviceIdentity& device) const
{
    QVector<ActionSpec> result;
    for (const ActionSpec& action : mActions)
    {
        if (isActionAllowed(action, device))
            result.append(action);
    }
    return result;
}

QVector<ActionSpec> ActionRepository::commonActions(const QVector<std::shared_ptr<DeviceBase>>& devices) const
{
    QVector<ActionSpec> result;
    if (devices.isEmpty())
        return result;

    for (const ActionSpec& action : mActions)
    {
        if (action.selection == QStringLiteral("single"))
            continue;

        bool allowedForAll = true;
        for (const std::shared_ptr<DeviceBase>& device : devices)
        {
            if (!device || !isActionAllowed(action, device->identity()))
            {
                allowedForAll = false;
                break;
            }
        }
        if (allowedForAll)
            result.append(action);
    }
    return result;
}

bool ActionRepository::isActionAllowed(const ActionSpec& action, const DeviceIdentity& device) const
{
    if (action.id == QStringLiteral("device.ping"))
        return true;

    if (!device.known)
        return false;

    const QSet<QString> caps = QSet<QString>::fromList(device.capabilities);
    for (const QString& required : action.requiredCapabilities)
    {
        if (!caps.contains(required))
            return false;
    }
    return true;
}
