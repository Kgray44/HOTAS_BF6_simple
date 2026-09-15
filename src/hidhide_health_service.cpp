#include "hidhide_health_service.h"

#include "app_issue.h"

#include <QSet>

#include <algorithm>

namespace hotas {
namespace {

bool isReady(HidHideHealthState state)
{
    return state == HidHideHealthState::Ready;
}

bool isActionable(HidHideRepairability repairability)
{
    return repairability == HidHideRepairability::FixNow
        || repairability == HidHideRepairability::GuidedRepair;
}

HidHideHealthDimension dimension(QString id, QString title, HidHideHealthState state,
                                 HidHideHealthSeverity severity, QString summary,
                                 QString explanation, QStringList checkIds,
                                 HidHideRepairability repairability = HidHideRepairability::None,
                                 QString technicalDetails = {})
{
    return {std::move(id), std::move(title), state, severity, std::move(summary),
            std::move(explanation), std::move(technicalDetails), std::move(checkIds), repairability};
}

QString nativeErrorText(const HidHideReadObservation &check)
{
    if (!check.hasNativeError) return {};
    return QStringLiteral("%1 0x%2 — %3").arg(check.nativeError.domain,
        QString::number(check.nativeError.code, 16).rightJustified(8, QLatin1Char('0')).toUpper(),
        check.nativeError.message);
}

const HidHideReadObservation *findCheck(const QList<HidHideReadObservation> &checks, const QString &operation)
{
    const auto it = std::find_if(checks.cbegin(), checks.cend(), [&operation](const HidHideReadObservation &check) {
        return check.operation == operation;
    });
    return it == checks.cend() ? nullptr : &*it;
}

bool containsExact(const QStringList &values, const QString &expected)
{
    return std::any_of(values.cbegin(), values.cend(), [&expected](const QString &value) {
        return value.compare(expected, Qt::CaseInsensitive) == 0;
    });
}

HidHideHealthFinding findingFrom(const HidHideHealthDimension &value, const QStringList &affected = {})
{
    HidHideHealthFinding finding;
    finding.id = QStringLiteral("hidhide-") + value.id;
    QString stableCodeSuffix = value.id.toUpper();
    stableCodeSuffix.replace(QLatin1Char('-'), QLatin1Char('_'));
    finding.code = QStringLiteral("HIDHIDE_") + stableCodeSuffix;
    finding.dimensionId = value.id;
    finding.severity = value.severity;
    finding.title = value.title;
    finding.explanation = value.shortSummary;
    finding.repairability = value.repairability;
    finding.technicalDetails = value.technicalDetails;
    finding.affectedObjectIds = affected;
    if (value.id == QStringLiteral("application-access")) {
        finding.whyItMatters = QStringLiteral("HidHide may hide the controller from HOTAS BF6 itself, preventing the mapper from reading it.");
    } else if (value.id == QStringLiteral("physical-isolation")) {
        finding.whyItMatters = QStringLiteral("A game may receive both the physical controller and the HOTAS BF6 virtual output, causing duplicate, conflicting, or stuck controls.");
    } else if (value.id == QStringLiteral("virtual-output")) {
        finding.whyItMatters = QStringLiteral("The game may be unable to see the mapped vJoy output.");
    } else if (value.id == QStringLiteral("control-api")) {
        finding.whyItMatters = QStringLiteral("HOTAS BF6 cannot reliably verify HidHide's current configuration, so automatic repair is withheld until the problem is understood.");
    } else {
        finding.whyItMatters = value.explanation;
    }
    return finding;
}

} // namespace

QString hidHideHealthStateLabel(HidHideHealthState state)
{
    switch (state) {
    case HidHideHealthState::Ready: return QStringLiteral("READY");
    case HidHideHealthState::Checking: return QStringLiteral("CHECKING");
    case HidHideHealthState::Degraded: return QStringLiteral("DEGRADED");
    case HidHideHealthState::RepairAvailable: return QStringLiteral("REPAIR AVAILABLE");
    case HidHideHealthState::UserActionRequired: return QStringLiteral("USER ACTION REQUIRED");
    case HidHideHealthState::DoctorRecommended: return QStringLiteral("DOCTOR RECOMMENDED");
    case HidHideHealthState::RestartRequired: return QStringLiteral("RESTART REQUIRED");
    case HidHideHealthState::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

QString hidHideHealthSeverityLabel(HidHideHealthSeverity severity)
{
    switch (severity) {
    case HidHideHealthSeverity::Info: return QStringLiteral("info");
    case HidHideHealthSeverity::Warning: return QStringLiteral("warning");
    case HidHideHealthSeverity::Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

QString hidHideRepairabilityLabel(HidHideRepairability repairability)
{
    switch (repairability) {
    case HidHideRepairability::None: return QStringLiteral("none");
    case HidHideRepairability::FixNow: return QStringLiteral("fix-now");
    case HidHideRepairability::GuidedRepair: return QStringLiteral("guided-repair");
    case HidHideRepairability::UserActionRequired: return QStringLiteral("user-action-required");
    case HidHideRepairability::DoctorRecommended: return QStringLiteral("doctor-recommended");
    case HidHideRepairability::DoctorRequired: return QStringLiteral("doctor-required");
    }
    return QStringLiteral("none");
}

QString hidHideHealthScanDepthLabel(HidHideHealthScanDepth depth)
{
    return depth == HidHideHealthScanDepth::Full ? QStringLiteral("FULL") : QStringLiteral("ESSENTIAL");
}

QVariantMap HidHideHealthDimension::toVariantMap() const
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("title"), title},
            {QStringLiteral("state"), hidHideHealthStateLabel(state)},
            {QStringLiteral("severity"), hidHideHealthSeverityLabel(severity)},
            {QStringLiteral("shortSummary"), shortSummary}, {QStringLiteral("explanation"), explanation},
            {QStringLiteral("technicalDetails"), technicalDetails}, {QStringLiteral("checkIds"), checkIds},
            {QStringLiteral("repairability"), hidHideRepairabilityLabel(repairability)}};
}

QVariantMap HidHideHealthFinding::toVariantMap() const
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("code"), code},
            {QStringLiteral("dimensionId"), dimensionId},
            {QStringLiteral("severity"), hidHideHealthSeverityLabel(severity)}, {QStringLiteral("title"), title},
            {QStringLiteral("explanation"), explanation}, {QStringLiteral("whyItMatters"), whyItMatters},
            {QStringLiteral("repairability"), hidHideRepairabilityLabel(repairability)},
            {QStringLiteral("technicalDetails"), technicalDetails},
            {QStringLiteral("affectedObjectIds"), affectedObjectIds}};
}

