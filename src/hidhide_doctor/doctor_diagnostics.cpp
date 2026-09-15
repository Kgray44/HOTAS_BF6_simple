#include "doctor_diagnostics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>

namespace hotas::doctor {
namespace {

QString titleFor(const DoctorCheckId &id)
{
    return DoctorCatalog::v11CheckTitle(id);
}

bool isPhaseTwoKnowledgeCheck(const DoctorCheckId &id)
{
    return id.value().startsWith(QStringLiteral("HD-X-")) || id.value().startsWith(QStringLiteral("HD-KB-"));
}

bool hasDirectPhaseOneSource(const DoctorCheckId &id)
{
    const QString value = id.value();
    return value.startsWith(QStringLiteral("HD-SYS-")) || value.startsWith(QStringLiteral("HD-PORT-"))
        || value.startsWith(QStringLiteral("HD-API-")) || value.startsWith(QStringLiteral("HD-CFG-"))
        || value.startsWith(QStringLiteral("HD-DEV-")) || value.startsWith(QStringLiteral("HD-WIN-"));
}

const ProtocolObservation *protocol(const ReadOnlyDiagnosticSnapshot &snapshot, const QString &operation)
{
    for (const ProtocolObservation &observation : snapshot.protocol) {
        if (observation.operation == operation) return &observation;
    }
    return nullptr;
}

const CatalogObservation *catalogObservation(const ReadOnlyDiagnosticSnapshot &snapshot, const QString &id)
{
    for (const CatalogObservation &observation : snapshot.catalogObservations) {
        if (observation.checkId == id) return &observation;
    }
    return nullptr;
}

bool hasArtifact(const ReadOnlyDiagnosticSnapshot &snapshot, DoctorArtifactKind kind)
{
    for (const FileArtifactObservation &artifact : snapshot.artifacts) {
        if (artifact.kind == kind && artifact.exists) return true;
    }
    return false;
}

QString statusSummary(DoctorCheckStatus status, const QString &summary)
{
    return QStringLiteral("%1: %2").arg(displayName(status), summary);
}

QJsonObject nativeErrorJson(const std::optional<NativeError> &error)
{
    if (!error) return {};
    return {{QStringLiteral("domain"), static_cast<int>(error->domain)}, {QStringLiteral("code"), static_cast<qint64>(error->code)},
        {QStringLiteral("symbol"), error->symbolicName}, {QStringLiteral("message"), error->message}};
}

QString redact(const QString &value, EvidenceSensitivity sensitivity, bool redacted)
{
    if (!redacted || sensitivity == EvidenceSensitivity::SafeToExport) return value;
    return QStringLiteral("[redacted]");
}

} // namespace

DoctorCatalog DoctorDiagnosticEngine::registeredCatalog()
{
    DoctorCatalog catalog;
    for (const QString &value : DoctorCatalog::v11DefinedCheckIds()) {
        DoctorCheckDefinition definition;
        definition.id = DoctorCheckId(value);
        definition.title = titleFor(definition.id);
        definition.phase = phaseFor(definition.id);
        definition.weight = {definition.phase == DoctorPhase::ProtocolApiHealth ? 3 : 1};
        definition.timeoutMs = definition.phase == DoctorPhase::ProtocolApiHealth ? 2500 : 10000;
        definition.implementation = isPhaseTwoKnowledgeCheck(definition.id) ? CatalogImplementationState::Deferred
            : (hasDirectPhaseOneSource(definition.id) ? CatalogImplementationState::Implemented
                                                      : CatalogImplementationState::Conditional);
        catalog.registerCheck(std::move(definition));
    }
    return catalog;
}

DoctorPhase DoctorDiagnosticEngine::phaseFor(const DoctorCheckId &id)
{
    const QString value = id.value();
    if (value.startsWith(QStringLiteral("HD-SYS-")) || value.startsWith(QStringLiteral("HD-PORT-"))) return DoctorPhase::SystemEnvironment;
    if (value.startsWith(QStringLiteral("HD-INST-"))) return DoctorPhase::InstallationDiscovery;
    if (value.startsWith(QStringLiteral("HD-PKG-"))) return DoctorPhase::PackageIntegrity;
    if (value.startsWith(QStringLiteral("HD-DRV-"))) return DoctorPhase::KernelDriverState;
    if (value.startsWith(QStringLiteral("HD-API-"))) return DoctorPhase::ProtocolApiHealth;
    if (value.startsWith(QStringLiteral("HD-CFG-"))) return DoctorPhase::ConfigurationIntegrity;
    if (value.startsWith(QStringLiteral("HD-DEV-"))) return DoctorPhase::DeviceEcosystem;
    if (value.startsWith(QStringLiteral("HD-ISO-"))) return DoctorPhase::IsolationVerification;
    if (value.startsWith(QStringLiteral("HD-WIN-"))) return DoctorPhase::WindowsEvidence;
    if (value.startsWith(QStringLiteral("HD-X-"))) return DoctorPhase::ConsistencyAnalysis;
    if (value.startsWith(QStringLiteral("HD-KB-"))) return DoctorPhase::Diagnosis;
    return DoctorPhase::SystemEnvironment;
}

DoctorCheckResult DoctorDiagnosticEngine::evaluate(const DoctorCheckDefinition &definition,
    const ReadOnlyDiagnosticSnapshot &snapshot)
{
    DoctorCheckResult result;
    result.checkId = definition.id;
    result.implementationConditional = definition.implementation == CatalogImplementationState::Deferred
        || definition.implementation == CatalogImplementationState::Conditional;
    const QString id = definition.id.value();
    if (definition.implementation == CatalogImplementationState::Deferred) {
        result.status = DoctorCheckStatus::NotApplicable;
        result.summary = QStringLiteral("Deferred explicitly: deterministic knowledge/signature evaluation belongs to Phase 2.");
        return result;
    }
    if (const CatalogObservation *observed = catalogObservation(snapshot, id)) {
        result.status = observed->status;
        result.summary = observed->summary;
        result.technicalDetails = observed->technicalDetails;
        result.nativeError = observed->nativeError;
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-API-"))) {
        QString operation;
        if (id == QStringLiteral("HD-API-001")) operation = QStringLiteral("OPEN_CONTROL");
        else if (id == QStringLiteral("HD-API-002") || id == QStringLiteral("HD-CFG-001")) operation = QStringLiteral("GET_ACTIVE");
        else if (id == QStringLiteral("HD-API-003") || id == QStringLiteral("HD-CFG-002")) operation = QStringLiteral("GET_INVERSE");
        else if (id == QStringLiteral("HD-API-004")) operation = QStringLiteral("GET_WHITELIST_SIZE");
        else if (id == QStringLiteral("HD-API-005")) operation = QStringLiteral("GET_WHITELIST");
        else if (id == QStringLiteral("HD-API-007")) operation = QStringLiteral("GET_BLACKLIST_SIZE");
        else if (id == QStringLiteral("HD-API-008")) operation = QStringLiteral("GET_BLACKLIST");
        if (!operation.isEmpty()) {
            if (const ProtocolObservation *probe = protocol(snapshot, operation)) {
                result.status = probe->status;
                result.durationMs = probe->durationMs;
                result.nativeError = probe->nativeError;
                result.summary = probe->value.isEmpty() ? QStringLiteral("%1 completed").arg(operation) : probe->value;
                result.technicalDetails = probe->nativeError ? probe->nativeError->message : QString();
                return result;
            }
            result.status = snapshot.environment.hidhide.present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable;
            result.summary = snapshot.environment.hidhide.present ? QStringLiteral("The independent protocol operation was not observed.")
                : QStringLiteral("HidHide is absent; direct protocol is not applicable.");
            return result;
        }
        if (id == QStringLiteral("HD-API-010") || id == QStringLiteral("HD-API-011")) {
            result.status = DoctorCheckStatus::NotApplicable;
            result.summary = QStringLiteral("Session mutation capability is deliberately not probed by the read-only engine.");
            return result;
        }
        result.status = DoctorCheckStatus::Informational;
        result.summary = QStringLiteral("Read-only protocol evidence is retained without invoking optional CLI/client behavior.");
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-INST-"))) {
        const bool client = hasArtifact(snapshot, DoctorArtifactKind::ClientExecutable);
        const bool cli = hasArtifact(snapshot, DoctorArtifactKind::CliExecutable);
        const bool driver = hasArtifact(snapshot, DoctorArtifactKind::DriverBinary) || snapshot.service.present;
        result.status = (!client && !cli && !driver) ? DoctorCheckStatus::Informational : DoctorCheckStatus::Healthy;
        result.summary = QStringLiteral("Client=%1; CLI=%2; driver/service=%3; discovered roots and metadata are retained.")
            .arg(client ? QStringLiteral("present") : QStringLiteral("absent"), cli ? QStringLiteral("present") : QStringLiteral("absent"),
                driver ? QStringLiteral("present") : QStringLiteral("absent"));
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-PKG-"))) {
        const int artifacts = std::count_if(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(),
            [](const FileArtifactObservation &artifact) { return artifact.exists; });
        result.status = artifacts == 0 ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Healthy;
        result.summary = QStringLiteral("%1 readable component artifact(s); hashes, version resources, architecture, and signature outcomes retained.").arg(artifacts);
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-DRV-"))) {
        if (!snapshot.service.present && !snapshot.environment.hidhide.present) {
            result.status = DoctorCheckStatus::NotApplicable;
            result.summary = QStringLiteral("HidHide service/driver is absent on this machine.");
        } else if (snapshot.service.nativeError) {
            result.status = DoctorCheckStatus::PermissionLimited;
            result.summary = snapshot.service.nativeError->message;
        } else {
            result.status = DoctorCheckStatus::Healthy;
            result.summary = QStringLiteral("Service=%1; state=%2; package candidates=%3.")
                .arg(snapshot.service.present ? QStringLiteral("present") : QStringLiteral("absent"), snapshot.service.currentState)
                .arg(snapshot.driverPackages.size());
        }
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-CFG-"))) {
        const ProtocolObservation *whitelist = protocol(snapshot, QStringLiteral("GET_WHITELIST"));
        const ProtocolObservation *blacklist = protocol(snapshot, QStringLiteral("GET_BLACKLIST"));
        if (!snapshot.environment.hidhide.present) {
            result.status = DoctorCheckStatus::NotApplicable;
            result.summary = QStringLiteral("HidHide persistent configuration is not present.");
        } else if ((whitelist && whitelist->status == DoctorCheckStatus::Failed) || (blacklist && blacklist->status == DoctorCheckStatus::Failed)) {
            result.status = DoctorCheckStatus::Warning;
            result.summary = QStringLiteral("Configuration was observed independently; one or more driver queries failed without stopping the scan.");
        } else {
            result.status = DoctorCheckStatus::Healthy;
            result.summary = QStringLiteral("Registry whitelist=%1; registry blacklist=%2; direct API evidence retained separately.")
                .arg(snapshot.registryWhitelist.size()).arg(snapshot.registryBlacklist.size());
        }
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-DEV-"))) {
        const int failures = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(),
            [](const DeviceObservation &device) { return !device.propertyFailures.isEmpty() || device.nativeError.has_value(); });
        result.status = failures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy;
        result.summary = QStringLiteral("%1 relevant device(s); %2 isolated per-device property failure(s).")
            .arg(snapshot.devices.size()).arg(failures);
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-ISO-"))) {
        result.status = DoctorCheckStatus::Informational;
        result.summary = snapshot.environment.hidhide.present
            ? QStringLiteral("Configuration evidence is available; live game-process visibility is intentionally unverified.")
            : QStringLiteral("No HidHide installation is present, so live isolation cannot be inferred.");
        return result;
    }
    if (id.startsWith(QStringLiteral("HD-WIN-"))) {
        result.status = DoctorCheckStatus::Informational;
        result.summary = QStringLiteral("Event Log=%1; WER=%2; SetupAPI=%3 bounded observation(s).")
            .arg(snapshot.events.size()).arg(snapshot.werReports.size()).arg(snapshot.setupApiEvidence.size());
        return result;
    }
    result.status = DoctorCheckStatus::Unknown;
    result.summary = QStringLiteral("No direct read-only observation was available for this catalog requirement on this system.");
    return result;
}

