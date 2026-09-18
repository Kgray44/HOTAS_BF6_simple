#include "doctor_session_view_model.h"
#include "doctor_report_composer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>

namespace hotas::doctor {
namespace {

constexpr int kPresentationLayoutVersion = 2;
// The reset layout deliberately gives the planning rail enough room for a
// full phase ledger while reserving a durable reading width for findings. Qt
// still enforces each pane's practical pixel minimum at narrow widths.
constexpr std::array<double, 4> kDefaultPaneFractions{0.49, 0.145, 0.22, 0.145};
constexpr std::array<double, 2> kDefaultBottomDockFractions{0.4, 0.6};
constexpr double kMinimumPersistedPaneFraction = 0.08;
constexpr double kFractionSumTolerance = 0.015;

struct ExportWriteResult final {
    std::atomic_bool complete = false;
    bool written = false;
    QString error;
};

QString presentationKey(const QString &suffix)
{
    return QStringLiteral("hidhideDoctorPhase2/") + suffix;
}

QString createDiagnosticBundleDirectory(const QString &parentPath, QString *error)
{
    const QString absoluteParent = QFileInfo(parentPath).absoluteFilePath();
    QDir parent(absoluteParent);
    if (!parent.exists() && !QDir().mkpath(absoluteParent)) {
        if (error) *error = QStringLiteral("Could not create the selected diagnostic-bundle destination folder.");
        return {};
    }
    parent.setPath(absoluteParent);
    const QString stem = QStringLiteral("HidHideDoctor-Diagnostic-Bundle-%1")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz'Z'")));
    for (int sequence = 1; sequence <= 100; ++sequence) {
        const QString name = sequence == 1 ? stem : QStringLiteral("%1-%2").arg(stem).arg(sequence);
        // DoctorReportComposer transactionally promotes this candidate only
        // after every bundle file has committed.  Reserving the name by
        // creating the final directory here would expose a partial bundle on
        // a later write failure.
        if (!QFileInfo::exists(parent.filePath(name))) return parent.filePath(name);
    }
    if (error) *error = QStringLiteral("Could not reserve a new diagnostic bundle folder in the selected destination.");
    return {};
}

QVariantList defaultBottomDockFractions()
{
    QVariantList values;
    for (const double fraction : kDefaultBottomDockFractions) values.append(fraction);
    return values;
}

QVariantList normalizedBottomDockFractions(const QVariantList &candidate)
{
    if (candidate.size() != static_cast<qsizetype>(kDefaultBottomDockFractions.size())) return defaultBottomDockFractions();
    QVariantList normalized;
    double sum = 0.0;
    for (const QVariant &value : candidate) {
        bool valid = false;
        const double fraction = value.toDouble(&valid);
        if (!valid || !std::isfinite(fraction) || fraction < 0.2 || fraction > 0.8) return defaultBottomDockFractions();
        normalized.append(fraction);
        sum += fraction;
    }
    return std::abs(sum - 1.0) <= kFractionSumTolerance ? normalized : defaultBottomDockFractions();
}

QString iconFor(DoctorCheckStatus status)
{
    switch (status) {
    case DoctorCheckStatus::Healthy: return QStringLiteral("✓");
    case DoctorCheckStatus::Warning: return QStringLiteral("▲");
    case DoctorCheckStatus::Failed: return QStringLiteral("×");
    case DoctorCheckStatus::PermissionLimited: return QStringLiteral("▣");
    case DoctorCheckStatus::NotApplicable: return QStringLiteral("⊘");
    case DoctorCheckStatus::Running: return QStringLiteral("●");
    case DoctorCheckStatus::Waiting: return QStringLiteral("○");
    case DoctorCheckStatus::Cancelled: return QStringLiteral("■");
    case DoctorCheckStatus::Blocked:
    case DoctorCheckStatus::TimedOut: return QStringLiteral("!");
    case DoctorCheckStatus::Unknown:
    case DoctorCheckStatus::Inconclusive: return QStringLiteral("?");
    case DoctorCheckStatus::Informational: return QStringLiteral("i");
    }
    return QStringLiteral("?");
}

QString toneFor(DoctorCheckStatus status)
{
    switch (status) {
    case DoctorCheckStatus::Healthy: return QStringLiteral("healthy");
    case DoctorCheckStatus::Warning: return QStringLiteral("warning");
    case DoctorCheckStatus::Failed:
    case DoctorCheckStatus::TimedOut:
    case DoctorCheckStatus::Blocked: return QStringLiteral("fault");
    case DoctorCheckStatus::PermissionLimited: return QStringLiteral("limited");
    case DoctorCheckStatus::Running: return QStringLiteral("running");
    default: return QStringLiteral("neutral");
    }
}

QString healthRailLabel(DoctorCheckStatus status)
{
    // Unknown is a deliberate diagnosis result: a safe direct observation did
    // not establish a verdict. Make that absence of a verdict clear without
    // relabelling the underlying result as healthy, warning, or failure.
    return status == DoctorCheckStatus::Unknown ? QStringLiteral("NO VERDICT")
        : displayName(status).toUpper();
}

QString elapsedText(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString sessionStateName(DoctorSessionState state)
{
    switch (state) {
    case DoctorSessionState::Preparing: return QStringLiteral("PREPARING");
    case DoctorSessionState::Diagnosing: return QStringLiteral("DIAGNOSING");
    case DoctorSessionState::Analyzing: return QStringLiteral("ANALYZING EVIDENCE");
    case DoctorSessionState::DiagnosisComplete: return QStringLiteral("DIAGNOSIS COMPLETE");
    case DoctorSessionState::DegradedComplete: return QStringLiteral("DEGRADED COMPLETE");
    case DoctorSessionState::Cancelled: return QStringLiteral("CANCELLED SAFELY");
    case DoctorSessionState::FailedSafely: return QStringLiteral("FAILED SAFELY");
    default: return QStringLiteral("READ-ONLY DIAGNOSTICS");
    }
}

DoctorCheckStatus aggregateStatus(const QList<DoctorCheckResult> &results, const QStringList &prefixes)
{
    bool any = false, warning = false, info = false, unknown = false, permission = false;
    for (const DoctorCheckResult &result : results) {
        const bool matches = std::any_of(prefixes.cbegin(), prefixes.cend(), [&](const QString &prefix) { return result.checkId.value().startsWith(prefix); });
        if (!matches) continue;
        any = true;
        if (result.status == DoctorCheckStatus::Failed || result.status == DoctorCheckStatus::TimedOut || result.status == DoctorCheckStatus::Blocked) return result.status;
        if (result.status == DoctorCheckStatus::PermissionLimited) permission = true;
        else if (result.status == DoctorCheckStatus::Warning) warning = true;
        else if (result.status == DoctorCheckStatus::Unknown || result.status == DoctorCheckStatus::Inconclusive) unknown = true;
        else if (result.status == DoctorCheckStatus::Informational) info = true;
        else if (result.status == DoctorCheckStatus::Running) return DoctorCheckStatus::Running;
    }
    if (!any) return DoctorCheckStatus::Waiting;
    if (permission) return DoctorCheckStatus::PermissionLimited;
    if (warning) return DoctorCheckStatus::Warning;
    if (unknown) return DoctorCheckStatus::Unknown;
    if (info) return DoctorCheckStatus::Informational;
    return DoctorCheckStatus::Healthy;
}

} // namespace

DoctorSessionViewModel::DoctorSessionViewModel(DoctorSession &session, QString buildIdentity, QObject *parent)
    : QObject(parent), m_session(session), m_buildIdentity(std::move(buildIdentity))
{
    m_progressPresentationTimer.setInterval(33);
    m_progressPresentationTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_progressPresentationTimer, &QTimer::timeout, this, &DoctorSessionViewModel::updateProgressPresentation);
    QSettings settings;
    m_commandCenter = settings.value(presentationKey(QStringLiteral("commandCenter")), true).toBool();
    const QString storedDensity = settings.value(presentationKey(QStringLiteral("density")), QStringLiteral("Compact")).toString();
    m_density = (storedDensity == QStringLiteral("Comfortable") || storedDensity == QStringLiteral("Dense")) ? storedDensity : QStringLiteral("Compact");
    m_liveEvidenceVisible = settings.value(presentationKey(QStringLiteral("liveEvidence")), false).toBool();
    const bool currentSchema = settings.value(presentationKey(QStringLiteral("layoutSchemaVersion")), 0).toInt() == kPresentationLayoutVersion;
    m_paneFractions = currentSchema
        ? normalizedPaneFractions(settings.value(presentationKey(QStringLiteral("paneFractions"))).toList())
        : defaultPaneFractions();
    m_bottomDockFractions = normalizedBottomDockFractions(settings.value(presentationKey(QStringLiteral("bottomDockFractions"))).toList());
    if (!currentSchema || m_paneFractions != settings.value(presentationKey(QStringLiteral("paneFractions"))).toList()) {
        settings.setValue(presentationKey(QStringLiteral("layoutSchemaVersion")), kPresentationLayoutVersion);
        settings.setValue(presentationKey(QStringLiteral("paneFractions")), m_paneFractions);
        settings.remove(presentationKey(QStringLiteral("paneWidths")));
    }
    if (m_bottomDockFractions != settings.value(presentationKey(QStringLiteral("bottomDockFractions"))).toList())
        settings.setValue(presentationKey(QStringLiteral("bottomDockFractions")), m_bottomDockFractions);
}

QString DoctorSessionViewModel::buildIdentity() const { return m_buildIdentity; }
QString DoctorSessionViewModel::sessionId() const { return m_session.id().value(); }
QString DoctorSessionViewModel::currentPhase() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) if (item.status == DoctorCheckStatus::Running) return displayName(item.check.phase);
    if (m_session.repairPlan()) return QStringLiteral("Repair plan review");
    return m_session.state() == DoctorSessionState::DiagnosisComplete ? QStringLiteral("Read-only analysis complete")
        : (m_session.state() == DoctorSessionState::Cancelled ? QStringLiteral("Diagnostic scan cancelled") : QStringLiteral("Preparing diagnostic scan"));
}
QString DoctorSessionViewModel::currentStep() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) if (item.status == DoctorCheckStatus::Running) return item.check.title;
    if (m_session.repairPlan()) return QStringLiteral("R1 plan generated; no live repair was requested");
    return m_session.state() == DoctorSessionState::DiagnosisComplete ? QStringLiteral("All applicable read-only checks completed") : QStringLiteral("No native operation is currently running");
}
QString DoctorSessionViewModel::currentStepId() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) if (item.status == DoctorCheckStatus::Running) return item.check.id.value();
    return {};
}
int DoctorSessionViewModel::overallProgress() const { return m_session.plan().progress().overallPercent; }
int DoctorSessionViewModel::currentStepProgress() const { return m_session.plan().progress().currentStepPercent; }
double DoctorSessionViewModel::displayProgress() const { return m_displayProgress; }
double DoctorSessionViewModel::displayCurrentStepProgress() const { return m_displayCurrentStepProgress; }
QString DoctorSessionViewModel::presentationElapsed() const
{
    if (m_completedPresentationMs >= 0) return elapsedText(m_completedPresentationMs);
    return m_scanClock.isValid() ? elapsedText(m_scanClock.elapsed()) : elapsed();
}
QStringList DoctorSessionViewModel::planItems() const
{
    QStringList values;
    for (const DiagnosticPlanItem &item : m_session.plan().items()) values.append(QStringLiteral("%1  %2 — %3").arg(item.check.id.value(), item.check.title, displayName(item.status)));
    return values;
}
QStringList DoctorSessionViewModel::resultItems() const
{
    QStringList values;
    for (const DoctorCheckResult &result : m_session.checkResults()) values.append(QStringLiteral("%1  %2 — %3").arg(result.checkId.value(), displayName(result.status), result.summary));
    return values;
}
QStringList DoctorSessionViewModel::findingItems() const
{
    QStringList values;
    for (const Finding &finding : m_session.findings()) values.append(finding.title + QStringLiteral(" — ") + finding.explanation);
    return values;
}
QString DoctorSessionViewModel::userActionTitle() const { return m_session.userAction().title; }
QString DoctorSessionViewModel::userActionDetail() const { return m_session.userAction().explanation; }
bool DoctorSessionViewModel::scanRunning() const
{
    return m_session.state() == DoctorSessionState::Preparing || m_session.state() == DoctorSessionState::Diagnosing || m_session.state() == DoctorSessionState::Analyzing;
}
int DoctorSessionViewModel::completedChecks() const { return m_session.plan().progress().completedChecks; }
int DoctorSessionViewModel::remainingChecks() const
{
    const OperationProgress progress = m_session.plan().progress();
    return std::max(0, progress.applicableChecks - progress.completedChecks);
}
int DoctorSessionViewModel::warningOrFailureCount() const { return warningCheckCount() + failedCheckCount(); }
QString DoctorSessionViewModel::elapsed() const
{
    if (m_completedPresentationMs >= 0) return elapsedText(m_completedPresentationMs);
    if (m_scanClock.isValid()) return elapsedText(m_scanClock.elapsed());
    return elapsedText(m_session.createdAt().msecsTo(QDateTime::currentDateTimeUtc()));
}
bool DoctorSessionViewModel::commandCenter() const { return m_commandCenter; }
QString DoctorSessionViewModel::density() const { return m_density; }
QString DoctorSessionViewModel::sessionState() const
{
    return m_session.repairPlan() ? QStringLiteral("REPAIR REVIEW") : sessionStateName(m_session.state());
}
QString DoctorSessionViewModel::simulationLabel() const { return m_session.sessionLabel(); }

