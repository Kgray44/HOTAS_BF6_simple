#include "doctor_diagnostics.h"
#include "doctor_fixtures.h"
#include "doctor_integration.h"
#include "doctor_repair_helper_client.h"
#include "doctor_repair_engine.h"
#include "doctor_session.h"
#include "doctor_session_view_model.h"
#include "hotas_build_version.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>
#include <QWindow>

#include <atomic>
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>

namespace {
bool hasArgument(int argc, char *argv[], const char *argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) return true;
    }
    return false;
}

QString argumentValue(int argc, char *argv[], const char *argument)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) return QString::fromLocal8Bit(argv[index + 1]);
    }
    return {};
}

constexpr auto kDoctorSingleInstanceEndpoint = "hotas-bf6-hidhide-doctor-v1";

bool forwardToExistingDoctor(const QString &integrationToken)
{
    QLocalSocket existing;
    existing.connectToServer(QString::fromLatin1(kDoctorSingleInstanceEndpoint));
    if (!existing.waitForConnected(150)) return false;
    if (!integrationToken.isEmpty()) {
        const QByteArray payload = integrationToken.toLatin1();
        if (payload.size() != 32 || existing.write(payload) != payload.size() || !existing.waitForBytesWritten(150)) {
            existing.disconnectFromServer();
            return false;
        }
    }
    existing.disconnectFromServer();
    return true;
}

bool listenForDoctorInstance(QLocalServer &server)
{
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (server.listen(QString::fromLatin1(kDoctorSingleInstanceEndpoint))) return true;
    // A failed client connection above can leave an abandoned named-pipe
    // endpoint after a crash. Remove only that stale endpoint, then retry.
    if (server.serverError() != QAbstractSocket::AddressInUseError
        || !QLocalServer::removeServer(QString::fromLatin1(kDoctorSingleInstanceEndpoint))) {
        return false;
    }
    server.setSocketOptions(QLocalServer::UserAccessOption);
    return server.listen(QString::fromLatin1(kDoctorSingleInstanceEndpoint));
}

void focusDoctorWindow(QQmlApplicationEngine &engine)
{
    for (QObject *root : engine.rootObjects()) {
        auto *window = qobject_cast<QWindow *>(root);
        if (!window) continue;
        window->showNormal();
        window->raise();
        window->requestActivate();
        return;
    }
}

void stampBuildProvenance(hotas::doctor::DiagnosticRunOutcome &outcome)
{
    outcome.snapshot.build.version = QString::fromLatin1(HOTAS_BF6_VERSION);
    outcome.snapshot.build.sourceRevision = QStringLiteral(HOTAS_BF6_BUILD_ID);
    outcome.snapshot.build.processArchitecture = QSysInfo::buildCpuArchitecture();
}

class ReadOnlyConfigurationObserver final : public hotas::doctor::IRepairConfigurationMutator {
public:
    explicit ReadOnlyConfigurationObserver(hotas::doctor::HidHideConfigurationSnapshot snapshot) : m_snapshot(std::move(snapshot)) {}
    hotas::doctor::HidHideConfigurationSnapshot readConfiguration() override { return m_snapshot; }
    bool apply(const hotas::doctor::RepairOperation &, hotas::doctor::NativeError *error) override
    {
        if (error) *error = {hotas::doctor::NativeErrorDomain::Protocol, 50,
            QStringLiteral("ERROR_NOT_SUPPORTED"), QStringLiteral("Restart reconciliation has no mutation surface.")};
        return false;
    }
private:
    hotas::doctor::HidHideConfigurationSnapshot m_snapshot;
};

QString reconcileIncompleteJournals(const hotas::doctor::ReadOnlyDiagnosticSnapshot &snapshot)
{
    // This runs only after the normal Doctor scan has independently obtained
    // complete GET evidence. It cannot launch the helper or call a SET.
    const std::optional<hotas::doctor::HidHideConfigurationSnapshot> configuration = hotas::doctor::RepairPlanner::configurationFrom(snapshot);
    if (!configuration) return {};
    hotas::doctor::RepairJournalStore journal;
    QString ignoredReason;
    const QList<hotas::doctor::RepairTransaction> records = journal.history(&ignoredReason);
    QStringList notices;
    for (const hotas::doctor::RepairTransaction &record : records) {
        const hotas::doctor::RepairRecoveryResult result = record.riskClass == hotas::doctor::RepairRiskClass::R1Configuration
            ? [&] { ReadOnlyConfigurationObserver observer(*configuration); return hotas::doctor::RepairTransactionCoordinator().reconcileIncomplete(record, observer, journal); }()
            : hotas::doctor::RepairTransactionCoordinator().reconcileAfterReboot(record, snapshot, journal);
        if (result.requiresOwnerReview)
            notices.append(QStringLiteral("%1 — %2").arg(result.transaction.id.value(), result.detail));
    }
    return notices.join(QLatin1Char('\n'));
}

