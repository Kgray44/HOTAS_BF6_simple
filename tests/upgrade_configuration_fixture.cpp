#include "button_mapping.h"
#include "config_store.h"
#include "profile_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include <cstring>
#include <iostream>

namespace {

using namespace Qt::StringLiterals;

constexpr auto kConfigKey = "mapper/config";

QString settingsFilePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return directory + u"/settings.ini"_qs;
}

bool writeFixture(int schemaVersion)
{
    hotas::MapperConfiguration configuration = hotas::defaultConfiguration();
    configuration.preferredDeviceId = QStringLiteral("upgrade-fixture-controller");
    configuration.vjoyDeviceId = 2;
    configuration.startMappingOnLaunch = false;
    configuration.disabledAxisValue = -0.25F;
    configuration.selectedAxisIndex = static_cast<int>(hotas::PhysicalAxis::Rz);

    hotas::ControllerProfile &profile = configuration.profiles.front();
    profile.axes[static_cast<size_t>(hotas::PhysicalAxis::X)].deadzone = 0.17F;
    profile.axes[static_cast<size_t>(hotas::PhysicalAxis::X)].customName = QStringLiteral("Upgrade Roll");
    profile.buttons = hotas::defaultButtonMappings(6, 32);
    profile.buttons[5] = {hotas::ButtonActionType::VirtualButton, 28, true, QStringLiteral("Upgrade Fire")};
    profile.povs.resize(1);
    profile.povs[0][static_cast<size_t>(hotas::povDirectionIndex(hotas::PovDirection::Up))] = {
        hotas::ButtonActionType::VirtualButton, 29, true, QStringLiteral("Upgrade POV")};

    hotas::AutomationDefinition automation;
    automation.id = QStringLiteral("upgrade-automation");
    automation.name = QStringLiteral("Upgrade Automation");
    hotas::AutomationConditionDefinition condition;
    condition.type = hotas::AutomationConditionType::ButtonHeld;
    condition.button = 6;
    hotas::AutomationActionDefinition action;
    action.type = hotas::AutomationActionType::VJoyButtonHold;
    action.virtualButton = 30;
    automation.conditions = {condition};
    automation.actions = {action};
    configuration.automations = {automation};

    QJsonObject document = hotas::ConfigStore::toJson(configuration);
    document.insert(QStringLiteral("version"), schemaVersion);
    if (schemaVersion == 14) {
        document.remove(QStringLiteral("savedControllers"));
        document.remove(QStringLiteral("activeControllerRecordId"));
        document.remove(QStringLiteral("autoSwitchVerifiedController"));
        document.remove(QStringLiteral("keepRunningInTray"));
    }

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(kConfigKey), QJsonDocument(document).toJson(QJsonDocument::Compact));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool assertMigratedFixture()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(QLatin1String(kConfigKey)).toByteArray());
    if (!document.isObject() || document.object().value(QStringLiteral("version")).toInt() != 19) {
        std::cerr << "Expected the installed mapper to persist schema 19.\n";
        return false;
    }

    bool valid = false;
    const hotas::MapperConfiguration configuration = hotas::ConfigStore::fromJson(document.object(), &valid);
    if (!valid || configuration.preferredDeviceId != QStringLiteral("upgrade-fixture-controller")
        || configuration.vjoyDeviceId != 2 || configuration.disabledAxisValue != -0.25F
        || configuration.automations.size() != 1
        || configuration.automations.front().name != QStringLiteral("Upgrade Automation")
        || configuration.profiles.empty() || configuration.outputLayouts.size() != 1) {
        std::cerr << "Migrated fixture lost application settings, profiles, or automation.\n";
        return false;
    }

    const hotas::ControllerProfile &profile = configuration.profiles.front();
    const hotas::AxisMapping &roll = profile.axes[static_cast<size_t>(hotas::PhysicalAxis::X)];
    if (roll.customName != QStringLiteral("Upgrade Roll") || roll.deadzone != 0.17F
        || profile.buttons.size() != 6 || profile.buttons[5].target != 28
        || profile.povs.size() != 1
        || profile.povs[0][static_cast<size_t>(hotas::povDirectionIndex(hotas::PovDirection::Up))].target != 29
        || profile.outputLayoutId != hotas::defaultOutputLayoutId()
        || configuration.outputLayouts.front().requirements.deviceId != 2
        || !hotas::ConfigStore::toJson(configuration).value(QStringLiteral("profiles")).toArray()
                .first().toObject().value(QStringLiteral("axes")).toArray().first().toObject()
                .contains(QStringLiteral("curve"))) {
        std::cerr << "Migrated fixture lost curve, button, or POV data.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    bool testMode = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--test-mode") == 0) {
            testMode = true;
            break;
        }
    }
    if (testMode) {
        // Match the application's isolated startup smoke location. This keeps
        // fixture seeding out of a developer's or player's live settings.
        QStandardPaths::setTestModeEnabled(true);
    }
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));

    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--clear"))) {
        QFile::remove(settingsFilePath());
        return 0;
    }
    if (arguments.contains(QStringLiteral("--seed-v14"))) return writeFixture(14) ? 0 : 1;
    if (arguments.contains(QStringLiteral("--seed-v15"))) return writeFixture(15) ? 0 : 1;
    if (arguments.contains(QStringLiteral("--assert-v19"))) return assertMigratedFixture() ? 0 : 1;

    std::cerr << "Use --clear, --seed-v14, --seed-v15, or --assert-v19 (optionally with --test-mode).\n";
    return 2;
}