QString DoctorSessionViewModel::environmentStrip() const
{
    if (!m_session.environment()) return QStringLiteral("Windows Unknown · HidHide Unknown · Session %1 · READ ONLY").arg(sessionId().right(8));
    const DoctorEnvironment &environment = *m_session.environment();
    const QString platform = QStringLiteral("%1 %2 · Build %3.%4 · %5")
        .arg(environment.platform.windowsEdition.isEmpty() ? QStringLiteral("Windows Unknown") : environment.platform.windowsEdition,
            environment.platform.windowsVersion.isEmpty() ? QStringLiteral("Unknown") : environment.platform.windowsVersion)
        .arg(environment.platform.build).arg(environment.platform.revision).arg(displayName(environment.platform.nativeArchitecture));
    const QString component = environment.hidhide.present
        ? QStringLiteral("HidHide %1 · Driver %2").arg(environment.hidhide.clientVersion.isEmpty() ? QStringLiteral("Unknown") : environment.hidhide.clientVersion,
            environment.hidhide.driverVersion.isEmpty() ? QStringLiteral("Unknown") : environment.hidhide.driverVersion)
        : QStringLiteral("HidHide Absent");
    return QStringLiteral("%1   |   %2   |   Session %3   |   READ ONLY").arg(platform, component, sessionId().right(8));
}