// The controller owns a single bounded worker for a scan.  It never exposes a
// repair operation; cancellation simply tells the observational provider not
// to schedule further reads and safely joins the worker on shutdown.
struct ScanCompletion final {
    hotas::doctor::DoctorSession session;
    QString recoveryNotice;
    QByteArray redactedReport;
};

class ScanController final {
public:
    ScanController(hotas::doctor::DoctorSessionViewModel &model, QString reportPath)
        : m_model(model), m_reportPath(std::move(reportPath)) {}
    ~ScanController() { stop(); }

    void start()
    {
        if (m_running.exchange(true)) return;
        if (m_worker.joinable()) m_worker.join();
        m_cancelled.store(false);
        ++m_generation;
        const quint64 generation = m_generation.load();
        // A scheduled deep continuation never replays installation here. It
        // only makes the observe-first restart state visible while this fresh
        // native scan is in progress.
        hotas::doctor::RepairJournalStore startupJournal;
        QString ignoredReason;
        const QList<hotas::doctor::RepairTransaction> records = startupJournal.history(&ignoredReason);
        const bool resuming = std::any_of(records.cbegin(), records.cend(), [](const hotas::doctor::RepairTransaction &record) {
            return record.riskClass != hotas::doctor::RepairRiskClass::R1Configuration
                && record.state == hotas::doctor::RepairTransactionState::AwaitingReboot;
        });
        m_model.setRecoveryNotice(resuming
            ? QStringLiteral("RESUMING REPAIR — rerunning a fresh read-only Doctor scan before any continuation decision.") : QString());
        hotas::doctor::DoctorDiagnosticEngine engine;
        m_model.replaceSession(engine.createPreparedSession());
        m_model.setRedactedDiagnosticReport({});
        m_worker = std::thread([this, generation] {
            hotas::doctor::ReadOnlyWindowsDiagnosticProvider provider;
            hotas::doctor::DoctorDiagnosticEngine engine;
            hotas::doctor::DiagnosticRunOutcome outcome = engine.run(provider, &m_cancelled);
            auto completion = std::make_shared<ScanCompletion>();
            completion->recoveryNotice = reconcileIncompleteJournals(outcome.snapshot);
            stampBuildProvenance(outcome);
            completion->redactedReport = hotas::doctor::DoctorDiagnosticEngine::serializeJson(outcome, true);
            if (!m_reportPath.isEmpty()) {
                QFile report(m_reportPath);
                if (report.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    report.write(completion->redactedReport);
            }
            completion->session = std::move(outcome.session);
            m_running.store(false);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, generation, completion] {
                if (generation == m_generation.load()) {
                    // The worker transfers its completed session exactly once.
                    // Do not copy a mutable implicitly-shared session through a
                    // queued callback: that was the native QtCore crash path.
                    m_model.replaceSession(std::move(completion->session));
                    m_model.setRecoveryNotice(std::move(completion->recoveryNotice));
                    m_model.setRedactedDiagnosticReport(std::move(completion->redactedReport));
                }
            }, Qt::QueuedConnection);
        });
    }

    void cancel() { m_cancelled.store(true); }
    void stop()
    {
        m_cancelled.store(true);
        if (m_worker.joinable()) m_worker.join();
        m_running.store(false);
    }

private:
    hotas::doctor::DoctorSessionViewModel &m_model;
    QString m_reportPath;
    std::atomic_bool m_cancelled{false};
    std::atomic_bool m_running{false};
    std::atomic<quint64> m_generation{0};
    std::thread m_worker;
};

// This controller is constructed only for the explicit development-fixture
// Lab Mode.  It has no normal-mode instance, joins its one worker before the
// view model is destroyed, and never provides a generic command surface.
class LabRepairController final {
public:
    LabRepairController(hotas::doctor::DoctorSessionViewModel &model,
        hotas::doctor::RepairPlanProposal proposal, hotas::doctor::DoctorEnvironment environment,
        QString buildId)
        : m_model(model), m_proposal(std::move(proposal)), m_environment(std::move(environment)), m_buildId(std::move(buildId)) {}
    ~LabRepairController() { if (m_worker.joinable()) m_worker.join(); }

