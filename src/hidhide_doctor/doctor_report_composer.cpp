#include "doctor_report_composer.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace hotas::doctor {
namespace {

// Version 2 is additive over the original export: forensic field groups,
// bounded attempts, and relationship backlinks are now emitted from the same
// canonical EvidenceRecord used by the Inspector.
constexpr int kReportSchemaVersion = 2;
constexpr qsizetype kMaximumReportBytes = 8 * 1024 * 1024;

QString reportText(QString value)
{
    return value.remove(QChar::Null);
}

QString actionStateName(UserActionLedgerState state)
{
    switch (state) {
    case UserActionLedgerState::NotRequired: return QStringLiteral("Not Required");
    case UserActionLedgerState::Required: return QStringLiteral("Required");
    case UserActionLedgerState::Waiting: return QStringLiteral("Waiting");
    case UserActionLedgerState::Completed: return QStringLiteral("Completed");
    case UserActionLedgerState::Cancelled: return QStringLiteral("Cancelled");
    case UserActionLedgerState::Superseded: return QStringLiteral("Superseded");
    case UserActionLedgerState::StillPending: return QStringLiteral("Still Pending");
    }
    return QStringLiteral("Unknown");
}

QString sessionStateName(DoctorSessionState state)
{
    switch (state) {
    case DoctorSessionState::Preparing: return QStringLiteral("Preparing");
    case DoctorSessionState::Diagnosing: return QStringLiteral("Diagnosing");
    case DoctorSessionState::Analyzing: return QStringLiteral("Analyzing evidence");
    case DoctorSessionState::DiagnosisComplete: return QStringLiteral("Diagnosis complete");
    case DoctorSessionState::AwaitingUser: return QStringLiteral("Awaiting user");
    case DoctorSessionState::RepairReady: return QStringLiteral("Repair ready");
    case DoctorSessionState::Repairing: return QStringLiteral("Repairing");
    case DoctorSessionState::AwaitingElevation: return QStringLiteral("Awaiting elevation");
    case DoctorSessionState::AwaitingReboot: return QStringLiteral("Awaiting reboot");
    case DoctorSessionState::ResumingAfterReboot: return QStringLiteral("Resuming after reboot");
    case DoctorSessionState::Verifying: return QStringLiteral("Verifying");
    case DoctorSessionState::RepairComplete: return QStringLiteral("Repair complete");
    case DoctorSessionState::DegradedComplete: return QStringLiteral("Degraded complete");
    case DoctorSessionState::Cancelled: return QStringLiteral("Cancelled safely");
    case DoctorSessionState::FailedSafely: return QStringLiteral("Failed safely");
    }
    return QStringLiteral("Unknown");
}

QString safeText(const QString &value, EvidenceSensitivity sensitivity, DoctorReportPrivacy privacy,
                 QStringList *redacted)
{
    if (privacy == DoctorReportPrivacy::SafeToShare && sensitivity != EvidenceSensitivity::SafeToExport) {
        if (redacted) redacted->append(QStringLiteral("%1 evidence text").arg(
            sensitivity == EvidenceSensitivity::PotentiallyIdentifying ? QStringLiteral("Potentially identifying") : QStringLiteral("Sensitive")));
        return QStringLiteral("[REDACTED — local diagnostic detail]");
    }
    return reportText(value);
}

bool includes(const QString &scope, const QString &section)
{
    const QString normalized = scope.trimmed().toCaseFolded();
    return normalized.isEmpty() || normalized == QStringLiteral("entire session") || normalized == QStringLiteral("everything")
        || normalized == section.toCaseFolded()
        || (normalized == QStringLiteral("findings & diagnoses") && (section == QStringLiteral("Findings") || section == QStringLiteral("Diagnoses")))
        || (normalized == QStringLiteral("current / historical steps")
            && (section == QStringLiteral("Check Results") || section == QStringLiteral("Activity Timeline")))
        || (normalized == QStringLiteral("selected evidence") && section == QStringLiteral("Evidence"));
}

QJsonArray identifiers(const QList<EvidenceId> &ids)
{
    QJsonArray values;
    for (const EvidenceId &id : ids) values.append(id.value());
    return values;
}

QJsonObject errorJson(const std::optional<NativeError> &error)
{
    if (!error) return {};
    return {{QStringLiteral("code"), error->code}, {QStringLiteral("symbol"), error->symbolicName},
        {QStringLiteral("message"), error->message}};
}

QJsonArray checkIdsJson(const QList<DoctorCheckId> &ids)
{
    QJsonArray values;
    for (const DoctorCheckId &id : ids) values.append(id.value());
    return values;
}

QJsonArray findingIdsJson(const QList<FindingId> &ids)
{
    QJsonArray values;
    for (const FindingId &id : ids) values.append(id.value());
    return values;
}

QJsonArray diagnosisIdsJson(const QList<DiagnosisId> &ids)
{
    QJsonArray values;
    for (const DiagnosisId &id : ids) values.append(id.value());
    return values;
}

QJsonObject forensicEvidenceJson(const EvidenceRecord &record, DoctorReportPrivacy privacy,
                                 DoctorReportDocument *document)
{
    QJsonArray fields;
    for (const EvidenceField &field : record.fields) fields.append(QJsonObject{
        {QStringLiteral("group"), displayName(field.category)}, {QStringLiteral("label"), field.label},
        {QStringLiteral("value"), safeText(field.value, field.sensitivity, privacy, &document->redacted)},
        {QStringLiteral("sensitivity"), static_cast<int>(field.sensitivity)}, {QStringLiteral("monospace"), field.monospace}});
    QJsonArray attempts;
    for (const EvidenceAttempt &attempt : record.attempts) attempts.append(QJsonObject{
        {QStringLiteral("ordinal"), attempt.ordinal}, {QStringLiteral("operation"), attempt.operation},
        {QStringLiteral("target"), safeText(attempt.target, EvidenceSensitivity::PotentiallyIdentifying, privacy, &document->redacted)},
        {QStringLiteral("outcome"), attempt.outcome}, {QStringLiteral("startedAt"), attempt.startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), attempt.completedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("monotonicDurationUs"), attempt.monotonicDurationUs}, {QStringLiteral("timeoutMs"), attempt.timeoutMs},
        {QStringLiteral("requestBytes"), attempt.requestBytes}, {QStringLiteral("responseBytes"), attempt.responseBytes},
        {QStringLiteral("nativeError"), errorJson(attempt.nativeError)}});
    return QJsonObject{{QStringLiteral("id"), record.id.value()}, {QStringLiteral("checkId"), record.checkId.value()},
        {QStringLiteral("source"), record.source}, {QStringLiteral("sourceDisplayName"), record.sourceDisplayName},
        {QStringLiteral("provider"), record.provider}, {QStringLiteral("subsystem"), record.subsystem},
        {QStringLiteral("operation"), record.operation}, {QStringLiteral("method"), record.method},
        {QStringLiteral("target"), QJsonObject{{QStringLiteral("type"), record.targetType},
            {QStringLiteral("identity"), safeText(record.targetIdentity, EvidenceSensitivity::PotentiallyIdentifying, privacy, &document->redacted)},
            {QStringLiteral("displayName"), safeText(record.targetDisplayName, EvidenceSensitivity::PotentiallyIdentifying, privacy, &document->redacted)}}},
        {QStringLiteral("expectedState"), safeText(record.expectedState, EvidenceSensitivity::RequiresRedaction, privacy, &document->redacted)},
        {QStringLiteral("observedState"), safeText(record.observedState, record.sensitivity, privacy, &document->redacted)},
        {QStringLiteral("statusReason"), safeText(record.statusReason, record.sensitivity, privacy, &document->redacted)},
        {QStringLiteral("summary"), safeText(record.humanSummary, record.sensitivity, privacy, &document->redacted)},
        {QStringLiteral("technicalDetails"), safeText(record.technicalDetails, record.sensitivity, privacy, &document->redacted)},
        {QStringLiteral("structuredValue"), safeText(record.structuredValue, record.sensitivity, privacy, &document->redacted)},
        {QStringLiteral("recordedAt"), record.recordedAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("startedAt"), record.startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), record.completedAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("durationMs"), record.durationMs},
        {QStringLiteral("monotonicDurationUs"), record.monotonicDurationUs}, {QStringLiteral("timeoutMs"), record.timeoutMs},
        {QStringLiteral("direct"), record.direct}, {QStringLiteral("fields"), fields}, {QStringLiteral("attempts"), attempts},
        {QStringLiteral("relationships"), QJsonObject{{QStringLiteral("evidenceIds"), identifiers(record.relatedEvidenceIds)},
            {QStringLiteral("checkIds"), checkIdsJson(record.relatedCheckIds)}, {QStringLiteral("findingIds"), findingIdsJson(record.relatedFindingIds)},
            {QStringLiteral("diagnosisIds"), diagnosisIdsJson(record.relatedDiagnosisIds)}}},
        {QStringLiteral("collection"), QJsonObject{{QStringLiteral("truncated"), record.collectionTruncated},
            {QStringLiteral("originalFieldCount"), record.originalFieldCount}, {QStringLiteral("truncationReason"), record.truncationReason}}},
        {QStringLiteral("nativeError"), errorJson(record.nativeError)}};
}

