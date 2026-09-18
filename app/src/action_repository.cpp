#include "action_repository.h"
#include "app_edition.h"
#include "base_device.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <utility>

bool ActionRepository::load(const QString& fileName, QString* error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("Cannot open actions %1: %2").arg(fileName, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
            *error = QStringLiteral("Actions %1 is not a JSON object").arg(fileName);
        return false;
    }
    const QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("schemaVersion")).isDouble()
        || root.value(QStringLiteral("schemaVersion")).toDouble() != 1.0
        || !root.value(QStringLiteral("actions")).isArray())
    {
        if (error)
            *error = QStringLiteral("CONFIG_INVALID_SCHEMA: actions require schemaVersion 1 and an actions array");
        return false;
    }

    QVector<ActionSpec> loaded;
    QSet<QString> ids;
    const QJsonArray actions = root.value(QStringLiteral("actions")).toArray();
    for (const QJsonValue& value : actions)
    {
        const QJsonObject obj = value.toObject();
        ActionSpec action;
        action.id = obj.value(QStringLiteral("id")).toString().trimmed();
        action.title = obj.value(QStringLiteral("title")).toString(action.id);
        action.workflow = obj.value(QStringLiteral("workflow")).toString().trimmed();
        action.selection = obj.value(QStringLiteral("selection")).toString(QStringLiteral("many"));
        if (action.id.isEmpty() || ids.contains(action.id) || action.workflow.isEmpty()
            || (action.selection != QStringLiteral("single")
                && action.selection != QStringLiteral("many")))
        {
            if (error)
                *error = QStringLiteral("Invalid or duplicate action: %1").arg(action.id);
            return false;
        }
        ids.insert(action.id);

        const QJsonObject when = obj.value(QStringLiteral("when")).toObject();
        const QJsonArray capabilities = when.value(QStringLiteral("capabilitiesAll")).toArray();
        for (const QJsonValue& cap : capabilities)
            action.requiredCapabilities.append(cap.toString());
        const QJsonArray states = when.value(QStringLiteral("statesAny")).toArray();
        for (const QJsonValue& state : states)
        {
            if (state.toString() != QStringLiteral("application")
                && state.toString() != QStringLiteral("bootloader"))
            {
                if (error)
                    *error = QStringLiteral("Invalid state in action %1").arg(action.id);
                return false;
            }
            action.allowedStates.append(state.toString());
        }

        const QJsonArray inputs = obj.value(QStringLiteral("inputs")).toArray();
        for (const QJsonValue& inputValue : inputs)
        {
            const QJsonObject input = inputValue.toObject();
            if (input.value(QStringLiteral("name")).toString() == QStringLiteral("artifact"))
                action.target = input.value(QStringLiteral("target")).toString();
        }

        if (AppEdition::allowsAction(action.id))
            loaded.append(action);
    }
    if (loaded.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Actions %1 has no actions for this edition").arg(fileName);
        return false;
    }
    mActions = std::move(loaded);
    return true;
}

bool ActionRepository::validateAgainstProfiles(
    const QVector<std::shared_ptr<const DeviceProfile>>& profiles, QString* error) const
{
    if (error)
        error->clear();
    for (const ActionSpec& action : mActions)
    {
        bool applicable = false;
        for (const auto& profile : profiles)
        {
            if (!profile)
                continue;
            bool supportsAction = true;
            for (const QString& required : action.requiredCapabilities)
            {
                if (!profile->capabilities.contains(required))
                {
                    supportsAction = false;
                    break;
                }
            }
            if (!supportsAction)
                continue;
            applicable = true;
            if (action.target.isEmpty())
                continue;
            bool hasArtifact = false;
            for (const FirmwareArtifact& artifact : profile->firmwareArtifacts)
            {
                if (artifact.target == action.target && !artifact.relativePath.isEmpty())
                {
                    hasArtifact = true;
                    break;
                }
            }
            if (!hasArtifact)
            {
                if (error)
                    *error = QStringLiteral("CONFIG_ACTION_MISSING_ARTIFACT: action %1 targets %2 on profile %3")
                        .arg(action.id, action.target, profile->id);
                return false;
            }
        }
        if (!applicable)
        {
            if (error)
                *error = QStringLiteral("CONFIG_ACTION_UNAVAILABLE: action %1 has no matching profile")
                    .arg(action.id);
            return false;
        }
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

QVector<ActionSpec> ActionRepository::actionsForDevice(const DeviceBase& device) const
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
            if (!device || !isActionAllowed(action, *device))
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

bool ActionRepository::isActionAllowed(const ActionSpec& action, const DeviceBase& device) const
{
    const auto& profile = device.profile();
    if (!profile)
        return isActionAllowed(action, device.identity());
    if (action.id == QStringLiteral("device.ping"))
        return true;
    if (!action.allowedStates.isEmpty()
        && !action.allowedStates.contains(device.identity().state))
        return false;
    for (const QString& required : action.requiredCapabilities)
    {
        if (!profile->capabilities.contains(required))
            return false;
    }
    return true;
}

bool ActionRepository::isActionAllowed(const ActionSpec& action, const DeviceIdentity& device) const
{
    if (action.id == QStringLiteral("device.ping"))
        return true;

    if (!device.known)
        return false;

    if (!action.allowedStates.isEmpty() && !action.allowedStates.contains(device.state))
        return false;

    for (const QString& required : action.requiredCapabilities)
    {
        if (!device.capabilities.contains(required))
            return false;
    }
    return true;
}
