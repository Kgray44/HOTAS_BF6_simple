#include "app_backend.h"
#include "theme_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStringList>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    hotas::AppBackend backend;
    hotas::ThemeManager themeManager;
    const QStringList themes{
        QStringLiteral("Legacy"),
        QStringLiteral("Standard"),
        QStringLiteral("Top Gun"),
    };

    for (const QString &theme : themes) {
        themeManager.setCurrentTheme(theme);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
        engine.loadFromModule(u"HOTASMapperStartupTest"_qs, u"Main"_qs);
        if (engine.rootObjects().isEmpty()
            || !qobject_cast<QWindow *>(engine.rootObjects().constFirst())) return 1;

        application.processEvents();
    }

    QTimer::singleShot(250, &application, &QCoreApplication::quit);
    return application.exec();
}
