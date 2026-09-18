#include "doctor_diagnostics.h"

#include "doctor_deep_repair.h"
#include "doctor_knowledge.h"
#include "doctor_repair_engine.h"

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

void addEvidenceField(EvidenceRecord *record, EvidenceFieldCategory category, QString label, QString value,
                      EvidenceSensitivity sensitivity = EvidenceSensitivity::SafeToExport, bool monospace = false)
{
    if (value.trimmed().isEmpty()) return;
    record->fields.append({category, std::move(label), std::move(value), sensitivity, monospace});
}

QString semanticProtocolValue(const ProtocolObservation &probe)
{
    const auto isTrue = [&] { return probe.value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0; };
    const auto isFalse = [&] { return probe.value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0; };
    if (probe.operation.startsWith(QStringLiteral("GET_ACTIVE")) && (isTrue() || isFalse()))
        return isTrue() ? QStringLiteral("HidHide active state is enabled.") : QStringLiteral("HidHide active state is disabled.");
    if (probe.operation.startsWith(QStringLiteral("GET_INVERSE")) && (isTrue() || isFalse()))
        return isTrue() ? QStringLiteral("Inverse / whitelist mode is enabled.") : QStringLiteral("Inverse / whitelist mode is disabled.");
    if (probe.operation == QStringLiteral("OPEN_CONTROL"))
        return QStringLiteral("The HidHide control endpoint accepted a read-only open.");
    if (!probe.value.isEmpty()) return probe.value;
    return QStringLiteral("The native operation completed without a readable value.");
}

QString protocolOperationFor(const DoctorCheckId &id)
{
    const QString value = id.value();
    if (value == QStringLiteral("HD-API-001")) return QStringLiteral("OPEN_CONTROL");
    if (value == QStringLiteral("HD-API-002") || value == QStringLiteral("HD-CFG-001")) return QStringLiteral("GET_ACTIVE");
    if (value == QStringLiteral("HD-API-003") || value == QStringLiteral("HD-CFG-002")) return QStringLiteral("GET_INVERSE");
    if (value == QStringLiteral("HD-API-004")) return QStringLiteral("GET_WHITELIST_SIZE");
    if (value == QStringLiteral("HD-API-005")) return QStringLiteral("GET_WHITELIST");
    if (value == QStringLiteral("HD-API-007")) return QStringLiteral("GET_BLACKLIST_SIZE");
    if (value == QStringLiteral("HD-API-008")) return QStringLiteral("GET_BLACKLIST");
    return {};
}

QString enabledState(const std::optional<bool> &value, const QString &subject)
{
    if (!value) return QStringLiteral("%1 was not readable.").arg(subject);
    return *value ? QStringLiteral("%1 is enabled.").arg(subject) : QStringLiteral("%1 is disabled.").arg(subject);
}

QStringList evidenceIds(const QList<EvidenceId> &ids)
{
    QStringList values;
    for (const EvidenceId &id : ids) values.append(id.value());
    return values;
}

