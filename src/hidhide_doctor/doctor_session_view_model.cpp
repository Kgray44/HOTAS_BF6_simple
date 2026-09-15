#include "doctor_session_view_model.h"

#include <QSettings>

#include <algorithm>
#include <array>
#include <cmath>

namespace hotas::doctor {
namespace {

constexpr int kPresentationLayoutVersion = 2;
constexpr std::array<double, 4> kDefaultPaneFractions{0.22, 0.265, 0.36, 0.155};
constexpr double kMinimumPersistedPaneFraction = 0.08;
constexpr double kFractionSumTolerance = 0.015;

QString presentationKey(const QString &suffix)
{
    return QStringLiteral("hidhideDoctorPhase2/") + suffix;
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
    QSettings settings;
    m_commandCenter = settings.value(presentationKey(QStringLiteral("commandCenter")), true).toBool();
    const QString storedDensity = settings.value(presentationKey(QStringLiteral("density")), QStringLiteral("Compact")).toString();
    m_density = (storedDensity == QStringLiteral("Comfortable") || storedDensity == QStringLiteral("Dense")) ? storedDensity : QStringLiteral("Compact");
    m_liveEvidenceVisible = settings.value(presentationKey(QStringLiteral("liveEvidence")), false).toBool();
    const bool currentSchema = settings.value(presentationKey(QStringLiteral("layoutSchemaVersion")), 0).toInt() == kPresentationLayoutVersion;
    m_paneFractions = currentSchema
        ? normalizedPaneFractions(settings.value(presentationKey(QStringLiteral("paneFractions"))).toList())
        : defaultPaneFractions();
    if (!currentSchema || m_paneFractions != settings.value(presentationKey(QStringLiteral("paneFractions"))).toList()) {
        settings.setValue(presentationKey(QStringLiteral("layoutSchemaVersion")), kPresentationLayoutVersion);
        settings.setValue(presentationKey(QStringLiteral("paneFractions")), m_paneFractions);
        settings.remove(presentationKey(QStringLiteral("paneWidths")));
    }
}

QString DoctorSessionViewModel::buildIdentity() const { return m_buildIdentity; }
QString DoctorSessionViewModel::sessionId() const { return m_session.id().value(); }
QString DoctorSessionViewModel::currentPhase() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) if (item.status == DoctorCheckStatus::Running) return displayName(item.check.phase);
    return m_session.state() == DoctorSessionState::DiagnosisComplete ? QStringLiteral("Read-only analysis complete")
        : (m_session.state() == DoctorSessionState::Cancelled ? QStringLiteral("Diagnostic scan cancelled") : QStringLiteral("Preparing diagnostic scan"));
}
QString DoctorSessionViewModel::currentStep() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) if (item.status == DoctorCheckStatus::Running) return item.check.title;
    return m_session.state() == DoctorSessionState::DiagnosisComplete ? QStringLiteral("All applicable read-only checks completed") : QStringLiteral("No native operation is currently running");
}
QString DoctorSessionViewModel::currentStepId() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) if (item.status == DoctorCheckStatus::Running) return item.check.id.value();
    return {};
}
int DoctorSessionViewModel::overallProgress() const { return m_session.plan().progress().overallPercent; }
int DoctorSessionViewModel::currentStepProgress() const { return m_session.plan().progress().currentStepPercent; }
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
QString DoctorSessionViewModel::elapsed() const { return elapsedText(m_session.createdAt().msecsTo(QDateTime::currentDateTimeUtc())); }
bool DoctorSessionViewModel::commandCenter() const { return m_commandCenter; }
QString DoctorSessionViewModel::density() const { return m_density; }
QString DoctorSessionViewModel::sessionState() const { return sessionStateName(m_session.state()); }
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
        values.append(QVariantMap{{QStringLiteral("label"), domain.first}, {QStringLiteral("status"), displayName(status).toUpper()},
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
        {QStringLiteral("title"), event.title}, {QStringLiteral("detail"), event.detail}, {QStringLiteral("tone"), toneFor(event.status)}, {QStringLiteral("evidenceId"), event.evidenceId.value()}});
    return values;
}

QVariantList DoctorSessionViewModel::evidenceRows() const
{
    QVariantList values;
    for (const EvidenceRecord &evidence : m_session.evidence()) values.append(QVariantMap{{QStringLiteral("id"), evidence.id.value()}, {QStringLiteral("checkId"), evidence.checkId.value()},
        {QStringLiteral("source"), evidence.source}, {QStringLiteral("summary"), evidence.humanSummary}, {QStringLiteral("technical"), evidence.technicalDetails},
        {QStringLiteral("duration"), QStringLiteral("%1 ms").arg(evidence.durationMs)}, {QStringLiteral("error"), evidence.nativeError ? QStringLiteral("%1 / 0x%2").arg(evidence.nativeError->symbolicName).arg(evidence.nativeError->code, 0, 16).toUpper() : QString()},
        {QStringLiteral("timestamp"), evidence.recordedAt.toLocalTime().toString(Qt::ISODateWithMs)}});
    return values;
}

