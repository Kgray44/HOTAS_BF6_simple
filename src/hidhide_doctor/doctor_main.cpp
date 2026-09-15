#include "doctor_diagnostics.h"
#include "doctor_fixtures.h"
#include "doctor_session.h"
#include "doctor_session_view_model.h"
#include "hotas_build_version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSysInfo>
#include <QTimer>

#include <atomic>
#include <cstring>
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

void stampBuildProvenance(hotas::doctor::DiagnosticRunOutcome &outcome)
{
    outcome.snapshot.build.version = QString::fromLatin1(HOTAS_BF6_VERSION);
    outcome.snapshot.build.sourceRevision = QStringLiteral(HOTAS_BF6_BUILD_ID);
    outcome.snapshot.build.processArchitecture = QSysInfo::buildCpuArchitecture();
}

// The controller owns a single bounded worker for a scan.  It never exposes a
// repair operation; cancellation simply tells the observational provider not
// to schedule further reads and safely joins the worker on shutdown.
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
        hotas::doctor::DoctorDiagnosticEngine engine;
        m_model.replaceSession(engine.createPreparedSession());
        m_worker = std::thread([this, generation] {
            hotas::doctor::ReadOnlyWindowsDiagnosticProvider provider;
            hotas::doctor::DoctorDiagnosticEngine engine;
            hotas::doctor::DiagnosticRunOutcome outcome = engine.run(provider, &m_cancelled,
                [this, generation](const hotas::doctor::DoctorSession &session) {
                    const hotas::doctor::DoctorSession copy = session;
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, generation, copy] {
                        if (generation == m_generation.load()) m_model.replaceSession(copy);
                    }, Qt::QueuedConnection);
                });
            stampBuildProvenance(outcome);
            if (!m_reportPath.isEmpty()) {
                QFile report(m_reportPath);
                if (report.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    report.write(hotas::doctor::DoctorDiagnosticEngine::serializeJson(outcome, true));
            }
            const hotas::doctor::DoctorSession finished = outcome.session;
            m_running.store(false);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, generation, finished] {
                if (generation == m_generation.load()) m_model.replaceSession(finished);
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
        hotas::doctor::DiagnosticRunOutcome outcome = diagnosticEngine.run(provider);
        outcome.session.setSessionLabel(fixtureLabel);
        stampBuildProvenance(outcome);
        if (!reportPath.isEmpty()) {
            QFile report(reportPath);
            if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
            report.write(hotas::doctor::DoctorDiagnosticEngine::serializeJson(outcome, true));
        }
        if (hasArgument(argc, argv, "--headless")) return 0;
        hotas::doctor::DoctorSessionViewModel viewModel(outcome.session, buildIdentity);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doctorSession"), &viewModel);
        engine.loadFromModule(u"HidHideDoctor"_qs, u"HidHideDoctorMain"_qs);
        if (engine.rootObjects().isEmpty()) return -1;
        if (hasArgument(argc, argv, "--startup-smoke")) return 0;
        return application.exec();
    }
    if (hasArgument(argc, argv, "--headless")) {
        std::atomic_bool cancellationRequested{false};
        hotas::doctor::ReadOnlyWindowsDiagnosticProvider provider;
        hotas::doctor::DoctorDiagnosticEngine engine;
        hotas::doctor::DiagnosticRunOutcome outcome = engine.run(provider, &cancellationRequested);
        stampBuildProvenance(outcome);
        if (!reportPath.isEmpty()) {
            QFile report(reportPath);
            if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
            report.write(hotas::doctor::DoctorDiagnosticEngine::serializeJson(outcome, true));
        }
        return outcome.cancelled ? 2 : 0;
    }
    hotas::doctor::DoctorDiagnosticEngine diagnosticEngine;
    hotas::doctor::DoctorSession prepared = diagnosticEngine.createPreparedSession();
    hotas::doctor::DoctorSessionViewModel viewModel(prepared, buildIdentity);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("doctorSession"), &viewModel);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(u"HidHideDoctor"_qs, u"HidHideDoctorMain"_qs);
    if (engine.rootObjects().isEmpty()) return -1;
    ScanController controller(viewModel, reportPath);
    viewModel.setScanActions([&controller] { controller.cancel(); }, [&controller] { controller.start(); });
    controller.start();
    if (hasArgument(argc, argv, "--startup-smoke")) QTimer::singleShot(0, &application, &QCoreApplication::quit);
    const int result = application.exec();
    controller.stop();
    return result;
}
