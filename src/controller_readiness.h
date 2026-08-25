#pragma once

#include "mapping_types.h"

#include <QByteArray>
#include <QDateTime>
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
    // A completed DirectInput poll is stronger evidence than a second handle
    // acquisition attempt. The mapper publishes this outside its report path.
    bool inputReportsReceived = false;
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
    // vJoyConfig only reports BUSY; the mapper already knows whether that
    // owner is this HOTAS BF6 process. Own acquisition is a healthy state.
    bool ownedByHotasBf6 = false;
    bool outputReportsSucceeding = false;
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
    Attention,
    Failed,
    Cancelled,
    RollingBack,
};

enum class VerificationMode {
    None,
    Quick,
    Full,
};

enum class VerificationSubsystemState {
    Unknown,
    Checking,
    Ready,
    Attention,
    Error,
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
    VerificationMode verificationMode = VerificationMode::None;
    VerificationSubsystemState physicalStatus = VerificationSubsystemState::Unknown;
    VerificationSubsystemState vjoyStatus = VerificationSubsystemState::Unknown;
    VerificationSubsystemState hidhideStatus = VerificationSubsystemState::Unknown;
    QString physicalSummary;
    QString vjoySummary;
    QString hidhideSummary;
    QDateTime lastChecked;
    bool isChecking = false;
};

struct SetupProcessResult {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QString output;
    QString error;
    int windowsErrorCode = 0;
    bool cancelled = false;
    QString errorOutput;

    bool succeeded() const { return started && finished && exitCode == 0; }
};

enum class AutomaticRepairOutcome {
    None,
    Ready,
    Attention,
    Failed,
    Cancelled,
};

// Kept outside MappingWorker with the rest of the setup control plane.  The
// complete result is retained for the verification UI and event log, while the
// normal Settings card receives only the concise plan status.
struct AutomaticRepairOperationResult {
    QString operationName;
    bool started = false;
    bool finished = false;
    bool succeeded = false;
    bool rollback = false;
    int exitCode = -1;
    int windowsErrorCode = 0;
    QString message;
    QString output;
    QString errorOutput;
};

struct AutomaticRepairResult {
    AutomaticRepairOutcome outcome = AutomaticRepairOutcome::None;
    QString message;
    QList<AutomaticRepairOperationResult> operations;
    bool requiresRestart = false;
    bool requiresReboot = false;
    bool physicalReacquisitionAttempted = false;
    bool physicalReacquisitionSucceeded = false;
    bool physicalReportsReceivedAfterRepair = false;
    bool rollbackAttempted = false;
    bool rollbackSucceeded = false;
    bool physicalReportsReceivedAfterRollback = false;

    bool completed() const {
        return outcome == AutomaticRepairOutcome::Ready || outcome == AutomaticRepairOutcome::Attention;
    }
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
                                           const HidHideCapabilities &hidhide,
                                           VerificationMode mode = VerificationMode::Full);
    static QString stateLabel(ControllerReadinessState state);
    static QString subsystemStateLabel(VerificationSubsystemState state);
    static QString normalizeDeviceInstanceId(QString value);
    static ControllerReadinessPlan checkingPlan(const PhysicalControllerCapabilities &physical,
                                                VerificationMode mode);
    static bool needsSetupAfterControllerArrival(bool isNewPhysicalArrival,
                                                 const ControllerReadinessPlan &plan);

    const ControllerReadinessPlan &inspect(const MapperConfiguration &configuration,
                                           const PhysicalControllerCapabilities &physical,
                                           VerificationMode mode = VerificationMode::Full,
                                           bool mapperOwnsVjoy = false,
                                           bool outputReportsSucceeding = false);
    bool applyAutomatically();
    bool undoLastAutomaticSetup();
    // The mapper performs this proof after a forced DirectInput reopen. A
    // HidHide CLI read-back is never sufficient to declare a repair safe.
    void completePhysicalAccessVerification(bool reacquired, bool reportsReceived,
                                            bool rollbackAttempted = false,
                                            bool rollbackSucceeded = false,
                                            bool reportsReceivedAfterRollback = false);
    // Used only after the post-change DirectInput proof fails. It reverses
    // the narrow journal entries created by this automatic transaction.
    bool recoverFromPhysicalAccessFailure();
    bool hasPendingRecovery() const { return m_journal.available; }

    const ControllerReadinessPlan &plan() const { return m_plan; }
    const AutomaticRepairResult &lastAutomaticRepairResult() const { return m_lastRepairResult; }
    void adoptPlan(ControllerReadinessPlan plan) { m_plan = std::move(plan); }
    bool transactionActive() const { return m_transactionActive; }
    bool canUndo() const;

private:
    struct RepairOperation {
        QString name;
        QString program;
        QStringList arguments;
        QString rollbackName;
        QStringList rollbackArguments;
        QString failureSummary;
    };

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
    QList<RepairOperation> repairOperationsFor(const ControllerReadinessPlan &plan, Journal *journal) const;
    AutomaticRepairResult runRepairTransaction(const QList<RepairOperation> &operations) const;
    bool verifyAfterRepair();
    bool rollback(Journal *journal, QString *failure);
    void persistRecoveryJournal() const;
    void loadRecoveryJournal();
    void clearRecoveryJournal() const;
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
    AutomaticRepairResult m_lastRepairResult;
    SetupUtilityPaths m_utilityPaths;
};

} // namespace hotas
