#include "app_backend.h"
#include "theme_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStringList>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace {

bool failAutomationEditorTest(const QString &message)
{
    const QByteArray encoded = message.toUtf8();
    std::fputs(encoded.constData(), stderr);
    std::fputc('\n', stderr);
    qCritical().noquote() << QStringLiteral("Automation QML interaction test failed: %1").arg(message);
    return false;
}

QVariantMap draftRow(QObject *root, const char *collection, int index)
{
    const QVariantList rows = root->property("draft").toMap().value(QString::fromLatin1(collection)).toList();
    return index >= 0 && index < rows.size() ? rows.at(index).toMap() : QVariantMap{};
}

bool evaluateEditorFunction(QObject *root, const QString &expression)
{
    QQmlExpression call(qmlContext(root), root,
        QStringLiteral("(function() { %1; return true; })()").arg(expression));
    call.evaluate();
    if (call.hasError()) return failAutomationEditorTest(call.error().toString());
    QCoreApplication::processEvents();
    return true;
}

bool verifyAutomationEditorInteraction(hotas::AppBackend &backend)
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule(u"HOTASMapperStartupTest"_qs, u"AutomationPage"_qs);
    if (component.status() != QQmlComponent::Ready) {
        return failAutomationEditorTest(component.errorString());
    }

    QObject *root = component.createWithInitialProperties(
        {{QStringLiteral("backendObject"), QVariant::fromValue(static_cast<QObject *>(&backend))}});
    if (!root) return failAutomationEditorTest(component.errorString());
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(root);
    if (!rootItem) {
        delete root;
        return failAutomationEditorTest(QStringLiteral("AutomationPage did not create a QQuickItem"));
    }
    QQuickWindow window;
    window.resize(1280, 720);
    rootItem->setParentItem(window.contentItem());
    rootItem->setSize(window.size());
    window.show();
    const QString automationId = backend.createAutomation();
    if (automationId.isEmpty()) {
        delete root;
        return failAutomationEditorTest(QStringLiteral("backend could not create an Automation draft"));
    }
    QCoreApplication::processEvents();
    QQmlExpression openEditor(qmlContext(root), root,
        QStringLiteral("openRuleById('%1')").arg(automationId));
    openEditor.evaluate();
    if (openEditor.hasError() || !root->property("editing").toBool()) {
        delete root;
        return failAutomationEditorTest(openEditor.hasError() ? openEditor.error().toString()
                                                               : QStringLiteral("Automation editor did not open"));
    }
    QQmlExpression prepare(qmlContext(root), root,
        u"(function() { addEffect(); addEffect(); setTriggerMode(false); addRequirement(0); addRequirement(0); return true; })()"_qs);
    prepare.evaluate();
    if (prepare.hasError()) {
        delete root;
        return failAutomationEditorTest(prepare.error().toString());
    }
    QCoreApplication::processEvents();

    struct ActionCase {
        int choice;
        int type;
    };
    constexpr ActionCase actionCases[] = {
        {1, 0}, {2, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {7, 6},
        {8, 7}, {9, 8}, {10, 9}, {11, 10}, {12, 11}, {13, 12}, {14, 13},
    };
    bool passed = true;
    for (const ActionCase &test : actionCases) {
        if (!evaluateEditorFunction(root, QStringLiteral("setEffectType(0, %1)").arg(test.choice))) {
            passed = false;
            break;
        }
        if (draftRow(root, "actions", 0).value(QStringLiteral("type")).toInt() != test.type) {
            passed = failAutomationEditorTest(QStringLiteral("action choice %1 updated the wrong draft type").arg(test.choice));
            break;
        }
    }

    if (passed) {
        passed = evaluateEditorFunction(root, u"setEffectType(0, 4); setEffectType(1, 11)"_qs)
            && draftRow(root, "actions", 0).value(QStringLiteral("type")).toInt() == 3
            && draftRow(root, "actions", 1).value(QStringLiteral("type")).toInt() == 10;
        if (!passed) passed = failAutomationEditorTest(QStringLiteral("multiple action rows did not update independently"));
    }
    if (passed) {
        passed = evaluateEditorFunction(root, u"setRequirementKind(0, 1); setRequirementKind(1, 2)"_qs)
            && draftRow(root, "conditions", 0).value(QStringLiteral("type")).toInt() == 5
            && draftRow(root, "conditions", 1).value(QStringLiteral("type")).toInt() == 1;
        if (!passed) passed = failAutomationEditorTest(QStringLiteral("multiple condition rows did not update independently"));
    }
    delete root;
    return passed;
}

}

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

    if (!verifyAutomationEditorInteraction(backend)) return 1;

    QTimer::singleShot(250, &application, &QCoreApplication::quit);
    return application.exec();
}