QVariantList DoctorSessionViewModel::environmentGroups() const
{
    const QString session = sessionId().right(8);
    if (!m_session.environment()) return {QVariantMap{{QStringLiteral("label"), QStringLiteral("WINDOWS")}, {QStringLiteral("value"), QStringLiteral("Unknown")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("HIDHIDE")}, {QStringLiteral("value"), QStringLiteral("Unknown")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("SESSION")}, {QStringLiteral("value"), session}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("MODE")}, {QStringLiteral("value"), QStringLiteral("READ ONLY")}}};
    const DoctorEnvironment &environment = *m_session.environment();
    const QString platform = QStringLiteral("%1 %2 · Build %3.%4 · %5")
        .arg(environment.platform.windowsEdition.isEmpty() ? QStringLiteral("Windows Unknown") : environment.platform.windowsEdition,
            environment.platform.windowsVersion.isEmpty() ? QStringLiteral("Unknown") : environment.platform.windowsVersion)
        .arg(environment.platform.build).arg(environment.platform.revision).arg(displayName(environment.platform.nativeArchitecture));
    const QString hidhide = environment.hidhide.present
        ? QStringLiteral("Client %1 · Driver %2").arg(environment.hidhide.clientVersion.isEmpty() ? QStringLiteral("Unknown") : environment.hidhide.clientVersion,
            environment.hidhide.driverVersion.isEmpty() ? QStringLiteral("Unknown") : environment.hidhide.driverVersion)
        : QStringLiteral("Absent");
    return {QVariantMap{{QStringLiteral("label"), QStringLiteral("WINDOWS")}, {QStringLiteral("value"), platform}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("HIDHIDE")}, {QStringLiteral("value"), hidhide}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("SESSION")}, {QStringLiteral("value"), session}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("MODE")}, {QStringLiteral("value"), QStringLiteral("READ ONLY")}}};
}

QVariantList DoctorSessionViewModel::healthDomains() const
{
    const QList<QPair<QString, QStringList>> domains = {{QStringLiteral("WINDOWS"), {QStringLiteral("HD-SYS-"), QStringLiteral("HD-PORT-")}},
        {QStringLiteral("INSTALL"), {QStringLiteral("HD-INST-"), QStringLiteral("HD-PKG-")}}, {QStringLiteral("DRIVER"), {QStringLiteral("HD-DRV-")}},
        {QStringLiteral("API"), {QStringLiteral("HD-API-")}}, {QStringLiteral("CONFIG"), {QStringLiteral("HD-CFG-")}},
        {QStringLiteral("DEVICES"), {QStringLiteral("HD-DEV-")}}, {QStringLiteral("ISOLATION"), {QStringLiteral("HD-ISO-")}}};
    QVariantList values;
    for (const auto &domain : domains) {
        const DoctorCheckStatus status = aggregateStatus(m_session.checkResults(), domain.second);
        values.append(QVariantMap{{QStringLiteral("label"), domain.first}, {QStringLiteral("status"), healthRailLabel(status)},
            {QStringLiteral("detail"), status == DoctorCheckStatus::Unknown
                ? QStringLiteral("No category verdict: one or more safe direct observations were not determinate.")
                : QStringLiteral("Aggregate status from completed read-only checks.")},
            {QStringLiteral("symbol"), iconFor(status)}, {QStringLiteral("tone"), toneFor(status)}});
    }
    return values;
}

QVariantList DoctorSessionViewModel::planPhases() const
{
    QVariantList values;
    for (DoctorPhase phase : {DoctorPhase::SystemEnvironment, DoctorPhase::InstallationDiscovery, DoctorPhase::PackageIntegrity, DoctorPhase::KernelDriverState,
        DoctorPhase::ProtocolApiHealth, DoctorPhase::ConfigurationIntegrity, DoctorPhase::DeviceEcosystem, DoctorPhase::IsolationVerification,
        DoctorPhase::WindowsEvidence, DoctorPhase::ConsistencyAnalysis, DoctorPhase::Diagnosis, DoctorPhase::RepairRecommendation}) {
        int total = 0, complete = 0;
        DoctorCheckStatus status = DoctorCheckStatus::Waiting;
        for (const DiagnosticPlanItem &item : m_session.plan().items()) {
            if (item.check.phase != phase) continue;
            ++total;
            if (isTerminal(item.status)) ++complete;
            if (item.status == DoctorCheckStatus::Running) status = DoctorCheckStatus::Running;
            else if (item.status == DoctorCheckStatus::Failed || item.status == DoctorCheckStatus::TimedOut) status = item.status;
            else if (status != DoctorCheckStatus::Failed && item.status == DoctorCheckStatus::Warning) status = DoctorCheckStatus::Warning;
            else if (status == DoctorCheckStatus::Waiting && isTerminal(item.status)) status = item.status;
        }
        if (!total) continue;
        values.append(QVariantMap{{QStringLiteral("title"), displayName(phase).toUpper()}, {QStringLiteral("complete"), complete}, {QStringLiteral("total"), total},
            {QStringLiteral("status"), displayName(status).toUpper()}, {QStringLiteral("symbol"), iconFor(status)}, {QStringLiteral("tone"), toneFor(status)},
            {QStringLiteral("progress"), total ? complete * 100 / total : 0}});
    }
    return values;
}

QVariantList DoctorSessionViewModel::findingCards() const
{
    QVariantList values;
    for (const Finding &finding : m_session.findings()) values.append(QVariantMap{{QStringLiteral("id"), finding.id.value()}, {QStringLiteral("title"), finding.title},
        {QStringLiteral("severity"), displayName(finding.severity).toUpper()}, {QStringLiteral("summary"), finding.explanation},
        {QStringLiteral("technical"), finding.technicalExplanation}, {QStringLiteral("confidence"), displayName(finding.confidence)},
        {QStringLiteral("repairability"), displayName(finding.repairability)}, {QStringLiteral("evidenceId"), finding.evidenceIds.isEmpty() ? QString() : finding.evidenceIds.first().value()},
        {QStringLiteral("tone"), finding.severity == FindingSeverity::Critical || finding.severity == FindingSeverity::Error ? QStringLiteral("fault") : finding.severity == FindingSeverity::Warning ? QStringLiteral("warning") : QStringLiteral("neutral")}});
    return values;
}

QVariantList DoctorSessionViewModel::diagnosisCards() const
{
    QVariantList values;
    for (const Diagnosis &diagnosis : m_session.diagnoses()) values.append(QVariantMap{{QStringLiteral("id"), diagnosis.id.value()}, {QStringLiteral("role"), displayName(diagnosis.role).toUpper()},
        {QStringLiteral("title"), diagnosis.title}, {QStringLiteral("family"), diagnosis.problemFamily}, {QStringLiteral("summary"), diagnosis.humanExplanation},
        {QStringLiteral("why"), diagnosis.technicalExplanation}, {QStringLiteral("impact"), diagnosis.userImpact}, {QStringLiteral("confidence"), displayName(diagnosis.confidence)},
        {QStringLiteral("score"), diagnosis.confidenceExplanation.score}, {QStringLiteral("confidenceReason"), diagnosis.confidenceExplanation.bandReason},
        {QStringLiteral("repairability"), displayName(diagnosis.repairability)}, {QStringLiteral("evidenceId"), diagnosis.supportingEvidence.isEmpty() ? QString() : diagnosis.supportingEvidence.first().value()},
        {QStringLiteral("tone"), diagnosis.severity == FindingSeverity::Critical || diagnosis.severity == FindingSeverity::Error ? QStringLiteral("fault") : diagnosis.severity == FindingSeverity::Warning ? QStringLiteral("warning") : QStringLiteral("neutral")}});
    return values;
}