EvidenceRecord forensicEvidence(const DoctorCheckDefinition &definition, const DoctorCheckResult &result,
                               const ReadOnlyDiagnosticSnapshot &snapshot, EvidenceKind kind,
                               EvidenceProvenance provenance, const QString &source, const QDateTime &startedAt,
                               const QDateTime &completedAt, qint64 monotonicDurationUs)
{
    EvidenceRecord record;
    record.checkId = result.checkId;
    record.kind = kind;
    record.provenance = provenance;
    record.sensitivity = EvidenceSensitivity::RequiresRedaction;
    record.source = source;
    record.sourceDisplayName = provenance == EvidenceProvenance::Derived
        ? QStringLiteral("Deterministic Doctor knowledge engine")
        : QStringLiteral("Windows / HidHide read-only diagnostic provider");
    record.provider = provenance == EvidenceProvenance::Derived
        ? QStringLiteral("DoctorKnowledgeEngine") : QStringLiteral("ReadOnlyWindowsDiagnosticProvider");
    record.subsystem = displayName(definition.phase);
    record.operation = protocolOperationFor(definition.id);
    record.method = provenance == EvidenceProvenance::Derived
        ? QStringLiteral("Deterministic rule evaluation over retained evidence")
        : QStringLiteral("Read-only Windows and HidHide observation");
    record.targetType = QStringLiteral("Diagnostic check");
    record.targetIdentity = result.checkId.value();
    record.targetDisplayName = definition.title;
    record.expectedState = DoctorCatalog::v11CheckPurpose(result.checkId);
    record.observedState = result.summary;
    record.statusReason = result.technicalDetails.isEmpty() ? result.summary : result.technicalDetails;
    record.technicalDetails = result.technicalDetails;
    record.humanSummary = result.summary;
    record.nativeError = result.nativeError;
    record.durationMs = result.durationMs;
    record.startedAt = startedAt;
    record.completedAt = completedAt;
    record.monotonicDurationUs = monotonicDurationUs;
    record.timeoutMs = definition.timeoutMs;

    addEvidenceField(&record, EvidenceFieldCategory::Identity, QStringLiteral("CHECK"), result.checkId.value(), EvidenceSensitivity::SafeToExport, true);
    addEvidenceField(&record, EvidenceFieldCategory::Identity, QStringLiteral("CHECK TITLE"), definition.title);
    addEvidenceField(&record, EvidenceFieldCategory::Identity, QStringLiteral("WHAT THIS CHECK MEANS"), record.expectedState);
    addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("STATUS"), displayName(result.status));
    addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("STATUS RATIONALE"), record.statusReason, EvidenceSensitivity::RequiresRedaction);
    addEvidenceField(&record, EvidenceFieldCategory::Method, QStringLiteral("PROVIDER"), record.provider, EvidenceSensitivity::SafeToExport, true);
    addEvidenceField(&record, EvidenceFieldCategory::Method, QStringLiteral("COLLECTION METHOD"), record.method);
    addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("STARTED (UTC)"), startedAt.toString(Qt::ISODateWithMs), EvidenceSensitivity::SafeToExport, true);
    addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("COMPLETED (UTC)"), completedAt.toString(Qt::ISODateWithMs), EvidenceSensitivity::SafeToExport, true);
    addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("MONOTONIC DURATION"), QStringLiteral("%1 us").arg(monotonicDurationUs), EvidenceSensitivity::SafeToExport, true);
    addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("CHECK TIMEOUT"), QStringLiteral("%1 ms").arg(definition.timeoutMs), EvidenceSensitivity::SafeToExport, true);

    const QString operation = protocolOperationFor(definition.id);
    if (!operation.isEmpty()) {
        if (const ProtocolObservation *probe = protocol(snapshot, operation)) {
            record.provider = QStringLiteral("HidHide control-device provider");
            record.operation = probe->operation;
            record.method = probe->api.isEmpty() ? QStringLiteral("CreateFileW + DeviceIoControl read-only GET") : probe->api;
            record.targetType = QStringLiteral("HidHide control endpoint");
            record.targetIdentity = probe->endpoint;
            record.targetDisplayName = probe->endpoint;
            record.observedState = semanticProtocolValue(*probe);
            record.statusReason = probe->nativeError ? probe->nativeError->message : record.observedState;
            record.nativeError = probe->nativeError;
            record.durationMs = probe->durationMs;
            record.startedAt = probe->startedAt.isValid() ? probe->startedAt : startedAt;
            record.completedAt = probe->completedAt.isValid() ? probe->completedAt : completedAt;
            record.monotonicDurationUs = probe->monotonicDurationUs > 0 ? probe->monotonicDurationUs : monotonicDurationUs;
            record.timeoutMs = probe->timeoutMs > 0 ? probe->timeoutMs : definition.timeoutMs;
            record.humanSummary = QStringLiteral("%1 %2").arg(displayName(probe->status), record.observedState);
            addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("ENDPOINT"), probe->endpoint, EvidenceSensitivity::PotentiallyIdentifying, true);
            addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("ACCESS"), probe->access, EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Method, QStringLiteral("API"), record.method, EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Method, QStringLiteral("OPERATION"), probe->operation, EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Method, QStringLiteral("IOCTL"), QStringLiteral("0x%1").arg(probe->ioctlCode, 8, 16, QLatin1Char('0')).toUpper(), EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("OBSERVED STATE"), record.observedState, EvidenceSensitivity::RequiresRedaction);
            addEvidenceField(&record, EvidenceFieldCategory::Raw, QStringLiteral("RAW RETURN VALUE"), probe->value, EvidenceSensitivity::RequiresRedaction, true);
            addEvidenceField(&record, EvidenceFieldCategory::Raw, QStringLiteral("MULTI_SZ ENTRY COUNT"), QString::number(probe->multiStringValues.size()), EvidenceSensitivity::SafeToExport, true);
            if (!probe->multiStringValues.isEmpty()) {
                const int retainedEntries = std::min(3, static_cast<int>(probe->multiStringValues.size()));
                QStringList retainedValues;
                for (int index = 0; index < retainedEntries; ++index) retainedValues.append(probe->multiStringValues.at(index));
                QString payloadSample = retainedValues.join(QStringLiteral(" | "));
                if (probe->multiStringValues.size() > retainedEntries)
                    payloadSample += QStringLiteral(" | … %1 additional entry(s) not duplicated here").arg(probe->multiStringValues.size() - retainedEntries);
                addEvidenceField(&record, EvidenceFieldCategory::Raw, QStringLiteral("BOUNDED RESPONSE PAYLOAD SAMPLE"),
                    payloadSample, EvidenceSensitivity::PotentiallyIdentifying, true);
            }
            addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("PROBE REQUEST BYTES"), QString::number(probe->requestBytes), EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("PROBE RESPONSE BYTES"), QString::number(probe->responseBytes), EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("PROBE ATTEMPTS"), QString::number(probe->attemptCount), EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Timing, QStringLiteral("PROBE TIMEOUT"), QStringLiteral("%1 ms").arg(record.timeoutMs), EvidenceSensitivity::SafeToExport, true);
            record.attempts.append({std::max(1, probe->attemptCount), probe->operation, probe->endpoint,
                displayName(probe->status), record.startedAt, record.completedAt, record.monotonicDurationUs,
                record.timeoutMs, probe->requestBytes, probe->responseBytes, probe->nativeError});
        }
    } else if (definition.id.value().startsWith(QStringLiteral("HD-INST-")) || definition.id.value().startsWith(QStringLiteral("HD-PKG-"))) {
        const int present = std::count_if(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(),
            [](const FileArtifactObservation &artifact) { return artifact.exists; });
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("COMPONENT ARTIFACTS DISCOVERED"), QString::number(present), EvidenceSensitivity::SafeToExport, true);
        if (!snapshot.artifacts.isEmpty()) {
            const FileArtifactObservation &artifact = snapshot.artifacts.first();
            addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("REPRESENTATIVE ARTIFACT ROLE"), artifact.role);
            addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("REPRESENTATIVE ARTIFACT PATH"), artifact.path, EvidenceSensitivity::PotentiallyIdentifying, true);
            addEvidenceField(&record, EvidenceFieldCategory::Technical, QStringLiteral("FILE VERSION"), artifact.fileVersion, EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Technical, QStringLiteral("SHA-256"), artifact.sha256, EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::NativeResult, QStringLiteral("SIGNATURE OUTCOME"), displayName(artifact.signatureTrust));
        }
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("DRIVER STORE PACKAGE CANDIDATES"), QString::number(snapshot.driverPackages.size()), EvidenceSensitivity::SafeToExport, true);
    } else if (definition.id.value().startsWith(QStringLiteral("HD-DRV-"))) {
        addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("SERVICE NAME"), snapshot.service.serviceName, EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("SERVICE STATE"), snapshot.service.currentState);
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("SERVICE START TYPE"), snapshot.service.startType);
        addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("SERVICE BINARY"), snapshot.service.binaryPath, EvidenceSensitivity::PotentiallyIdentifying, true);
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("DRIVER PACKAGE CANDIDATES"), QString::number(snapshot.driverPackages.size()), EvidenceSensitivity::SafeToExport, true);
    } else if (definition.id.value().startsWith(QStringLiteral("HD-CFG-"))) {
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("REGISTRY ACTIVE STATE"), enabledState(snapshot.registryActive, QStringLiteral("Registry active state")));
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("REGISTRY INVERSE STATE"), enabledState(snapshot.registryInverse, QStringLiteral("Registry inverse state")));
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("REGISTRY WHITELIST ENTRIES"), QString::number(snapshot.registryWhitelist.size()), EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("REGISTRY BLACKLIST ENTRIES"), QString::number(snapshot.registryBlacklist.size()), EvidenceSensitivity::SafeToExport, true);
    } else if (definition.id.value().startsWith(QStringLiteral("HD-DEV-"))) {
        const auto failed = std::find_if(snapshot.devices.cbegin(), snapshot.devices.cend(), [](const DeviceObservation &device) {
            return !device.propertyFailures.isEmpty() || device.nativeError.has_value() || device.problemCode != 0;
        });
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("RELEVANT DEVICES"), QString::number(snapshot.devices.size()), EvidenceSensitivity::SafeToExport, true);
        if (failed != snapshot.devices.cend()) {
            addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("AFFECTED DEVICE"), failed->friendlyName, EvidenceSensitivity::PotentiallyIdentifying);
            addEvidenceField(&record, EvidenceFieldCategory::Target, QStringLiteral("DEVICE INSTANCE"), failed->instanceId, EvidenceSensitivity::PotentiallyIdentifying, true);
            addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("DEVICE PROBLEM CODE"), QString::number(failed->problemCode), EvidenceSensitivity::SafeToExport, true);
            addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("DEVICE PROPERTY FAILURES"), failed->propertyFailures.join(QStringLiteral("; ")), EvidenceSensitivity::RequiresRedaction);
        }
    } else if (definition.id.value().startsWith(QStringLiteral("HD-WIN-"))) {
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("EVENT LOG OBSERVATIONS"), QString::number(snapshot.events.size()), EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("WINDOWS ERROR REPORT OBSERVATIONS"), QString::number(snapshot.werReports.size()), EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::Observation, QStringLiteral("SETUPAPI OBSERVATIONS"), QString::number(snapshot.setupApiEvidence.size()), EvidenceSensitivity::SafeToExport, true);
        if (!snapshot.events.isEmpty()) {
            const EventObservation &event = snapshot.events.first();
            addEvidenceField(&record, EvidenceFieldCategory::Technical, QStringLiteral("REPRESENTATIVE EVENT"),
                QStringLiteral("%1 / %2 / %3").arg(event.channel, event.provider).arg(event.eventId), event.sensitivity, true);
        }
    }
    if (record.nativeError) {
        addEvidenceField(&record, EvidenceFieldCategory::NativeResult, QStringLiteral("NATIVE ERROR DOMAIN"), QString::number(static_cast<int>(record.nativeError->domain)), EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::NativeResult, QStringLiteral("NATIVE ERROR CODE"), QString::number(record.nativeError->code), EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::NativeResult, QStringLiteral("NATIVE ERROR SYMBOL"), record.nativeError->symbolicName, EvidenceSensitivity::SafeToExport, true);
        addEvidenceField(&record, EvidenceFieldCategory::NativeResult, QStringLiteral("NATIVE ERROR MESSAGE"), record.nativeError->message, EvidenceSensitivity::RequiresRedaction);
    }
    // A canonical record may never become an unbounded payload transport.
    // The full provider snapshot remains separately bounded; Inspector and
    // reports receive at most these labelled facts and an explicit marker if
    // a future source grows beyond the contract.
    record.originalFieldCount = record.fields.size();
    constexpr int maxEvidenceFields = 48;
    if (record.fields.size() > maxEvidenceFields) {
        record.fields.erase(record.fields.begin() + maxEvidenceFields, record.fields.end());
        record.collectionTruncated = true;
        record.truncationReason = QStringLiteral("Retained the first %1 structured fields from %2; no unbounded raw payload was copied.")
            .arg(maxEvidenceFields).arg(record.originalFieldCount);
    }
    return record;
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
        definition.implementation = (isPhaseTwoKnowledgeCheck(definition.id) || hasDirectPhaseOneSource(definition.id))
            ? CatalogImplementationState::Implemented : CatalogImplementationState::Conditional;
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
                result.summary = semanticProtocolValue(*probe);
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
    std::atomic_bool *cancelled, ProgressCallback onProgress, bool explicitApprovedUpgradeRequest) const
{
    DiagnosticRunOutcome outcome;
    QElapsedTimer timer;
    timer.start();
    outcome.startedAt = QDateTime::currentDateTimeUtc();
    outcome.session = createPreparedSession();
    outcome.catalogCoverage = registeredCatalog().coverage();
    outcome.session.transitionTo(DoctorSessionState::Diagnosing);
    const auto appendActivity = [&](DoctorActivityEventType type, const DoctorCheckId &checkId, DoctorPhase phase,
                                    DoctorCheckStatus status, QString title, QString detail,
                                    const QList<EvidenceId> &relatedEvidence = {}, QString reason = {},
                                    QString target = {}, QString result = {}) {
        DoctorActivityEvent event;
        event.timestamp = QDateTime::currentDateTimeUtc();
        event.type = type;
        event.checkId = checkId;
        event.phase = phase;
        event.status = status;
        event.title = std::move(title);
        event.detail = std::move(detail);
        event.reason = std::move(reason);
        event.target = std::move(target);
        event.result = std::move(result);
        event.evidenceIds = relatedEvidence;
        event.evidenceId = relatedEvidence.value(0);
        if (event.evidenceId.isValid()) {
            const auto evidence = std::find_if(outcome.session.evidence().cbegin(), outcome.session.evidence().cend(),
                [&](const EvidenceRecord &record) { return record.id.value() == event.evidenceId.value(); });
            if (evidence != outcome.session.evidence().cend()) {
                event.startedAt = evidence->startedAt;
                event.completedAt = evidence->completedAt;
                event.monotonicDurationUs = evidence->monotonicDurationUs;
            }
        }
        outcome.session.appendActivity(std::move(event));
    };
    appendActivity(DoctorActivityEventType::SessionStarted, {}, DoctorPhase::SystemEnvironment,
        DoctorCheckStatus::Running, QStringLiteral("Read-only diagnostic session started"),
        QStringLiteral("No HidHide or Windows configuration mutation is available to this diagnostic session."));
    if (onProgress) onProgress(outcome.session);
    outcome.snapshot = provider.observe(cancelled, [&](const DoctorCheckId &checkId, int percent) {
        const DoctorStepId stepId(QStringLiteral("STEP-") + checkId.value());
        outcome.session.plan().setStatus(stepId, DoctorCheckStatus::Running, QStringLiteral("Reading native read-only evidence."));
        outcome.session.plan().setCurrentStepProgress(stepId, percent);
        if (onProgress) onProgress(outcome.session);
    });
    outcome.session.setEnvironment(outcome.snapshot.environment);
    DoctorPhase lastPhase = DoctorPhase::ExtendedInvestigation;
    for (const DiagnosticPlanItem &item : outcome.session.plan().items()) {
        if (isPhaseTwoKnowledgeCheck(item.check.id) && !(cancelled && cancelled->load())) continue;
        if (item.check.phase != lastPhase) {
            appendActivity(DoctorActivityEventType::PhaseStarted, item.check.id, item.check.phase, DoctorCheckStatus::Running,
                displayName(item.check.phase), QStringLiteral("Beginning the %1 diagnostic phase.").arg(displayName(item.check.phase)));
            lastPhase = item.check.phase;
        }
        if (cancelled && cancelled->load()) {
            DoctorCheckResult result;
            result.checkId = item.check.id;
            result.status = DoctorCheckStatus::Cancelled;
            result.summary = QStringLiteral("Scan cancellation requested before this check started.");
            const QDateTime now = QDateTime::currentDateTimeUtc();
            EvidenceRecord evidence = forensicEvidence(item.check, result, outcome.snapshot, EvidenceKind::Observation,
                EvidenceProvenance::Direct, QStringLiteral("phase1-read-only-engine"), now, now, 0);
            outcome.session.appendEvidence(evidence);
            result.evidenceIds.append(outcome.session.evidence().back().id);
            outcome.session.appendCheckResult(result);
            outcome.session.plan().setStatus(item.stepId, DoctorCheckStatus::Cancelled, QStringLiteral("Scan cancellation requested."));
            appendActivity(DoctorActivityEventType::EvidenceRecorded, result.checkId, item.check.phase, result.status,
                QStringLiteral("Cancellation evidence recorded"), result.summary, result.evidenceIds);
            appendActivity(DoctorActivityEventType::CheckCompleted, result.checkId, item.check.phase, result.status,
                item.check.title, result.summary, result.evidenceIds);
            if (onProgress) onProgress(outcome.session);
            continue;
        }
        outcome.session.plan().setStatus(item.stepId, DoctorCheckStatus::Running, QStringLiteral("Evaluating observed evidence."));
        outcome.session.setCurrentOperation({DoctorOperationId(QStringLiteral("OP-") + item.check.id.value()),
            DoctorOperationState::Running, item.check.title, 0, item.check.timeoutMs});
        const QDateTime evaluationStartedAt = QDateTime::currentDateTimeUtc();
        QElapsedTimer evaluationTimer;
        evaluationTimer.start();
        appendActivity(DoctorActivityEventType::CheckStarted, item.check.id, item.check.phase, DoctorCheckStatus::Running,
            item.check.title, QStringLiteral("Evaluating retained read-only observation for %1.").arg(item.check.id.value()), {}, {}, item.check.title);
        if (onProgress) onProgress(outcome.session);
        DoctorCheckResult result = evaluate(item.check, outcome.snapshot);
        const QDateTime evaluationCompletedAt = QDateTime::currentDateTimeUtc();
        const qint64 evaluationDurationUs = std::max<qint64>(1, evaluationTimer.nsecsElapsed() / 1000);
        if (result.durationMs <= 0) result.durationMs = std::max<qint64>(1, (evaluationDurationUs + 999) / 1000);
        EvidenceRecord evidence = forensicEvidence(item.check, result, outcome.snapshot, EvidenceKind::Observation,
            EvidenceProvenance::Direct, QStringLiteral("phase1-read-only-engine"), evaluationStartedAt,
            evaluationCompletedAt, evaluationDurationUs);
        outcome.session.appendEvidence(evidence);
        result.evidenceIds.append(outcome.session.evidence().back().id);
        outcome.session.appendCheckResult(result);
        outcome.session.plan().setStatus(item.stepId, result.status, result.summary);
        outcome.session.setCurrentOperation({DoctorOperationId(QStringLiteral("OP-") + item.check.id.value()),
            DoctorOperationState::Completed, item.check.title, 100, item.check.timeoutMs});
        if (!result.evidenceIds.isEmpty() && item.check.phase == DoctorPhase::ProtocolApiHealth) {
            appendActivity(DoctorActivityEventType::ProbeCompleted, result.checkId, item.check.phase, result.status,
                QStringLiteral("Native protocol observation complete"), outcome.session.evidence().back().observedState,
                result.evidenceIds, {}, outcome.session.evidence().back().targetDisplayName, outcome.session.evidence().back().statusReason);
        }
        appendActivity(DoctorActivityEventType::EvidenceRecorded, result.checkId, item.check.phase, result.status,
            QStringLiteral("Forensic evidence recorded"), outcome.session.evidence().back().humanSummary, result.evidenceIds);
        appendActivity(DoctorActivityEventType::CheckCompleted, result.checkId, item.check.phase, result.status,
            item.check.title, result.summary, result.evidenceIds, outcome.session.evidence().back().statusReason);
        if (onProgress) onProgress(outcome.session);
    }
    outcome.cancelled = cancelled && cancelled->load();
    QString planningReason;
    outcome.session.transitionTo(outcome.cancelled ? DoctorSessionState::Cancelled : DoctorSessionState::Analyzing);
    if (!outcome.cancelled) {
        const DoctorKnowledgeEngine knowledge;
        const KnowledgeAnalysis analysis = knowledge.analyze(outcome.session, outcome.snapshot);
        outcome.knowledgeEngineVersion = analysis.engineVersion;
        outcome.findingRuleCount = analysis.findingRuleCount;
        outcome.diagnosisRuleCount = analysis.diagnosisRuleCount;
        for (const DiagnosticPlanItem &item : outcome.session.plan().items()) {
            if (!isPhaseTwoKnowledgeCheck(item.check.id)) continue;
            outcome.session.plan().setStatus(item.stepId, DoctorCheckStatus::Running,
                QStringLiteral("Evaluating deterministic Phase 2 knowledge rules."));
            const QDateTime evaluationStartedAt = QDateTime::currentDateTimeUtc();
            QElapsedTimer evaluationTimer;
            evaluationTimer.start();
            appendActivity(DoctorActivityEventType::CheckStarted, item.check.id, item.check.phase, DoctorCheckStatus::Running,
                item.check.title, QStringLiteral("Evaluating deterministic retained-evidence correlations."));
            DoctorCheckResult result = analysis.catalogResults.value(item.check.id.value());
            if (!result.checkId.isValid()) {
                result.checkId = item.check.id;
                result.status = DoctorCheckStatus::NotApplicable;
                result.summary = QStringLiteral("No specialized correlation rule was applicable to this evidence set.");
                result.technicalDetails = QStringLiteral("%1 deterministic knowledge engine.").arg(analysis.engineVersion);
            }
            const QDateTime evaluationCompletedAt = QDateTime::currentDateTimeUtc();
            const qint64 evaluationDurationUs = std::max<qint64>(1, evaluationTimer.nsecsElapsed() / 1000);
            if (result.durationMs <= 0) result.durationMs = std::max<qint64>(1, (evaluationDurationUs + 999) / 1000);
            EvidenceRecord evidence = forensicEvidence(item.check, result, outcome.snapshot, EvidenceKind::DerivedCorrelation,
                EvidenceProvenance::Derived, QStringLiteral("phase2-deterministic-knowledge-engine"), evaluationStartedAt,
                evaluationCompletedAt, evaluationDurationUs);
            outcome.session.appendEvidence(evidence);
            result.evidenceIds.append(outcome.session.evidence().back().id);
            outcome.session.appendCheckResult(result);
            outcome.session.plan().setStatus(item.stepId, result.status, result.summary);
            appendActivity(DoctorActivityEventType::EvidenceRecorded, result.checkId, item.check.phase, result.status,
                QStringLiteral("Derived evidence recorded"), outcome.session.evidence().back().humanSummary, result.evidenceIds);
            appendActivity(DoctorActivityEventType::CheckCompleted, result.checkId, item.check.phase, result.status,
                item.check.title, result.summary, result.evidenceIds, outcome.session.evidence().back().statusReason);
        }
        outcome.session.annotateEvidenceRelationships();
        for (const Finding &finding : outcome.session.findings()) {
            appendActivity(DoctorActivityEventType::FindingCreated, {}, DoctorPhase::ConsistencyAnalysis, finding.status,
                finding.title, finding.explanation, finding.evidenceIds, finding.technicalExplanation, finding.affectedObject);
        }
        for (const Diagnosis &diagnosis : outcome.session.diagnoses()) {
            appendActivity(DoctorActivityEventType::DiagnosisCreated, {}, DoctorPhase::Diagnosis,
                diagnosis.confidence == DiagnosisConfidence::Uncertain ? DoctorCheckStatus::Inconclusive : DoctorCheckStatus::Warning,
                diagnosis.title, diagnosis.humanExplanation, diagnosis.supportingEvidence,
                diagnosis.technicalExplanation, diagnosis.problemFamily, displayName(diagnosis.confidence));
        }
        outcome.session.transitionTo(DoctorSessionState::DiagnosisComplete);
        // Planning is a read-only continuation of diagnosis.  Normal mode
        // intentionally produces a reviewable Lab-qualified plan only; it
        // cannot authorize or invoke a repair helper.
        const RepairPlanProposal proposal = RepairPlanner().propose(outcome.session, outcome.snapshot, false, explicitApprovedUpgradeRequest);
        planningReason = proposal.reason;
        outcome.repairPlanningStatus = displayName(proposal.status);
        outcome.repairPlanningReason = proposal.reason;
        if (!proposal.plan.operations.isEmpty()) {
            outcome.session.setRepairPlan(proposal.plan);
            appendActivity(DoctorActivityEventType::RepairPlanCreated, DoctorCheckId(QStringLiteral("HD-KB-005")),
                DoctorPhase::RepairRecommendation, DoctorCheckStatus::Informational, QStringLiteral("Read-only repair plan generated"),
                proposal.reason, {}, QStringLiteral("The plan remains LabQualified and cannot execute in normal mode."));
        }
    }
    const bool hasDiagnoses = !outcome.session.diagnoses().isEmpty();
    const bool hasPlan = outcome.session.repairPlan().has_value();
    QString userActionTitle;
    QString userActionDetail;
    if (outcome.cancelled) {
        userActionTitle = QStringLiteral("Scan cancelled safely");
        userActionDetail = QStringLiteral("Collected read-only evidence is retained; no Windows or HidHide state changed.");
    } else if (hasPlan) {
        userActionTitle = QStringLiteral("Repair plan ready for review");
        userActionDetail = QStringLiteral("A precise plan is available for review. It is LabQualified, so normal production mode will not execute it.");
    } else if (hasDiagnoses) {
        userActionTitle = QStringLiteral("Diagnosis complete — no qualified deep repair");
        userActionDetail = planningReason.isEmpty()
            ? QStringLiteral("No qualified component, package, upgrade, or recovery plan is available. Nothing changed.")
            : planningReason + QStringLiteral(" Nothing changed.");
    } else {
        userActionTitle = QStringLiteral("Nothing required");
        userActionDetail = QStringLiteral("No material HidHide issue was diagnosed. The scan remained read only.");
    }
    outcome.session.setUserAction({hasPlan ? UserActionState::Required
                                      : hasDiagnoses ? UserActionState::Optional
                                                     : UserActionState::NothingRequired,
        hasDiagnoses ? FindingSeverity::Warning : FindingSeverity::Informational,
        userActionTitle, userActionDetail, {}, {}, {}});
    appendActivity(hasPlan ? DoctorActivityEventType::UserActionRequired : DoctorActivityEventType::UserActionCompleted,
        {}, DoctorPhase::RepairRecommendation, hasPlan ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational,
        userActionTitle, userActionDetail);
    outcome.completedAt = QDateTime::currentDateTimeUtc();
    outcome.durationMs = timer.elapsed();
    appendActivity(DoctorActivityEventType::SessionCompleted, {}, DoctorPhase::RepairRecommendation,
        outcome.cancelled ? DoctorCheckStatus::Cancelled : DoctorCheckStatus::Healthy,
        outcome.cancelled ? QStringLiteral("Read-only diagnostic session cancelled") : QStringLiteral("Read-only diagnostic session completed"),
        outcome.cancelled ? QStringLiteral("Collected evidence is retained; no Windows or HidHide state changed.")
            : QStringLiteral("The diagnostic session completed without mutating Windows or HidHide state."));
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
    // Schema 6 adds canonical, field-level forensic records. Schema 5
    // readers can continue using their existing summary/check fields; readers
    // that understand 6 consume evidenceRecords.fields and relationships.
    root.insert(QStringLiteral("schemaVersion"), 6);
    root.insert(QStringLiteral("schemaCompatibility"), QJsonObject{{QStringLiteral("minimumReaderVersion"), 5},
        {QStringLiteral("forensicEvidenceIntroducedIn"), 6},
        {QStringLiteral("backwardReading"), QStringLiteral("Schema 5 readers may ignore additive forensic fields.")}});
    root.insert(QStringLiteral("sessionId"), outcome.session.id().value());
    root.insert(QStringLiteral("sessionLabel"), outcome.session.sessionLabel());
    root.insert(QStringLiteral("startedAt"), outcome.startedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("completedAt"), outcome.completedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("scanDurationMs"), outcome.durationMs);
    root.insert(QStringLiteral("cancelled"), outcome.cancelled);
    root.insert(QStringLiteral("knowledgeEngine"), QJsonObject{{QStringLiteral("version"), outcome.knowledgeEngineVersion},
        {QStringLiteral("findingRuleCount"), outcome.findingRuleCount}, {QStringLiteral("diagnosisRuleCount"), outcome.diagnosisRuleCount}});
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
        {QStringLiteral("processArchitecture"), displayName(outcome.snapshot.environment.platform.processArchitecture)},
        {QStringLiteral("doctorBinaryArchitecture"), displayName(outcome.snapshot.environment.platform.doctorBinaryArchitecture)},
        {QStringLiteral("helperBinaryArchitecture"), displayName(outcome.snapshot.environment.platform.helperBinaryArchitecture)},
        {QStringLiteral("wow64OrEmulated"), outcome.snapshot.environment.platform.wow64OrEmulated}});
    root.insert(QStringLiteral("executionCompatibility"), QJsonObject{
        {QStringLiteral("directProtocolAvailable"), outcome.snapshot.environment.capabilities.directProtocolAvailable},
        {QStringLiteral("helperArchitectureCompatible"), outcome.snapshot.environment.capabilities.helperArchitectureCompatible},
        {QStringLiteral("highestQualifiedRepairTier"), displayName(outcome.snapshot.environment.capabilities.highestQualifiedRepairTier)}});
    root.insert(QStringLiteral("hidhideComponent"), QJsonObject{
        {QStringLiteral("present"), outcome.snapshot.environment.hidhide.present},
        {QStringLiteral("provider"), outcome.snapshot.environment.hidhide.provider},
        {QStringLiteral("clientVersion"), outcome.snapshot.environment.hidhide.clientVersion},
        {QStringLiteral("driverVersion"), outcome.snapshot.environment.hidhide.driverVersion},
        {QStringLiteral("packageVersion"), outcome.snapshot.environment.hidhide.packageVersion},
        {QStringLiteral("packageArchitecture"), displayName(outcome.snapshot.environment.hidhide.packageArchitecture)}});
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
        {QStringLiteral("endpoint"), redact(probe.endpoint, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("access"), probe.access}, {QStringLiteral("api"), probe.api},
        {QStringLiteral("ioctl"), QStringLiteral("0x%1").arg(probe.ioctlCode, 8, 16, QLatin1Char('0')).toUpper()},
        {QStringLiteral("requestBytes"), probe.requestBytes}, {QStringLiteral("responseBytes"), probe.responseBytes},
        {QStringLiteral("attemptCount"), probe.attemptCount}, {QStringLiteral("timeoutMs"), probe.timeoutMs},
        {QStringLiteral("startedAt"), probe.startedAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("completedAt"), probe.completedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("monotonicDurationUs"), probe.monotonicDurationUs},
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
    const auto ids = [](const auto &values) {
        QJsonArray result;
        for (const auto &value : values) result.append(value.value());
        return result;
    };
    const auto evidenceJson = [&](const EvidenceRecord &record) {
        QJsonArray fields;
        for (const EvidenceField &field : record.fields) fields.append(QJsonObject{
            {QStringLiteral("group"), displayName(field.category)}, {QStringLiteral("label"), field.label},
            {QStringLiteral("value"), redact(field.value, field.sensitivity, redactSensitive)},
            {QStringLiteral("sensitivity"), static_cast<int>(field.sensitivity)}, {QStringLiteral("monospace"), field.monospace}});
        QJsonArray attempts;
        for (const EvidenceAttempt &attempt : record.attempts) attempts.append(QJsonObject{
            {QStringLiteral("ordinal"), attempt.ordinal}, {QStringLiteral("operation"), attempt.operation},
            {QStringLiteral("target"), redact(attempt.target, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
            {QStringLiteral("outcome"), attempt.outcome}, {QStringLiteral("startedAt"), attempt.startedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("completedAt"), attempt.completedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("monotonicDurationUs"), attempt.monotonicDurationUs}, {QStringLiteral("timeoutMs"), attempt.timeoutMs},
            {QStringLiteral("requestBytes"), attempt.requestBytes}, {QStringLiteral("responseBytes"), attempt.responseBytes},
            {QStringLiteral("error"), nativeErrorJson(attempt.nativeError)}});
        return QJsonObject{{QStringLiteral("evidenceId"), record.id.value()}, {QStringLiteral("checkId"), record.checkId.value()},
            {QStringLiteral("kind"), static_cast<int>(record.kind)}, {QStringLiteral("provenance"), static_cast<int>(record.provenance)},
            {QStringLiteral("source"), record.source}, {QStringLiteral("sourceDisplayName"), record.sourceDisplayName},
            {QStringLiteral("provider"), record.provider}, {QStringLiteral("subsystem"), record.subsystem},
            {QStringLiteral("operation"), record.operation}, {QStringLiteral("method"), record.method},
            {QStringLiteral("target"), QJsonObject{{QStringLiteral("type"), record.targetType},
                {QStringLiteral("identity"), redact(record.targetIdentity, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
                {QStringLiteral("displayName"), redact(record.targetDisplayName, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)}}},
            {QStringLiteral("expectedState"), redact(record.expectedState, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
            {QStringLiteral("observedState"), redact(record.observedState, record.sensitivity, redactSensitive)},
            {QStringLiteral("statusReason"), redact(record.statusReason, record.sensitivity, redactSensitive)},
            {QStringLiteral("recordedAt"), record.recordedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("startedAt"), record.startedAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("completedAt"), record.completedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("monotonicDurationUs"), record.monotonicDurationUs}, {QStringLiteral("timeoutMs"), record.timeoutMs},
            {QStringLiteral("summary"), redact(record.humanSummary, record.sensitivity, redactSensitive)},
            {QStringLiteral("technicalDetails"), redact(record.technicalDetails, record.sensitivity, redactSensitive)},
            {QStringLiteral("structuredValue"), redact(record.structuredValue, record.sensitivity, redactSensitive)},
            {QStringLiteral("durationMs"), static_cast<qint64>(record.durationMs)}, {QStringLiteral("direct"), record.direct},
            {QStringLiteral("fields"), fields}, {QStringLiteral("attempts"), attempts},
            {QStringLiteral("relationships"), QJsonObject{{QStringLiteral("evidenceIds"), ids(record.relatedEvidenceIds)},
                {QStringLiteral("checkIds"), ids(record.relatedCheckIds)}, {QStringLiteral("findingIds"), ids(record.relatedFindingIds)},
                {QStringLiteral("diagnosisIds"), ids(record.relatedDiagnosisIds)}}},
            {QStringLiteral("collection"), QJsonObject{{QStringLiteral("truncated"), record.collectionTruncated},
                {QStringLiteral("originalFieldCount"), record.originalFieldCount}, {QStringLiteral("truncationReason"), record.truncationReason}}},
            {QStringLiteral("error"), nativeErrorJson(record.nativeError)}};
    };
    QJsonArray evidence;
    for (const EvidenceRecord &record : outcome.session.evidence()) evidence.append(evidenceJson(record));
    root.insert(QStringLiteral("evidenceRecords"), evidence);
    QJsonArray findings;
    for (const Finding &finding : outcome.session.findings()) {
        QStringList evidenceIds;
        QStringList relatedIds;
        for (const EvidenceId &id : finding.evidenceIds) evidenceIds.append(id.value());
        for (const FindingId &id : finding.relatedFindings) relatedIds.append(id.value());
        findings.append(QJsonObject{{QStringLiteral("findingId"), finding.id.value()},
            {QStringLiteral("severity"), displayName(finding.severity)}, {QStringLiteral("title"), finding.title},
            {QStringLiteral("humanExplanation"), finding.explanation},
            {QStringLiteral("technicalExplanation"), redact(finding.technicalExplanation, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
            {QStringLiteral("affectedObject"), redact(finding.affectedObject, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
            {QStringLiteral("environmentalScope"), finding.environmentalScope},
            {QStringLiteral("confidence"), displayName(finding.confidence)}, {QStringLiteral("status"), displayName(finding.status)},
            {QStringLiteral("repairability"), displayName(finding.repairability)},
            {QStringLiteral("evidenceIds"), QJsonArray::fromStringList(evidenceIds)},
            {QStringLiteral("relatedFindingIds"), QJsonArray::fromStringList(relatedIds)},
            {QStringLiteral("observedAt"), finding.observedAt.toString(Qt::ISODateWithMs)}});
    }
    root.insert(QStringLiteral("findings"), findings);
    QJsonArray diagnoses;
    for (const Diagnosis &diagnosis : outcome.session.diagnoses()) {
        QStringList supportingIds;
        QStringList contradictingIds;
        for (const EvidenceId &id : diagnosis.supportingEvidence) supportingIds.append(id.value());
        for (const EvidenceId &id : diagnosis.contradictingEvidence) contradictingIds.append(id.value());
        diagnoses.append(QJsonObject{{QStringLiteral("diagnosisId"), diagnosis.id.value()},
            {QStringLiteral("signatureId"), diagnosis.signatureId.value()}, {QStringLiteral("knowledgeVersion"), diagnosis.knowledgeVersion},
            {QStringLiteral("role"), displayName(diagnosis.role)}, {QStringLiteral("problemFamily"), diagnosis.problemFamily},
            {QStringLiteral("severity"), displayName(diagnosis.severity)}, {QStringLiteral("title"), diagnosis.title},
            {QStringLiteral("humanExplanation"), diagnosis.humanExplanation},
            {QStringLiteral("technicalExplanation"), redact(diagnosis.technicalExplanation, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
            {QStringLiteral("userImpact"), diagnosis.userImpact}, {QStringLiteral("usualResolution"), diagnosis.usualResolution},
            {QStringLiteral("repairability"), displayName(diagnosis.repairability)},
            {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("band"), displayName(diagnosis.confidence)},
                {QStringLiteral("score"), diagnosis.confidenceExplanation.score}, {QStringLiteral("reason"), diagnosis.confidenceExplanation.bandReason},
                {QStringLiteral("requiredEvidence"), QJsonArray::fromStringList(diagnosis.confidenceExplanation.requiredEvidence)},
                {QStringLiteral("supportingEvidence"), QJsonArray::fromStringList(diagnosis.confidenceExplanation.supportingEvidence)},
                {QStringLiteral("contradictingEvidence"), QJsonArray::fromStringList(diagnosis.confidenceExplanation.contradictingEvidence)},
                {QStringLiteral("missingExpectedEvidence"), QJsonArray::fromStringList(diagnosis.confidenceExplanation.missingExpectedEvidence)}}},
            {QStringLiteral("supportingEvidenceIds"), QJsonArray::fromStringList(supportingIds)},
            {QStringLiteral("contradictingEvidenceIds"), QJsonArray::fromStringList(contradictingIds)},
            {QStringLiteral("candidateRepairIds"), QJsonArray::fromStringList(diagnosis.candidateRepairIds)},
            {QStringLiteral("provenance"), diagnosis.provenance}});
    }
    root.insert(QStringLiteral("diagnoses"), diagnoses);
    const UserAction &userAction = outcome.session.userAction();
    root.insert(QStringLiteral("userAction"), QJsonObject{{QStringLiteral("state"), static_cast<int>(userAction.state)},
        {QStringLiteral("severity"), static_cast<int>(userAction.severity)}, {QStringLiteral("title"), userAction.title},
        {QStringLiteral("detail"), userAction.explanation},
        {QStringLiteral("instructions"), QJsonArray::fromStringList(userAction.instructions)},
        {QStringLiteral("availableActions"), QJsonArray::fromStringList(userAction.availableActions)}});
    root.insert(QStringLiteral("repairProposal"), QJsonObject{{QStringLiteral("status"), outcome.repairPlanningStatus},
        {QStringLiteral("reason"), outcome.repairPlanningReason}});
    if (outcome.session.repairPlan()) {
        const RepairPlan &plan = *outcome.session.repairPlan();
        QJsonArray operations;
        for (const RepairOperation &operation : plan.operations) operations.append(QJsonObject{
            {QStringLiteral("operationId"), operation.id.value()}, {QStringLiteral("kind"), static_cast<int>(operation.kind)},
            {QStringLiteral("targetKind"), static_cast<int>(operation.targetKind)},
            {QStringLiteral("target"), redact(operation.targetIdentity, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
            {QStringLiteral("requestedValue"), redact(operation.requestedValue, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)}});
        root.insert(QStringLiteral("repairPlan"), QJsonObject{{QStringLiteral("planId"), plan.id.value()},
            {QStringLiteral("recipeId"), plan.recipeId.value()}, {QStringLiteral("recipeVersion"), plan.recipeVersion},
            {QStringLiteral("title"), plan.title}, {QStringLiteral("description"), plan.description},
            {QStringLiteral("riskClass"), static_cast<int>(plan.riskClass)}, {QStringLiteral("qualification"), static_cast<int>(plan.qualification)},
            {QStringLiteral("preconditionFingerprint"), plan.preconditionFingerprint},
            {QStringLiteral("expectedPreState"), redact(plan.expectedPreState, EvidenceSensitivity::SensitiveLocalOnly, redactSensitive)},
            {QStringLiteral("expectedPostState"), redact(plan.expectedPostState, EvidenceSensitivity::SensitiveLocalOnly, redactSensitive)},
            {QStringLiteral("expectedPostFingerprint"), plan.expectedPostFingerprint},
            {QStringLiteral("elevationRequired"), plan.elevationRequired}, {QStringLiteral("restartRequired"), plan.restartRequired},
            {QStringLiteral("estimatedSeconds"), plan.estimatedSeconds}, {QStringLiteral("authorization"), static_cast<int>(plan.authorization)},
            {QStringLiteral("maximumReboots"), plan.maximumReboots}, {QStringLiteral("deepRepair"), plan.deepRepair},
            {QStringLiteral("operations"), operations}, {QStringLiteral("collateralPreserved"), QJsonArray::fromStringList(plan.unchangedCollateral)}});
    } else {
        root.insert(QStringLiteral("repairPlan"), QJsonValue::Null);
    }
    QJsonArray packageCatalog;
    for (const ApprovedPackage &package : ApprovedPackageCatalog::packages()) {
        packageCatalog.append(QJsonObject{{QStringLiteral("packageId"), package.packageId},
            {QStringLiteral("provider"), package.provider}, {QStringLiteral("version"), package.version},
            {QStringLiteral("architecture"), displayName(package.architecture)}, {QStringLiteral("channel"), package.channel},
            {QStringLiteral("source"), package.source}, {QStringLiteral("sourceKind"), displayName(package.sourceKind)}, {QStringLiteral("expectedSha256"), package.expectedSha256},
            {QStringLiteral("signaturePolicy"), displayName(package.signaturePolicy)}, {QStringLiteral("signerIdentity"), package.signerIdentity},
            {QStringLiteral("minimumWindowsBuild"), static_cast<int>(package.minimumWindowsBuild)},
            {QStringLiteral("maximumWindowsBuild"), static_cast<int>(package.maximumWindowsBuild)},
            {QStringLiteral("maximumReboots"), package.expectedMaximumReboots},
            {QStringLiteral("qualification"), static_cast<int>(package.qualification)}, {QStringLiteral("provenance"), package.provenance}});
    }
    root.insert(QStringLiteral("approvedPackageCatalog"), packageCatalog);
    QJsonArray activity;
    for (const DoctorActivityEvent &event : outcome.session.activity()) activity.append(QJsonObject{
        {QStringLiteral("timestamp"), event.timestamp.toString(Qt::ISODateWithMs)}, {QStringLiteral("checkId"), event.checkId.value()},
        {QStringLiteral("eventType"), displayName(event.type)}, {QStringLiteral("phase"), displayName(event.phase)},
        {QStringLiteral("status"), displayName(event.status)}, {QStringLiteral("title"), event.title},
        {QStringLiteral("detail"), redact(event.detail, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
        {QStringLiteral("reason"), redact(event.reason, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
        {QStringLiteral("target"), redact(event.target, EvidenceSensitivity::PotentiallyIdentifying, redactSensitive)},
        {QStringLiteral("result"), redact(event.result, EvidenceSensitivity::RequiresRedaction, redactSensitive)},
        {QStringLiteral("nextStep"), event.nextStep}, {QStringLiteral("startedAt"), event.startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), event.completedAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("monotonicDurationUs"), event.monotonicDurationUs},
        {QStringLiteral("evidenceId"), event.evidenceId.value()}, {QStringLiteral("evidenceIds"), ids(event.evidenceIds)},
        {QStringLiteral("findingIds"), ids(event.relatedFindingIds)}, {QStringLiteral("diagnosisIds"), ids(event.relatedDiagnosisIds)}});
    root.insert(QStringLiteral("activityTimeline"), activity);
    root.insert(QStringLiteral("activityCollection"), QJsonObject{{QStringLiteral("retainedEvents"), outcome.session.activity().size()},
        {QStringLiteral("droppedEvents"), outcome.session.activityEventsDropped()}, {QStringLiteral("bounded"), true}});
    root.insert(QStringLiteral("contradictions"), QJsonArray::fromStringList(outcome.snapshot.contradictions));
    root.insert(QStringLiteral("operationalLimits"), QJsonArray::fromStringList(outcome.snapshot.operationalLimits));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace hotas::doctor