DiagnosticRunOutcome DoctorDiagnosticEngine::run(IReadOnlyDiagnosticProvider &provider,
    std::atomic_bool *cancelled, ProgressCallback onProgress) const
{
    DiagnosticRunOutcome outcome;
    QElapsedTimer timer;
    timer.start();
    outcome.startedAt = QDateTime::currentDateTimeUtc();
    outcome.session = createPreparedSession();
    outcome.catalogCoverage = registeredCatalog().coverage();
    outcome.session.transitionTo(DoctorSessionState::Diagnosing);
    if (onProgress) onProgress(outcome.session);
    outcome.snapshot = provider.observe(cancelled, [&](const DoctorCheckId &checkId, int percent) {
        const DoctorStepId stepId(QStringLiteral("STEP-") + checkId.value());
        outcome.session.plan().setStatus(stepId, DoctorCheckStatus::Running, QStringLiteral("Reading native read-only evidence."));
        outcome.session.plan().setCurrentStepProgress(stepId, percent);
        if (onProgress) onProgress(outcome.session);
    });
    for (const DiagnosticPlanItem &item : outcome.session.plan().items()) {
        if (cancelled && cancelled->load()) {
            DoctorCheckResult result;
            result.checkId = item.check.id;
            result.status = DoctorCheckStatus::Cancelled;
            result.summary = QStringLiteral("Scan cancellation requested before this check started.");
            EvidenceRecord evidence;
            evidence.checkId = result.checkId;
            evidence.kind = EvidenceKind::Observation;
            evidence.provenance = EvidenceProvenance::Direct;
            evidence.sensitivity = EvidenceSensitivity::SafeToExport;
            evidence.source = QStringLiteral("phase1-read-only-engine");
            evidence.humanSummary = result.summary;
            outcome.session.appendEvidence(evidence);
            result.evidenceIds.append(outcome.session.evidence().back().id);
            outcome.session.appendCheckResult(result);
            outcome.session.plan().setStatus(item.stepId, DoctorCheckStatus::Cancelled, QStringLiteral("Scan cancellation requested."));
            if (onProgress) onProgress(outcome.session);
            continue;
        }
        outcome.session.plan().setStatus(item.stepId, DoctorCheckStatus::Running, QStringLiteral("Evaluating observed evidence."));
        if (onProgress) onProgress(outcome.session);
        DoctorCheckResult result = evaluate(item.check, outcome.snapshot);
        EvidenceRecord evidence;
        evidence.checkId = result.checkId;
        evidence.kind = EvidenceKind::Observation;
        evidence.provenance = EvidenceProvenance::Direct;
        evidence.sensitivity = EvidenceSensitivity::RequiresRedaction;
        evidence.source = QStringLiteral("phase1-read-only-engine");
        evidence.humanSummary = result.summary;
        evidence.technicalDetails = result.technicalDetails;
        evidence.nativeError = result.nativeError;
        evidence.durationMs = result.durationMs;
        outcome.session.appendEvidence(evidence);
        result.evidenceIds.append(outcome.session.evidence().back().id);
        outcome.session.appendCheckResult(result);
        outcome.session.plan().setStatus(item.stepId, result.status, result.summary);
        if (onProgress) onProgress(outcome.session);
    }
    outcome.cancelled = cancelled && cancelled->load();
    outcome.session.transitionTo(outcome.cancelled ? DoctorSessionState::Cancelled : DoctorSessionState::Analyzing);
    if (!outcome.cancelled) outcome.session.transitionTo(DoctorSessionState::DiagnosisComplete);
    outcome.session.setUserAction({UserActionState::NothingRequired, FindingSeverity::Informational,
        outcome.cancelled ? QStringLiteral("Scan cancelled safely") : QStringLiteral("No repair action in Phase 1"),
        outcome.cancelled ? QStringLiteral("Collected read-only evidence is retained; no Windows or HidHide state changed.")
                          : QStringLiteral("The scan is read-only and did not request elevation or change HidHide."), {}, {}, {}});
    outcome.completedAt = QDateTime::currentDateTimeUtc();
    outcome.durationMs = timer.elapsed();
    return outcome;
}

