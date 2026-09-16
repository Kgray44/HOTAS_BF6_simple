#include "hidhide_health_service.h"

#include "app_issue.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winver.h>
#endif

namespace hotas {
namespace {

// A timeout opening the read-only control endpoint can be a transient service
// transition. Retrying only that bounded observation once is useful; retrying
// every later GET would turn one delayed response into a long serial wait and
// hide the independent evidence already collected.
constexpr int kDirectControlOpenRetryLimit = 1;
constexpr unsigned long kDirectControlOpenRetryDelayMs = 125;

QString fileVersion(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(native.c_str(), &ignored);
    if (!size) return {};
    QByteArray bytes(static_cast<int>(size), Qt::Uninitialized);
    if (!GetFileVersionInfoW(native.c_str(), 0, size, bytes.data())) return {};
    VS_FIXEDFILEINFO *info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<LPVOID *>(&info), &length)
        || !info || length < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xfeef04bd) return {};
    return QStringLiteral("%1.%2.%3.%4")
        .arg(HIWORD(info->dwFileVersionMS)).arg(LOWORD(info->dwFileVersionMS))
        .arg(HIWORD(info->dwFileVersionLS)).arg(LOWORD(info->dwFileVersionLS));
#else
    Q_UNUSED(path);
    return {};
#endif
}

void collectBoundedPackageEvidence(HidHideHealthContext *context)
{
    if (!context || context->packageEvidenceInspected) return;
    context->packageEvidenceInspected = true;
    const QStringList roots{qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
                            QStringLiteral("C:/Program Files")};
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString base = QDir(root).filePath(QStringLiteral("Nefarius Software Solutions/HidHide/x64"));
        const QString client = QDir(base).filePath(QStringLiteral("HidHideClient.exe"));
        const QString cli = QDir(base).filePath(QStringLiteral("HidHideCLI.exe"));
        if (context->clientVersion.isEmpty() && QFileInfo(client).isFile()) context->clientVersion = fileVersion(client);
        if (context->cliVersion.isEmpty() && QFileInfo(cli).isFile()) context->cliVersion = fileVersion(cli);
    }
    const QString windowsRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QString driver = QDir(windowsRoot).filePath(QStringLiteral("System32/drivers/HidHide.sys"));
    if (context->onDiskDriverVersion.isEmpty() && QFileInfo(driver).isFile()) {
        context->onDiskDriverVersion = fileVersion(driver);
    }
    const QDir store(QDir(windowsRoot).filePath(QStringLiteral("System32/DriverStore/FileRepository")));
    const QFileInfoList candidates = store.entryInfoList({QStringLiteral("*hidhide*"), QStringLiteral("*HidHide*")},
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &candidate : candidates) {
        const QStringList infs = QDir(candidate.absoluteFilePath()).entryList({QStringLiteral("*.inf")}, QDir::Files);
        for (const QString &infName : infs) {
            QFile inf(QDir(candidate.absoluteFilePath()).filePath(infName));
            if (!inf.open(QIODevice::ReadOnly)) continue;
            const QString text = QString::fromUtf8(inf.read(64 * 1024));
            const auto match = QRegularExpression(QStringLiteral("^\\s*DriverVer\\s*=\\s*(.+?)\\s*$"),
                QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption).match(text);
            if (!match.hasMatch()) continue;
            const HidHideDriverPackageEvidence evidence{
                candidate.fileName() + QLatin1Char('/') + infName,
                match.captured(1).trimmed()};
            const bool duplicate = std::any_of(context->driverStorePackageCandidates.cbegin(),
                context->driverStorePackageCandidates.cend(), [&evidence](const HidHideDriverPackageEvidence &existing) {
                    return existing.packageId.compare(evidence.packageId, Qt::CaseInsensitive) == 0;
                });
            if (!duplicate) context->driverStorePackageCandidates.append(evidence);
        }
    }
#ifdef Q_OS_WIN
    QSettings sessionManager(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager"),
                             QSettings::NativeFormat);
    context->pendingPackageRestartKnown = true;
    const QVariant pending = sessionManager.value(QStringLiteral("PendingFileRenameOperations"));
    const QStringList entries = pending.toStringList();
    context->pendingPackageRestart = std::any_of(entries.cbegin(), entries.cend(), [](const QString &entry) {
        return entry.contains(QStringLiteral("hidhide"), Qt::CaseInsensitive);
    });
#endif
}

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

QString normalizedInstance(QString value)
{
    value = value.trimmed();
    value.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (value.startsWith(QStringLiteral("\\\\?\\"))) value.remove(0, 4);
    return value.toUpper();
}

