#include "doctor_knowledge.h"

#include "doctor_diagnostics.h"

#include <QDir>
#include <QFileInfo>
#include <QVersionNumber>

#include <algorithm>

namespace hotas::doctor {
namespace {

constexpr auto kKnowledgeVersion = "HD-KB-2.0";

struct Facts final {
    bool hidhideAbsent = false;
    bool clientPresent = false;
    bool driverPresent = false;
    bool clientOnly = false;
    bool driverOnly = false;
    bool versionMismatch = false;
    bool newerPackageAndOldDriver = false;
    bool pendingRestart = false;
    bool controlMissing = false;
    bool accessDenied = false;
    bool whitelistInvalidParameter = false;
    bool allProtocolFailed = false;
    bool guiCrash = false;
    int malformedDevices = 0;
    bool staleConfiguration = false;
    bool hotasExemptionMissing = false;
    bool virtualOutputHidden = false;
    bool architectureMismatch = false;
    bool futureWindows = false;
    bool permissionLimited = false;
    bool insufficientEvidence = false;
    QStringList contradictions;
};

QList<EvidenceId> evidenceFor(const DoctorSession &session, std::initializer_list<const char *> checkIds)
{
    QList<EvidenceId> ids;
    for (const DoctorCheckResult &result : session.checkResults()) {
        const bool wanted = std::any_of(checkIds.begin(), checkIds.end(), [&](const char *value) {
            return result.checkId.value() == QLatin1String(value);
        });
        if (wanted) ids += result.evidenceIds;
    }
    return ids;
}

bool statusIsGood(const ProtocolObservation *probe)
{
    return probe && (probe->status == DoctorCheckStatus::Healthy || probe->status == DoctorCheckStatus::Informational);
}

bool statusIsBad(const ProtocolObservation *probe)
{
    return probe && (probe->status == DoctorCheckStatus::Failed || probe->status == DoctorCheckStatus::TimedOut
        || probe->status == DoctorCheckStatus::PermissionLimited);
}

const ProtocolObservation *probe(const ReadOnlyDiagnosticSnapshot &snapshot, const QString &operation)
{
    for (const ProtocolObservation &candidate : snapshot.protocol) {
        if (candidate.operation == operation) return &candidate;
    }
    return nullptr;
}

bool nativeCode(const ProtocolObservation *observation, qint64 code)
{
    return observation && observation->nativeError && observation->nativeError->domain == NativeErrorDomain::Win32
        && observation->nativeError->code == code;
}

bool hasArtifact(const ReadOnlyDiagnosticSnapshot &snapshot, DoctorArtifactKind kind)
{
    return std::any_of(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(), [kind](const FileArtifactObservation &artifact) {
        return artifact.kind == kind && artifact.exists;
    });
}

QString artifactVersion(const ReadOnlyDiagnosticSnapshot &snapshot, DoctorArtifactKind kind)
{
    for (const FileArtifactObservation &artifact : snapshot.artifacts) {
        if (artifact.kind == kind && artifact.exists && !artifact.fileVersion.isEmpty()) return artifact.fileVersion;
    }
    return {};
}

bool versionNewer(const QString &candidate, const QString &baseline)
{
    const QVersionNumber candidateVersion = QVersionNumber::fromString(candidate);
    const QVersionNumber baselineVersion = QVersionNumber::fromString(baseline);
    return !candidateVersion.isNull() && !baselineVersion.isNull() && candidateVersion > baselineVersion;
}

bool containsInsensitive(const QString &value, const QString &needle)
{
    return value.contains(needle, Qt::CaseInsensitive);
}

Facts collectFacts(const DoctorSession &session, const ReadOnlyDiagnosticSnapshot &snapshot)
{
    Q_UNUSED(session)
    Facts facts;
    facts.clientPresent = hasArtifact(snapshot, DoctorArtifactKind::ClientExecutable);
    facts.driverPresent = hasArtifact(snapshot, DoctorArtifactKind::DriverBinary) || snapshot.service.present;
    facts.hidhideAbsent = !snapshot.environment.hidhide.present && !facts.clientPresent && !facts.driverPresent;
    facts.clientOnly = facts.clientPresent && !facts.driverPresent;
    facts.driverOnly = facts.driverPresent && !facts.clientPresent;
    facts.pendingRestart = !snapshot.pendingRestart.isEmpty();

    const QString clientVersion = !artifactVersion(snapshot, DoctorArtifactKind::ClientExecutable).isEmpty()
        ? artifactVersion(snapshot, DoctorArtifactKind::ClientExecutable) : snapshot.environment.hidhide.clientVersion;
    const QString driverVersion = !artifactVersion(snapshot, DoctorArtifactKind::DriverBinary).isEmpty()
        ? artifactVersion(snapshot, DoctorArtifactKind::DriverBinary) : snapshot.environment.hidhide.driverVersion;
    facts.versionMismatch = !clientVersion.isEmpty() && !driverVersion.isEmpty() && clientVersion != driverVersion;
    for (const DriverPackageObservation &package : snapshot.driverPackages) {
        if (versionNewer(package.version, driverVersion)) facts.newerPackageAndOldDriver = true;
    }
    facts.newerPackageAndOldDriver = facts.newerPackageAndOldDriver || (facts.versionMismatch && facts.pendingRestart
        && versionNewer(clientVersion, driverVersion));

    const ProtocolObservation *open = probe(snapshot, QStringLiteral("OPEN_CONTROL"));
    const ProtocolObservation *active = probe(snapshot, QStringLiteral("GET_ACTIVE"));
    const ProtocolObservation *inverse = probe(snapshot, QStringLiteral("GET_INVERSE"));
    const ProtocolObservation *whiteSize = probe(snapshot, QStringLiteral("GET_WHITELIST_SIZE"));
    const ProtocolObservation *white = probe(snapshot, QStringLiteral("GET_WHITELIST"));
    const ProtocolObservation *blackSize = probe(snapshot, QStringLiteral("GET_BLACKLIST_SIZE"));
    const ProtocolObservation *black = probe(snapshot, QStringLiteral("GET_BLACKLIST"));
    facts.controlMissing = nativeCode(open, 2) || nativeCode(open, 3);
    facts.accessDenied = nativeCode(open, 5) || nativeCode(active, 5) || nativeCode(whiteSize, 5) || nativeCode(white, 5);
    facts.whitelistInvalidParameter = (nativeCode(whiteSize, 0x57) || nativeCode(white, 0x57))
        && statusIsGood(open) && statusIsGood(active) && statusIsGood(inverse)
        && (statusIsGood(blackSize) || statusIsGood(black));
    const QList<const ProtocolObservation *> primaryProbes = {open, active, inverse, whiteSize, blackSize};
    int observed = 0;
    int bad = 0;
    int good = 0;
    for (const ProtocolObservation *candidate : primaryProbes) {
        if (!candidate) continue;
        ++observed;
        if (statusIsBad(candidate)) ++bad;
        if (statusIsGood(candidate)) ++good;
    }
    facts.allProtocolFailed = observed >= 3 && bad == observed && good == 0;

    for (const EventObservation &event : snapshot.events) {
        if ((containsInsensitive(event.summary, QStringLiteral("hidhideclient"))
                || containsInsensitive(event.summary, QStringLiteral("hidhidecli")))
            && (containsInsensitive(event.summary, QStringLiteral("crash"))
                || containsInsensitive(event.summary, QStringLiteral("exception")))) facts.guiCrash = true;
        if (event.nativeError && event.nativeError->code == 5) facts.permissionLimited = true;
    }
    for (const EventObservation &event : snapshot.werReports) {
        if (containsInsensitive(event.summary, QStringLiteral("hidhide"))) facts.guiCrash = true;
    }
    for (const DeviceObservation &device : snapshot.devices) {
        if (!device.propertyFailures.isEmpty() || device.nativeError) ++facts.malformedDevices;
    }
    const QStringList whitelist = probe(snapshot, QStringLiteral("GET_WHITELIST"))
        ? probe(snapshot, QStringLiteral("GET_WHITELIST"))->multiStringValues : snapshot.registryWhitelist;
    const QStringList blacklist = probe(snapshot, QStringLiteral("GET_BLACKLIST"))
        ? probe(snapshot, QStringLiteral("GET_BLACKLIST"))->multiStringValues : snapshot.registryBlacklist;
    if (snapshot.environment.repairIntent.suppliedByHotas && !snapshot.environment.repairIntent.expectedExecutable.isEmpty()) {
        const QString expected = snapshot.environment.repairIntent.expectedExecutable;
        facts.hotasExemptionMissing = std::none_of(whitelist.cbegin(), whitelist.cend(), [&expected](const QString &entry) {
            return QDir::fromNativeSeparators(entry).compare(QDir::fromNativeSeparators(expected), Qt::CaseInsensitive) == 0;
        });
    }
    for (const QString &entry : whitelist) {
        if (containsInsensitive(entry, QStringLiteral("missing")) || containsInsensitive(entry, QStringLiteral("stale")))
            facts.staleConfiguration = true;
    }
    for (const QString &entry : blacklist) {
        bool resolved = false;
        for (const DeviceObservation &device : snapshot.devices) {
            if (device.instanceId.compare(entry, Qt::CaseInsensitive) == 0 && device.present) {
                resolved = true;
                if (device.classification == DeviceClassification::VJoyVirtualOutput
                    || device.classification == DeviceClassification::VirtualGamingDevice) facts.virtualOutputHidden = true;
            }
        }
        if (containsInsensitive(entry, QStringLiteral("stale"))) facts.staleConfiguration = true;
        if (!resolved && !entry.isEmpty()) facts.staleConfiguration = true;
        if (containsInsensitive(entry, QStringLiteral("vjoy"))) facts.virtualOutputHidden = true;
    }
    facts.architectureMismatch = snapshot.environment.hidhide.present
        && snapshot.environment.hidhide.packageArchitecture != CpuArchitecture::Unknown
        && snapshot.environment.hidhide.packageArchitecture != snapshot.environment.platform.nativeArchitecture;
    facts.futureWindows = snapshot.environment.platform.build >= 30000;
    facts.permissionLimited = facts.permissionLimited || facts.accessDenied;
    facts.contradictions = snapshot.contradictions;
    facts.insufficientEvidence = !facts.hidhideAbsent && snapshot.protocol.isEmpty() && snapshot.artifacts.isEmpty()
        && !snapshot.service.present && snapshot.devices.isEmpty() && snapshot.events.isEmpty();
    return facts;
}

Finding finding(const QString &id, FindingSeverity severity, const QString &title, const QString &human,
    const QString &technical, const QString &affected, DiagnosisConfidence confidence,
    Repairability repairability, QList<EvidenceId> evidence)
{
    Finding result;
    result.id = FindingId(id);
    result.severity = severity;
    result.title = title;
    result.explanation = human;
    result.technicalExplanation = technical;
    result.affectedObject = affected;
    result.confidence = confidence;
    result.status = severity == FindingSeverity::Informational ? DoctorCheckStatus::Informational : DoctorCheckStatus::Warning;
    result.repairability = repairability;
    result.evidenceIds = std::move(evidence);
    result.observedAt = QDateTime::currentDateTimeUtc();
    return result;
}

DiagnosisConfidence bandForScore(int score)
{
    if (score >= 98) return DiagnosisConfidence::Confirmed;
    if (score >= 90) return DiagnosisConfidence::VeryHigh;
    if (score >= 75) return DiagnosisConfidence::High;
    if (score >= 55) return DiagnosisConfidence::Moderate;
    return DiagnosisConfidence::Uncertain;
}

Diagnosis diagnosis(const QString &id, const QString &signature, FindingSeverity severity,
    int initialScore, const QString &title, const QString &family, const QString &human,
    const QString &technical, const QString &impact, Repairability repairability,
    const QStringList &required, const QStringList &supporting, const QStringList &contradictions,
    QList<EvidenceId> supportingEvidence, DiagnosisRole role = DiagnosisRole::Secondary)
{
    const int contradictionPenalty = static_cast<int>(contradictions.size()) * 20;
    const int score = std::clamp(initialScore - contradictionPenalty, 0, 100);
    Diagnosis result;
    result.id = DiagnosisId(id);
    result.signatureId = KnowledgeSignatureId(signature);
    result.severity = severity;
    result.confidence = bandForScore(score);
    result.repairability = repairability;
    result.title = title;
    result.problemFamily = family;
    result.humanExplanation = human;
    result.technicalExplanation = technical;
    result.userImpact = impact;
    result.usualResolution = QStringLiteral("Repairs are disabled in this Phase 2 build; no system state was changed.");
    result.provenance = QStringLiteral("Deterministic rule evaluation of Phase 1 evidence");
    result.knowledgeVersion = QLatin1String(kKnowledgeVersion);
    result.role = role;
    result.supportingEvidence = std::move(supportingEvidence);
    result.confidenceExplanation = {score,
        QStringLiteral("%1 required evidence item(s), %2 supporting item(s), %3 contradiction(s).")
            .arg(required.size()).arg(supporting.size()).arg(contradictions.size()),
        required, supporting, contradictions, {}};
    return result;
}

DoctorCheckResult knowledgeResult(const QString &checkId, DoctorCheckStatus status, const QString &summary)
{
    DoctorCheckResult result;
    result.checkId = DoctorCheckId(checkId);
    result.status = status;
    result.summary = summary;
    result.technicalDetails = QStringLiteral("%1 deterministic knowledge engine.").arg(QLatin1String(kKnowledgeVersion));
    return result;
}

} // namespace

int DoctorFindingEngine::ruleCount() { return 17; }

QList<Finding> DoctorFindingEngine::evaluate(const DoctorSession &session,
    const ReadOnlyDiagnosticSnapshot &snapshot) const
{
    const Facts facts = collectFacts(session, snapshot);
    QList<Finding> findings;
    const auto apiEvidence = evidenceFor(session, {"HD-API-001", "HD-API-002", "HD-API-003", "HD-API-004", "HD-API-005", "HD-API-007", "HD-API-008"});
    if (facts.hidhideAbsent) findings.append(finding(QStringLiteral("HD-FND-ABSENT"), FindingSeverity::Informational,
        QStringLiteral("HidHide is not installed"), QStringLiteral("No HidHide client, driver, or service was observed."),
        QStringLiteral("The absence is a valid machine state, not a failed scan."), QStringLiteral("HidHide installation"),
        DiagnosisConfidence::Confirmed, Repairability::NoRepairRequired, evidenceFor(session, {"HD-INST-001", "HD-DRV-003"})));
    if (facts.clientOnly) findings.append(finding(QStringLiteral("HD-FND-CLIENT-ONLY"), FindingSeverity::Error,
        QStringLiteral("Partial HidHide installation: client only"), QStringLiteral("The client is present but the driver/service is not."),
        QStringLiteral("User-mode component inventory is not matched by a driver registration."), QStringLiteral("HidHide installation"),
        DiagnosisConfidence::High, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-INST-004", "HD-DRV-003"})));
    if (facts.driverOnly) findings.append(finding(QStringLiteral("HD-FND-DRIVER-ONLY"), FindingSeverity::Error,
        QStringLiteral("Partial HidHide installation: driver only"), QStringLiteral("The driver/service is present but the client is not."),
        QStringLiteral("Driver state has no corresponding client payload."), QStringLiteral("HidHide installation"),
        DiagnosisConfidence::High, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-INST-004", "HD-DRV-003"})));
    if (facts.versionMismatch) findings.append(finding(QStringLiteral("HD-FND-VERSION-MISMATCH"), FindingSeverity::Warning,
        QStringLiteral("HidHide component versions differ"), QStringLiteral("Installed client/package evidence and loaded-driver evidence do not agree."),
        QStringLiteral("The finding retains separate client, package, driver-file, and loaded-driver observations."), QStringLiteral("HidHide component matrix"),
        DiagnosisConfidence::High, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-PKG-001", "HD-PKG-004", "HD-DRV-013"})));
    if (facts.pendingRestart) findings.append(finding(QStringLiteral("HD-FND-PENDING-RESTART"), FindingSeverity::Warning,
        QStringLiteral("Pending driver replacement evidence"), QStringLiteral("Windows reports pending restart or replacement evidence relevant to HidHide."),
        QStringLiteral("The record is preserved without initiating a restart."), QStringLiteral("Windows restart state"),
        DiagnosisConfidence::High, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-SYS-010", "HD-SYS-011", "HD-WIN-017"})));
    if (facts.controlMissing) findings.append(finding(QStringLiteral("HD-FND-CONTROL-MISSING"), FindingSeverity::Error,
        QStringLiteral("HidHide control endpoint is unavailable"), QStringLiteral("The direct read-only control open returned a path-not-found error."),
        QStringLiteral("This is distinct from a failed configuration GET operation."), QStringLiteral("HidHide control endpoint"),
        DiagnosisConfidence::VeryHigh, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-API-001", "HD-DRV-019"})));
    if (facts.permissionLimited) findings.append(finding(QStringLiteral("HD-FND-PERMISSION"), FindingSeverity::Warning,
        QStringLiteral("Evidence access is permission-limited"), QStringLiteral("A required observation was denied; this is not proof that HidHide is broken."),
        QStringLiteral("Native access-denied evidence is retained for the affected operation."), QStringLiteral("Diagnostic access"),
        DiagnosisConfidence::VeryHigh, Repairability::InsufficientEvidence, apiEvidence));
    if (facts.whitelistInvalidParameter) findings.append(finding(QStringLiteral("HD-FND-WHITELIST-57"), FindingSeverity::Error,
        QStringLiteral("Application-list interface failure"), QStringLiteral("HidHide's application-list query returned ERROR_INVALID_PARAMETER while other direct queries remained healthy."),
        QStringLiteral("GET_WHITELIST returned Win32 87 / 0x00000057; direct control, active, inverse, and blacklist observations were retained independently."),
        QStringLiteral("HidHide control protocol"), DiagnosisConfidence::VeryHigh, Repairability::UpstreamOrComponentDefect, apiEvidence));
    if (facts.allProtocolFailed) findings.append(finding(QStringLiteral("HD-FND-PROTOCOL-ALL"), FindingSeverity::Critical,
        QStringLiteral("HidHide control protocol is broadly unavailable"), QStringLiteral("Every observed independent control operation failed."),
        QStringLiteral("The failure is evidence-driven; unrelated device and Windows checks continued."), QStringLiteral("HidHide control protocol"),
        DiagnosisConfidence::High, Repairability::PotentialRepairAvailableButUnqualified, apiEvidence));
    if (facts.guiCrash) findings.append(finding(QStringLiteral("HD-FND-CLIENT-CRASH"), FindingSeverity::Warning,
        QStringLiteral("HidHide user-mode client crash evidence"), QStringLiteral("Existing Windows crash evidence references a HidHide client component."),
        QStringLiteral("The Doctor did not launch a client to provoke this condition."), QStringLiteral("HidHide Client/CLI"),
        DiagnosisConfidence::High, Repairability::UpstreamOrComponentDefect, evidenceFor(session, {"HD-WIN-001", "HD-WIN-002", "HD-WIN-003", "HD-WIN-004"})));
    if (facts.malformedDevices) findings.append(finding(QStringLiteral("HD-FND-BROKEN-DEVICE"), FindingSeverity::Warning,
        QStringLiteral("Device enumeration is degraded"), QStringLiteral("%1 device observation(s) reported a property or access failure.").arg(facts.malformedDevices),
        QStringLiteral("Failures are isolated per device and do not abort the session."), QStringLiteral("HID device ecosystem"),
        DiagnosisConfidence::High, Repairability::ManualInterventionRequired, evidenceFor(session, {"HD-DEV-003", "HD-DEV-015", "HD-DEV-016", "HD-DEV-023"})));
    if (facts.staleConfiguration) findings.append(finding(QStringLiteral("HD-FND-STALE-CONFIG"), FindingSeverity::Warning,
        QStringLiteral("Stale HidHide configuration reference"), QStringLiteral("A configured application or hidden-device reference could not be resolved."),
        QStringLiteral("The original configuration evidence is retained; Phase 2 does not remove any entry."), QStringLiteral("HidHide configuration"),
        DiagnosisConfidence::High, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-CFG-006", "HD-CFG-013", "HD-CFG-018"})));
    if (facts.virtualOutputHidden) findings.append(finding(QStringLiteral("HD-FND-VIRTUAL-HIDDEN"), FindingSeverity::Error,
        QStringLiteral("Virtual output appears in HidHide's hidden-device configuration"), QStringLiteral("A known virtual output is matched to the hidden-device configuration."),
        QStringLiteral("The match is based on observed device identity and configuration evidence."), QStringLiteral("Virtual output visibility"),
        DiagnosisConfidence::VeryHigh, Repairability::PotentialRepairAvailableButUnqualified, evidenceFor(session, {"HD-CFG-015", "HD-DEV-018", "HD-ISO-005"})));
    if (facts.architectureMismatch) findings.append(finding(QStringLiteral("HD-FND-ARCH-MISMATCH"), FindingSeverity::Error,
        QStringLiteral("HidHide package architecture does not match Windows"), QStringLiteral("The observed HidHide package architecture differs from the native machine architecture."),
        QStringLiteral("Phase 2 blocks repairability on architecture mismatch."), QStringLiteral("Platform compatibility"),
        DiagnosisConfidence::Confirmed, Repairability::UnsupportedEnvironment, evidenceFor(session, {"HD-PORT-005", "HD-PORT-012", "HD-PORT-013"})));
    if (facts.futureWindows) findings.append(finding(QStringLiteral("HD-FND-FUTURE-WINDOWS"), FindingSeverity::Warning,
        QStringLiteral("Windows build is not repair-qualified"), QStringLiteral("This Windows build is newer than the current knowledge qualification range."),
        QStringLiteral("Read-only diagnosis continues; environment-sensitive repair stays disabled."), QStringLiteral("Windows platform"),
        DiagnosisConfidence::High, Repairability::UnsupportedEnvironment, evidenceFor(session, {"HD-PORT-003", "HD-PORT-029"})));
    if (!facts.contradictions.isEmpty()) findings.append(finding(QStringLiteral("HD-FND-CONTRADICTION"), FindingSeverity::Warning,
        QStringLiteral("Evidence conflict detected"), QStringLiteral("Live and persisted or correlated evidence does not fully agree."),
        facts.contradictions.join(QStringLiteral(" | ")), QStringLiteral("Evidence consistency"), DiagnosisConfidence::High,
        Repairability::InsufficientEvidence, evidenceFor(session, {"HD-CFG-018", "HD-X-009"})));
    if (facts.insufficientEvidence) findings.append(finding(QStringLiteral("HD-FND-INCONCLUSIVE"), FindingSeverity::Warning,
        QStringLiteral("Insufficient evidence for a root cause"), QStringLiteral("The available observations cannot safely support a diagnosis."),
        QStringLiteral("The Doctor deliberately retains an inconclusive result instead of guessing."), QStringLiteral("Diagnostic evidence"),
        DiagnosisConfidence::Uncertain, Repairability::InsufficientEvidence, {}));
    return findings;
}

QString DoctorKnowledgeEngine::version() { return QLatin1String(kKnowledgeVersion); }
int DoctorKnowledgeEngine::ruleCount() { return 17; }

KnowledgeAnalysis DoctorKnowledgeEngine::analyze(DoctorSession &session,
    const ReadOnlyDiagnosticSnapshot &snapshot) const
{
    KnowledgeAnalysis analysis;
    analysis.engineVersion = version();
    analysis.findingRuleCount = DoctorFindingEngine::ruleCount();
    analysis.diagnosisRuleCount = ruleCount();
    const Facts facts = collectFacts(session, snapshot);
    const DoctorFindingEngine findingEngine;
    for (Finding finding : findingEngine.evaluate(session, snapshot)) session.appendFinding(std::move(finding));

    const auto apiEvidence = evidenceFor(session, {"HD-API-001", "HD-API-002", "HD-API-003", "HD-API-004", "HD-API-005", "HD-API-007", "HD-API-008"});
    const auto absentEvidence = evidenceFor(session, {"HD-INST-001", "HD-DRV-003"});
    const auto partialEvidence = evidenceFor(session, {"HD-INST-004", "HD-DRV-003"});
    const auto replacementEvidence = evidenceFor(session, {"HD-X-001", "HD-X-002", "HD-SYS-010", "HD-DRV-013"});
    const auto versionEvidence = evidenceFor(session, {"HD-X-001", "HD-PKG-001", "HD-DRV-013"});
    const auto controlEvidence = evidenceFor(session, {"HD-API-001", "HD-DRV-019"});
    const auto clientCrashEvidence = evidenceFor(session, {"HD-WIN-001", "HD-WIN-003", "HD-API-001", "HD-API-002"});
    const auto deviceEvidence = evidenceFor(session, {"HD-DEV-003", "HD-DEV-015", "HD-DEV-016", "HD-DEV-023"});
    const auto staleConfigEvidence = evidenceFor(session, {"HD-CFG-006", "HD-CFG-013"});
    const auto virtualEvidence = evidenceFor(session, {"HD-CFG-015", "HD-DEV-018", "HD-ISO-005"});
    const auto architectureEvidence = evidenceFor(session, {"HD-PORT-005", "HD-PORT-012", "HD-PORT-013"});
    const auto futureEvidence = evidenceFor(session, {"HD-PORT-003", "HD-PORT-029"});
    const auto contradictionEvidence = evidenceFor(session, {"HD-CFG-018", "HD-X-009"});
    QList<Diagnosis> diagnoses;
    if (facts.hidhideAbsent) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-ABSENT"), QStringLiteral("HD-KSIG-ABSENT-V1"),
        FindingSeverity::Informational, 100, QStringLiteral("HidHide is absent"), QStringLiteral("Installation state"),
        QStringLiteral("No HidHide installation was observed. This is a valid state."), QStringLiteral("No client, driver, or service evidence was found."),
        QStringLiteral("HidHide-specific isolation is not active on this machine."), Repairability::NoRepairRequired,
        {QStringLiteral("absence of client/driver/service")}, {}, facts.contradictions, absentEvidence));
    if (facts.clientOnly || facts.driverOnly) {
        const QString partialTechnical = facts.clientOnly ? QStringLiteral("Client payload exists without a driver/service.")
                                                          : QStringLiteral("Driver/service exists without a client payload.");
        diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-PARTIAL-INSTALL"), QStringLiteral("HD-KSIG-PARTIAL-INSTALL-V1"),
            FindingSeverity::Error, 91, QStringLiteral("Partial HidHide installation"), QStringLiteral("Installation integrity"),
            QStringLiteral("HidHide has only part of its expected user-mode and driver components."), partialTechnical,
            QStringLiteral("Configuration and user-mode management may be incomplete."), Repairability::PotentialRepairAvailableButUnqualified,
            {QStringLiteral("client/driver component asymmetry")}, {}, facts.contradictions, partialEvidence));
    }
    if (facts.newerPackageAndOldDriver) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-INCOMPLETE-REPLACEMENT"), QStringLiteral("HD-KSIG-002-V1"),
        FindingSeverity::Error, 97, QStringLiteral("Incomplete HidHide driver replacement"), QStringLiteral("Version and restart correlation"),
        QStringLiteral("A newer installed component/package is present while an older driver remains loaded and replacement evidence is pending."),
        QStringLiteral("Version triad plus pending-restart markers correlate into one root-cause candidate."),
        QStringLiteral("HidHide client and driver communication may be unreliable until replacement is completed."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("newer package"), QStringLiteral("older loaded driver"), QStringLiteral("pending restart")},
        {QStringLiteral("client/driver version mismatch")}, facts.contradictions, replacementEvidence));
    else if (facts.versionMismatch) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-VERSION-MISMATCH"), QStringLiteral("HD-KSIG-VERSION-MISMATCH-V1"),
        FindingSeverity::Warning, 84, QStringLiteral("HidHide component version mismatch"), QStringLiteral("Version compatibility"),
        QStringLiteral("HidHide client and driver/component evidence report different versions."), QStringLiteral("No restart correlation was sufficient to promote this to incomplete replacement."),
        QStringLiteral("Configuration access may be unreliable."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("different client and driver versions")}, {}, facts.contradictions, versionEvidence));
    if (facts.controlMissing) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-CONTROL-MISSING"), QStringLiteral("HD-KSIG-CONTROL-MISSING-V1"),
        FindingSeverity::Error, 93, QStringLiteral("HidHide control endpoint is unavailable"), QStringLiteral("Control interface"),
        QStringLiteral("The read-only control endpoint cannot be opened because Windows reports it missing."), QStringLiteral("OPEN_CONTROL preserved the native path-not-found error."),
        QStringLiteral("HidHide configuration cannot be verified through the direct protocol."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("OPEN_CONTROL path-not-found")}, {}, facts.contradictions, controlEvidence));
    if (facts.permissionLimited) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-PERMISSION-LIMITED"), QStringLiteral("HD-KSIG-PERMISSION-V1"),
        FindingSeverity::Warning, 96, QStringLiteral("Diagnosis is permission-limited"), QStringLiteral("Diagnostic access"),
        QStringLiteral("Windows denied one or more diagnostic observations."), QStringLiteral("The evidence is classified as access limitation, not a HidHide fault."),
        QStringLiteral("Some system state remains unverified."), Repairability::InsufficientEvidence,
        {QStringLiteral("access-denied observation")}, {}, facts.contradictions, apiEvidence));
    if (facts.whitelistInvalidParameter) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-WHITELIST-API"), QStringLiteral("HD-KSIG-001-V1"),
        FindingSeverity::Error, 97, QStringLiteral("HidHide application-list interface failure"), QStringLiteral("Control/API compatibility"),
        QStringLiteral("The application whitelist query is failing even though the control interface and other configuration queries are working."),
        QStringLiteral("GET_WHITELIST returned ERROR_INVALID_PARAMETER (0x00000057) while active, inverse, and blacklist queries were healthy."),
        QStringLiteral("Application whitelist configuration cannot be reliably read by this HidHide interface."), Repairability::UpstreamOrComponentDefect,
        {QStringLiteral("GET_WHITELIST Win32 87"), QStringLiteral("healthy sibling GET operations")}, {}, facts.contradictions, apiEvidence));
    if (facts.allProtocolFailed) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-PROTOCOL-BROAD-FAILURE"), QStringLiteral("HD-KSIG-PROTOCOL-BROAD-V1"),
        FindingSeverity::Critical, 84, QStringLiteral("HidHide control protocol failure"), QStringLiteral("Control/API availability"),
        QStringLiteral("All observed independent control operations failed."), QStringLiteral("The direct protocol failures are preserved individually rather than flattened into one boolean."),
        QStringLiteral("HidHide configuration and isolation cannot be confirmed."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("multiple failed independent GET operations")}, {}, facts.contradictions, apiEvidence));
    if (facts.guiCrash && !facts.allProtocolFailed && !facts.controlMissing) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-CLIENT-FAILURE"), QStringLiteral("HD-KSIG-004-V1"),
        FindingSeverity::Warning, 88, QStringLiteral("HidHide user-mode client failure"), QStringLiteral("Client/GUI failure"),
        QStringLiteral("Existing crash evidence implicates the HidHide user-mode client while direct driver API evidence remains available."),
        QStringLiteral("The rule explicitly avoids blaming the kernel driver when independent protocol checks are healthy."),
        QStringLiteral("The HidHide GUI or CLI may be unreliable; direct read-only diagnosis can continue."), Repairability::UpstreamOrComponentDefect,
        {QStringLiteral("client crash evidence"), QStringLiteral("healthy direct protocol")}, {}, facts.contradictions,
        clientCrashEvidence));
    if (facts.malformedDevices) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-DEVICE-ENUMERATION"), QStringLiteral("HD-KSIG-003-V1"),
        FindingSeverity::Warning, 86, QStringLiteral("Broken HID device enumeration"), QStringLiteral("Device ecosystem"),
        QStringLiteral("One or more HID devices failed an isolated property or access observation."),
        QStringLiteral("The specific failed device evidence remains linked; other checks continued."),
        QStringLiteral("A device-specific issue may affect HidHide client enumeration."), Repairability::ManualInterventionRequired,
        {QStringLiteral("isolated per-device failure")}, {}, facts.contradictions, deviceEvidence));
    if (facts.staleConfiguration) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-STALE-CONFIG"), QStringLiteral("HD-KSIG-STALE-CONFIG-V1"),
        FindingSeverity::Warning, 80, QStringLiteral("Stale HidHide configuration"), QStringLiteral("Configuration integrity"),
        QStringLiteral("One or more configured paths or device references no longer resolve."), QStringLiteral("Entries were read but not changed by Phase 2."),
        QStringLiteral("Isolation configuration may include obsolete references."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("unresolved configuration reference")}, {}, facts.contradictions, staleConfigEvidence));
    if (facts.hotasExemptionMissing) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-MISSING-HOTAS-EXEMPTION"), QStringLiteral("HD-KSIG-MISSING-HOTAS-EXEMPTION-V1"),
        FindingSeverity::Error, 96, QStringLiteral("Missing HOTAS BF6 HidHide exemption"), QStringLiteral("Configuration integrity"),
        QStringLiteral("The verified HOTAS BF6 executable is not present in HidHide's application exemption list."),
        QStringLiteral("A HOTAS-launched expected executable was compared against the independently read whitelist."),
        QStringLiteral("HOTAS BF6 may be unable to access controllers hidden from ordinary applications."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("verified HOTAS executable"), QStringLiteral("independent whitelist read")}, {}, facts.contradictions, staleConfigEvidence));
    if (facts.virtualOutputHidden) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-VIRTUAL-HIDDEN"), QStringLiteral("HD-KSIG-VIRTUAL-HIDDEN-V1"),
        FindingSeverity::Error, 94, QStringLiteral("Virtual output is hidden by HidHide"), QStringLiteral("Isolation configuration"),
        QStringLiteral("A virtual output is selected for hiding, which can remove game input."), QStringLiteral("Observed virtual-device identity matches a blacklist entry."),
        QStringLiteral("Virtual controller output may not reach games."), Repairability::PotentialRepairAvailableButUnqualified,
        {QStringLiteral("virtual output identity"), QStringLiteral("matching hidden-device entry")}, {}, facts.contradictions,
        virtualEvidence));
    if (facts.architectureMismatch) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-ARCH-MISMATCH"), QStringLiteral("HD-KSIG-ARCH-MISMATCH-V1"),
        FindingSeverity::Error, 100, QStringLiteral("Unsupported HidHide architecture combination"), QStringLiteral("Platform qualification"),
        QStringLiteral("Installed HidHide package architecture does not match this Windows architecture."), QStringLiteral("Architecture comparison is direct platform evidence."),
        QStringLiteral("Component repair must remain blocked."), Repairability::UnsupportedEnvironment,
        {QStringLiteral("native/package architecture mismatch")}, {}, facts.contradictions, architectureEvidence));
    if (facts.futureWindows) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-FUTURE-WINDOWS"), QStringLiteral("HD-KSIG-FUTURE-WINDOWS-V1"),
        FindingSeverity::Warning, 79, QStringLiteral("Windows platform is diagnosis-only"), QStringLiteral("Platform qualification"),
        QStringLiteral("This Windows build is not in the current repair qualification range."), QStringLiteral("The knowledge rule retains read-only evidence and rejects repair inference."),
        QStringLiteral("Automatic repair is unavailable on this unqualified build."), Repairability::UnsupportedEnvironment,
        {QStringLiteral("future Windows build")}, {}, facts.contradictions, futureEvidence));
    if (!facts.contradictions.isEmpty()) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-INCONSISTENT-EVIDENCE"), QStringLiteral("HD-KSIG-CONTRADICTION-V1"),
        FindingSeverity::Warning, 82, QStringLiteral("Evidence is inconsistent"), QStringLiteral("Evidence consistency"),
        QStringLiteral("At least two observations disagree, so the Doctor preserves uncertainty."), facts.contradictions.join(QStringLiteral(" | ")),
        QStringLiteral("A repair decision cannot be safely inferred until the conflict is resolved."), Repairability::InsufficientEvidence,
        {QStringLiteral("contradicting evidence")}, {}, facts.contradictions, contradictionEvidence));
    if (facts.insufficientEvidence) diagnoses.append(diagnosis(QStringLiteral("HD-DIAG-INCONCLUSIVE"), QStringLiteral("HD-KSIG-INCONCLUSIVE-V1"),
        FindingSeverity::Warning, 35, QStringLiteral("Insufficient evidence for a root-cause diagnosis"), QStringLiteral("Inconclusive diagnosis"),
        QStringLiteral("The Doctor does not have enough reliable evidence to identify a cause."), QStringLiteral("No signature was promoted from incomplete observations."),
        QStringLiteral("No automatic repair can be offered."), Repairability::InsufficientEvidence,
        {QStringLiteral("sufficient direct evidence")}, {}, facts.contradictions, {}));

    // Promote exactly one root cause. A correlation that explains the version
    // mismatch and pending restart outranks its contributing observations.
    if (!diagnoses.isEmpty()) {
        int primary = 0;
        for (int index = 0; index < diagnoses.size(); ++index) {
            if (diagnoses.at(index).id.value() == QStringLiteral("HD-DIAG-INCOMPLETE-REPLACEMENT")) { primary = index; break; }
            if (diagnoses.at(index).severity > diagnoses.at(primary).severity) primary = index;
        }
        if (!facts.contradictions.isEmpty()) {
            for (int index = 0; index < diagnoses.size(); ++index) {
                if (diagnoses.at(index).id.value() == QStringLiteral("HD-DIAG-INCONSISTENT-EVIDENCE")) { primary = index; break; }
            }
        }
        for (int index = 0; index < diagnoses.size(); ++index) {
            diagnoses[index].role = index == primary ? DiagnosisRole::Primary : DiagnosisRole::Contributing;
            session.appendDiagnosis(std::move(diagnoses[index]));
        }
    }

    analysis.contradictions = facts.contradictions;
    analysis.catalogResults.insert(QStringLiteral("HD-X-001"), knowledgeResult(QStringLiteral("HD-X-001"), facts.versionMismatch ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy,
        facts.versionMismatch ? QStringLiteral("Client/driver/package version triad differs.") : QStringLiteral("Observed component versions are consistent.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-002"), knowledgeResult(QStringLiteral("HD-X-002"), facts.newerPackageAndOldDriver ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable,
        facts.newerPackageAndOldDriver ? QStringLiteral("Newer package plus older loaded driver and restart evidence detected.") : QStringLiteral("Incomplete replacement signature not observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-003"), knowledgeResult(QStringLiteral("HD-X-003"), facts.controlMissing ? DoctorCheckStatus::Failed : DoctorCheckStatus::Informational,
        facts.controlMissing ? QStringLiteral("Control endpoint is missing.") : QStringLiteral("Control-endpoint correlation retained.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-004"), knowledgeResult(QStringLiteral("HD-X-004"), facts.whitelistInvalidParameter ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy,
        facts.whitelistInvalidParameter ? QStringLiteral("One GET operation failed while siblings remained healthy.") : QStringLiteral("No isolated control operation failure detected.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-005"), knowledgeResult(QStringLiteral("HD-X-005"), facts.whitelistInvalidParameter ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable,
        facts.whitelistInvalidParameter ? QStringLiteral("Known invalid-parameter failure family matched.") : QStringLiteral("Whitelist invalid-parameter signature not observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-006"), knowledgeResult(QStringLiteral("HD-X-006"), facts.guiCrash ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable,
        facts.guiCrash ? QStringLiteral("Client crash evidence correlated with direct API evidence.") : QStringLiteral("No client-specific failure correlation observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-007"), knowledgeResult(QStringLiteral("HD-X-007"), facts.allProtocolFailed ? DoctorCheckStatus::Failed : DoctorCheckStatus::NotApplicable,
        facts.allProtocolFailed ? QStringLiteral("Service/driver evidence and broad protocol failure require cautious diagnosis.") : QStringLiteral("Broad protocol failure correlation not observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-008"), knowledgeResult(QStringLiteral("HD-X-008"), facts.malformedDevices ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy,
        facts.malformedDevices ? QStringLiteral("Per-device failure evidence supports broken enumeration analysis.") : QStringLiteral("No malformed device evidence observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-009"), knowledgeResult(QStringLiteral("HD-X-009"), facts.contradictions.isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning,
        facts.contradictions.isEmpty() ? QStringLiteral("No registry/live configuration conflict observed.") : QStringLiteral("Evidence conflict detected and retained.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-010"), knowledgeResult(QStringLiteral("HD-X-010"), facts.staleConfiguration ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy,
        facts.staleConfiguration ? QStringLiteral("Unresolved configuration reference detected.") : QStringLiteral("No stale configuration reference detected.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-013"), knowledgeResult(QStringLiteral("HD-X-013"), facts.virtualOutputHidden ? DoctorCheckStatus::Failed : DoctorCheckStatus::Healthy,
        facts.virtualOutputHidden ? QStringLiteral("Virtual output is configured to be hidden.") : QStringLiteral("No virtual-output hiding match detected.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-014"), knowledgeResult(QStringLiteral("HD-X-014"), facts.driverOnly ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable,
        facts.driverOnly ? QStringLiteral("Driver observed without official client payload.") : QStringLiteral("Driver-only partial-install correlation not observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-016"), knowledgeResult(QStringLiteral("HD-X-016"), facts.newerPackageAndOldDriver ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable,
        facts.newerPackageAndOldDriver ? QStringLiteral("Update/restart version mismatch correlation detected.") : QStringLiteral("No incomplete-update correlation observed.")));
    analysis.catalogResults.insert(QStringLiteral("HD-X-018"), knowledgeResult(QStringLiteral("HD-X-018"), session.diagnoses().isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Informational,
        session.diagnoses().isEmpty() ? QStringLiteral("No diagnosis requires a repair recommendation.") : QStringLiteral("Every diagnosis is reportable; Phase 2 exposes no repair execution.")));
    analysis.catalogResults.insert(QStringLiteral("HD-KB-001"), knowledgeResult(QStringLiteral("HD-KB-001"), session.diagnoses().isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Informational,
        session.diagnoses().isEmpty() ? QStringLiteral("No failure signature matched.") : QStringLiteral("Deterministic knowledge signatures evaluated.")));
    analysis.catalogResults.insert(QStringLiteral("HD-KB-002"), knowledgeResult(QStringLiteral("HD-KB-002"), facts.insufficientEvidence ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy,
        facts.insufficientEvidence ? QStringLiteral("Insufficient evidence suppressed root-cause inference.") : QStringLiteral("Required evidence gate applied to every matched signature.")));
    analysis.catalogResults.insert(QStringLiteral("HD-KB-003"), knowledgeResult(QStringLiteral("HD-KB-003"), facts.contradictions.isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning,
        facts.contradictions.isEmpty() ? QStringLiteral("No signature contradiction observed.") : QStringLiteral("Contradicting evidence lowered or redirected confidence.")));
    analysis.catalogResults.insert(QStringLiteral("HD-KB-004"), knowledgeResult(QStringLiteral("HD-KB-004"), DoctorCheckStatus::Informational,
        QStringLiteral("Knowledge engine %1 recorded in this session.").arg(version())));
    analysis.catalogResults.insert(QStringLiteral("HD-KB-005"), knowledgeResult(QStringLiteral("HD-KB-005"), DoctorCheckStatus::Informational,
        QStringLiteral("Repairability is classification only; repairs are disabled in Phase 2.")));
    session.appendActivity({QDateTime::currentDateTimeUtc(), DoctorCheckId(QStringLiteral("HD-KB-001")), DoctorCheckStatus::Informational,
        QStringLiteral("Deterministic diagnosis analysis complete"), QStringLiteral("%1 finding rule(s), %2 diagnosis rule(s)").arg(analysis.findingRuleCount).arg(analysis.diagnosisRuleCount), {}});
    return analysis;
}

} // namespace hotas::doctor
