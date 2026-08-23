#include "app_backend.h"

#include <QGuiApplication>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));
    application.setApplicationVersion(QStringLiteral("1.5.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    hotas::AppBackend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    QObject::connect(&engine, &QQmlEngine::warnings, &application,
        [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                std::fprintf(stderr, "%s\n", qPrintable(warning.toString()));
            }
        });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
    if (engine.rootObjects().isEmpty()) return -1;
    return application.exec();
}