    void start(bool executeMutation)
    {
        if (m_running.exchange(true)) return;
        if (m_worker.joinable()) m_worker.join();
        hotas::doctor::RepairPlan plan = m_proposal.plan;
        plan.authorization = hotas::doctor::RepairAuthorization::OwnerLabAuthorized;
        plan.integrityDigest = hotas::doctor::RepairHelperContract::seal(plan);
        hotas::doctor::RepairPlanProposal bound = m_proposal;
        bound.plan = plan;
        const hotas::doctor::RepairTransactionId transactionId(QStringLiteral("REPAIR-TX-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper());
        hotas::doctor::RepairJournalStore journal;
        const hotas::doctor::RepairExecutionResult staged = hotas::doctor::RepairTransactionCoordinator()
            .dryRun(bound, m_environment, plan.sessionId, journal, transactionId);
        hotas::doctor::RepairTransaction transaction = staged.transaction;
        if (transaction.state == hotas::doctor::RepairTransactionState::FailedSafely || !transaction.id.isValid()) {
            m_model.setRepairRuntime(QStringLiteral("REPAIR FAILED SAFELY"), staged.detail.isEmpty()
                ? QStringLiteral("The authorization journal could not be created; no helper was launched.") : staged.detail, false);
            m_running.store(false);
            return;
        }
        const auto persistBoundary = [&](const QString &failureDetail) {
            QString reason;
            if (journal.persist(transaction, &reason)) return true;
            m_model.setRepairRuntime(QStringLiteral("REPAIR FAILED SAFELY"), failureDetail + QStringLiteral(" ") + reason, false);
            m_running.store(false);
            return false;
        };
        transaction.state = hotas::doctor::RepairTransactionState::AwaitingAuthorization;
        transaction.finalStatus = QStringLiteral("Owner/lab action bound exact plan, recipe version, targets, fingerprints, qualification, and digest.");
        if (!persistBoundary(QStringLiteral("The authorization boundary was not durable; no helper was launched."))) return;
        transaction.state = hotas::doctor::RepairTransactionState::Authorized;
        if (!persistBoundary(QStringLiteral("The authorization record was not durable; no helper was launched."))) return;
        transaction.state = hotas::doctor::RepairTransactionState::AwaitingElevation;
        transaction.finalStatus = executeMutation
            ? QStringLiteral("Final authorization recorded; awaiting UAC for the bounded helper.")
            : QStringLiteral("Connectivity-only helper test recorded; no configuration mutation is authorized.");
        if (!persistBoundary(QStringLiteral("The elevation boundary was not durable; no helper was launched."))) return;
        m_worker = std::thread([this, plan, transactionId, executeMutation] {
            const hotas::doctor::RepairHelperClientResult helper = hotas::doctor::RepairHelperClient::invoke(plan,
                m_environment, m_buildId, m_buildId, !executeMutation, transactionId);
            hotas::doctor::RepairJournalStore resultJournal;
            if (const std::optional<hotas::doctor::RepairTransaction> persisted = resultJournal.load(transactionId)) {
                hotas::doctor::RepairTransaction reconciled = *persisted;
                if (!executeMutation && helper.outcome == hotas::doctor::RepairHelperOutcome::Accepted) {
                    reconciled.state = hotas::doctor::RepairTransactionState::Completed;
                    reconciled.finalStatus = QStringLiteral("Elevated helper connectivity and sealed-plan validation completed; no HidHide SET was requested.");
                } else if (helper.outcome == hotas::doctor::RepairHelperOutcome::AuthorizationCancelled) {
                    reconciled.state = hotas::doctor::RepairTransactionState::Cancelled;
                    reconciled.finalStatus = QStringLiteral("Authorization cancelled — no changes made.");
                } else if (helper.outcome == hotas::doctor::RepairHelperOutcome::Disconnected
                    || helper.outcome == hotas::doctor::RepairHelperOutcome::ConnectTimedOut) {
                    reconciled.state = hotas::doctor::RepairTransactionState::RecoveryRequired;
                    reconciled.finalStatus = QStringLiteral("Helper outcome is uncertain; no retry was attempted and read-only reconciliation is required.");
                } else if (helper.outcome != hotas::doctor::RepairHelperOutcome::Accepted) {
                    reconciled.state = hotas::doctor::RepairTransactionState::FailedSafely;
                    reconciled.finalStatus = helper.detail.isEmpty() ? QStringLiteral("Helper rejected the request before mutation.") : helper.detail;
                }
                resultJournal.persist(reconciled, nullptr);
            }
            const QString state = helper.outcome == hotas::doctor::RepairHelperOutcome::Accepted
                ? (executeMutation ? hotas::doctor::displayName(helper.state) : QStringLiteral("CONNECTIVITY TEST COMPLETE"))
                : hotas::doctor::displayName(helper.outcome);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, state, detail = helper.detail] {
                m_model.setRepairRuntime(state, detail.isEmpty() ? QStringLiteral("The helper returned no additional detail.") : detail, false);
            }, Qt::QueuedConnection);
            m_running.store(false);
        });
    }

private:
    hotas::doctor::DoctorSessionViewModel &m_model;
    hotas::doctor::RepairPlanProposal m_proposal;
    hotas::doctor::DoctorEnvironment m_environment;
    QString m_buildId;
    std::atomic_bool m_running{false};
    std::thread m_worker;
};
} // namespace