QJsonObject environmentJson(const DoctorSession &session)
{
    if (!session.environment()) return {{QStringLiteral("status"), QStringLiteral("Unknown")}};
    const DoctorEnvironment &environment = *session.environment();
    return {{QStringLiteral("windowsEdition"), reportText(environment.platform.windowsEdition)},
        {QStringLiteral("windowsVersion"), reportText(environment.platform.windowsVersion)},
        {QStringLiteral("build"), static_cast<qint64>(environment.platform.build)}, {QStringLiteral("revision"), static_cast<qint64>(environment.platform.revision)},
        {QStringLiteral("architecture"), displayName(environment.platform.nativeArchitecture)},
        {QStringLiteral("hidhidePresent"), environment.hidhide.present},
        {QStringLiteral("hidhideClientVersion"), reportText(environment.hidhide.clientVersion)},
        {QStringLiteral("hidhideDriverVersion"), reportText(environment.hidhide.driverVersion)}};
}

void addLine(QStringList *lines, const QString &line)
{
    lines->append(line);
}

bool atomicWrite(const QString &path, const QByteArray &payload, QString *error)
{
    if (payload.size() > kMaximumReportBytes) {
        if (error) *error = QStringLiteral("Report exceeds the bounded 8 MiB export limit.");
        return false;
    }
    const QFileInfo destination(path);
    if (!destination.dir().exists()) {
        if (error) *error = QStringLiteral("The selected export folder does not exist: %1")
            .arg(QDir::toNativeSeparators(destination.dir().absolutePath()));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not start writing %1: %2")
            .arg(QDir::toNativeSeparators(destination.absoluteFilePath()), file.errorString());
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (error) *error = QStringLiteral("Could not write all report data to %1: %2")
            .arg(QDir::toNativeSeparators(destination.absoluteFilePath()), file.errorString());
        return false;
    }
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Could not finalize %1: %2")
            .arg(QDir::toNativeSeparators(destination.absoluteFilePath()), file.errorString());
        return false;
    }
    const QFileInfo written(destination.absoluteFilePath());
    if (!written.isFile() || written.size() != payload.size()) {
        if (error) *error = QStringLiteral("Export did not create the expected file: %1")
            .arg(QDir::toNativeSeparators(destination.absoluteFilePath()));
        return false;
    }
    return true;
}

} // namespace

