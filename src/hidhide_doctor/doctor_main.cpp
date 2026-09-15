#include "doctor_session.h"
#include "doctor_session_view_model.h"
#include "hotas_build_version.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSysInfo>

#include <cstring>

namespace {
bool hasArgument(int argc, char *argv[], const char *argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) return true;
    }
    return false;
}
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

    // Phase 0 intentionally has no live provider. This durable fixture proves
    // standalone/session/presentation ownership without reading or changing
    // the machine's HidHide, HOTAS BF6, profile, or Device Rig state.
    hotas::doctor::DoctorSession session = hotas::doctor::createPhase0FixtureSession();
    const QString buildIdentity = QStringLiteral("Development build %1 · %2 · %3")
        .arg(QString::fromLatin1(HOTAS_BF6_VERSION), QStringLiteral(HOTAS_BF6_BUILD_ID), QSysInfo::buildCpuArchitecture());
    hotas::doctor::DoctorSessionViewModel viewModel(session, buildIdentity);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("doctorSession"), &viewModel);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(u"HidHideDoctor"_qs, u"HidHideDoctorMain"_qs);
    if (engine.rootObjects().isEmpty()) return -1;
    if (hasArgument(argc, argv, "--startup-smoke")) return 0;
    return application.exec();
}