QVariantList DoctorSessionViewModel::activityRows() const
{
    QVariantList values;
    for (const DoctorActivityEvent &event : m_session.activity()) values.append(QVariantMap{{QStringLiteral("time"), event.timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"))},
        {QStringLiteral("checkId"), event.checkId.value()}, {QStringLiteral("symbol"), iconFor(event.status)}, {QStringLiteral("status"), displayName(event.status).toUpper()},
        {QStringLiteral("eventType"), displayName(event.type).toUpper()}, {QStringLiteral("phase"), displayName(event.phase)},
        {QStringLiteral("title"), event.title}, {QStringLiteral("detail"), event.detail}, {QStringLiteral("reason"), event.reason},
        {QStringLiteral("target"), event.target}, {QStringLiteral("result"), event.result}, {QStringLiteral("nextStep"), event.nextStep},
        {QStringLiteral("tone"), toneFor(event.status)}, {QStringLiteral("evidenceId"), event.evidenceId.value()},
        {QStringLiteral("evidenceIds"), [&] { QStringList ids; for (const EvidenceId &id : event.evidenceIds) ids.append(id.value()); return ids; }()},
        {QStringLiteral("findingIds"), [&] { QStringList ids; for (const FindingId &id : event.relatedFindingIds) ids.append(id.value()); return ids; }()},
        {QStringLiteral("diagnosisIds"), [&] { QStringList ids; for (const DiagnosisId &id : event.relatedDiagnosisIds) ids.append(id.value()); return ids; }()}});
    return values;
}

QVariantList DoctorSessionViewModel::evidenceRows() const
{
    QVariantList values;
    for (const EvidenceRecord &evidence : m_session.evidence()) values.append(QVariantMap{{QStringLiteral("id"), evidence.id.value()}, {QStringLiteral("checkId"), evidence.checkId.value()},
        {QStringLiteral("source"), evidence.sourceDisplayName.isEmpty() ? evidence.source : evidence.sourceDisplayName}, {QStringLiteral("provider"), evidence.provider},
        {QStringLiteral("operation"), evidence.operation}, {QStringLiteral("summary"), evidence.humanSummary}, {QStringLiteral("technical"), evidence.technicalDetails},
        {QStringLiteral("duration"), QStringLiteral("%1 us (%2 ms)").arg(evidence.monotonicDurationUs).arg(evidence.durationMs)}, {QStringLiteral("error"), evidence.nativeError ? QStringLiteral("%1 / 0x%2").arg(evidence.nativeError->symbolicName).arg(evidence.nativeError->code, 0, 16).toUpper() : QString()},
        {QStringLiteral("timestamp"), evidence.recordedAt.toLocalTime().toString(Qt::ISODateWithMs)}});
    return values;
}

QVariantMap DoctorSessionViewModel::selectedEvidence() const
{
    for (int index = 0; index < m_session.evidence().size(); ++index) {
        const EvidenceRecord &evidence = m_session.evidence().at(index);
        if (evidence.id.value() != m_selectedEvidenceId) continue;
        QVariantList groups;
        for (EvidenceFieldCategory category : {EvidenceFieldCategory::Identity, EvidenceFieldCategory::Observation,
                 EvidenceFieldCategory::Target, EvidenceFieldCategory::Method, EvidenceFieldCategory::Timing,
                 EvidenceFieldCategory::NativeResult, EvidenceFieldCategory::Relationships, EvidenceFieldCategory::Technical,
                 EvidenceFieldCategory::Raw}) {
            QVariantList fields;
            for (const EvidenceField &field : evidence.fields) {
                if (field.category != category) continue;
                fields.append(QVariantMap{{QStringLiteral("label"), field.label}, {QStringLiteral("value"), field.value},
                    {QStringLiteral("monospace"), field.monospace}, {QStringLiteral("sensitivity"), static_cast<int>(field.sensitivity)}});
            }
            if (!fields.isEmpty()) groups.append(QVariantMap{{QStringLiteral("name"), displayName(category).toUpper()}, {QStringLiteral("fields"), fields}});
        }
        const auto labels = [](const auto &ids) { QStringList values; for (const auto &id : ids) values.append(id.value()); return values; };
        QVariantList relationshipFields;
        const QString relatedEvidence = labels(evidence.relatedEvidenceIds).join(QStringLiteral(", "));
        const QString relatedChecks = labels(evidence.relatedCheckIds).join(QStringLiteral(", "));
        const QString relatedFindings = labels(evidence.relatedFindingIds).join(QStringLiteral(", "));
        const QString relatedDiagnoses = labels(evidence.relatedDiagnosisIds).join(QStringLiteral(", "));
        if (!relatedEvidence.isEmpty()) relationshipFields.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("EVIDENCE LINKS")}, {QStringLiteral("value"), relatedEvidence}, {QStringLiteral("monospace"), true}});
        if (!relatedChecks.isEmpty()) relationshipFields.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("CHECK LINKS")}, {QStringLiteral("value"), relatedChecks}, {QStringLiteral("monospace"), true}});
        if (!relatedFindings.isEmpty()) relationshipFields.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("FINDING LINKS")}, {QStringLiteral("value"), relatedFindings}, {QStringLiteral("monospace"), true}});
        if (!relatedDiagnoses.isEmpty()) relationshipFields.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("DIAGNOSIS LINKS")}, {QStringLiteral("value"), relatedDiagnoses}, {QStringLiteral("monospace"), true}});
        if (!relationshipFields.isEmpty()) groups.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("RELATIONSHIPS")}, {QStringLiteral("fields"), relationshipFields}});
        QVariantList attempts;
        for (const EvidenceAttempt &attempt : evidence.attempts) attempts.append(QVariantMap{{QStringLiteral("ordinal"), attempt.ordinal},
            {QStringLiteral("operation"), attempt.operation}, {QStringLiteral("target"), attempt.target}, {QStringLiteral("outcome"), attempt.outcome},
            {QStringLiteral("startedAt"), attempt.startedAt.toLocalTime().toString(Qt::ISODateWithMs)},
            {QStringLiteral("completedAt"), attempt.completedAt.toLocalTime().toString(Qt::ISODateWithMs)},
            {QStringLiteral("duration"), QStringLiteral("%1 us").arg(attempt.monotonicDurationUs)}, {QStringLiteral("timeout"), QStringLiteral("%1 ms").arg(attempt.timeoutMs)},
            {QStringLiteral("bytes"), QStringLiteral("%1 request / %2 response").arg(attempt.requestBytes).arg(attempt.responseBytes)},
            {QStringLiteral("error"), attempt.nativeError ? attempt.nativeError->message : QStringLiteral("None")}});
        return QVariantMap{{QStringLiteral("id"), evidence.id.value()}, {QStringLiteral("checkId"), evidence.checkId.value()},
            {QStringLiteral("source"), evidence.sourceDisplayName.isEmpty() ? evidence.source : evidence.sourceDisplayName}, {QStringLiteral("provider"), evidence.provider},
            {QStringLiteral("subsystem"), evidence.subsystem}, {QStringLiteral("operation"), evidence.operation}, {QStringLiteral("method"), evidence.method},
            {QStringLiteral("target"), evidence.targetDisplayName}, {QStringLiteral("summary"), evidence.humanSummary}, {QStringLiteral("technical"), evidence.technicalDetails},
            {QStringLiteral("structured"), evidence.structuredValue}, {QStringLiteral("statusReason"), evidence.statusReason},
            {QStringLiteral("duration"), QStringLiteral("%1 us (%2 ms)").arg(evidence.monotonicDurationUs).arg(evidence.durationMs)},
            {QStringLiteral("timestamp"), evidence.recordedAt.toLocalTime().toString(Qt::ISODateWithMs)},
            {QStringLiteral("startedAt"), evidence.startedAt.toLocalTime().toString(Qt::ISODateWithMs)}, {QStringLiteral("completedAt"), evidence.completedAt.toLocalTime().toString(Qt::ISODateWithMs)},
            {QStringLiteral("provenance"), evidence.provenance == EvidenceProvenance::Direct ? QStringLiteral("Direct observation") : evidence.provenance == EvidenceProvenance::Derived ? QStringLiteral("Derived correlation") : QStringLiteral("Fixture observation")},
            {QStringLiteral("error"), evidence.nativeError ? QStringLiteral("%1 (%2 / 0x%3)").arg(evidence.nativeError->symbolicName).arg(evidence.nativeError->code).arg(evidence.nativeError->code, 0, 16).toUpper() : QStringLiteral("None")},
            {QStringLiteral("groups"), groups}, {QStringLiteral("attempts"), attempts}, {QStringLiteral("relatedEvidenceIds"), labels(evidence.relatedEvidenceIds)},
            {QStringLiteral("relatedCheckIds"), labels(evidence.relatedCheckIds)}, {QStringLiteral("relatedFindingIds"), labels(evidence.relatedFindingIds)},
            {QStringLiteral("relatedDiagnosisIds"), labels(evidence.relatedDiagnosisIds)}, {QStringLiteral("hasPrevious"), index > 0},
            {QStringLiteral("hasNext"), index + 1 < m_session.evidence().size()}, {QStringLiteral("evidenceIndex"), index + 1}, {QStringLiteral("evidenceCount"), m_session.evidence().size()},
            {QStringLiteral("collectionTruncated"), evidence.collectionTruncated}, {QStringLiteral("truncationReason"), evidence.truncationReason}};
    }
    return {};
}