DoctorReportFormat doctorReportFormatFromString(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == QStringLiteral("json")) return DoctorReportFormat::Json;
    if (normalized == QStringLiteral("plain text") || normalized == QStringLiteral("text")) return DoctorReportFormat::PlainText;
    if (normalized == QStringLiteral("diagnostic bundle") || normalized == QStringLiteral("bundle")) return DoctorReportFormat::DiagnosticBundle;
    return DoctorReportFormat::Markdown;
}

DoctorReportDetail doctorReportDetailFromString(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == QStringLiteral("summary")) return DoctorReportDetail::Summary;
    if (normalized == QStringLiteral("forensic / everything") || normalized == QStringLiteral("forensic") || normalized == QStringLiteral("everything")) return DoctorReportDetail::Forensic;
    return DoctorReportDetail::Detailed;
}

DoctorReportPrivacy doctorReportPrivacyFromString(const QString &value)
{
    return value.trimmed().compare(QStringLiteral("Local / Unredacted"), Qt::CaseInsensitive) == 0
        ? DoctorReportPrivacy::LocalUnredacted : DoctorReportPrivacy::SafeToShare;
}

DoctorReportDocument DoctorReportComposer::compose(const DoctorSession &session, const QString &buildIdentity,
                                                    const DoctorReportRequest &request)
{
    DoctorReportDocument document;
    document.included = {QStringLiteral("Session summary"), QStringLiteral("Final state"), QStringLiteral("Build provenance")};
    document.excluded = {QStringLiteral("Unrelated local files"), QStringLiteral("Automatic network upload (not performed)")};
    qint64 engineCheckDurationMs = 0;
    QDateTime lastActivityAt;
    for (const DoctorCheckResult &result : session.checkResults()) engineCheckDurationMs += std::max<qint64>(0, result.durationMs);
    for (const DoctorActivityEvent &event : session.activity()) {
        if (!lastActivityAt.isValid() || event.timestamp > lastActivityAt) lastActivityAt = event.timestamp;
    }
    const qint64 recordedSpanMs = lastActivityAt.isValid()
        ? std::max<qint64>(0, session.createdAt().msecsTo(lastActivityAt)) : 0;
    QJsonObject root{{QStringLiteral("schemaVersion"), kReportSchemaVersion},
        {QStringLiteral("sessionSummary"), QJsonObject{{QStringLiteral("sessionId"), session.id().value()},
            {QStringLiteral("createdAt"), session.createdAt().toString(Qt::ISODateWithMs)},
            {QStringLiteral("state"), sessionStateName(session.state())}, {QStringLiteral("label"), session.sessionLabel()}}},
        {QStringLiteral("buildProvenance"), QJsonObject{{QStringLiteral("doctorBuild"), buildIdentity},
            {QStringLiteral("reportPrivacy"), request.privacy == DoctorReportPrivacy::SafeToShare ? QStringLiteral("Safe to Share") : QStringLiteral("Local / Unredacted")}}},
        {QStringLiteral("timingPerformance"), QJsonObject{{QStringLiteral("engineCheckDurationMs"), engineCheckDurationMs},
            {QStringLiteral("recordedSessionSpanMs"), recordedSpanMs},
            {QStringLiteral("timingSource"), QStringLiteral("Recorded Doctor session events; not a user-perceived latency measurement")}}}};

    QStringList markdown{QStringLiteral("# HIDHIDE DOCTOR REPORT"), QString(),
        QStringLiteral("## SESSION SUMMARY"), QStringLiteral("- Session: `%1`").arg(session.id().value()),
        QStringLiteral("- State: %1").arg(sessionStateName(session.state())),
        QStringLiteral("- Started: %1").arg(session.createdAt().toString(Qt::ISODateWithMs)),
        QStringLiteral("- Doctor build: %1").arg(buildIdentity),
        QStringLiteral("- Privacy: %1").arg(request.privacy == DoctorReportPrivacy::SafeToShare ? QStringLiteral("Safe to Share") : QStringLiteral("Local / Unredacted")),
        QStringLiteral("- Recorded engine check time: %1 ms").arg(engineCheckDurationMs),
        QStringLiteral("- Recorded session span: %1 ms (session events; not user-perceived latency)").arg(recordedSpanMs)};

    if (includes(request.scope, QStringLiteral("Environment"))) {
        root.insert(QStringLiteral("environment"), environmentJson(session));
        document.included.append(QStringLiteral("Environment"));
        const QJsonObject environment = environmentJson(session);
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## ENVIRONMENT"));
        for (auto it = environment.begin(); it != environment.end(); ++it) addLine(&markdown, QStringLiteral("- %1: %2").arg(it.key(), it.value().toVariant().toString()));
    }

    if (includes(request.scope, QStringLiteral("Diagnostic Plan"))) {
        QJsonArray plan;
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## DIAGNOSTIC PLAN"));
        for (const DiagnosticPlanItem &item : session.plan().items()) {
            plan.append(QJsonObject{{QStringLiteral("stepId"), item.stepId.value()}, {QStringLiteral("checkId"), item.check.id.value()},
                {QStringLiteral("phase"), displayName(item.check.phase)}, {QStringLiteral("title"), item.check.title},
                {QStringLiteral("status"), displayName(item.status)}, {QStringLiteral("progressPercent"), item.currentStepProgressPercent},
                {QStringLiteral("weight"), item.check.weight.units}, {QStringLiteral("statusDetail"), item.statusDetail}});
            addLine(&markdown, QStringLiteral("- `%1` %2 — %3 (%4%%)").arg(item.check.id.value(), item.check.title,
                displayName(item.status)).arg(item.currentStepProgressPercent));
        }
        root.insert(QStringLiteral("diagnosticPlan"), plan); document.included.append(QStringLiteral("Diagnostic Plan"));
    }

    if (request.detail != DoctorReportDetail::Summary && includes(request.scope, QStringLiteral("Check Results"))) {
        QJsonArray checks;
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## CHECK RESULTS"));
        for (const DoctorCheckResult &result : session.checkResults()) {
            checks.append(QJsonObject{{QStringLiteral("checkId"), result.checkId.value()}, {QStringLiteral("status"), displayName(result.status)},
                {QStringLiteral("summary"), result.summary}, {QStringLiteral("technicalDetails"), result.technicalDetails},
                {QStringLiteral("durationMs"), result.durationMs}, {QStringLiteral("evidenceIds"), identifiers(result.evidenceIds)},
                {QStringLiteral("nativeError"), errorJson(result.nativeError)}});
            addLine(&markdown, QStringLiteral("- `%1` %2 — %3").arg(result.checkId.value(), displayName(result.status), result.summary));
        }
        root.insert(QStringLiteral("checkResults"), checks); document.included.append(QStringLiteral("Check Results"));
    }

    if (includes(request.scope, QStringLiteral("Findings"))) {
        QJsonArray findings;
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## FINDINGS"));
        for (const Finding &finding : session.findings()) {
            findings.append(QJsonObject{{QStringLiteral("id"), finding.id.value()}, {QStringLiteral("severity"), displayName(finding.severity)},
                {QStringLiteral("title"), finding.title}, {QStringLiteral("explanation"), finding.explanation},
                {QStringLiteral("confidence"), displayName(finding.confidence)}, {QStringLiteral("repairability"), displayName(finding.repairability)}});
            addLine(&markdown, QStringLiteral("- **%1** — %2").arg(finding.title, finding.explanation));
        }
        root.insert(QStringLiteral("findings"), findings); document.included.append(QStringLiteral("Findings"));
    }

    if (includes(request.scope, QStringLiteral("Diagnoses"))) {
        QJsonArray diagnoses;
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## FINDINGS & DIAGNOSES"));
        for (const Diagnosis &diagnosis : session.diagnoses()) {
            diagnoses.append(QJsonObject{{QStringLiteral("id"), diagnosis.id.value()}, {QStringLiteral("role"), displayName(diagnosis.role)},
                {QStringLiteral("title"), diagnosis.title}, {QStringLiteral("confidence"), displayName(diagnosis.confidence)},
                {QStringLiteral("confidenceScore"), diagnosis.confidenceExplanation.score}, {QStringLiteral("explanation"), diagnosis.humanExplanation},
                {QStringLiteral("supportingEvidence"), identifiers(diagnosis.supportingEvidence)},
                {QStringLiteral("contradictingEvidence"), identifiers(diagnosis.contradictingEvidence)},
                {QStringLiteral("repairability"), displayName(diagnosis.repairability)}});
            addLine(&markdown, QStringLiteral("- **%1** (%2, %3%%) — %4").arg(diagnosis.title,
                displayName(diagnosis.confidence)).arg(diagnosis.confidenceExplanation.score).arg(diagnosis.humanExplanation));
        }
        root.insert(QStringLiteral("diagnoses"), diagnoses); document.included.append(QStringLiteral("Diagnoses"));
    }

    if (includes(request.scope, QStringLiteral("User Actions"))) {
        QJsonArray actions;
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## USER ACTION LEDGER"));
        for (const UserActionLedgerEntry &entry : session.userActionHistory()) {
            actions.append(QJsonObject{{QStringLiteral("actionId"), entry.actionId}, {QStringLiteral("title"), entry.title},
                {QStringLiteral("explanation"), entry.explanation}, {QStringLiteral("state"), actionStateName(entry.state)},
                {QStringLiteral("firstRequiredAt"), entry.firstRequiredAt.toString(Qt::ISODateWithMs)},
                {QStringLiteral("completedAt"), entry.completedAt.toString(Qt::ISODateWithMs)},
                {QStringLiteral("cancelledAt"), entry.cancelledAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("reason"), entry.reason},
                {QStringLiteral("associatedCheck"), entry.associatedCheckId}, {QStringLiteral("associatedDiagnosis"), entry.associatedDiagnosisId},
                {QStringLiteral("userResponse"), entry.userResponse}});
            addLine(&markdown, QStringLiteral("- [%1] **%2** — %3").arg(actionStateName(entry.state), entry.title, entry.explanation));
        }
        root.insert(QStringLiteral("userActionLedger"), actions); document.included.append(QStringLiteral("User Actions"));
    }

    QJsonArray activity;
    for (const DoctorActivityEvent &event : session.activity()) activity.append(QJsonObject{{QStringLiteral("timestamp"), event.timestamp.toString(Qt::ISODateWithMs)},
        {QStringLiteral("eventType"), displayName(event.type)}, {QStringLiteral("phase"), displayName(event.phase)},
        {QStringLiteral("checkId"), event.checkId.value()}, {QStringLiteral("status"), displayName(event.status)},
        {QStringLiteral("title"), event.title}, {QStringLiteral("detail"), safeText(event.detail, EvidenceSensitivity::RequiresRedaction, request.privacy, &document.redacted)},
        {QStringLiteral("reason"), safeText(event.reason, EvidenceSensitivity::RequiresRedaction, request.privacy, &document.redacted)},
        {QStringLiteral("target"), safeText(event.target, EvidenceSensitivity::PotentiallyIdentifying, request.privacy, &document.redacted)},
        {QStringLiteral("result"), safeText(event.result, EvidenceSensitivity::RequiresRedaction, request.privacy, &document.redacted)},
        {QStringLiteral("nextStep"), event.nextStep}, {QStringLiteral("startedAt"), event.startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), event.completedAt.toString(Qt::ISODateWithMs)}, {QStringLiteral("monotonicDurationUs"), event.monotonicDurationUs},
        {QStringLiteral("evidenceId"), event.evidenceId.value()}, {QStringLiteral("evidenceIds"), identifiers(event.evidenceIds)},
        {QStringLiteral("findingIds"), findingIdsJson(event.relatedFindingIds)}, {QStringLiteral("diagnosisIds"), diagnosisIdsJson(event.relatedDiagnosisIds)}});
    document.timelineJson = QJsonDocument(activity).toJson(QJsonDocument::Indented);
    if (request.detail != DoctorReportDetail::Summary && includes(request.scope, QStringLiteral("Activity Timeline"))) {
        root.insert(QStringLiteral("activityTimeline"), activity); document.included.append(QStringLiteral("Activity Timeline"));
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## ACTIVITY TIMELINE"));
        for (const DoctorActivityEvent &event : session.activity()) addLine(&markdown, QStringLiteral("- %1 `%2` %3 — %4").arg(
            event.timestamp.toString(Qt::ISODateWithMs), event.checkId.value(), displayName(event.status), event.detail));
    }

    QJsonArray evidence;
    const bool selectedEvidenceScope = request.scope.trimmed().compare(QStringLiteral("Selected Evidence"), Qt::CaseInsensitive) == 0;
    for (const EvidenceRecord &record : session.evidence()) {
        if (selectedEvidenceScope && record.id.value() != request.selectedEvidenceId) continue;
        evidence.append(forensicEvidenceJson(record, request.privacy, &document));
    }
    document.evidenceJson = QJsonDocument(evidence).toJson(QJsonDocument::Indented);
    if (request.detail == DoctorReportDetail::Forensic && includes(request.scope, QStringLiteral("Evidence"))) {
        root.insert(QStringLiteral("evidenceRecords"), evidence); document.included.append(QStringLiteral("Evidence Records"));
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## EVIDENCE RECORDS"));
        for (const QJsonValue &value : evidence) {
            const QJsonObject item = value.toObject();
            addLine(&markdown, QStringLiteral("- `%1` %2 — %3").arg(item.value(QStringLiteral("checkId")).toString(),
                item.value(QStringLiteral("source")).toString(), item.value(QStringLiteral("summary")).toString()));
        }
    }

    if (session.repairPlan() && includes(request.scope, QStringLiteral("Repair History"))) {
        const RepairPlan &plan = *session.repairPlan();
        root.insert(QStringLiteral("repairPlan"), QJsonObject{{QStringLiteral("planId"), plan.id.value()}, {QStringLiteral("title"), plan.title},
            {QStringLiteral("description"), plan.description}, {QStringLiteral("qualification"), static_cast<int>(plan.qualification)},
            {QStringLiteral("riskClass"), static_cast<int>(plan.riskClass)}, {QStringLiteral("elevationRequired"), plan.elevationRequired},
            {QStringLiteral("restartRequired"), plan.restartRequired}, {QStringLiteral("operationCount"), plan.operations.size()}});
        document.included.append(QStringLiteral("Repair Plan"));
        addLine(&markdown, QString()); addLine(&markdown, QStringLiteral("## REPAIR PLAN"));
        addLine(&markdown, QStringLiteral("- %1").arg(plan.title));
        addLine(&markdown, QStringLiteral("- Qualification: %1").arg(plan.qualification == RepairQualificationLevel::LabQualified ? QStringLiteral("LabQualified — not production authorized") : QStringLiteral("Not field qualified")));
    }

    root.insert(QStringLiteral("finalState"), QJsonObject{{QStringLiteral("state"), sessionStateName(session.state())},
        {QStringLiteral("currentUserAction"), session.userAction().title}, {QStringLiteral("repairPlanPresent"), session.repairPlan().has_value()}});
    root.insert(QStringLiteral("activityCollection"), QJsonObject{{QStringLiteral("retainedEvents"), session.activity().size()},
        {QStringLiteral("droppedEvents"), session.activityEventsDropped()}, {QStringLiteral("bounded"), true}});
    root.insert(QStringLiteral("schemaCompatibility"), QJsonObject{{QStringLiteral("minimumReaderVersion"), 1},
        {QStringLiteral("forensicEvidenceIntroducedIn"), 2},
        {QStringLiteral("backwardReading"), QStringLiteral("Version 1 readers may ignore additive forensic fields.")}});
    root.insert(QStringLiteral("redactionManifest"), QJsonObject{{QStringLiteral("included"), QJsonArray::fromStringList(document.included)},
        {QStringLiteral("redacted"), QJsonArray::fromStringList(document.redacted)}, {QStringLiteral("excluded"), QJsonArray::fromStringList(document.excluded)}});
    document.json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    document.markdown = markdown.join(QLatin1Char('\n')).toUtf8();
    QStringList plain = markdown;
    for (QString &line : plain) { line.remove(QStringLiteral("**")); line.remove(QLatin1Char('`')); }
    document.plainText = plain.join(QLatin1Char('\n')).toUtf8();
    return document;
}

