#include "doctor_diagnostics.h"
#include "doctor_hidhide_configuration_mutator.h"
#include "doctor_deep_execution.h"
#include "doctor_repair_helper_protocol.h"
#include "doctor_repair_engine.h"
#include "hotas_build_version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QScopeGuard>

#include <atomic>

#include <windows.h>
#include <sddl.h>

namespace {

using namespace hotas::doctor;

bool writeExact(HANDLE pipe, const QByteArray &payload)
{
    const DWORD size = static_cast<DWORD>(payload.size());
    DWORD written = 0;
    return WriteFile(pipe, &size, sizeof(size), &written, nullptr) && written == sizeof(size)
        && WriteFile(pipe, payload.constData(), size, &written, nullptr) && written == size;
}

bool readExact(HANDLE pipe, void *buffer, DWORD bytes)
{
    DWORD read = 0;
    return ReadFile(pipe, buffer, bytes, &read, nullptr) && read == bytes;
}

bool elevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    const auto close = qScopeGuard([&] { CloseHandle(token); });
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    return GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) && elevation.TokenIsElevated;
}

QByteArray response(bool accepted, const QString &detail, const RepairExecutionResult *result = nullptr)
{
    QJsonObject object{{QStringLiteral("protocolVersion"), RepairHelperRequest::protocolVersion},
        {QStringLiteral("accepted"), accepted}, {QStringLiteral("detail"), detail}};
    if (result) {
        object.insert(QStringLiteral("transactionId"), result->transaction.id.value());
        object.insert(QStringLiteral("state"), displayName(result->transaction.state));
        object.insert(QStringLiteral("mutated"), result->mutated);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

class SnapshotPostconditionProvider final : public IReadOnlyDiagnosticProvider {
public:
    explicit SnapshotPostconditionProvider(ReadOnlyDiagnosticSnapshot snapshot) : m_snapshot(std::move(snapshot)) {}
    ReadOnlyDiagnosticSnapshot observe(std::atomic_bool *, ObservationProgress) override { return m_snapshot; }
private:
    ReadOnlyDiagnosticSnapshot m_snapshot;
};

class NativePostconditionVerifier final : public IRepairPostconditionVerifier {
public:
    bool verify(const RepairPlan &plan, const HidHideConfigurationSnapshot &, QString *reason) override
    {
        // This is deliberately a second provider observation rather than a
        // re-use of the pre-mutation snapshot or the helper mutator's cached
        // values. It verifies configuration, provider capability, collateral,
        // and the affected diagnosis from fresh read-only evidence.
        ReadOnlyWindowsDiagnosticProvider provider;
        ReadOnlyDiagnosticSnapshot fresh = provider.observe();
        if (!fresh.environment.hidhide.present || !fresh.environment.capabilities.directProtocolAvailable
            || !fresh.environment.capabilities.helperArchitectureCompatible) {
            if (reason) *reason = QStringLiteral("Independent post-repair provider health or capability verification failed.");
            return false;
        }
        const std::optional<HidHideConfigurationSnapshot> configuration = RepairPlanner::configurationFrom(fresh);
        if (!configuration || configuration->fingerprint() != plan.expectedPostFingerprint) {
            if (reason) *reason = QStringLiteral("Independent post-repair configuration and collateral read-back did not match the authorized post-state.");
            return false;
        }
        // Reconstruct only the plan's already-bound HOTAS intent for the
        // affected diagnosis. The helper still obtains every mutable target
        // from the sealed plan; no ambient application identity is trusted.
        fresh.environment.repairIntent.suppliedByHotas = true;
        for (const RepairOperation &operation : plan.operations) {
            if (operation.kind == RepairOperationKind::AddWhitelistEntry)
                fresh.environment.repairIntent.expectedExecutable = operation.targetIdentity;
            if (operation.kind == RepairOperationKind::RemoveBlacklistEntry)
                fresh.environment.repairIntent.expectedVirtualOutputIds.append(operation.targetIdentity);
        }
        SnapshotPostconditionProvider diagnosisProvider(fresh);
        const DiagnosticRunOutcome outcome = DoctorDiagnosticEngine().run(diagnosisProvider);
        const std::optional<RepairRecipe> recipe = RepairRecipeRegistry::recipe(plan.recipeId);
        if (!recipe) { if (reason) *reason = QStringLiteral("Post-repair recipe identity is unavailable."); return false; }
        for (const Diagnosis &diagnosis : outcome.session.diagnoses()) {
            if (recipe->supportedDiagnosisIds.contains(diagnosis.id.value())) {
                if (reason) *reason = QStringLiteral("The affected diagnosis remains after independent verification.");
                return false;
            }
        }
        return true;
    }
};

int serveOneRequest(const QString &pipeName, const QString &nonce, const QString &doctorBuild)
{
    // Owner (the interactive user who elevated this short-lived helper) and
    // LocalSystem are the only principals admitted to this native pipe.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)(A;;GA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr)) return 2;
    const auto freeDescriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const QString fullName = QStringLiteral("\\\\.\\pipe\\") + pipeName;
    HANDLE pipe = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(fullName.utf16()), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
        static_cast<DWORD>(RepairHelperRequest::maximumMessageBytes + sizeof(DWORD)),
        static_cast<DWORD>(RepairHelperRequest::maximumMessageBytes + sizeof(DWORD)), 45000, &attributes);
    if (pipe == INVALID_HANDLE_VALUE) return 2;
    const auto close = qScopeGuard([&] { DisconnectNamedPipe(pipe); CloseHandle(pipe); });
    const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (!connected) return 3;

    DWORD clientPid = 0, clientSession = 0, helperSession = 0;
    if (!GetNamedPipeClientProcessId(pipe, &clientPid) || !ProcessIdToSessionId(clientPid, &clientSession)
        || !ProcessIdToSessionId(GetCurrentProcessId(), &helperSession) || clientSession != helperSession) {
        writeExact(pipe, response(false, QStringLiteral("Client process is not in the expected interactive session.")));
        return 4;
    }
    DWORD size = 0;
    if (!readExact(pipe, &size, sizeof(size)) || size == 0 || size > RepairHelperRequest::maximumMessageBytes) {
        writeExact(pipe, response(false, QStringLiteral("Malformed or oversized helper frame.")));
        return 4;
    }
    QByteArray frame(static_cast<int>(size), Qt::Uninitialized);
    if (!readExact(pipe, frame.data(), size)) return 4;
    QString reason;
    const std::optional<RepairHelperRequest> request = RepairHelperProtocol::parse(frame, &reason);
    if (!request || request->helperBuildId != QString::fromLatin1(HOTAS_BF6_BUILD_ID)) {
        writeExact(pipe, response(false, request ? QStringLiteral("Doctor/helper build compatibility did not match.") : reason));
        return 4;
    }
    if (!elevated()) {
        writeExact(pipe, response(false, QStringLiteral("Administrator elevation was not present; no changes made.")));
        return 5;
    }

    // The helper repeats the observation itself.  It does not trust any GUI
    // snapshot, and the read-only provider remains a separate implementation.
    ReadOnlyWindowsDiagnosticProvider provider;
    const ReadOnlyDiagnosticSnapshot observed = provider.observe();
    if (!RepairHelperProtocol::validate(*request, observed.environment, doctorBuild, nonce, &reason)) {
        writeExact(pipe, response(false, reason));
        return 4;
    }
    if (request->connectivityOnly) {
        writeExact(pipe, response(true, QStringLiteral("Elevated helper connectivity and sealed-plan validation succeeded — no changes made.")));
        return 0;
    }
    RepairPlanProposal proposal;
    proposal.status = RepairProposalStatus::AvailableForOwnerLab;
    proposal.plan = request->plan;
    RepairJournalStore journal;
    if (request->plan.riskClass != RepairRiskClass::R1Configuration) {
        // Deep execution is sealed-plan-only and has its own intentionally
        // narrow native implementation. It is never routed through the R1
        // HidHide SET mutator or a generic process/command abstraction.
        const RepairExecutionResult staged = RepairTransactionCoordinator().dryRun(proposal, observed.environment,
            request->plan.sessionId, journal, request->transactionId);
        if (!staged.transaction.id.isValid() || staged.transaction.state == RepairTransactionState::FailedSafely) {
            writeExact(pipe, response(false, staged.detail.isEmpty() ? QStringLiteral("Deep repair journal could not be created.") : staged.detail, &staged));
            return 6;
        }
        const RepairExecutionResult result = DeepRepairExecutor::execute(request->plan, observed.environment,
            staged.transaction, journal);
        const bool accepted = result.transaction.state == RepairTransactionState::Completed
            || result.transaction.state == RepairTransactionState::AwaitingReboot;
        writeExact(pipe, response(accepted, result.detail, &result));
        return accepted ? 0 : 6;
    }
    HidHideConfigurationMutator mutator(observed.environment.hidhide.provider.isEmpty()
            ? QStringLiteral("HidHide WDM control device") : observed.environment.hidhide.provider,
        observed.environment.hidhide.driverVersion);
    NativePostconditionVerifier postconditionVerifier;
    const RepairExecutionResult result = RepairTransactionCoordinator().executeOwnerLab(proposal, observed.environment,
        mutator, journal, request->transactionId, &postconditionVerifier);
    writeExact(pipe, response(result.transaction.state == RepairTransactionState::Completed, result.detail, &result));
    return result.transaction.state == RepairTransactionState::Completed ? 0 : 6;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Narrow elevated sealed-plan HidHide repair helper"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("pipe"), QStringLiteral("One-time secured named-pipe suffix."), QStringLiteral("name")});
    parser.addOption({QStringLiteral("nonce"), QStringLiteral("One-time upper-case hexadecimal request nonce."), QStringLiteral("nonce")});
    parser.addOption({QStringLiteral("doctor-build"), QStringLiteral("Expected caller build identity."), QStringLiteral("build")});
    parser.addOption({QStringLiteral("protocol-version"), QStringLiteral("Print the supported IPC protocol version and exit.")});
    parser.process(application);
    if (parser.isSet(QStringLiteral("protocol-version"))) {
        qInfo().noquote() << RepairHelperRequest::protocolVersion;
        return 0;
    }
    const QString pipe = parser.value(QStringLiteral("pipe"));
    const QString nonce = parser.value(QStringLiteral("nonce"));
    const QString build = parser.value(QStringLiteral("doctor-build"));
    if (pipe.isEmpty() || nonce.isEmpty() || build.isEmpty()) return 2;
    return serveOneRequest(pipe, nonce, build);
}