QVariantMap DoctorSessionViewModel::currentOperationDetails() const
{
    const std::optional<DoctorOperation> operation = m_session.currentOperation();
    return QVariantMap{{QStringLiteral("phase"), currentPhase()}, {QStringLiteral("checkId"), currentStepId()}, {QStringLiteral("title"), currentStep()},
        {QStringLiteral("status"), operation ? displayName(operation->state == DoctorOperationState::Running ? DoctorCheckStatus::Running : DoctorCheckStatus::Healthy).toUpper() : sessionState()},
        {QStringLiteral("progress"), currentStepProgress()}, {QStringLiteral("timeout"), operation && operation->timeoutMs ? QStringLiteral("%1 s").arg(operation->timeoutMs / 1000.0, 0, 'f', 2) : QStringLiteral("Not applicable")},
        {QStringLiteral("elapsed"), presentationElapsed()}, {QStringLiteral("detail"), scanRunning() ? QStringLiteral("No user action required. The Doctor is using bounded read-only operations.") : QStringLiteral("The last operation has completed; select evidence for its recorded details.")}};
}

bool DoctorSessionViewModel::repairPlanAvailable() const { return m_session.repairPlan().has_value(); }

QVariantMap DoctorSessionViewModel::repairPlanSummary() const
{
    if (!m_session.repairPlan()) return {};
    const RepairPlan &plan = *m_session.repairPlan();
    const QString qualification = plan.qualification == RepairQualificationLevel::LabQualified
        ? QStringLiteral("LAB QUALIFIED — OWNER TEST ONLY")
        : (plan.qualification == RepairQualificationLevel::FieldQualified ? QStringLiteral("FIELD QUALIFIED") : QStringLiteral("EXPERIMENTAL"));
    const auto risk = [&] {
        switch (plan.riskClass) {
        case RepairRiskClass::R1Configuration: return QStringLiteral("R1 · CONFIGURATION REPAIR");
        case RepairRiskClass::R2Component: return QStringLiteral("R2 · COMPONENT REPAIR");
        case RepairRiskClass::R3Package: return QStringLiteral("R3 · PACKAGE REPAIR");
        case RepairRiskClass::R4ApprovedUpgrade: return QStringLiteral("R4 · APPROVED UPGRADE");
        case RepairRiskClass::R5Recovery: return QStringLiteral("R5 · RECOVERY");
        case RepairRiskClass::R0Observe: return QStringLiteral("R0 · OBSERVE");
        }
        return QStringLiteral("REPAIR");
    }();
    const QJsonObject package = plan.deepRepair.value(QStringLiteral("package")).toObject();
    const QString packageSummary = package.isEmpty() ? QStringLiteral("Not applicable")
        : QStringLiteral("%1 · %2 · %3\n%4\nSHA-256 %5\nSigner %6")
            .arg(package.value(QStringLiteral("packageId")).toString(), package.value(QStringLiteral("version")).toString(),
                package.value(QStringLiteral("architecture")).toString(), package.value(QStringLiteral("source")).toString(),
                package.value(QStringLiteral("expectedSha256")).toString(), package.value(QStringLiteral("signerIdentity")).toString());
    return {{QStringLiteral("planId"), plan.id.value()}, {QStringLiteral("title"), plan.title},
        {QStringLiteral("description"), plan.description}, {QStringLiteral("recipe"), plan.recipeId.value() + QStringLiteral(" v") + plan.recipeVersion},
        {QStringLiteral("risk"), risk}, {QStringLiteral("qualification"), qualification},
        {QStringLiteral("deep"), plan.riskClass != RepairRiskClass::R1Configuration},
        {QStringLiteral("before"), plan.expectedPreState}, {QStringLiteral("after"), plan.expectedPostState},
        {QStringLiteral("elevation"), plan.elevationRequired ? QStringLiteral("Required for live helper execution") : QStringLiteral("Not required")},
        {QStringLiteral("restart"), plan.restartRequired ? QStringLiteral("Required · maximum %1 restart(s) · observation first after restart").arg(plan.maximumReboots) : QStringLiteral("No")},
        {QStringLiteral("backup"), plan.riskClass == RepairRiskClass::R1Configuration ? QStringLiteral("Captured before any mutation") : QStringLiteral("Deep recovery snapshot captured before package/component mutation")},
        {QStringLiteral("rollback"), plan.riskClass == RepairRiskClass::R1Configuration ? QStringLiteral("Exact pre-state; blocked on external change")
             : plan.deepRepair.value(QStringLiteral("rollback")).toString()},
        {QStringLiteral("package"), packageSummary},
        {QStringLiteral("continuation"), plan.deepRepair.value(QStringLiteral("reboot")).toObject().value(QStringLiteral("observeFirst")).toBool()
             ? QStringLiteral("AwaitingReboot is durable; restart-later blocks conflicting deep repair; resume reads actual state before any mutation.") : QStringLiteral("No reboot continuation required")},
        {QStringLiteral("expectedTime"), QStringLiteral("~%1 seconds").arg(plan.estimatedSeconds)},
        {QStringLiteral("userAction"), m_labRepairMode
            ? QStringLiteral("Lab fixture only: deliberate authorization is required; the helper revalidates every sealed package/component target before any operation.")
            : QStringLiteral("Review only. Normal mode cannot execute LabQualified repairs.")}};
}

QStringList DoctorSessionViewModel::repairPlanOperations() const
{
    QStringList rows;
    if (!m_session.repairPlan()) return rows;
    for (const RepairOperation &operation : m_session.repairPlan()->operations) {
        const QString verb = operation.kind == RepairOperationKind::AddWhitelistEntry ? QStringLiteral("ADD APPLICATION EXEMPTION")
            : operation.kind == RepairOperationKind::RemoveWhitelistEntry ? QStringLiteral("REMOVE APPLICATION EXEMPTION")
            : operation.kind == RepairOperationKind::AddBlacklistEntry ? QStringLiteral("ADD HIDDEN DEVICE")
            : operation.kind == RepairOperationKind::RemoveBlacklistEntry ? QStringLiteral("REMOVE HIDDEN DEVICE")
            : operation.kind == RepairOperationKind::SetHidHideActive ? QStringLiteral("SET HIDHIDE CLOAK")
            : operation.kind == RepairOperationKind::SetHidHideInverse ? QStringLiteral("SET HIDHIDE INVERSE")
            : operation.kind == RepairOperationKind::RepairExactServiceConfiguration ? QStringLiteral("REPAIR EXACT HIDHIDE SERVICE")
            : operation.kind == RepairOperationKind::RepairExactFilterRegistration ? QStringLiteral("REPAIR EXACT HIDHIDE FILTER")
            : operation.kind == RepairOperationKind::ValidateApprovedPackage ? QStringLiteral("VERIFY APPROVED PACKAGE")
            : operation.kind == RepairOperationKind::StageApprovedPackage ? QStringLiteral("STAGE VERIFIED PACKAGE")
            : operation.kind == RepairOperationKind::InstallApprovedHidHidePackage ? QStringLiteral("INSTALL APPROVED PACKAGE")
            : operation.kind == RepairOperationKind::RemoveSpecificInactiveHidHidePackage ? QStringLiteral("REMOVE EXACT INACTIVE PACKAGE")
            : operation.kind == RepairOperationKind::RequestSystemRestart ? QStringLiteral("PERSIST RESTART BOUNDARY")
            : operation.kind == RepairOperationKind::ReconcileHidHideConfiguration ? QStringLiteral("OBSERVE AND RECONCILE CONFIGURATION")
            : QStringLiteral("RESTORE SNAPSHOT");
        rows.append(verb + QStringLiteral("  ·  ") + operation.targetIdentity);
    }
    return rows;
}