int main(int argc, char *argv[])
{
    using namespace Qt::StringLiterals;
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS BF6"));
    application.setOrganizationDomain(QStringLiteral("local.hotasbf6"));
    application.setApplicationName(QStringLiteral("HidHide Doctor"));
    application.setApplicationVersion(QString::fromLatin1(HOTAS_BF6_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const bool fixtureMode = hasArgument(argc, argv, "--development-fixture");
    const bool scanSmoke = hasArgument(argc, argv, "--scan-smoke");
    const QString integrationToken = argumentValue(argc, argv, "--integration-context");
    // Only the interactive production workstation is single-instance. The
    // explicit headless, fixture, and startup-smoke paths remain independent
    // verification processes and must never bind to an owner's open Doctor.
    const bool interactiveProductionWindow = !fixtureMode
        && !hasArgument(argc, argv, "--headless")
        && !hasArgument(argc, argv, "--startup-smoke")
        && !scanSmoke;
    if (interactiveProductionWindow && forwardToExistingDoctor(integrationToken)) return 0;
    std::optional<QLocalServer> instanceServer;
    if (interactiveProductionWindow) {
        instanceServer.emplace();
        if (!listenForDoctorInstance(*instanceServer)) instanceServer.reset();
    }
    const hotas::doctor::DoctorIntegrationReadResult integration = integrationToken.isEmpty()
        ? hotas::doctor::DoctorIntegrationReadResult{} : hotas::doctor::consumeDoctorLaunchContext(integrationToken);
    const bool componentMismatch = integration.accepted
        && integration.context.invokingVersion != QString::fromLatin1(HOTAS_BF6_VERSION);
    const QString integrationNotice = integrationToken.isEmpty() ? QString{}
        : !integration.accepted ? QStringLiteral("HOTAS integration context rejected: %1").arg(integration.rejection)
        : componentMismatch ? QStringLiteral("COMPONENT VERSION MISMATCH — HOTAS BF6 %1 requested Doctor %2. Repair is blocked; independent diagnosis continues.")
              .arg(integration.context.invokingVersion, QString::fromLatin1(HOTAS_BF6_VERSION))
        : QStringLiteral("Opened by HOTAS BF6 for %1. This is an intent hint only; Doctor independently verifies all system evidence.")
              .arg(integration.context.reason);
    const bool labRepairMode = fixtureMode && !componentMismatch && hasArgument(argc, argv, "--lab-repair-mode");
    const bool repairPlanningRequested = hasArgument(argc, argv, "--plan-repair");
    const bool dryRunRequested = hasArgument(argc, argv, "--dry-run-repair");
    const bool approvedUpgradeRequested = fixtureMode && hasArgument(argc, argv, "--approved-upgrade");
    const QString resumeTransaction = argumentValue(argc, argv, "--resume-transaction");
    const QString buildIdentity = QStringLiteral("Development build %1 · %2 · %3")
        .arg(QString::fromLatin1(HOTAS_BF6_VERSION), QStringLiteral(HOTAS_BF6_BUILD_ID), QSysInfo::buildCpuArchitecture());
    const QString reportPath = argumentValue(argc, argv, "--report");
    if (fixtureMode) {
        // Fixture mode runs the same deterministic diagnosis engine as GUI,
        // headless, and tests.  It is visibly labelled so no simulated
        // observation can be mistaken for this machine's condition.
        QString fixtureLabel;
        const QString fixtureName = argumentValue(argc, argv, "--development-fixture");
        hotas::doctor::FixtureDiagnosticProvider provider(hotas::doctor::createDevelopmentFixture(
            fixtureName.isEmpty() ? QStringLiteral("Healthy System") : fixtureName, &fixtureLabel));
        hotas::doctor::DoctorDiagnosticEngine diagnosticEngine;
        hotas::doctor::DiagnosticRunOutcome outcome = diagnosticEngine.run(provider, nullptr, {}, approvedUpgradeRequested);
        outcome.session.setSessionLabel(fixtureLabel);
        // Planning is always read-only.  The explicit switch exists for
        // headless callers that want to assert this intent in an invocation;
        // normal UI diagnosis also plans when evidence supports a candidate.
        if (dryRunRequested && !repairPlanningRequested) return 2;
        if (dryRunRequested) {
            const hotas::doctor::RepairPlanProposal proposal = hotas::doctor::RepairPlanner().propose(outcome.session, outcome.snapshot, true, approvedUpgradeRequested);
            if (proposal.status != hotas::doctor::RepairProposalStatus::AvailableForOwnerLab) return 2;
            hotas::doctor::RepairJournalStore journal;
            const hotas::doctor::RepairExecutionResult dryRun = hotas::doctor::RepairTransactionCoordinator().dryRun(
                proposal, outcome.snapshot.environment, outcome.session.id(), journal);
            if (dryRun.transaction.state != hotas::doctor::RepairTransactionState::Planned) return 3;
        }
        stampBuildProvenance(outcome);
        if (!reportPath.isEmpty()) {
            QFile report(reportPath);
            if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
            report.write(hotas::doctor::DoctorDiagnosticEngine::serializeJson(outcome, true));
        }
        if (hasArgument(argc, argv, "--headless")) return 0;
        hotas::doctor::DoctorSessionViewModel viewModel(outcome.session, buildIdentity);
        viewModel.setCopyAction([](QString text) { QGuiApplication::clipboard()->setText(text); });
        viewModel.setIntegrationNotice(integrationNotice, componentMismatch);
        if (integration.accepted) hotas::doctor::writeDoctorIntegrationResult(integration.context, QStringLiteral("Doctor opened"),
            componentMismatch ? QStringLiteral("Component version mismatch; repair is blocked.") : QStringLiteral("Independent diagnosis is starting."));
        std::optional<LabRepairController> labController;
        if (labRepairMode) {
            const hotas::doctor::RepairPlanProposal proposal = hotas::doctor::RepairPlanner().propose(outcome.session, outcome.snapshot, true, approvedUpgradeRequested);
            if (proposal.status != hotas::doctor::RepairProposalStatus::AvailableForOwnerLab) return 2;
            labController.emplace(viewModel, proposal, outcome.snapshot.environment, QString::fromLatin1(HOTAS_BF6_BUILD_ID));
            viewModel.setLabRepairActions(true, [&labController](bool executeMutation) { labController->start(executeMutation); });
        }
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doctorSession"), &viewModel);
        engine.loadFromModule(u"HidHideDoctor"_qs, u"HidHideDoctorMain"_qs);
        if (engine.rootObjects().isEmpty()) return -1;
        if (hasArgument(argc, argv, "--startup-smoke")) return 0;
        return application.exec();
    }
    if (hasArgument(argc, argv, "--headless")) {
        if (dryRunRequested && !repairPlanningRequested) return 2;
        std::atomic_bool cancellationRequested{false};
        hotas::doctor::ReadOnlyWindowsDiagnosticProvider provider;
        hotas::doctor::DoctorDiagnosticEngine engine;
        hotas::doctor::DiagnosticRunOutcome outcome = engine.run(provider, &cancellationRequested);
        reconcileIncompleteJournals(outcome.snapshot);
        if (dryRunRequested) {
            // A real-machine Phase 4 dry run is intentionally planning-only:
            // no UAC, helper launch, installer, restart, or HidHide mutation.
            // A missing exact approved package is a successful safe block,
            // not a reason to substitute "latest" or an arbitrary cache.
            const hotas::doctor::RepairPlanProposal proposal = hotas::doctor::RepairPlanner().propose(
                outcome.session, outcome.snapshot, false);
            if (!proposal.plan.operations.isEmpty()) {
                hotas::doctor::RepairJournalStore journal;
                hotas::doctor::RepairTransactionCoordinator().dryRun(proposal, outcome.snapshot.environment,
                    outcome.session.id(), journal);
            }
        }
        stampBuildProvenance(outcome);
        if (!reportPath.isEmpty()) {
            QFile report(reportPath);
            if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
            report.write(hotas::doctor::DoctorDiagnosticEngine::serializeJson(outcome, true));
        }
        return outcome.cancelled ? 2 : 0;
    }
    // The RunOnce continuation is deliberately just a normal Doctor launch
    // with a stable transaction reference. ScanController independently
    // enumerates durable AwaitingReboot records and performs no replay.
    Q_UNUSED(resumeTransaction);
    hotas::doctor::DoctorDiagnosticEngine diagnosticEngine;
    hotas::doctor::DoctorSession prepared = diagnosticEngine.createPreparedSession();
    hotas::doctor::DoctorSessionViewModel viewModel(prepared, buildIdentity);
    viewModel.setCopyAction([](QString text) { QGuiApplication::clipboard()->setText(text); });
    viewModel.setIntegrationNotice(integrationNotice, componentMismatch);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("doctorSession"), &viewModel);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(u"HidHideDoctor"_qs, u"HidHideDoctorMain"_qs);
    if (engine.rootObjects().isEmpty()) return -1;
    if (instanceServer) {
        QObject::connect(&*instanceServer, &QLocalServer::newConnection, &application,
            [&engine, &application, server = &*instanceServer] {
                while (QLocalSocket *socket = server->nextPendingConnection()) {
                    QObject::connect(socket, &QLocalSocket::readyRead, &application,
                        [&engine, socket] {
                            const QByteArray payload = socket->readAll();
                            const QString forwardedToken = QString::fromLatin1(payload).trimmed();
                            if (payload.size() == 32 && hotas::doctor::isValidDoctorIntegrationSessionId(forwardedToken)) {
                                const hotas::doctor::DoctorIntegrationReadResult forwarded =
                                    hotas::doctor::consumeDoctorLaunchContext(forwardedToken);
                                if (forwarded.accepted) {
                                    hotas::doctor::writeDoctorIntegrationResult(forwarded.context, QStringLiteral("Doctor opened"),
                                        QStringLiteral("An existing HidHide Doctor window was focused; no second process was started."));
                                }
                            }
                            focusDoctorWindow(engine);
                            socket->disconnectFromServer();
                            socket->deleteLater();
                        });
                    QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
    }
    ScanController controller(viewModel, reportPath);
    viewModel.setScanActions([&controller] { controller.cancel(); }, [&controller] { controller.start(); });
    if (integration.accepted) {
        hotas::doctor::writeDoctorIntegrationResult(integration.context, QStringLiteral("Doctor opened"),
            componentMismatch ? QStringLiteral("Component version mismatch; repair is blocked.") : QStringLiteral("Independent diagnosis is starting."));
        bool diagnosisResultReported = false;
        QObject::connect(&viewModel, &hotas::doctor::DoctorSessionViewModel::sessionChanged, &application,
            [&viewModel, &integration, &diagnosisResultReported] {
                if (diagnosisResultReported || viewModel.scanRunning()) return;
                diagnosisResultReported = true;
                hotas::doctor::writeDoctorIntegrationResult(integration.context, QStringLiteral("Diagnosis complete"),
                    QStringLiteral("A fresh read-only scan completed; HOTAS BF6 may refresh its low-frequency readiness view."));
            });
    }
    bool scanSmokeFinished = false;
    if (scanSmoke) {
        QObject::connect(&viewModel, &hotas::doctor::DoctorSessionViewModel::sessionChanged, &application,
            [&application, &viewModel, &scanSmokeFinished] {
                if (!viewModel.scanRunning() && !scanSmokeFinished) {
                    scanSmokeFinished = true;
                    QTimer::singleShot(750, &application, &QCoreApplication::quit);
                }
            });
        // A scan-smoke timeout is a test failure. It remains a read-only
        // verification process and never claims an interactive owner window.
        QTimer::singleShot(30000, &application, [] { QCoreApplication::exit(2); });
    }
    controller.start();
    if (hasArgument(argc, argv, "--startup-smoke")) QTimer::singleShot(0, &application, &QCoreApplication::quit);
    const int result = application.exec();
    controller.stop();
    return result;
}