DoctorSession DoctorDiagnosticEngine::createPreparedSession() const
{
    DoctorSession session;
    DoctorCatalog catalog = registeredCatalog();
    for (const DoctorCheckDefinition &definition : catalog.registeredChecks())
        session.plan().addItem({DoctorStepId(QStringLiteral("STEP-") + definition.id.value()), definition});
    session.plan().freeze();
    session.setUserAction({UserActionState::NothingRequired, FindingSeverity::Informational,
        QStringLiteral("Read-only scan preparing"), QStringLiteral("No elevation or HidHide change will be requested."), {}, {}, {}});
    return session;
}

QByteArray DoctorDiagnosticEngine::serializeJson(const DiagnosticRunOutcome &outcome, bool redactSensitive)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.insert(QStringLiteral("sessionId"), outcome.session.id().value());
    root.insert(QStringLiteral("startedAt"), outcome.startedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("completedAt"), outcome.completedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("scanDurationMs"), outcome.durationMs);
    root.insert(QStringLiteral("cancelled"), outcome.cancelled);
    root.insert(QStringLiteral("catalogCoverage"), QJsonObject{{QStringLiteral("defined"), outcome.catalogCoverage.defined},
        {QStringLiteral("registered"), outcome.catalogCoverage.registered}, {QStringLiteral("implemented"), outcome.catalogCoverage.implemented},
        {QStringLiteral("conditional"), outcome.catalogCoverage.conditional},
        {QStringLiteral("deferred"), outcome.catalogCoverage.registered - outcome.catalogCoverage.implemented
            - outcome.catalogCoverage.conditional - outcome.catalogCoverage.unsupported},
        {QStringLiteral("unsupported"), outcome.catalogCoverage.unsupported}});
    root.insert(QStringLiteral("doctorBuild"), QJsonObject{{QStringLiteral("version"), outcome.snapshot.build.version},
        {QStringLiteral("sourceRevision"), outcome.snapshot.build.sourceRevision}, {QStringLiteral("channel"), outcome.snapshot.build.channel},
        {QStringLiteral("processArchitecture"), outcome.snapshot.build.processArchitecture}});
    root.insert(QStringLiteral("platform"), QJsonObject{{QStringLiteral("edition"), outcome.snapshot.environment.platform.windowsEdition},
        {QStringLiteral("build"), static_cast<int>(outcome.snapshot.environment.platform.build)},
        {QStringLiteral("revision"), static_cast<int>(outcome.snapshot.environment.platform.revision)},
        {QStringLiteral("nativeArchitecture"), displayName(outcome.snapshot.environment.platform.nativeArchitecture)},
        {QStringLiteral("processArchitecture"), displayName(outcome.snapshot.environment.platform.processArchitecture)}});
    QJsonArray results;
    for (const DoctorCheckResult &result : outcome.session.checkResults()) results.append(QJsonObject{
        {QStringLiteral("checkId"), result.checkId.value()}, {QStringLiteral("status"), displayName(result.status)},
        {QStringLiteral("summary"), redact(result.summary, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
        {QStringLiteral("durationMs"), static_cast<qint64>(result.durationMs)}, {QStringLiteral("conditional"), result.implementationConditional},
        {QStringLiteral("evidenceIds"), QJsonArray::fromStringList([&] { QStringList ids; for (const EvidenceId &id : result.evidenceIds) ids.append(id.value()); return ids; }())},
        {QStringLiteral("error"), nativeErrorJson(result.nativeError)}});
    root.insert(QStringLiteral("checkResults"), results);
    QJsonArray artifacts;
    for (const FileArtifactObservation &artifact : outcome.snapshot.artifacts) artifacts.append(QJsonObject{
        {QStringLiteral("role"), artifact.role}, {QStringLiteral("present"), artifact.exists},
        {QStringLiteral("path"), redact(artifact.path, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("fileVersion"), artifact.fileVersion}, {QStringLiteral("productVersion"), artifact.productVersion},
        {QStringLiteral("architecture"), displayName(artifact.architecture)}, {QStringLiteral("sha256"), artifact.sha256},
        {QStringLiteral("signatureTrust"), displayName(artifact.signatureTrust)}});
    root.insert(QStringLiteral("componentArtifacts"), artifacts);
    QJsonArray packages;
    for (const DriverPackageObservation &package : outcome.snapshot.driverPackages) packages.append(QJsonObject{
        {QStringLiteral("infName"), package.infName}, {QStringLiteral("provider"), package.provider}, {QStringLiteral("version"), package.version},
        {QStringLiteral("architecture"), displayName(package.architecture)}});
    root.insert(QStringLiteral("driverPackages"), packages);
    root.insert(QStringLiteral("service"), QJsonObject{{QStringLiteral("present"), outcome.snapshot.service.present},
        {QStringLiteral("state"), outcome.snapshot.service.currentState}, {QStringLiteral("startType"), outcome.snapshot.service.startType},
        {QStringLiteral("binaryPath"), redact(outcome.snapshot.service.binaryPath, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("error"), nativeErrorJson(outcome.snapshot.service.nativeError)}});
    QJsonArray protocol;
    for (const ProtocolObservation &probe : outcome.snapshot.protocol) protocol.append(QJsonObject{
        {QStringLiteral("operation"), probe.operation}, {QStringLiteral("status"), displayName(probe.status)},
        {QStringLiteral("value"), redact(probe.value, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
        {QStringLiteral("durationMs"), static_cast<qint64>(probe.durationMs)}, {QStringLiteral("error"), nativeErrorJson(probe.nativeError)}});
    root.insert(QStringLiteral("protocol"), protocol);
    QJsonArray devices;
    for (const DeviceObservation &device : outcome.snapshot.devices) devices.append(QJsonObject{
        {QStringLiteral("instanceId"), redact(device.instanceId, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("classification"), displayName(device.classification)}, {QStringLiteral("problemCode"), static_cast<int>(device.problemCode)},
        {QStringLiteral("usagePage"), static_cast<int>(device.usagePage)}, {QStringLiteral("usage"), static_cast<int>(device.usage)},
        {QStringLiteral("containerId"), redact(device.containerId, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("driverProvider"), device.driverProvider}, {QStringLiteral("driverVersion"), device.driverVersion},
        {QStringLiteral("interfaceCount"), device.interfacePaths.size()}, {QStringLiteral("propertyFailureCount"), device.propertyFailures.size()}});
    root.insert(QStringLiteral("devices"), devices);
    QJsonArray restart;
    for (const PendingRestartObservation &observation : outcome.snapshot.pendingRestart) restart.append(QJsonObject{
        {QStringLiteral("source"), observation.source}, {QStringLiteral("value"), redact(observation.value, observation.sensitivity, redactSensitive)},
        {QStringLiteral("error"), nativeErrorJson(observation.nativeError)}});
    root.insert(QStringLiteral("pendingRestartEvidence"), restart);
    QJsonArray processes;
    for (const ProcessObservation &process : outcome.snapshot.processes) processes.append(QJsonObject{
        {QStringLiteral("imageName"), process.imageName}, {QStringLiteral("processId"), static_cast<int>(process.processId)},
        {QStringLiteral("executablePath"), redact(process.executablePath, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("error"), nativeErrorJson(process.nativeError)}});
    root.insert(QStringLiteral("relevantProcesses"), processes);
    root.insert(QStringLiteral("hidHideInterfacePaths"), QJsonArray::fromStringList([&] { QStringList values; for (const QString &path : outcome.snapshot.hidHideInterfacePaths) values.append(redact(path, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)); return values; }()));
    root.insert(QStringLiteral("hidHideFilterRegistrations"), QJsonArray::fromStringList(outcome.snapshot.hidHideFilterRegistrations));
    root.insert(QStringLiteral("hidHideFilterEnumerationError"), nativeErrorJson(outcome.snapshot.hidHideFilterEnumerationError));
    const auto eventJson = [&](const QList<EventObservation> &observations) {
        QJsonArray values;
        for (const EventObservation &event : observations) values.append(QJsonObject{
            {QStringLiteral("channel"), event.channel}, {QStringLiteral("provider"), event.provider}, {QStringLiteral("eventId"), static_cast<int>(event.eventId)},
            {QStringLiteral("level"), event.level}, {QStringLiteral("timestamp"), event.timestamp.toString(Qt::ISODateWithMs)},
            {QStringLiteral("summary"), redact(event.summary, event.sensitivity, redactSensitive)}, {QStringLiteral("error"), nativeErrorJson(event.nativeError)}});
        return values;
    };
    root.insert(QStringLiteral("eventLogEvidence"), eventJson(outcome.snapshot.events));
    root.insert(QStringLiteral("werEvidence"), eventJson(outcome.snapshot.werReports));
    root.insert(QStringLiteral("setupApiEvidence"), eventJson(outcome.snapshot.setupApiEvidence));
    QJsonArray evidence;
    for (const EvidenceRecord &record : outcome.session.evidence()) evidence.append(QJsonObject{
        {QStringLiteral("evidenceId"), record.id.value()}, {QStringLiteral("checkId"), record.checkId.value()},
        {QStringLiteral("kind"), static_cast<int>(record.kind)}, {QStringLiteral("provenance"), static_cast<int>(record.provenance)},
        {QStringLiteral("source"), record.source}, {QStringLiteral("recordedAt"), record.recordedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("summary"), redact(record.humanSummary, record.sensitivity, redactSensitive)},
        {QStringLiteral("technicalDetails"), redact(record.technicalDetails, record.sensitivity, redactSensitive)},
        {QStringLiteral("structuredValue"), redact(record.structuredValue, record.sensitivity, redactSensitive)},
        {QStringLiteral("durationMs"), static_cast<qint64>(record.durationMs)}, {QStringLiteral("direct"), record.direct},
        {QStringLiteral("error"), nativeErrorJson(record.nativeError)}});
    root.insert(QStringLiteral("evidenceRecords"), evidence);
    root.insert(QStringLiteral("findings"), QJsonArray());
    root.insert(QStringLiteral("diagnoses"), QJsonArray());
    root.insert(QStringLiteral("contradictions"), QJsonArray::fromStringList(outcome.snapshot.contradictions));
    root.insert(QStringLiteral("operationalLimits"), QJsonArray::fromStringList(outcome.snapshot.operationalLimits));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace hotas::doctor
