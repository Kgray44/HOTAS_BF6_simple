#pragma once

#include "mapping_types.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>

namespace hotas {

// Setup is deliberately modeled separately from MappingWorker.  These are
// control-plane snapshots and plans; the mapper's report loop never owns a
// process, registry, service, or driver-configuration concern.
struct PhysicalControllerCapabilities {
    QString name;
    QString directInputId;
    // Normalized HID instance identity captured from DirectInput's device
    // path. An empty value means automatic HidHide changes are unsafe.
    QString hidInstanceId;
    bool connected = false;
    std::array<bool, kPhysicalAxisCount> axes{};
    int buttons = 0;
    int povs = 0;
};

struct VJoyCapabilities {
    bool installed = false;
    bool configurationUtilityAvailable = false;
    bool driverReady = false;
    bool devicePresent = false;
    bool busy = false;
    bool reportValid = false;
    int deviceId = 1;
    QList<int> availableDeviceIds;
    std::array<bool, kVirtualAxisSlotCount> axes{};
    int buttons = 0;
    int continuousPovs = 0;
    int discretePovs = 0;
    // vJoyConfig has to recreate the descriptor when changing capabilities.
    // Preserve FFB verbatim when it can be reported; otherwise do not offer a
    // destructive automatic reconfiguration.
    bool forceFeedbackKnown = false;
    QStringList forceFeedbackEffects;
    QString restoreCommand;
    QString diagnostic;
};

struct HidHideCapabilities {
    bool installed = false;
    bool cliAvailable = false;
    bool serviceReady = false;
    bool cloakKnown = false;
    bool cloaked = false;
    bool mapperAllowlisted = false;
    bool selectedControllerResolved = false;
    bool selectedControllerHidden = false;
    QStringList allowlistedApplications;
    QStringList hiddenDeviceInstanceIds;
    QString diagnostic;
};

struct MapperOutputRequirements {
    std::array<bool, kVirtualAxisSlotCount> axes{};
    int buttons = 0;
    int continuousPovs = 0;
    int discretePovs = 0;
    bool incompatiblePovMix = false;
};

enum class ControllerReadinessState {
    Idle,
    Inspecting,
    NeedsChanges,
    AwaitingPermission,
    Applying,
    Verifying,
    Ready,
    Failed,
    RollingBack,
};

struct ControllerReadinessPlan {
    ControllerReadinessState state = ControllerReadinessState::Idle;
    PhysicalControllerCapabilities physical;
    MapperOutputRequirements requirements;
    VJoyCapabilities vjoy;
    HidHideCapabilities hidhide;
    bool vjoyNeedsChanges = false;
    bool hidhideNeedsChanges = false;
    bool vjoyCanApply = false;
    bool hidhideCanApply = false;
    bool canApplyAutomatically = false;
    QStringList findings;
    QStringList proposedChanges;
    QString status;
};

struct SetupProcessResult {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QString output;
    QString error;

    bool succeeded() const { return started && finished && exitCode == 0; }
};

// Production discovers these paths from the supported installations. Tests
// inject an explicit set so planning/application transactions are deterministic
// and never require a driver in CI.
struct SetupUtilityPaths {
    bool supplied = false;
    QString vjoyConfig;
    QString vjoyConf;
    QString hidhideCli;
    QString hidhideClient;
    bool hidhideServiceReady = false;
};

class SetupProcessRunner {
public:
    virtual ~SetupProcessRunner() = default;
    virtual SetupProcessResult run(const QString &program, const QStringList &arguments,
                                   int timeoutMs) = 0;
    virtual SetupProcessResult runElevated(const QString &program, const QStringList &arguments,
                                           int timeoutMs) = 0;
};

// Real runner uses QProcess for inspection and ShellExecuteEx("runas") only
// after the user has confirmed an automatic repair in the UI.
class WindowsSetupProcessRunner final : public SetupProcessRunner {
public:
    SetupProcessResult run(const QString &program, const QStringList &arguments,
                           int timeoutMs) override;
    SetupProcessResult runElevated(const QString &program, const QStringList &arguments,
                                   int timeoutMs) override;
};

class ControllerReadinessService final {
public:
    explicit ControllerReadinessService(std::unique_ptr<SetupProcessRunner> runner = {},
                                        SetupUtilityPaths utilityPaths = {});

    static MapperOutputRequirements requirementsFor(const MapperConfiguration &configuration);
    static ControllerReadinessPlan planFor(const PhysicalControllerCapabilities &physical,
                                           const MapperOutputRequirements &requirements,
                                           const VJoyCapabilities &vjoy,
                                           const HidHideCapabilities &hidhide);
    static QString stateLabel(ControllerReadinessState state);
    static QString normalizeDeviceInstanceId(QString value);

    const ControllerReadinessPlan &inspect(const MapperConfiguration &configuration,
                                           const PhysicalControllerCapabilities &physical);
    bool applyAutomatically();
    bool undoLastAutomaticSetup();

    const ControllerReadinessPlan &plan() const { return m_plan; }
    bool transactionActive() const { return m_transactionActive; }
    bool canUndo() const;

private:
    struct Journal {
        bool available = false;
        bool vjoyChanged = false;
        bool vjoyWasAbsent = false;
        bool mapperWasAdded = false;
        bool controllerWasHidden = false;
        bool cloakWasEnabled = false;
        QString vjoyRestoreCommand;
        QString mapperExecutable;
        QString controllerInstanceId;
    };

    VJoyCapabilities inspectVJoy(int deviceId) const;
    HidHideCapabilities inspectHidHide(const PhysicalControllerCapabilities &physical) const;
    bool applyVJoy(const ControllerReadinessPlan &plan, Journal *journal, QString *failure);
    bool applyHidHide(const ControllerReadinessPlan &plan, Journal *journal, QString *failure);
    bool rollback(Journal *journal, QString *failure);
    bool verifyReady();
    SetupProcessResult runHidHide(bool elevated, const QStringList &arguments) const;
    SetupProcessResult runVJoy(bool elevated, const QStringList &arguments) const;
    QString mapperExecutablePath() const;
    QString vjoyConfigPath() const;
    QString vjoyConfPath() const;
    QString hidhideCliPath() const;
    QString hidhideClientPath() const;
    bool hidhideServiceReady() const;
    static QString findVJoyConfig();
    static QString findVJoyConf();
    static QString findHidHideCli();
    static QString findHidHideClient();
    static bool hasHidHideService();
    static QString decodeOutput(const QByteArray &bytes);
    static VJoyCapabilities parseVJoyReport(const QString &report, int deviceId);
    static QStringList parseHidHideCommands(const QString &output, const QString &command);
    static bool outputContainsDevice(const QString &output, const QString &instanceId);
    static QStringList vjoyConfigurationArguments(const VJoyCapabilities &before,
                                                   const MapperOutputRequirements &requirements);
    static QString describeVJoyRequirement(const MapperOutputRequirements &requirements);
    static bool isVJoySufficient(const VJoyCapabilities &vjoy,
                                 const MapperOutputRequirements &requirements);

    std::unique_ptr<SetupProcessRunner> m_runner;
    ControllerReadinessPlan m_plan;
    MapperConfiguration m_configuration;
    PhysicalControllerCapabilities m_physical;
    bool m_transactionActive = false;
    Journal m_journal;
    SetupUtilityPaths m_utilityPaths;
};

} // namespace hotas