QVariantMap HidHideHealthActivity::toVariantMap() const
{
    return {{QStringLiteral("timestamp"), timestamp.toString(Qt::ISODateWithMs)},
            {QStringLiteral("event"), event}, {QStringLiteral("detail"), detail}};
}

QVariantMap HidHideHealthSnapshot::toVariantMap() const
{
    QVariantList dimensionsModel;
    for (const HidHideHealthDimension &dimension : dimensions) dimensionsModel.append(dimension.toVariantMap());
    QVariantList checksModel;
    for (const HidHideReadObservation &check : checks) {
        QVariantMap value{{QStringLiteral("id"), check.id}, {QStringLiteral("operation"), check.operation},
            {QStringLiteral("state"), hidHideReadStateLabel(check.state)}, {QStringLiteral("summary"), check.summary},
            {QStringLiteral("value"), check.value}, {QStringLiteral("values"), check.values},
            {QStringLiteral("durationMs"), check.durationMs}, {QStringLiteral("sizeNegotiation"), check.sizeNegotiation}};
        if (check.hasNativeError) value.insert(QStringLiteral("nativeError"),
            QVariantMap{{QStringLiteral("domain"), check.nativeError.domain}, {QStringLiteral("code"), check.nativeError.code},
                        {QStringLiteral("operation"), check.nativeError.operation}, {QStringLiteral("message"), check.nativeError.message}});
        checksModel.append(value);
    }
    QVariantList findingsModel;
    for (const HidHideHealthFinding &finding : findings) findingsModel.append(finding.toVariantMap());
    QVariantList activityModel;
    for (const HidHideHealthActivity &event : activity) activityModel.append(event.toVariantMap());
    return {{QStringLiteral("sessionId"), QVariant::fromValue(sessionId)}, {QStringLiteral("contextKey"), contextKey},
            {QStringLiteral("scanDepth"), hidHideHealthScanDepthLabel(scanDepth)},
            {QStringLiteral("overallState"), hidHideHealthStateLabel(overallState)},
            {QStringLiteral("lastChecked"), lastChecked.toString(Qt::ISODateWithMs)}, {QStringLiteral("inProgress"), inProgress},
            {QStringLiteral("cancelled"), cancelled}, {QStringLiteral("checksCompleted"), checksCompleted},
            {QStringLiteral("checksTotal"), checksTotal}, {QStringLiteral("currentStage"), currentStage},
            {QStringLiteral("dimensions"), dimensionsModel}, {QStringLiteral("checks"), checksModel},
            {QStringLiteral("findings"), findingsModel}, {QStringLiteral("activity"), activityModel}};
}