QVariantMap DoctorSessionViewModel::selectedEvidence() const
{
    for (const EvidenceRecord &evidence : m_session.evidence()) {
        if (evidence.id.value() != m_selectedEvidenceId) continue;
        return QVariantMap{{QStringLiteral("id"), evidence.id.value()}, {QStringLiteral("checkId"), evidence.checkId.value()}, {QStringLiteral("source"), evidence.source},
            {QStringLiteral("summary"), evidence.humanSummary}, {QStringLiteral("technical"), evidence.technicalDetails}, {QStringLiteral("structured"), evidence.structuredValue},
            {QStringLiteral("duration"), QStringLiteral("%1 ms").arg(evidence.durationMs)}, {QStringLiteral("timestamp"), evidence.recordedAt.toLocalTime().toString(Qt::ISODateWithMs)},
            {QStringLiteral("provenance"), evidence.provenance == EvidenceProvenance::Direct ? QStringLiteral("Direct observation") : QStringLiteral("Derived correlation")},
            {QStringLiteral("error"), evidence.nativeError ? QStringLiteral("%1 (%2 / 0x%3)").arg(evidence.nativeError->symbolicName).arg(evidence.nativeError->code).arg(evidence.nativeError->code, 0, 16).toUpper() : QStringLiteral("None")}};
    }
    return {};
}

QVariantMap DoctorSessionViewModel::currentOperationDetails() const
{
    const std::optional<DoctorOperation> operation = m_session.currentOperation();
    return QVariantMap{{QStringLiteral("phase"), currentPhase()}, {QStringLiteral("checkId"), currentStepId()}, {QStringLiteral("title"), currentStep()},
        {QStringLiteral("status"), operation ? displayName(operation->state == DoctorOperationState::Running ? DoctorCheckStatus::Running : DoctorCheckStatus::Healthy).toUpper() : sessionState()},
        {QStringLiteral("progress"), currentStepProgress()}, {QStringLiteral("timeout"), operation && operation->timeoutMs ? QStringLiteral("%1 s").arg(operation->timeoutMs / 1000.0, 0, 'f', 2) : QStringLiteral("Not applicable")},
        {QStringLiteral("elapsed"), elapsed()}, {QStringLiteral("detail"), scanRunning() ? QStringLiteral("No user action required. The Doctor is using bounded read-only operations.") : QStringLiteral("The last operation has completed; select evidence for its recorded details.")}};
}

int DoctorSessionViewModel::healthyCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Healthy; }); }
int DoctorSessionViewModel::informationalCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Informational; }); }
int DoctorSessionViewModel::warningCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Warning || result.status == DoctorCheckStatus::PermissionLimited || result.status == DoctorCheckStatus::Unknown || result.status == DoctorCheckStatus::Inconclusive; }); }
int DoctorSessionViewModel::failedCheckCount() const { return std::count_if(m_session.checkResults().cbegin(), m_session.checkResults().cend(), [](const DoctorCheckResult &result) { return result.status == DoctorCheckStatus::Failed || result.status == DoctorCheckStatus::TimedOut || result.status == DoctorCheckStatus::Blocked; }); }
bool DoctorSessionViewModel::liveEvidenceVisible() const { return m_liveEvidenceVisible; }
QString DoctorSessionViewModel::maximizedPane() const { return m_maximizedPane; }
QVariantList DoctorSessionViewModel::paneFractions() const { return m_paneFractions; }

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
void DoctorSessionViewModel::resetWorkspaceLayout()
{
    m_commandCenter = true;
    m_density = QStringLiteral("Compact");
    m_liveEvidenceVisible = false;
    m_maximizedPane.clear();
    m_paneFractions = defaultPaneFractions();
    QSettings settings;
    settings.setValue(presentationKey(QStringLiteral("commandCenter")), m_commandCenter);
    settings.setValue(presentationKey(QStringLiteral("density")), m_density);
    settings.setValue(presentationKey(QStringLiteral("liveEvidence")), m_liveEvidenceVisible);
    settings.setValue(presentationKey(QStringLiteral("layoutSchemaVersion")), kPresentationLayoutVersion);
    settings.setValue(presentationKey(QStringLiteral("paneFractions")), m_paneFractions);
    settings.remove(presentationKey(QStringLiteral("paneWidths")));
    emit presentationChanged();
}
void DoctorSessionViewModel::selectEvidence(const QString &evidenceId)
{
    if (m_selectedEvidenceId == evidenceId) return;
    m_selectedEvidenceId = evidenceId;
    emit sessionChanged();
}
void DoctorSessionViewModel::notifySessionChanged() { emit sessionChanged(); }
void DoctorSessionViewModel::requestCancellation() { if (m_cancellation) m_cancellation(); }
void DoctorSessionViewModel::requestRerun() { if (m_rerun) m_rerun(); }
void DoctorSessionViewModel::replaceSession(DoctorSession session)
{
    m_session = std::move(session);
    if (m_selectedEvidenceId.isEmpty() && !m_session.evidence().isEmpty()) m_selectedEvidenceId = m_session.evidence().last().id.value();
    emit sessionChanged();
}
void DoctorSessionViewModel::setScanActions(std::function<void()> cancellation, std::function<void()> rerun)
{
    m_cancellation = std::move(cancellation);
    m_rerun = std::move(rerun);
}

} // namespace hotas::doctor