QStringList DoctorSessionViewModel::repairPlanCollateral() const
{
    return m_session.repairPlan() ? m_session.repairPlan()->unchangedCollateral : QStringList{};
}
bool DoctorSessionViewModel::labRepairMode() const { return m_labRepairMode; }
QString DoctorSessionViewModel::repairRuntimeState() const { return m_repairRuntimeState; }
QString DoctorSessionViewModel::repairRuntimeDetail() const { return m_repairRuntimeDetail; }
bool DoctorSessionViewModel::repairOperationInFlight() const { return m_repairOperationInFlight; }
QString DoctorSessionViewModel::recoveryNotice() const { return m_recoveryNotice; }

int DoctorSessionViewModel::healthyCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Healthy; }); }
int DoctorSessionViewModel::informationalCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Informational; }); }
int DoctorSessionViewModel::warningCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Warning || result.status == DoctorCheckStatus::PermissionLimited || result.status == DoctorCheckStatus::Unknown || result.status == DoctorCheckStatus::Inconclusive; }); }
int DoctorSessionViewModel::failedCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Failed || result.status == DoctorCheckStatus::TimedOut || result.status == DoctorCheckStatus::Blocked; }); }
bool DoctorSessionViewModel::liveEvidenceVisible() const { return m_liveEvidenceVisible; }
QString DoctorSessionViewModel::maximizedPane() const { return m_maximizedPane; }
QVariantList DoctorSessionViewModel::paneFractions() const { return m_paneFractions; }
QVariantList DoctorSessionViewModel::bottomDockFractions() const { return m_bottomDockFractions; }
int DoctorSessionViewModel::layoutResetEpoch() const { return m_layoutResetEpoch; }
QString DoctorSessionViewModel::integrationNotice() const { return m_integrationNotice; }
bool DoctorSessionViewModel::integrationComponentMismatch() const { return m_integrationComponentMismatch; }
bool DoctorSessionViewModel::exportAvailable() const { return !m_redactedDiagnosticReport.isEmpty(); }
bool DoctorSessionViewModel::reportBusy() const { return m_reportBusy; }
QString DoctorSessionViewModel::reportStatus() const { return m_reportStatus; }

QVariantList DoctorSessionViewModel::defaultPaneFractions()
{
    QVariantList values;
    for (const double fraction : kDefaultPaneFractions) values.append(fraction);
    return values;
}

QVariantList DoctorSessionViewModel::normalizedPaneFractions(const QVariantList &candidate)
{
    if (candidate.size() != static_cast<qsizetype>(kDefaultPaneFractions.size())) return defaultPaneFractions();
    double sum = 0.0;
    QVariantList normalized;
    for (const QVariant &value : candidate) {
        bool valid = false;
        const double fraction = value.toDouble(&valid);
        if (!valid || !std::isfinite(fraction) || fraction < kMinimumPersistedPaneFraction || fraction > 0.70) return defaultPaneFractions();
        sum += fraction;
        normalized.append(fraction);
    }
    return std::abs(sum - 1.0) <= kFractionSumTolerance ? normalized : defaultPaneFractions();
}

void DoctorSessionViewModel::togglePresentation() { setCommandCenter(!m_commandCenter); }
void DoctorSessionViewModel::setCommandCenter(bool commandCenter)
{
    if (m_commandCenter == commandCenter) return;
    m_commandCenter = commandCenter;
    if (!m_commandCenter) m_maximizedPane.clear();
    QSettings().setValue(presentationKey(QStringLiteral("commandCenter")), m_commandCenter);
    emit presentationChanged();
}
void DoctorSessionViewModel::setDensity(const QString &density)
{
    const QString normalized = density.trimmed();
    if (normalized != QStringLiteral("Comfortable") && normalized != QStringLiteral("Compact") && normalized != QStringLiteral("Dense")) return;
    if (m_density == normalized) return;
    m_density = normalized;
    QSettings().setValue(presentationKey(QStringLiteral("density")), m_density);
    emit presentationChanged();
}
void DoctorSessionViewModel::setLiveEvidenceVisible(bool visible)
{
    if (m_liveEvidenceVisible == visible) return;
    m_liveEvidenceVisible = visible;
    QSettings().setValue(presentationKey(QStringLiteral("liveEvidence")), visible);
    emit presentationChanged();
}
void DoctorSessionViewModel::setMaximizedPane(const QString &pane)
{
    const QString normalized = pane.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("plan") || normalized == QStringLiteral("current") || normalized == QStringLiteral("findings") || normalized == QStringLiteral("action")) ? normalized : QString();
    if (next == m_maximizedPane) return;
    m_maximizedPane = next;
    emit presentationChanged();
}
void DoctorSessionViewModel::savePaneFractions(const QVariantList &fractions)
{
    const QVariantList normalized = normalizedPaneFractions(fractions);
    if (normalized == defaultPaneFractions() && fractions != defaultPaneFractions()) return;
    if (m_paneFractions == normalized) return;
    m_paneFractions = normalized;
    QSettings settings;
    settings.setValue(presentationKey(QStringLiteral("layoutSchemaVersion")), kPresentationLayoutVersion);
    settings.setValue(presentationKey(QStringLiteral("paneFractions")), m_paneFractions);
    emit presentationChanged();
}
void DoctorSessionViewModel::saveBottomDockFractions(const QVariantList &fractions)
{
    const QVariantList normalized = normalizedBottomDockFractions(fractions);
    if (normalized == defaultBottomDockFractions() && fractions != defaultBottomDockFractions()) return;
    if (m_bottomDockFractions == normalized) return;
    m_bottomDockFractions = normalized;
    QSettings().setValue(presentationKey(QStringLiteral("bottomDockFractions")), m_bottomDockFractions);
    emit presentationChanged();
}
void DoctorSessionViewModel::resetWorkspaceLayout()
{
    m_commandCenter = true;
    m_density = QStringLiteral("Compact");
    m_liveEvidenceVisible = false;
    m_maximizedPane.clear();
    m_paneFractions = defaultPaneFractions();
    m_bottomDockFractions = defaultBottomDockFractions();
    ++m_layoutResetEpoch;
    QSettings settings;
    settings.setValue(presentationKey(QStringLiteral("commandCenter")), m_commandCenter);
    settings.setValue(presentationKey(QStringLiteral("density")), m_density);
    settings.setValue(presentationKey(QStringLiteral("liveEvidence")), m_liveEvidenceVisible);
    settings.setValue(presentationKey(QStringLiteral("layoutSchemaVersion")), kPresentationLayoutVersion);
    settings.setValue(presentationKey(QStringLiteral("paneFractions")), m_paneFractions);
    settings.setValue(presentationKey(QStringLiteral("bottomDockFractions")), m_bottomDockFractions);
    settings.remove(presentationKey(QStringLiteral("paneWidths")));
    emit presentationChanged();
}
void DoctorSessionViewModel::selectEvidence(const QString &evidenceId)
{
    if (m_selectedEvidenceId == evidenceId) return;
    m_selectedEvidenceId = evidenceId;
    emit sessionChanged();
}