HidHideHealthService::HidHideHealthService(ReadOnlyProbe probe)
    : m_probe(std::move(probe))
{
}

HidHideHealthSnapshot HidHideHealthService::checkingSnapshot(const HidHideHealthContext &context,
                                                              HidHideHealthScanDepth depth)
{
    HidHideHealthSnapshot snapshot;
    snapshot.sessionId = context.sessionId;
    snapshot.contextKey = context.contextKey;
    snapshot.scanDepth = depth;
    snapshot.overallState = HidHideHealthState::Checking;
    snapshot.inProgress = true;
    snapshot.checksTotal = depth == HidHideHealthScanDepth::Full ? 5 : 9;
    snapshot.currentStage = depth == HidHideHealthScanDepth::Full
        ? QStringLiteral("Opening the HidHide control interface")
        : QStringLiteral("Checking HidHide readiness evidence");
    return snapshot;
}

HidHideHealthSnapshot HidHideHealthService::inspect(const HidHideHealthContext &context,
                                                     HidHideHealthScanDepth depth,
                                                     std::atomic_bool *cancelled) const
{
    HidHideHealthSnapshot snapshot = checkingSnapshot(context, depth);
    snapshot.inProgress = false;
    snapshot.lastChecked = QDateTime::currentDateTime();
    if (depth == HidHideHealthScanDepth::Full && m_probe && (!cancelled || !cancelled->load())) {
        snapshot.checks = m_probe(cancelled);
    }
    snapshot.cancelled = cancelled && cancelled->load();
    snapshot.checksCompleted = depth == HidHideHealthScanDepth::Full
        ? static_cast<int>(snapshot.checks.size()) : snapshot.checksTotal;
    snapshot.currentStage = snapshot.cancelled ? QStringLiteral("Full check cancelled")
        : QStringLiteral("HidHide health evaluation complete");

    const HidHideReadObservation *control = findCheck(snapshot.checks, QStringLiteral("OPEN_CONTROL"));
    const HidHideReadObservation *active = findCheck(snapshot.checks, QStringLiteral("GET_ACTIVE"));
    const HidHideReadObservation *whitelist = findCheck(snapshot.checks, QStringLiteral("GET_WHITELIST"));
    const HidHideReadObservation *blacklist = findCheck(snapshot.checks, QStringLiteral("GET_BLACKLIST"));

    const bool directControlHealthy = control && control->state == HidHideReadState::Pass;
    const bool installationEvidence = context.installed || (context.serviceReady && directControlHealthy);
    snapshot.dimensions.append(installationEvidence
        ? dimension(QStringLiteral("installation"), QStringLiteral("Installation"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("HidHide installation evidence is available."),
                    QStringLiteral("HOTAS BF6 can inspect the installed HidHide components."),
                    {QStringLiteral("HD-INST-001")})
        : dimension(QStringLiteral("installation"), QStringLiteral("Installation"), HidHideHealthState::DoctorRecommended,
                    HidHideHealthSeverity::Warning, QStringLiteral("HidHide is not available to HOTAS BF6."),
                    QStringLiteral("No safe in-app package or service repair is available."),
                    {QStringLiteral("HD-INST-001"), QStringLiteral("HD-DRV-003")},
                    HidHideRepairability::DoctorRequired));

    const bool fullPackageEvidence = depth == HidHideHealthScanDepth::Full && context.cliAvailable && context.serviceReady;
    const bool packageInspectionRequested = depth == HidHideHealthScanDepth::Full;
    snapshot.dimensions.append(dimension(QStringLiteral("package-version"), QStringLiteral("Package and version consistency"),
        fullPackageEvidence ? HidHideHealthState::Ready : HidHideHealthState::Unknown,
        packageInspectionRequested && !fullPackageEvidence ? HidHideHealthSeverity::Warning : HidHideHealthSeverity::Info,
        fullPackageEvidence ? QStringLiteral("No package mutation was attempted.")
                            : packageInspectionRequested ? QStringLiteral("Package evidence is incomplete.")
                                                         : QStringLiteral("Run a Full Check for bounded package evidence."),
        QStringLiteral("Version/package repair is intentionally deferred to HidHide Doctor."),
        {QStringLiteral("HD-PKG-005")},
        packageInspectionRequested && !fullPackageEvidence
            ? HidHideRepairability::DoctorRecommended : HidHideRepairability::None));

    snapshot.dimensions.append(context.serviceReady
        ? dimension(QStringLiteral("kernel-driver"), QStringLiteral("Kernel driver"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("The HidHide service is available."),
                    QStringLiteral("The existing readiness system reported service availability."),
                    {QStringLiteral("HD-DRV-003")})
        : dimension(QStringLiteral("kernel-driver"), QStringLiteral("Kernel driver"), HidHideHealthState::DoctorRecommended,
                    HidHideHealthSeverity::Warning, QStringLiteral("The HidHide service is unavailable."),
                    QStringLiteral("HOTAS BF6 will not recreate services or alter driver registration."),
                    {QStringLiteral("HD-DRV-003")}, HidHideRepairability::DoctorRequired));

    if (depth == HidHideHealthScanDepth::Full && control) {
        const bool healthy = control->state == HidHideReadState::Pass;
        const QString detail = nativeErrorText(*control);
        snapshot.dimensions.append(dimension(QStringLiteral("control-api"), QStringLiteral("Control API"),
            healthy ? HidHideHealthState::Ready : HidHideHealthState::DoctorRecommended,
            healthy ? HidHideHealthSeverity::Info : HidHideHealthSeverity::Warning,
            healthy ? QStringLiteral("Direct read-only control access succeeded.")
                    : QStringLiteral("The direct HidHide control interface could not be verified."),
            healthy ? QStringLiteral("Independent GET probes are reported separately below.")
                    : QStringLiteral("Other HidHide evidence is retained; no automatic repair is offered for an API fault."),
            {QStringLiteral("HD-API-001")}, healthy ? HidHideRepairability::None : HidHideRepairability::DoctorRecommended,
            detail));
    } else {
        snapshot.dimensions.append(dimension(QStringLiteral("control-api"), QStringLiteral("Control API"),
            context.inspectionComplete ? HidHideHealthState::Ready : HidHideHealthState::Unknown,
            context.inspectionComplete ? HidHideHealthSeverity::Info : HidHideHealthSeverity::Warning,
            context.inspectionComplete ? QStringLiteral("Existing setup inspection completed.")
                                       : QStringLiteral("Direct protocol inspection has not run yet."),
            QStringLiteral("Run a Full Check to inspect the control endpoint independently."),
            {QStringLiteral("HD-API-001")}, context.inspectionComplete
                ? HidHideRepairability::None : HidHideRepairability::DoctorRecommended));
    }

    const bool directWhitelistObserved = depth == HidHideHealthScanDepth::Full && whitelist;
    const bool directWhitelistFailed = directWhitelistObserved && whitelist->state != HidHideReadState::Pass;
    const bool directWhitelistKnown = whitelist && whitelist->state == HidHideReadState::Pass;
    // Once a Full Check has actually returned a whitelist result, it is the
    // authoritative evidence for this snapshot.  A failed GET must remain an
    // Unknown result, never be disguised by an older readiness observation.
    const bool mapperAllowed = directWhitelistKnown
        ? (!context.mapperExecutable.isEmpty() && containsExact(whitelist->values, context.mapperExecutable))
        : (!directWhitelistObserved && context.mapperAllowlisted);
    const bool accessKnown = directWhitelistObserved ? directWhitelistKnown : context.mapperAllowlistKnown;
    snapshot.dimensions.append(!accessKnown
        ? dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Warning, QStringLiteral("HOTAS BF6 could not confirm its HidHide exemption."),
                    QStringLiteral("Automatic repair is withheld until the application-list evidence is readable."),
                    {QStringLiteral("HD-CFG-003")}, HidHideRepairability::DoctorRecommended,
                    directWhitelistFailed ? nativeErrorText(*whitelist) : QString())
        : mapperAllowed
            ? dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::Ready,
                        HidHideHealthSeverity::Info, QStringLiteral("HOTAS BF6 is permitted by HidHide."),
                        QStringLiteral("The mapper can retain access to its selected physical controller."),
                        {QStringLiteral("HD-ISO-003"), QStringLiteral("HD-CFG-003")})
            : dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::RepairAvailable,
                        HidHideHealthSeverity::Warning, QStringLiteral("HOTAS BF6 is not in HidHide's allowed application list."),
                        QStringLiteral("The existing tested mapper-only allowlist transaction can add exactly the running HOTAS BF6 executable."),
                        {QStringLiteral("HD-ISO-003"), QStringLiteral("HD-CFG-003")}, HidHideRepairability::FixNow));

    const bool isolationKnown = context.hiddenDeviceListKnown && context.selectedControllerResolved;
    snapshot.dimensions.append(context.expectedPhysicalInstances.isEmpty()
        ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Info, QStringLiteral("No exact selected physical HID identity is available."),
                    QStringLiteral("HOTAS BF6 will not guess a device from a friendly name or hide an unresolved device."),
                    {QStringLiteral("HD-ISO-006")})
        : !isolationKnown
            ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::Unknown,
                        HidHideHealthSeverity::Warning, QStringLiteral("Physical-device visibility could not be verified."),
                        QStringLiteral("Run a Full Check or open HidHide Doctor; no hide operation is proposed from incomplete identity evidence."),
                        {QStringLiteral("HD-ISO-006")}, HidHideRepairability::DoctorRecommended)
            : context.selectedControllerHidden
                ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::Ready,
                            HidHideHealthSeverity::Info, QStringLiteral("The exact selected physical controller is hidden from games."),
                            QStringLiteral("HidHide isolation uses the current exact HID identity."), {QStringLiteral("HD-ISO-006")})
                : dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::RepairAvailable,
                            HidHideHealthSeverity::Warning, QStringLiteral("One selected physical controller is visible to games."),
                            QStringLiteral("The existing validated physical-input visibility transaction can be reviewed before it changes only this exact controller."),
                            {QStringLiteral("HD-ISO-006")}, HidHideRepairability::GuidedRepair));

    snapshot.dimensions.append(context.managedVirtualOutputInstances.isEmpty()
        ? dimension(QStringLiteral("virtual-output"), QStringLiteral("Virtual-output visibility"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Info, QStringLiteral("No exact managed vJoy HID identity is available."),
                    QStringLiteral("HOTAS BF6 does not infer virtual-output visibility from a display name."),
                    {QStringLiteral("HD-ISO-008")})
        : !context.managedVirtualOutputInspectionKnown
            ? dimension(QStringLiteral("virtual-output"), QStringLiteral("Virtual-output visibility"), HidHideHealthState::Unknown,
                        HidHideHealthSeverity::Warning, QStringLiteral("Managed vJoy visibility could not be verified."),
                        QStringLiteral("HOTAS BF6 will not change a virtual output until its exact current identity is verified."),
                        {QStringLiteral("HD-ISO-008")}, HidHideRepairability::DoctorRecommended)
            : context.managedVirtualOutputHidden
                ? dimension(QStringLiteral("virtual-output"), QStringLiteral("Virtual-output visibility"), HidHideHealthState::RepairAvailable,
                            HidHideHealthSeverity::Warning, QStringLiteral("A managed vJoy output is hidden from games."),
                            QStringLiteral("The existing managed-output visibility transaction can restore only the exact validated vJoy HID identity."),
                            {QStringLiteral("HD-ISO-008")}, HidHideRepairability::GuidedRepair)
                : dimension(QStringLiteral("virtual-output"), QStringLiteral("Virtual-output visibility"), HidHideHealthState::Ready,
                            HidHideHealthSeverity::Info, QStringLiteral("Managed vJoy output is visible to games."),
                            QStringLiteral("The exact managed virtual-output identity is not hidden by HidHide."),
                            {QStringLiteral("HD-ISO-008")}));

    snapshot.dimensions.append(context.selectedControllerResolved
        ? dimension(QStringLiteral("device-enumeration"), QStringLiteral("Device enumeration"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("The selected physical controller resolved to an exact identity."),
                    QStringLiteral("Exact identities, not friendly-name guesses, are used for all visibility decisions."),
                    {QStringLiteral("HD-DEV-001")})
        : dimension(QStringLiteral("device-enumeration"), QStringLiteral("Device enumeration"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Warning, QStringLiteral("The selected physical controller did not resolve to an exact HidHide identity."),
                    QStringLiteral("No physical-device mutation is available until the identity is resolved."),
                    {QStringLiteral("HD-DEV-001")}));

    snapshot.dimensions.append(context.pendingReadinessRecovery
        ? dimension(QStringLiteral("restart-state"), QStringLiteral("Restart and recovery state"), HidHideHealthState::UserActionRequired,
                    HidHideHealthSeverity::Warning, QStringLiteral("A prior setup transaction still requires physical-controller recovery verification."),
                    QStringLiteral("Reconnect and DirectInput report proof remain required before setup can be declared safe."),
                    {QStringLiteral("HD-ISO-013")}, HidHideRepairability::UserActionRequired)
        : dimension(QStringLiteral("restart-state"), QStringLiteral("Restart and recovery state"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("No pending HOTAS BF6 recovery state is known."),
                    QStringLiteral("This does not infer package-level pending replacement state."), {QStringLiteral("HD-ISO-013")}));

    for (const HidHideHealthDimension &value : snapshot.dimensions) {
        // An unknown result is actionable evidence when it represents an
        // incomplete safety decision.  Purely informational unknowns (for
        // example, no managed virtual-output identity in this Rig) stay out
        // of App Health so they cannot masquerade as a fault.
        if (!isReady(value.state)
            && !(value.state == HidHideHealthState::Unknown
                 && value.severity == HidHideHealthSeverity::Info)) {
            snapshot.findings.append(findingFrom(value, value.id == QStringLiteral("physical-isolation")
                ? context.expectedPhysicalInstances : QStringList{}));
        }
    }

    const bool userAction = std::any_of(snapshot.dimensions.cbegin(), snapshot.dimensions.cend(), [](const HidHideHealthDimension &value) {
        return value.state == HidHideHealthState::UserActionRequired || value.state == HidHideHealthState::RestartRequired;
    });
    const bool repair = std::any_of(snapshot.dimensions.cbegin(), snapshot.dimensions.cend(), [](const HidHideHealthDimension &value) {
        return isActionable(value.repairability);
    });
    const bool doctor = std::any_of(snapshot.dimensions.cbegin(), snapshot.dimensions.cend(), [](const HidHideHealthDimension &value) {
        return value.repairability == HidHideRepairability::DoctorRequired || value.repairability == HidHideRepairability::DoctorRecommended;
    });
    const bool degraded = std::any_of(snapshot.dimensions.cbegin(), snapshot.dimensions.cend(), [](const HidHideHealthDimension &value) {
        return value.state == HidHideHealthState::Degraded;
    });
    const bool unknown = std::any_of(snapshot.dimensions.cbegin(), snapshot.dimensions.cend(), [](const HidHideHealthDimension &value) {
        return value.state == HidHideHealthState::Unknown && value.severity != HidHideHealthSeverity::Info;
    });
    snapshot.overallState = snapshot.cancelled ? HidHideHealthState::Unknown
        : userAction ? HidHideHealthState::UserActionRequired
        : repair ? HidHideHealthState::RepairAvailable
        : doctor ? HidHideHealthState::DoctorRecommended
        : degraded ? HidHideHealthState::Degraded
        : unknown ? HidHideHealthState::Unknown : HidHideHealthState::Ready;
    return snapshot;
}

QVariantList HidHideHealthService::appIssues(const HidHideHealthSnapshot &snapshot)
{
    QVariantList issues;
    for (const HidHideHealthFinding &finding : snapshot.findings) {
        if (finding.severity == HidHideHealthSeverity::Info) continue;
        AppIssue issue;
        issue.id = finding.id;
        issue.code = finding.code;
        issue.category = QStringLiteral("HidHide Health");
        issue.severity = hidHideHealthSeverityLabel(finding.severity);
        issue.scopeType = QStringLiteral("hidhide");
        issue.scopeId = finding.dimensionId;
        issue.affectedObjectType = QStringLiteral("hidhideDimension");
        issue.affectedObjectId = finding.dimensionId;
        issue.affectedObjectIds = finding.affectedObjectIds;
        issue.title = finding.title;
        issue.explanation = finding.explanation + QStringLiteral("\n\nWhy this matters: ") + finding.whyItMatters;
        issue.recommendedAction = hidHideRepairabilityLabel(finding.repairability);
        issue.recommendedActionLabel = finding.repairability == HidHideRepairability::FixNow
            ? QStringLiteral("FIX NOW") : finding.repairability == HidHideRepairability::GuidedRepair
                ? QStringLiteral("REVIEW & REPAIR") : QStringLiteral("OPEN HIDHIDE DOCTOR");
        issue.automaticallyFixable = isActionable(finding.repairability);
        issue.priority = finding.dimensionId == QStringLiteral("physical-isolation") ? 30 : 80;
        issue.technicalDetails = finding.technicalDetails;
        issue.navigationTarget = {{QStringLiteral("page"), QStringLiteral("devices")},
                                  {QStringLiteral("section"), QStringLiteral("hidhide-health")}};
        issues.append(issue.toVariantMap());
    }
    return issues;
}

} // namespace hotas