bool containsNormalized(const QStringList &values, const QString &expected)
{
    const QString normalized = normalizedInstance(expected);
    return !normalized.isEmpty() && std::any_of(values.cbegin(), values.cend(), [&normalized](const QString &value) {
        return normalizedInstance(value) == normalized;
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

QString sanitizeEvidenceText(QString text)
{
    // The health report is shareable.  Scrub identity-bearing HidHide device
    // strings and filesystem/UNC paths wherever they appear, including an
    // unexpected native-error payload or a future nested diagnostic field.
    static const QRegularExpression hidPattern(
        QStringLiteral("(?:\\\\\\\\\\?\\\\)?HID[\\\\/][^\\s,;\\\"{}\\[\\]]+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pathPattern(
        QStringLiteral("(?:[A-Z]:[\\\\/]|\\\\\\\\)[^\\s,;\\\"{}\\[\\]]+"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(hidPattern, QStringLiteral("[redacted HID identity]"));
    text.replace(pathPattern, QStringLiteral("[redacted path]"));
    return text;
}

QVariant sanitizeEvidenceVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QString) return sanitizeEvidenceText(value.toString());
    if (value.canConvert<QVariantList>()) {
        QVariantList sanitized;
        for (const QVariant &entry : value.toList()) sanitized.append(sanitizeEvidenceVariant(entry));
        return sanitized;
    }
    if (value.canConvert<QVariantMap>()) {
        QVariantMap sanitized;
        const QVariantMap source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            sanitized.insert(it.key(), sanitizeEvidenceVariant(it.value()));
        }
        return sanitized;
    }
    return value;
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
            {QStringLiteral("repairability"), hidHideRepairabilityLabel(repairability)},
            {QStringLiteral("evidenceSource"), evidenceSource},
            {QStringLiteral("lastSuccessfulVerification"), lastSuccessfulVerification.toString(Qt::ISODateWithMs)},
            {QStringLiteral("latestRefreshAttempt"), latestRefreshAttempt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("latestRefreshResult"), latestRefreshResult},
            {QStringLiteral("stale"), stale}, {QStringLiteral("contradiction"), contradiction}};
}

QVariantMap HidHidePhysicalDeviceHealth::toVariantMap() const
{
    return {{QStringLiteral("controllerRecordId"), controllerRecordId},
            {QStringLiteral("friendlyName"), friendlyName}, {QStringLiteral("required"), required},
            {QStringLiteral("connected"), connected}, {QStringLiteral("exactCurrentHidInstances"), exactCurrentHidInstances},
            {QStringLiteral("identityResolved"), identityResolved}, {QStringLiteral("hiddenStateKnown"), hiddenStateKnown},
            {QStringLiteral("historicalOwnedHidInstanceCount"), historicalOwnedHidInstanceCount},
            {QStringLiteral("expectedHidden"), expectedHidden}, {QStringLiteral("actualHidden"), actualHidden},
            {QStringLiteral("availabilityState"), availabilityState},
            {QStringLiteral("visibilityDeferred"), visibilityDeferred},
            {QStringLiteral("state"), hidHideHealthStateLabel(state)},
            {QStringLiteral("repairability"), hidHideRepairabilityLabel(repairability)},
            {QStringLiteral("technicalDetails"), technicalDetails}};
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
    QVariantList physicalDevicesModel;
    for (const HidHidePhysicalDeviceHealth &device : physicalDevices) physicalDevicesModel.append(device.toVariantMap());
    return {{QStringLiteral("sessionId"), QVariant::fromValue(sessionId)}, {QStringLiteral("contextKey"), contextKey},
            {QStringLiteral("scanDepth"), hidHideHealthScanDepthLabel(scanDepth)},
            {QStringLiteral("overallState"), hidHideHealthStateLabel(overallState)},
            {QStringLiteral("inspectionStartedAt"), inspectionStartedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("lastChecked"), lastChecked.toString(Qt::ISODateWithMs)}, {QStringLiteral("inProgress"), inProgress},
            {QStringLiteral("cancelled"), cancelled}, {QStringLiteral("responseDelayed"), responseDelayed},
            {QStringLiteral("retryCount"), retryCount}, {QStringLiteral("retryLimit"), retryLimit},
            {QStringLiteral("checksCompleted"), checksCompleted},
            {QStringLiteral("checksTotal"), checksTotal}, {QStringLiteral("percentComplete"), percentComplete},
            {QStringLiteral("currentStage"), currentStage}, {QStringLiteral("currentCheckId"), currentCheckId},
            {QStringLiteral("currentCheckTitle"), currentCheckTitle}, {QStringLiteral("physicalDevices"), physicalDevicesModel},
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
    snapshot.inspectionStartedAt = QDateTime::currentDateTimeUtc();
    snapshot.inProgress = true;
    snapshot.retryLimit = kDirectControlOpenRetryLimit;
    // A Full Check has seven deterministic work units. MULTI_SZ size reads
    // are implementation detail, never user-visible work units.
    snapshot.checksTotal = depth == HidHideHealthScanDepth::Full ? 7 : 9;
    snapshot.currentStage = depth == HidHideHealthScanDepth::Full
        ? QStringLiteral("Collecting bounded package evidence")
        : QStringLiteral("Checking HidHide readiness evidence");
    snapshot.currentCheckId = depth == HidHideHealthScanDepth::Full ? QStringLiteral("package-evidence")
                                                                     : QStringLiteral("essential-evidence");
    snapshot.currentCheckTitle = depth == HidHideHealthScanDepth::Full ? QStringLiteral("Reading installed HidHide evidence")
                                                                        : QStringLiteral("Reading existing setup evidence");
    return snapshot;
}

HidHideHealthSnapshot HidHideHealthService::inspect(const HidHideHealthContext &context,
                                                     HidHideHealthScanDepth depth,
                                                     std::atomic_bool *cancelled,
                                                     ProgressCallback progress) const
{
    HidHideHealthContext inspectedContext = context;
    HidHideHealthSnapshot snapshot = checkingSnapshot(inspectedContext, depth);
    const auto publishProgress = [&snapshot, &progress](int complete, QString id, QString title, QString stage) {
        snapshot.checksCompleted = std::min(complete, snapshot.checksTotal);
        snapshot.percentComplete = snapshot.checksTotal > 0 ? (snapshot.checksCompleted * 100) / snapshot.checksTotal : 0;
        snapshot.currentCheckId = std::move(id);
        snapshot.currentCheckTitle = std::move(title);
        snapshot.currentStage = std::move(stage);
        if (progress) progress(snapshot);
    };
    if (depth == HidHideHealthScanDepth::Full) {
        collectBoundedPackageEvidence(&inspectedContext);
        publishProgress(1, QStringLiteral("package-evidence"), QStringLiteral("Reading installed HidHide evidence"),
                        QStringLiteral("Reading client, CLI, driver, Driver Store, and restart evidence"));
    }
    snapshot.lastChecked = QDateTime::currentDateTimeUtc();
    if (depth == HidHideHealthScanDepth::Full && m_probe && (!cancelled || !cancelled->load())) {
        for (int attempt = 0; attempt <= kDirectControlOpenRetryLimit; ++attempt) {
            snapshot.checks = m_probe(cancelled, [&publishProgress, &snapshot](const HidHideReadObservation &check) {
                const int unit = check.operation == QStringLiteral("OPEN_CONTROL") ? 2
                    : check.operation == QStringLiteral("GET_ACTIVE") ? 3
                    : check.operation == QStringLiteral("GET_INVERSE") ? 4
                    : check.operation.startsWith(QStringLiteral("GET_WHITELIST")) ? 5
                    : check.operation.startsWith(QStringLiteral("GET_BLACKLIST")) ? 6 : snapshot.checksCompleted;
                publishProgress(unit, check.operation, check.operation, check.summary);
            });
            const HidHideReadObservation *open = findCheck(snapshot.checks, QStringLiteral("OPEN_CONTROL"));
            const bool retryOpen = open && open->state == HidHideReadState::TimedOut
                && attempt < kDirectControlOpenRetryLimit && (!cancelled || !cancelled->load());
            if (!retryOpen) break;

            snapshot.responseDelayed = true;
            snapshot.retryCount = attempt + 1;
            snapshot.currentCheckId = QStringLiteral("OPEN_CONTROL");
            snapshot.currentCheckTitle = QStringLiteral("Waiting for HidHide control endpoint");
            snapshot.currentStage = QStringLiteral("HidHide response delayed; retrying bounded read-only control access in the background");
            if (progress) progress(snapshot);
            // This is bounded and runs only on the dedicated HidHide worker;
            // it never blocks QML, controller discovery, or the mapping path.
            QThread::msleep(kDirectControlOpenRetryDelayMs);
            if (cancelled && cancelled->load()) break;
        }
    }
    snapshot.cancelled = cancelled && cancelled->load();
    snapshot.checksCompleted = snapshot.cancelled ? std::min(snapshot.checksCompleted, snapshot.checksTotal)
        : snapshot.checksTotal;
    snapshot.percentComplete = snapshot.checksTotal > 0 ? (snapshot.checksCompleted * 100) / snapshot.checksTotal : 0;
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

    const auto versionsAgree = [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) == 0 || left.contains(right, Qt::CaseInsensitive)
            || right.contains(left, Qt::CaseInsensitive);
    };
    const bool versionsMismatch = !inspectedContext.clientVersion.isEmpty() && !inspectedContext.cliVersion.isEmpty()
        && !versionsAgree(inspectedContext.clientVersion, inspectedContext.cliVersion);
    const bool runtimePackageMismatch = inspectedContext.runtimeLoadedDriverVersionKnown
        && !inspectedContext.runtimeLoadedDriverVersion.isEmpty()
        && inspectedContext.activeDriverPackageKnown && !inspectedContext.activeDriverPackageVersion.isEmpty()
        && !versionsAgree(inspectedContext.runtimeLoadedDriverVersion, inspectedContext.activeDriverPackageVersion);
    const bool activePackageInStore = inspectedContext.activeDriverPackageKnown
        && !inspectedContext.activeDriverPackageId.isEmpty()
        && std::any_of(inspectedContext.driverStorePackageCandidates.cbegin(),
            inspectedContext.driverStorePackageCandidates.cend(), [&inspectedContext, &versionsAgree](const HidHideDriverPackageEvidence &candidate) {
                return candidate.packageId.compare(inspectedContext.activeDriverPackageId, Qt::CaseInsensitive) == 0
                    && versionsAgree(candidate.version, inspectedContext.activeDriverPackageVersion);
            });
    const bool fullPackageEvidence = depth == HidHideHealthScanDepth::Full && inspectedContext.packageEvidenceInspected
        && !inspectedContext.clientVersion.isEmpty() && !inspectedContext.cliVersion.isEmpty()
        && !inspectedContext.onDiskDriverVersion.isEmpty()
        && inspectedContext.runtimeLoadedDriverVersionKnown && !inspectedContext.runtimeLoadedDriverVersion.isEmpty()
        && inspectedContext.activeDriverPackageKnown && !inspectedContext.activeDriverPackageId.isEmpty()
        && !inspectedContext.activeDriverPackageVersion.isEmpty()
        && !inspectedContext.driverStorePackageCandidates.isEmpty() && activePackageInStore
        && !versionsMismatch && !runtimePackageMismatch && !inspectedContext.pendingPackageRestart;
    const bool packageInspectionRequested = depth == HidHideHealthScanDepth::Full;
    const QStringList candidateIds = [&inspectedContext] {
        QStringList values;
        for (const HidHideDriverPackageEvidence &candidate : inspectedContext.driverStorePackageCandidates) {
            values.append(candidate.packageId + QStringLiteral(" (") + candidate.version + QLatin1Char(')'));
        }
        return values;
    }();
    const QString packageTechnicalDetails = QStringLiteral("Client: %1; CLI: %2; on-disk System32 candidate: %3; runtime-loaded driver: %4; Driver Store candidates: %5; active bound package: %6 (%7).")
        .arg(inspectedContext.clientVersion.isEmpty() ? QStringLiteral("not observed") : inspectedContext.clientVersion,
             inspectedContext.cliVersion.isEmpty() ? QStringLiteral("not observed") : inspectedContext.cliVersion,
             inspectedContext.onDiskDriverVersion.isEmpty() ? QStringLiteral("not observed") : inspectedContext.onDiskDriverVersion,
             inspectedContext.runtimeLoadedDriverVersionKnown
                ? (inspectedContext.runtimeLoadedDriverVersion.isEmpty() ? QStringLiteral("unavailable") : inspectedContext.runtimeLoadedDriverVersion)
                : QStringLiteral("not proven"),
             candidateIds.isEmpty() ? QStringLiteral("none observed") : candidateIds.join(QStringLiteral(", ")),
             inspectedContext.activeDriverPackageKnown
                ? (inspectedContext.activeDriverPackageId.isEmpty() ? QStringLiteral("unavailable") : inspectedContext.activeDriverPackageId)
                : QStringLiteral("not proven"),
             inspectedContext.activeDriverPackageVersion.isEmpty() ? QStringLiteral("not observed") : inspectedContext.activeDriverPackageVersion);
    const bool packageContradiction = versionsMismatch || runtimePackageMismatch
        || (inspectedContext.activeDriverPackageKnown && !activePackageInStore);
    const bool runtimeBindingUnknown = inspectedContext.runtimeLoadedDriverVersionKnown
        && !inspectedContext.activeDriverPackageKnown;
    const HidHideHealthState packageState = fullPackageEvidence ? HidHideHealthState::Ready
        : inspectedContext.pendingPackageRestart ? HidHideHealthState::RestartRequired
        : packageContradiction ? HidHideHealthState::DoctorRecommended : HidHideHealthState::Unknown;
    snapshot.dimensions.append(dimension(QStringLiteral("package-version"), QStringLiteral("Package and version consistency"),
        packageState,
        packageState == HidHideHealthState::Ready || !packageInspectionRequested
            ? HidHideHealthSeverity::Info : HidHideHealthSeverity::Warning,
        fullPackageEvidence ? QStringLiteral("Client, CLI, on-disk candidate, runtime driver, and active Driver Store package are consistent.")
                            : inspectedContext.pendingPackageRestart ? QStringLiteral("A pending HidHide replacement requires a Windows restart before version health can be judged.")
                            : packageContradiction ? QStringLiteral("Observed HidHide package evidence contradicts the active binding.")
                            : packageInspectionRequested ? QStringLiteral("Package evidence is incomplete; no version relationship is inferred.")
                                                         : QStringLiteral("Run a Full Check for bounded package evidence."),
        QStringLiteral("Version/package repair is intentionally deferred to HidHide Doctor."),
        {QStringLiteral("HD-PKG-005")},
        packageState == HidHideHealthState::RestartRequired ? HidHideRepairability::UserActionRequired
            : packageState == HidHideHealthState::DoctorRecommended ? HidHideRepairability::DoctorRecommended
            : packageInspectionRequested && !fullPackageEvidence ? HidHideRepairability::DoctorRecommended
            : HidHideRepairability::None,
        packageTechnicalDetails));

    snapshot.dimensions.append(inspectedContext.runtimeLoadedDriverVersionKnown
        ? dimension(QStringLiteral("kernel-driver"), QStringLiteral("Kernel driver"),
                    inspectedContext.pendingPackageRestart ? HidHideHealthState::RestartRequired
                        : runtimePackageMismatch ? HidHideHealthState::DoctorRecommended
                        : runtimeBindingUnknown ? HidHideHealthState::Unknown : HidHideHealthState::Ready,
                    inspectedContext.pendingPackageRestart || runtimePackageMismatch || runtimeBindingUnknown
                        ? HidHideHealthSeverity::Warning : HidHideHealthSeverity::Info,
                    inspectedContext.pendingPackageRestart
                        ? QStringLiteral("Runtime-loaded HidHide driver may still be the pre-update version; restart is required.")
                        : runtimeBindingUnknown
                            ? QStringLiteral("Runtime-loaded driver was observed, but the active Driver Store package binding is unproven.")
                        : QStringLiteral("Runtime-loaded HidHide driver version: %1.").arg(inspectedContext.runtimeLoadedDriverVersion),
                    QStringLiteral("Runtime state is reported separately from the on-disk System32 candidate and Driver Store candidates."),
                    {QStringLiteral("HD-DRV-003"), QStringLiteral("HD-PKG-004")},
                    inspectedContext.pendingPackageRestart ? HidHideRepairability::UserActionRequired
                        : runtimePackageMismatch || runtimeBindingUnknown ? HidHideRepairability::DoctorRecommended : HidHideRepairability::None,
                    packageTechnicalDetails)
        : context.serviceReady && depth == HidHideHealthScanDepth::Essential
            ? dimension(QStringLiteral("kernel-driver"), QStringLiteral("Kernel driver"), HidHideHealthState::Unknown,
                        HidHideHealthSeverity::Info, QStringLiteral("Service registration is available; driver version was not directly checked."),
                        QStringLiteral("Run a Full Check for bounded on-disk and Driver Store evidence; runtime loading remains unproven unless directly observed."),
                        {QStringLiteral("HD-DRV-003")})
        : dimension(QStringLiteral("kernel-driver"), QStringLiteral("Kernel driver"),
                    HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Warning,
                    inspectedContext.onDiskDriverVersion.isEmpty() ? QStringLiteral("No direct runtime-driver evidence is available.")
                                                                 : QStringLiteral("An on-disk HidHide binary was observed, but runtime loading is unproven."),
                    QStringLiteral("HOTAS BF6 will not infer runtime loading from System32, service registration, or a Driver Store candidate."),
                    {QStringLiteral("HD-DRV-003")},
                    depth == HidHideHealthScanDepth::Full ? HidHideRepairability::DoctorRecommended : HidHideRepairability::None,
                    packageTechnicalDetails));

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
            HidHideHealthState::Unknown, HidHideHealthSeverity::Info,
            context.inspectionComplete ? QStringLiteral("Not directly checked; existing setup evidence is available.")
                                       : QStringLiteral("Not directly checked."),
            QStringLiteral("Essential mode never presents CLI/setup inspection as direct control-device proof. Run a Full Check for that evidence."),
            {QStringLiteral("HD-API-001")}));
    }

    const bool directWhitelistObserved = depth == HidHideHealthScanDepth::Full && whitelist;
    const bool directActiveObserved = depth == HidHideHealthScanDepth::Full && active;
    const HidHideReadObservation *inverse = findCheck(snapshot.checks, QStringLiteral("GET_INVERSE"));
    const bool directInverseObserved = depth == HidHideHealthScanDepth::Full && inverse;
    const auto directBool = [](const HidHideReadObservation *observation, bool *value) {
        if (!observation || observation->state != HidHideReadState::Pass) return false;
        if (observation->value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) { *value = true; return true; }
        if (observation->value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) { *value = false; return true; }
        return false;
    };
    bool directActive = false;
    bool directInverse = false;
    const bool directActiveKnown = directBool(active, &directActive);
    const bool directInverseKnown = directBool(inverse, &directInverse);
    const bool activeContradiction = directActiveKnown && context.cloakKnown && directActive != context.cloaked;
    const bool inverseContradiction = directInverseKnown && context.inverseKnown && directInverse != context.inverse;
    const bool rigExpectsPhysicalIsolation = !context.expectedPhysicalInstances.isEmpty()
        || std::any_of(context.physicalDevices.cbegin(), context.physicalDevices.cend(),
            [](const HidHidePhysicalDeviceHealth &device) { return device.expectedHidden && device.connected; });
    snapshot.dimensions.append(!directActiveObserved && depth == HidHideHealthScanDepth::Essential
        ? dimension(QStringLiteral("cloak-state"), QStringLiteral("Cloak state"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Info, QStringLiteral("Not directly checked in Essential mode."),
                    QStringLiteral("Existing readiness evidence remains visible but is not promoted to direct proof."), {QStringLiteral("HD-CFG-001")})
        : !directActiveKnown ? dimension(QStringLiteral("cloak-state"), QStringLiteral("Cloak state"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Warning, QStringLiteral("GET_ACTIVE did not provide a valid result."),
                    QStringLiteral("The Full Check will not substitute older CLI evidence after a failed direct GET."), {QStringLiteral("HD-CFG-001")},
                    HidHideRepairability::DoctorRecommended, active ? nativeErrorText(*active) : QString())
        : activeContradiction ? dimension(QStringLiteral("cloak-state"), QStringLiteral("Cloak state"), HidHideHealthState::DoctorRecommended,
                    HidHideHealthSeverity::Warning, QStringLiteral("GET_ACTIVE contradicts existing setup evidence."),
                    QStringLiteral("No automatic mutation is offered from contradictory evidence."), {QStringLiteral("HD-CFG-001")},
                    HidHideRepairability::DoctorRecommended)
        : !directActive && rigExpectsPhysicalIsolation
            ? dimension(QStringLiteral("cloak-state"), QStringLiteral("Cloak state"), HidHideHealthState::RepairAvailable,
                        HidHideHealthSeverity::Warning, QStringLiteral("Cloaking is disabled while this Device Rig expects physical inputs to be hidden."),
                        QStringLiteral("Review the existing qualified Device Rig isolation repair before it changes any exact current controller identity."),
                        {QStringLiteral("HD-CFG-001"), QStringLiteral("HD-ISO-006")}, HidHideRepairability::GuidedRepair)
            : !directActive
                ? dimension(QStringLiteral("cloak-state"), QStringLiteral("Cloak state"), HidHideHealthState::Ready,
                            HidHideHealthSeverity::Info, QStringLiteral("Cloaking is inactive, which matches this Rig's current isolation intent."),
                            QStringLiteral("No physical input is currently expected to be hidden, so no cloak repair is proposed."),
                            {QStringLiteral("HD-CFG-001")})
        : dimension(QStringLiteral("cloak-state"), QStringLiteral("Cloak state"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("GET_ACTIVE returned true."),
                    QStringLiteral("Fresh direct GET evidence is authoritative for this Full Check."), {QStringLiteral("HD-CFG-001")}));
    snapshot.dimensions.append(!directInverseObserved && depth == HidHideHealthScanDepth::Essential
        ? dimension(QStringLiteral("inverse-mode"), QStringLiteral("Inverse mode"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Info, QStringLiteral("Not directly checked in Essential mode."),
                    QStringLiteral("Run a Full Check to read GET_INVERSE."), {QStringLiteral("HD-CFG-002")})
        : !directInverseKnown ? dimension(QStringLiteral("inverse-mode"), QStringLiteral("Inverse mode"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Warning, QStringLiteral("GET_INVERSE did not provide a valid result."),
                    QStringLiteral("No older CLI result is substituted after a failed direct GET."), {QStringLiteral("HD-CFG-002")},
                    HidHideRepairability::DoctorRecommended, inverse ? nativeErrorText(*inverse) : QString())
        : inverseContradiction ? dimension(QStringLiteral("inverse-mode"), QStringLiteral("Inverse mode"), HidHideHealthState::DoctorRecommended,
                    HidHideHealthSeverity::Warning, QStringLiteral("GET_INVERSE contradicts existing setup evidence."),
                    QStringLiteral("No automatic mutation is offered from contradictory evidence."), {QStringLiteral("HD-CFG-002")},
                    HidHideRepairability::DoctorRecommended)
        : dimension(QStringLiteral("inverse-mode"), QStringLiteral("Inverse mode"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("GET_INVERSE returned %1.").arg(directInverse ? QStringLiteral("true") : QStringLiteral("false")),
                    QStringLiteral("Fresh direct GET evidence is authoritative for this Full Check."), {QStringLiteral("HD-CFG-002")}));
    const bool directWhitelistFailed = directWhitelistObserved && whitelist->state != HidHideReadState::Pass;
    const bool directWhitelistKnown = whitelist && whitelist->state == HidHideReadState::Pass;
    // Full-check access is effective policy, not raw whitelist membership:
    // an inactive cloak leaves the mapper accessible; inverse mode reverses
    // list membership.  Missing direct evidence remains Unknown and never
    // authorizes the existing mapper-only allowlist transaction.
    const bool mapperEntryPresent = directWhitelistKnown && !context.mapperExecutable.isEmpty()
        && containsExact(whitelist->values, context.mapperExecutable);
    bool mapperAllowed = false;
    bool accessKnown = false;
    bool allowlistFixApplies = false;
    if (depth == HidHideHealthScanDepth::Full && directActiveObserved) {
        if (directActiveKnown && !directActive) {
            mapperAllowed = true;
            accessKnown = true;
        } else if (directActiveKnown && directActive && directInverseKnown && directWhitelistKnown
                   && !context.mapperExecutable.isEmpty()) {
            mapperAllowed = directInverse ? !mapperEntryPresent : mapperEntryPresent;
            accessKnown = true;
            allowlistFixApplies = !mapperAllowed && !directInverse;
        }
    } else if (depth == HidHideHealthScanDepth::Essential) {
        mapperAllowed = context.mapperAllowlisted;
        accessKnown = context.mapperAllowlistKnown;
        allowlistFixApplies = accessKnown && !mapperAllowed;
    }
    const bool whitelistContradiction = depth == HidHideHealthScanDepth::Full && accessKnown
        && context.mapperAllowlistKnown && mapperAllowed != context.mapperAllowlisted;
    snapshot.dimensions.append(!accessKnown
        ? dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Warning, QStringLiteral("HOTAS BF6 could not confirm its HidHide exemption."),
                    QStringLiteral("Automatic repair is withheld until the application-list evidence is readable."),
                    {QStringLiteral("HD-CFG-003")}, HidHideRepairability::DoctorRecommended,
                    directWhitelistFailed ? nativeErrorText(*whitelist) : QString())
        : whitelistContradiction
            ? dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::DoctorRecommended,
                        HidHideHealthSeverity::Warning, QStringLiteral("GET_WHITELIST contradicts existing setup evidence."),
                        QStringLiteral("No allowlist mutation is offered from a contradictory Full Check snapshot."),
                        {QStringLiteral("HD-CFG-003")}, HidHideRepairability::DoctorRecommended)
        : mapperAllowed
            ? dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::Ready,
                        HidHideHealthSeverity::Info, QStringLiteral("HOTAS BF6 is permitted by HidHide."),
                        directActiveKnown && !directActive
                            ? QStringLiteral("Cloaking is inactive, so the mapper retains access regardless of list membership.")
                            : QStringLiteral("The mapper can retain access to its selected physical controller."),
                        {QStringLiteral("HD-ISO-003"), QStringLiteral("HD-CFG-003")})
            : allowlistFixApplies
                ? dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::RepairAvailable,
                            HidHideHealthSeverity::Warning, QStringLiteral("HOTAS BF6 is not effectively permitted by HidHide."),
                            QStringLiteral("The existing tested mapper-only allowlist transaction can add exactly the running HOTAS BF6 executable."),
                            {QStringLiteral("HD-ISO-003"), QStringLiteral("HD-CFG-003")}, HidHideRepairability::FixNow)
                : dimension(QStringLiteral("application-access"), QStringLiteral("HOTAS BF6 application access"), HidHideHealthState::DoctorRecommended,
                            HidHideHealthSeverity::Warning, QStringLiteral("HOTAS BF6 is not effectively permitted by HidHide."),
                            QStringLiteral("Inverse-mode or incomplete direct evidence means the existing add-to-whitelist repair is not safe to offer."),
                            {QStringLiteral("HD-ISO-003"), QStringLiteral("HD-CFG-003")}, HidHideRepairability::DoctorRecommended));

    QList<HidHidePhysicalDeviceHealth> devices = context.physicalDevices;
    const bool usingSelectedFallback = devices.isEmpty();
    if (devices.isEmpty() && (!context.selectedControllerId.isEmpty() || !context.expectedPhysicalInstances.isEmpty())) {
        HidHidePhysicalDeviceHealth device;
        device.controllerRecordId = context.selectedControllerId;
        device.friendlyName = QStringLiteral("Selected physical controller");
        device.connected = context.selectedControllerResolved;
        device.exactCurrentHidInstances = context.expectedPhysicalInstances;
        device.identityResolved = context.selectedControllerResolved;
        devices.append(device);
    }
    const bool directBlacklistObserved = depth == HidHideHealthScanDepth::Full && blacklist;
    const bool directBlacklistKnown = blacklist && blacklist->state == HidHideReadState::Pass;
    const QStringList blacklistEntries = directBlacklistKnown ? blacklist->values : context.hiddenDeviceInstances;
    const bool cloakBlocksExpectedIsolation = directActiveKnown && !directActive && rigExpectsPhysicalIsolation;
    int connectedManaged = 0;
    int isolatedManaged = 0;
    QStringList visibleRecords;
    for (HidHidePhysicalDeviceHealth &device : devices) {
        const bool known = directBlacklistObserved ? directBlacklistKnown : context.hiddenDeviceListKnown;
        device.hiddenStateKnown = known && device.identityResolved && !device.exactCurrentHidInstances.isEmpty();
        if (device.hiddenStateKnown) {
            device.actualHidden = !directBlacklistObserved && usingSelectedFallback
                ? context.selectedControllerHidden
                : std::all_of(device.exactCurrentHidInstances.cbegin(), device.exactCurrentHidInstances.cend(),
                    [&blacklistEntries](const QString &instance) { return containsNormalized(blacklistEntries, instance); });
        }
        if (!device.connected) {
            // A saved controller can be unavailable for an ordinary unplug or
            // travel setup. Do not turn that into Unknown/Doctor Recommended:
            // the configuration is still valid and exact visibility will be
            // checked only after Windows supplies a current instance again.
            device.state = HidHideHealthState::Ready;
            device.availabilityState = QStringLiteral("DEVICE NOT CONNECTED");
            device.visibilityDeferred = true;
            device.technicalDetails = device.required ? QStringLiteral("Required Rig member is not connected; isolation will be verified when it reconnects.")
                                                     : QStringLiteral("Optional Rig member is not connected; isolation will be verified when it reconnects.");
        } else if (!device.hiddenStateKnown) {
            device.state = HidHideHealthState::Unknown;
            device.repairability = HidHideRepairability::DoctorRecommended;
            device.technicalDetails = directBlacklistObserved ? QStringLiteral("GET_BLACKLIST did not provide usable direct evidence.")
                                                              : QStringLiteral("Existing hidden-device evidence is incomplete.");
        } else if (device.expectedHidden && cloakBlocksExpectedIsolation) {
            device.state = HidHideHealthState::Degraded;
            device.repairability = HidHideRepairability::GuidedRepair;
            device.technicalDetails = QStringLiteral("Cloaking is disabled; blacklist membership cannot isolate this exact current identity.");
            visibleRecords.append(device.controllerRecordId);
        } else if (device.expectedHidden && !device.actualHidden) {
            device.state = HidHideHealthState::RepairAvailable;
            device.repairability = HidHideRepairability::GuidedRepair;
            device.technicalDetails = QStringLiteral("Exact current HID instances are visible to games.");
            visibleRecords.append(device.controllerRecordId);
        } else {
            device.state = HidHideHealthState::Ready;
            device.technicalDetails = QStringLiteral("Exact current HID instances are hidden from games.");
        }
        if (device.connected && device.identityResolved) {
            ++connectedManaged;
            if (device.state == HidHideHealthState::Ready) ++isolatedManaged;
        }
    }
    snapshot.physicalDevices = devices;
    const bool directSelectedHidden = !context.expectedPhysicalInstances.isEmpty()
        && std::all_of(context.expectedPhysicalInstances.cbegin(), context.expectedPhysicalInstances.cend(),
            [&blacklistEntries](const QString &instance) { return containsNormalized(blacklistEntries, instance); });
    const bool blacklistContradiction = directBlacklistKnown && context.hiddenDeviceListKnown
        && context.selectedControllerResolved && directSelectedHidden != context.selectedControllerHidden;
    const bool isolationKnown = !devices.isEmpty() && (!usingSelectedFallback || context.selectedControllerResolved)
        && std::all_of(devices.cbegin(), devices.cend(), [](const HidHidePhysicalDeviceHealth &device) {
        return !device.connected || device.hiddenStateKnown;
    });
    const bool anyVisible = !visibleRecords.isEmpty();
    const int disconnectedManaged = static_cast<int>(std::count_if(devices.cbegin(), devices.cend(),
        [](const HidHidePhysicalDeviceHealth &device) { return !device.connected; }));
    QStringList affectedPhysicalRecords = visibleRecords;
    for (const HidHidePhysicalDeviceHealth &device : devices) {
        if (device.connected && device.expectedHidden && !device.controllerRecordId.isEmpty()
            && !affectedPhysicalRecords.contains(device.controllerRecordId)) {
            affectedPhysicalRecords.append(device.controllerRecordId);
        }
    }
    snapshot.dimensions.append(devices.isEmpty()
        ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Info, QStringLiteral("No exact selected physical HID identity is available."),
                    QStringLiteral("HOTAS BF6 will not guess a device from a friendly name or hide an unresolved device."),
                    {QStringLiteral("HD-ISO-006")})
        : !isolationKnown
            ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::Unknown,
                        HidHideHealthSeverity::Warning, QStringLiteral("Physical-device visibility could not be verified."),
                        QStringLiteral("Run a Full Check or open HidHide Doctor; no hide operation is proposed from incomplete identity evidence."),
                        {QStringLiteral("HD-ISO-006")}, HidHideRepairability::DoctorRecommended)
            : blacklistContradiction
                ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::DoctorRecommended,
                            HidHideHealthSeverity::Warning, QStringLiteral("GET_BLACKLIST contradicts existing visibility evidence."),
                            QStringLiteral("No physical-device mutation is offered from a contradictory Full Check snapshot."),
                            {QStringLiteral("HD-CFG-005"), QStringLiteral("HD-ISO-006")}, HidHideRepairability::DoctorRecommended)
            : cloakBlocksExpectedIsolation
                ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::RepairAvailable,
                            HidHideHealthSeverity::Warning, QStringLiteral("Cloaking is disabled, so physical input isolation is not effective."),
                            QStringLiteral("Review the existing qualified Device Rig isolation repair; no direct HID mutation is inferred from this health check."),
                            {QStringLiteral("HD-CFG-001"), QStringLiteral("HD-ISO-006")}, HidHideRepairability::GuidedRepair)
            : !anyVisible
                ? dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::Ready,
                            HidHideHealthSeverity::Info,
                            disconnectedManaged > 0
                                ? QStringLiteral("%1 device%2 not connected. Isolation will be verified when %3 reconnect%4.")
                                      .arg(disconnectedManaged).arg(disconnectedManaged == 1 ? QString{} : QStringLiteral("s"))
                                      .arg(disconnectedManaged == 1 ? QStringLiteral("it") : QStringLiteral("they"))
                                      .arg(disconnectedManaged == 1 ? QStringLiteral("s") : QString{})
                                : QStringLiteral("Physical inputs %1 / %2 isolated.").arg(isolatedManaged).arg(connectedManaged),
                            disconnectedManaged > 0
                                ? QStringLiteral("Saved Device Rig membership remains valid while a controller is unavailable.")
                                : QStringLiteral("Every connected relevant Rig member is evaluated by exact HID identity."), {QStringLiteral("HD-ISO-006")})
                : dimension(QStringLiteral("physical-isolation"), QStringLiteral("Physical-device isolation"), HidHideHealthState::RepairAvailable,
                            HidHideHealthSeverity::Warning, QStringLiteral("Physical inputs %1 / %2 isolated; one or more are visible to games.").arg(isolatedManaged).arg(connectedManaged),
                            QStringLiteral("The existing validated physical-input visibility transaction can be reviewed before it changes only exact resolved controller identities."),
                            {QStringLiteral("HD-ISO-006")}, HidHideRepairability::GuidedRepair,
                            QStringLiteral("Visible controller records: %1.").arg(visibleRecords.join(QStringLiteral(", ")))));

    const bool virtualKnown = directBlacklistObserved ? directBlacklistKnown : context.managedVirtualOutputInspectionKnown;
    const bool virtualHidden = virtualKnown && (directBlacklistObserved
        ? std::any_of(context.managedVirtualOutputInstances.cbegin(),
        context.managedVirtualOutputInstances.cend(), [&blacklistEntries](const QString &instance) {
            return containsNormalized(blacklistEntries, instance);
        }) : context.managedVirtualOutputHidden);
    snapshot.dimensions.append(context.managedVirtualOutputInstances.isEmpty()
        ? dimension(QStringLiteral("virtual-output"), QStringLiteral("Virtual-output visibility"), HidHideHealthState::Unknown,
                    HidHideHealthSeverity::Info, QStringLiteral("No exact managed vJoy HID identity is available."),
                    QStringLiteral("HOTAS BF6 does not infer virtual-output visibility from a display name."),
                    {QStringLiteral("HD-ISO-008")})
        : !virtualKnown
            ? dimension(QStringLiteral("virtual-output"), QStringLiteral("Virtual-output visibility"), HidHideHealthState::Unknown,
                        HidHideHealthSeverity::Warning, QStringLiteral("Managed vJoy visibility could not be verified."),
                        QStringLiteral("HOTAS BF6 will not change a virtual output until its exact current identity is verified."),
                        {QStringLiteral("HD-ISO-008")}, HidHideRepairability::DoctorRecommended)
            : virtualHidden
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
        ? dimension(QStringLiteral("hotas-recovery"), QStringLiteral("HOTAS setup recovery"), HidHideHealthState::UserActionRequired,
                    HidHideHealthSeverity::Warning, QStringLiteral("A prior setup transaction still requires physical-controller recovery verification."),
                    QStringLiteral("Reconnect and DirectInput report proof remain required before setup can be declared safe."),
                    {QStringLiteral("HD-ISO-013")}, HidHideRepairability::UserActionRequired)
        : dimension(QStringLiteral("hotas-recovery"), QStringLiteral("HOTAS setup recovery"), HidHideHealthState::Ready,
                    HidHideHealthSeverity::Info, QStringLiteral("No pending HOTAS BF6 recovery state is known."),
                    QStringLiteral("This is separate from package replacement state."), {QStringLiteral("HD-ISO-013")}));
    snapshot.dimensions.append(!inspectedContext.pendingPackageRestartKnown
        ? dimension(QStringLiteral("package-restart-state"), QStringLiteral("Package restart state"), HidHideHealthState::Unknown,
                    depth == HidHideHealthScanDepth::Full ? HidHideHealthSeverity::Warning : HidHideHealthSeverity::Info,
                    depth == HidHideHealthScanDepth::Full ? QStringLiteral("Pending package replacement evidence was unavailable.")
                                                         : QStringLiteral("Not checked in Essential mode."),
                    QStringLiteral("A green state is never inferred from absence of a HOTAS recovery journal."), {QStringLiteral("HD-WIN-015")},
                    depth == HidHideHealthScanDepth::Full ? HidHideRepairability::DoctorRecommended : HidHideRepairability::None)
        : inspectedContext.pendingPackageRestart
            ? dimension(QStringLiteral("package-restart-state"), QStringLiteral("Package restart state"), HidHideHealthState::RestartRequired,
                        HidHideHealthSeverity::Warning, QStringLiteral("Pending HidHide replacement/restart evidence was observed."),
                        QStringLiteral("Restart Windows before relying on new package/driver evidence."), {QStringLiteral("HD-WIN-015")},
                        HidHideRepairability::UserActionRequired)
            : dimension(QStringLiteral("package-restart-state"), QStringLiteral("Package restart state"), HidHideHealthState::Ready,
                        HidHideHealthSeverity::Info, QStringLiteral("No HidHide pending replacement marker was observed."),
                        QStringLiteral("This bounded check covers the Session Manager pending-rename marker only."), {QStringLiteral("HD-WIN-015")}));

    for (const HidHideHealthDimension &value : snapshot.dimensions) {
        // An unknown result is actionable evidence when it represents an
        // incomplete safety decision.  Purely informational unknowns (for
        // example, no managed virtual-output identity in this Rig) stay out
        // of App Health so they cannot masquerade as a fault.
        if (!isReady(value.state)
            && !(value.state == HidHideHealthState::Unknown
                 && value.severity == HidHideHealthSeverity::Info)) {
            snapshot.findings.append(findingFrom(value, value.id == QStringLiteral("physical-isolation")
                ? affectedPhysicalRecords : QStringList{}));
        }
    }
    const auto appendContradiction = [&snapshot](const QString &id, const QString &title, const QString &detail) {
        HidHideHealthFinding finding;
        finding.id = id;
        finding.code = QStringLiteral("HIDHIDE_") + id.toUpper().replace(QLatin1Char('-'), QLatin1Char('_'));
        finding.dimensionId = QStringLiteral("configuration-consistency");
        finding.severity = HidHideHealthSeverity::Warning;
        finding.title = title;
        finding.explanation = QStringLiteral("Fresh direct HidHide evidence contradicts older CLI/readiness evidence.");
        finding.whyItMatters = QStringLiteral("Automatic repair is withheld until a standalone diagnosis can reconcile the evidence.");
        finding.repairability = HidHideRepairability::DoctorRecommended;
        finding.technicalDetails = detail;
        snapshot.findings.append(finding);
    };
    if (activeContradiction) appendContradiction(QStringLiteral("direct-get-active-contradiction"),
        QStringLiteral("GET_ACTIVE contradiction"), QStringLiteral("Direct=%1; existing=%2.").arg(directActive).arg(context.cloaked));
    if (inverseContradiction) appendContradiction(QStringLiteral("direct-get-inverse-contradiction"),
        QStringLiteral("GET_INVERSE contradiction"), QStringLiteral("Direct=%1; existing=%2.").arg(directInverse).arg(context.inverse));
    if (whitelistContradiction) appendContradiction(QStringLiteral("direct-get-whitelist-contradiction"),
        QStringLiteral("GET_WHITELIST contradiction"), QStringLiteral("Direct mapper exemption=%1; existing=%2.").arg(mapperAllowed).arg(context.mapperAllowlisted));
    if (blacklistContradiction) appendContradiction(QStringLiteral("direct-get-blacklist-contradiction"),
        QStringLiteral("GET_BLACKLIST contradiction"), QStringLiteral("Direct selected visibility=%1; existing=%2.").arg(directSelectedHidden).arg(context.selectedControllerHidden));

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
    snapshot.currentCheckId = snapshot.cancelled ? QStringLiteral("cancelled") : QStringLiteral("evaluation");
    snapshot.currentCheckTitle = snapshot.cancelled ? QStringLiteral("Full Check cancelled") : QStringLiteral("Evaluating HidHide Health");
    snapshot.currentStage = snapshot.cancelled ? QStringLiteral("Full check cancelled; collected evidence was retained.")
                                               : QStringLiteral("HidHide health evaluation complete");
    if (!snapshot.cancelled) {
        snapshot.checksCompleted = snapshot.checksTotal;
        snapshot.percentComplete = 100;
    }
    // Every emitted pre-terminal snapshot remains a real in-flight state so
    // QML cannot re-enable a Full Check while the worker still owns the probe.
    snapshot.inProgress = false;
    const QString refreshResult = snapshot.cancelled ? QStringLiteral("Cancelled")
        : std::any_of(snapshot.checks.cbegin(), snapshot.checks.cend(), [](const HidHideReadObservation &check) {
              return check.state == HidHideReadState::TimedOut;
          }) ? QStringLiteral("Timed out")
        : std::any_of(snapshot.checks.cbegin(), snapshot.checks.cend(), [](const HidHideReadObservation &check) {
              return check.state != HidHideReadState::Pass;
          }) ? QStringLiteral("Could not complete") : QStringLiteral("Verified");
    for (HidHideHealthDimension &value : snapshot.dimensions) {
        value.evidenceSource = depth == HidHideHealthScanDepth::Full
            ? QStringLiteral("Current HidHide check") : QStringLiteral("Current setup check");
        value.latestRefreshAttempt = snapshot.lastChecked;
        value.latestRefreshResult = refreshResult;
        value.contradiction = value.shortSummary.contains(QStringLiteral("contradict"), Qt::CaseInsensitive);
        if (value.state != HidHideHealthState::Unknown && !snapshot.cancelled) {
            value.lastSuccessfulVerification = snapshot.lastChecked;
        }
    }
    if (progress) progress(snapshot);
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