void DoctorSessionViewModel::selectAdjacentEvidence(int direction)
{
    if (direction == 0 || m_session.evidence().isEmpty()) return;
    int current = -1;
    for (int index = 0; index < m_session.evidence().size(); ++index) {
        if (m_session.evidence().at(index).id.value() == m_selectedEvidenceId) { current = index; break; }
    }
    if (current < 0) current = direction > 0 ? -1 : m_session.evidence().size();
    const int next = std::clamp(current + (direction > 0 ? 1 : -1), 0, static_cast<int>(m_session.evidence().size()) - 1);
    selectEvidence(m_session.evidence().at(next).id.value());
}
void DoctorSessionViewModel::notifySessionChanged() { emit sessionChanged(); }
void DoctorSessionViewModel::requestCancellation() { if (m_cancellation) m_cancellation(); }
void DoctorSessionViewModel::requestRerun() { if (m_rerun) m_rerun(); }
void DoctorSessionViewModel::requestHelperConnectivityTest()
{
    if (!m_labRepairMode || !m_labRepairAction || m_repairOperationInFlight) return;
    setRepairRuntime(QStringLiteral("AWAITING ELEVATION"), QStringLiteral("Starting the Lab-only helper connectivity test. No configuration mutation is requested."), true);
    m_labRepairAction(false);
}
void DoctorSessionViewModel::requestLabRepairAuthorization()
{
    if (!m_labRepairMode || !m_labRepairAction || m_repairOperationInFlight) return;
    setRepairRuntime(QStringLiteral("AWAITING AUTHORIZATION"), QStringLiteral("Owner/lab authorization is binding this exact plan before UAC. Cancelling UAC makes no changes."), true);
    m_labRepairAction(true);
}
bool DoctorSessionViewModel::exportDiagnosticReport(const QString &fileName)
{
    if (!exportAvailable() || fileName.trimmed().isEmpty()) return false;
    beginAsynchronousExport(fileName, QStringLiteral("Entire Session"), QStringLiteral("Detailed"),
                            QStringLiteral("JSON"), QStringLiteral("Safe to Share"));
    return true;
}
void DoctorSessionViewModel::replaceSession(DoctorSession session)
{
    const bool incomingScanRunning = session.state() == DoctorSessionState::Preparing || session.state() == DoctorSessionState::Diagnosing
        || session.state() == DoctorSessionState::Analyzing;
    if (incomingScanRunning && (!m_scanClock.isValid() || m_completedPresentationMs >= 0)) {
        m_scanClock.start();
        m_completedPresentationMs = -1;
        m_displayProgress = 0.0;
        m_displayCurrentStepProgress = 0.0;
    }
    m_session = std::move(session);
    if (m_selectedEvidenceId.isEmpty() && !m_session.evidence().isEmpty()) m_selectedEvidenceId = m_session.evidence().last().id.value();
    if (incomingScanRunning && !m_progressPresentationTimer.isActive()) m_progressPresentationTimer.start();
    if (!incomingScanRunning && m_scanClock.isValid()) {
        m_completedPresentationMs = m_scanClock.elapsed();
        m_progressPresentationTimer.stop();
        m_displayProgress = std::max(m_displayProgress, static_cast<double>(overallProgress()));
        m_displayCurrentStepProgress = static_cast<double>(currentStepProgress());
    }
    updateProgressPresentation();
    emit sessionChanged();
}
void DoctorSessionViewModel::setScanActions(std::function<void()> cancellation, std::function<void()> rerun)
{
    m_cancellation = std::move(cancellation);
    m_rerun = std::move(rerun);
}
void DoctorSessionViewModel::setLabRepairActions(bool enabled, std::function<void(bool)> action)
{
    m_labRepairMode = enabled;
    m_labRepairAction = std::move(action);
    m_repairRuntimeState = enabled ? QStringLiteral("LAB REPAIR MODE") : QStringLiteral("READ ONLY");
    m_repairRuntimeDetail = enabled
        ? QStringLiteral("Development fixture only. Review the exact sealed plan before a connectivity test or final authorization.")
        : QStringLiteral("Normal Doctor mode is read-only. Lab-qualified repair plans cannot execute here.");
    emit repairRuntimeChanged();
}

void DoctorSessionViewModel::setCopyAction(std::function<void(QString)> action)
{
    m_copyAction = std::move(action);
}

void DoctorSessionViewModel::setRepairRuntime(QString state, QString detail, bool inFlight)
{
    m_repairRuntimeState = std::move(state);
    m_repairRuntimeDetail = std::move(detail);
    m_repairOperationInFlight = inFlight;
    emit repairRuntimeChanged();
}
void DoctorSessionViewModel::setRecoveryNotice(QString notice)
{
    if (m_recoveryNotice == notice) return;
    m_recoveryNotice = std::move(notice);
    emit repairRuntimeChanged();
}

void DoctorSessionViewModel::setIntegrationNotice(QString notice, bool componentMismatch)
{
    if (m_integrationNotice == notice && m_integrationComponentMismatch == componentMismatch) return;
    m_integrationNotice = std::move(notice);
    m_integrationComponentMismatch = componentMismatch;
    emit presentationChanged();
}

void DoctorSessionViewModel::setRedactedDiagnosticReport(QByteArray report)
{
    if (report.size() > 8 * 1024 * 1024) report.clear();
    if (m_redactedDiagnosticReport == report) return;
    m_redactedDiagnosticReport = std::move(report);
    emit presentationChanged();
}

void DoctorSessionViewModel::updateProgressPresentation()
{
    if (!scanRunning()) {
        emit progressPresentationChanged();
        return;
    }
    if (!m_scanClock.isValid()) m_scanClock.start();
    const QList<DiagnosticPlanItem> &items = m_session.plan().items();
    int totalWeight = 0;
    int completedWeight = 0;
    const DiagnosticPlanItem *active = nullptr;
    for (const DiagnosticPlanItem &item : items) {
        totalWeight += item.check.weight.units;
        if (item.status == DoctorCheckStatus::Running) active = &item;
        else if (item.status != DoctorCheckStatus::Waiting) completedWeight += item.check.weight.units;
    }
    const double authoritative = static_cast<double>(overallProgress());
    if (!active || totalWeight <= 0) {
        m_displayProgress = std::max(m_displayProgress, authoritative);
        m_displayCurrentStepProgress = static_cast<double>(currentStepProgress());
        emit progressPresentationChanged();
        return;
    }
    if (m_activePresentationStep != active->stepId.value()) {
        m_activePresentationStep = active->stepId.value();
        m_activeOperationClock.start();
        m_displayCurrentStepProgress = static_cast<double>(active->currentStepProgressPercent);
        // A completed milestone is authoritative. A new operation begins at
        // that stable boundary and can only interpolate its own weight.
        m_displayProgress = std::max(m_displayProgress, 100.0 * completedWeight / totalWeight);
    }
    const double expectedMs = active->check.timeoutMs > 0
        ? std::clamp(static_cast<double>(active->check.timeoutMs) * 0.35, 350.0, 3000.0) : 800.0;
    const double elapsedRatio = m_activeOperationClock.isValid() ? m_activeOperationClock.elapsed() / expectedMs : 0.0;
    const double estimatedFraction = std::min(0.94, std::max(0.0, elapsedRatio / (1.0 + elapsedRatio)) * 1.88);
    const double actualFraction = active->currentStepProgressPercent / 100.0;
    const double activeWeight = static_cast<double>(active->check.weight.units) * 100.0 / totalWeight;
    const double estimate = 100.0 * completedWeight / totalWeight + activeWeight * std::max(actualFraction, estimatedFraction);
    m_displayProgress = std::min(99.9, std::max({m_displayProgress, authoritative, estimate}));
    m_displayCurrentStepProgress = std::min(94.0, std::max(m_displayCurrentStepProgress, std::max(100.0 * actualFraction, 100.0 * estimatedFraction)));
    emit progressPresentationChanged();
}