bool DoctorReportComposer::write(const DoctorReportDocument &document, const QString &destination,
                                 DoctorReportFormat format, QString *error)
{
    if (destination.trimmed().isEmpty()) { if (error) *error = QStringLiteral("Choose an export destination."); return false; }
    if (format == DoctorReportFormat::DiagnosticBundle) {
        const QFileInfo finalBundle(destination);
        QDir parent = finalBundle.dir();
        if (!parent.exists()) {
            if (!QDir().mkpath(parent.absolutePath()) || !parent.exists()) {
                if (error) *error = QStringLiteral("Could not create the diagnostic-bundle parent folder: %1")
                .arg(QDir::toNativeSeparators(parent.absolutePath()));
                return false;
            }
        }
        if (finalBundle.exists()) {
            if (error) *error = QStringLiteral("The diagnostic-bundle folder already exists: %1")
                .arg(QDir::toNativeSeparators(finalBundle.absoluteFilePath()));
            return false;
        }

        // A bundle is one user-visible artifact.  Commit the individual files
        // in a private sibling directory and expose the final folder only
        // after every bounded atomic write succeeds.
        const QString stagingName = QStringLiteral(".%1.partial-%2")
            .arg(finalBundle.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!parent.mkdir(stagingName)) {
            if (error) *error = QStringLiteral("Could not create a private diagnostic-bundle staging folder.");
            return false;
        }
        const QString stagingPath = parent.filePath(stagingName);
        const QDir staging(stagingPath);
        const QJsonObject manifest{{QStringLiteral("schemaVersion"), kReportSchemaVersion}, {QStringLiteral("files"), QJsonArray{
            QStringLiteral("report.md"), QStringLiteral("report.json"), QStringLiteral("timeline.json"), QStringLiteral("evidence.json")}}};
        const bool written = atomicWrite(staging.filePath(QStringLiteral("report.md")), document.markdown, error)
            && atomicWrite(staging.filePath(QStringLiteral("report.json")), document.json, error)
            && atomicWrite(staging.filePath(QStringLiteral("timeline.json")), document.timelineJson, error)
            && atomicWrite(staging.filePath(QStringLiteral("evidence.json")), document.evidenceJson, error)
            && atomicWrite(staging.filePath(QStringLiteral("manifest.json")), QJsonDocument(manifest).toJson(QJsonDocument::Indented), error);
        if (!written) {
            QDir(stagingPath).removeRecursively();
            return false;
        }
        if (!parent.rename(stagingName, finalBundle.fileName())) {
            QDir(stagingPath).removeRecursively();
            if (error) *error = QStringLiteral("Could not finalize the diagnostic bundle folder.");
            return false;
        }
        return true;
    }
    const QByteArray payload = format == DoctorReportFormat::Json ? document.json
        : format == DoctorReportFormat::PlainText ? document.plainText : document.markdown;
    return atomicWrite(destination, payload, error);
}

QString DoctorReportComposer::sectionMarkdown(const DoctorSession &session, const QString &buildIdentity,
                                              const QString &scope, DoctorReportPrivacy privacy)
{
    DoctorReportRequest request;
    request.scope = scope;
    request.privacy = privacy;
    request.detail = DoctorReportDetail::Forensic;
    return QString::fromUtf8(compose(session, buildIdentity, request).markdown);
}

} // namespace hotas::doctor