QVariantMap HidHideHealthService::sanitizedEvidence(const HidHideHealthSnapshot &snapshot)
{
    QVariantMap sanitized = snapshot.toVariantMap();
    sanitized.insert(QStringLiteral("contextKey"), QStringLiteral("redacted"));
    QVariantList checks = sanitized.value(QStringLiteral("checks")).toList();
    for (QVariant &entry : checks) {
        QVariantMap check = entry.toMap();
        check.insert(QStringLiteral("entryCount"), check.value(QStringLiteral("values")).toStringList().size());
        check.remove(QStringLiteral("values"));
        check.remove(QStringLiteral("value"));
        entry = check;
    }
    sanitized.insert(QStringLiteral("checks"), checks);
    QVariantList devices = sanitized.value(QStringLiteral("physicalDevices")).toList();
    for (QVariant &entry : devices) {
        QVariantMap device = entry.toMap();
        device.insert(QStringLiteral("exactCurrentHidInstanceCount"),
                      device.value(QStringLiteral("exactCurrentHidInstances")).toStringList().size());
        device.remove(QStringLiteral("exactCurrentHidInstances"));
        entry = device;
    }
    sanitized.insert(QStringLiteral("physicalDevices"), devices);
    return sanitizeEvidenceVariant(sanitized).toMap();
}

} // namespace hotas