void DoctorSessionViewModel::copyReportSection(const QString &scope, const QString &format, const QString &privacy)
{
    DoctorReportRequest request;
    request.scope = scope;
    request.format = doctorReportFormatFromString(format);
    request.detail = DoctorReportDetail::Forensic;
    request.privacy = doctorReportPrivacyFromString(privacy);
    const DoctorReportDocument report = DoctorReportComposer::compose(m_session, m_buildIdentity, request);
    const QString text = request.format == DoctorReportFormat::Json ? QString::fromUtf8(report.json)
        : request.format == DoctorReportFormat::PlainText ? QString::fromUtf8(report.plainText) : QString::fromUtf8(report.markdown);
    if (m_copyAction) m_copyAction(text);
    m_reportStatus = QStringLiteral("Copied %1 as %2.").arg(scope, format);
    emit presentationChanged();
}

void DoctorSessionViewModel::copySelectedEvidence(const QString &format, const QString &privacy)
{
    if (m_selectedEvidenceId.isEmpty()) {
        m_reportStatus = QStringLiteral("Select evidence before copying its technical details.");
        emit presentationChanged();
        return;
    }
    DoctorReportRequest request;
    request.scope = QStringLiteral("Selected Evidence");
    request.selectedEvidenceId = m_selectedEvidenceId;
    request.format = doctorReportFormatFromString(format);
    request.detail = DoctorReportDetail::Forensic;
    request.privacy = doctorReportPrivacyFromString(privacy);
    const DoctorReportDocument report = DoctorReportComposer::compose(m_session, m_buildIdentity, request);
    const QString text = request.format == DoctorReportFormat::Json ? QString::fromUtf8(report.json)
        : request.format == DoctorReportFormat::PlainText ? QString::fromUtf8(report.plainText) : QString::fromUtf8(report.markdown);
    if (m_copyAction) m_copyAction(text);
    m_reportStatus = QStringLiteral("Copied selected evidence as %1.").arg(format);
    emit presentationChanged();
}

void DoctorSessionViewModel::copySelectedEvidenceMode(const QString &mode, const QString &privacy)
{
    if (m_selectedEvidenceId.isEmpty()) {
        m_reportStatus = QStringLiteral("Select evidence before copying it.");
        emit presentationChanged();
        return;
    }
    DoctorReportRequest request;
    request.scope = QStringLiteral("Selected Evidence");
    request.selectedEvidenceId = m_selectedEvidenceId;
    request.detail = DoctorReportDetail::Forensic;
    request.privacy = doctorReportPrivacyFromString(privacy);
    const DoctorReportDocument report = DoctorReportComposer::compose(m_session, m_buildIdentity, request);
    const QJsonObject root = QJsonDocument::fromJson(report.json).object();
    const QJsonArray records = root.value(QStringLiteral("evidenceRecords")).toArray();
    const QJsonObject record = records.isEmpty() ? QJsonObject{} : records.at(0).toObject();
    const QString normalized = mode.trimmed().toCaseFolded();
    QString text;
    if (normalized == QStringLiteral("summary")) {
        text = QStringLiteral("%1\n%2\n%3").arg(record.value(QStringLiteral("checkId")).toString(),
            record.value(QStringLiteral("summary")).toString(), record.value(QStringLiteral("statusReason")).toString());
    } else if (normalized == QStringLiteral("technical")) {
        QStringList lines{QStringLiteral("# TECHNICAL EVIDENCE"),
            QStringLiteral("- Check: %1").arg(record.value(QStringLiteral("checkId")).toString()),
            QStringLiteral("- Provider: %1").arg(record.value(QStringLiteral("provider")).toString()),
            QStringLiteral("- Operation: %1").arg(record.value(QStringLiteral("operation")).toString())};
        const QSet<QString> technicalGroups{QStringLiteral("METHOD"), QStringLiteral("TIMING"), QStringLiteral("NATIVE RESULT"), QStringLiteral("TECHNICAL"), QStringLiteral("RAW")};
        for (const QJsonValue &groupValue : record.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject field = groupValue.toObject();
            if (technicalGroups.contains(field.value(QStringLiteral("group")).toString()))
                lines.append(QStringLiteral("- %1: %2").arg(field.value(QStringLiteral("label")).toString(), field.value(QStringLiteral("value")).toString()));
        }
        text = lines.join(QLatin1Char('\n'));
    } else if (normalized == QStringLiteral("json")) {
        text = QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Indented));
    } else {
        text = QString::fromUtf8(report.markdown);
    }
    if (m_copyAction) m_copyAction(text);
    m_reportStatus = QStringLiteral("Copied %1 evidence.").arg(normalized.isEmpty() ? QStringLiteral("complete") : normalized);
    emit presentationChanged();
}

void DoctorSessionViewModel::exportReportUrl(const QUrl &fileUrl, const QString &scope, const QString &detail,
                                             const QString &format, const QString &privacy)
{
    if (!fileUrl.isLocalFile()) {
        m_reportStatus = QStringLiteral("Export failed safely: choose a local report destination.");
        emit presentationChanged();
        return;
    }
    exportReport(fileUrl.toLocalFile(), scope, detail, format, privacy);
}

void DoctorSessionViewModel::exportReport(const QString &fileName, const QString &scope, const QString &detail,
                                          const QString &format, const QString &privacy)
{
    beginAsynchronousExport(fileName, scope, detail, format, privacy);
}

void DoctorSessionViewModel::exportDiagnosticBundleUrl(const QUrl &directoryUrl, const QString &privacy)
{
    if (!directoryUrl.isLocalFile()) {
        m_reportStatus = QStringLiteral("Export failed safely: choose a local diagnostic bundle folder.");
        emit presentationChanged();
        return;
    }
    exportDiagnosticBundle(directoryUrl.toLocalFile(), privacy);
}

void DoctorSessionViewModel::exportDiagnosticBundle(const QString &directory, const QString &privacy)
{
    QString error;
    const QString bundleDirectory = createDiagnosticBundleDirectory(directory, &error);
    if (bundleDirectory.isEmpty()) {
        m_reportStatus = QStringLiteral("Export failed safely: %1").arg(error);
        emit presentationChanged();
        return;
    }
    beginAsynchronousExport(bundleDirectory, QStringLiteral("Entire Session"), QStringLiteral("Forensic / Everything"),
                            QStringLiteral("Diagnostic Bundle"), privacy);
}

void DoctorSessionViewModel::beginAsynchronousExport(QString destination, QString scope, QString detail, QString format, QString privacy)
{
    if (m_reportBusy || destination.trimmed().isEmpty()) return;
    m_reportBusy = true;
    m_reportStatus = QStringLiteral("Collecting report sections…");
    emit presentationChanged();
    const DoctorSession session = m_session;
    const QString buildIdentity = m_buildIdentity;
    const QString reportedDestination = QDir::toNativeSeparators(QFileInfo(destination).absoluteFilePath());
    const auto result = std::make_shared<ExportWriteResult>();
    auto *completionTimer = new QTimer(this);
    completionTimer->setInterval(40);
    QObject::connect(completionTimer, &QTimer::timeout, this, [this, result, format, reportedDestination, completionTimer] {
        if (!result->complete.load(std::memory_order_acquire)) return;
        completionTimer->stop();
        completionTimer->deleteLater();
        m_reportBusy = false;
        m_reportStatus = result->written ? QStringLiteral("%1 export completed locally: %2. Nothing was uploaded.")
            .arg(format, reportedDestination)
            : QStringLiteral("Export failed safely: %1").arg(result->error);
        emit presentationChanged();
    });
    completionTimer->start();
    std::thread([result, session, buildIdentity, destination = std::move(destination), scope = std::move(scope),
                 detail = std::move(detail), format = std::move(format), privacy = std::move(privacy)] {
        DoctorReportRequest request;
        request.scope = scope;
        request.format = doctorReportFormatFromString(format);
        request.detail = doctorReportDetailFromString(detail);
        request.privacy = doctorReportPrivacyFromString(privacy);
        const DoctorReportDocument report = DoctorReportComposer::compose(session, buildIdentity, request);
        result->written = DoctorReportComposer::write(report, destination, request.format, &result->error);
        result->complete.store(true, std::memory_order_release);
    }).detach();
}

} // namespace hotas::doctor
