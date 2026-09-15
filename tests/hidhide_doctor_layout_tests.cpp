#include "doctor_session.h"
#include "doctor_session_view_model.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace hotas::doctor;

namespace {
QQuickItem *item(QObject *root, const char *objectName)
{
    return root->findChild<QQuickItem *>(QString::fromLatin1(objectName));
}

void loadFixture(QQmlApplicationEngine &engine, DoctorSessionViewModel &model)
{
    engine.rootContext()->setContextProperty(QStringLiteral("doctorSession"), &model);
    engine.load(QUrl::fromLocalFile(QStringLiteral(HOTAS_DOCTOR_SOURCE_ROOT) + QStringLiteral("/qml/HidHideDoctorMain.qml")));
}

void sizeAndShow(QQuickWindow *window, int width, int height)
{
    window->setWidth(width);
    window->setHeight(height);
    window->show();
    QTest::qWait(120);
}
} // namespace

class HidHideDoctorLayoutTests final : public QObject {
    Q_OBJECT
private slots:
    void focusMaximizedFixtureUsesTheWorkspaceWidth();
    void commandCenterFixtureHonorsFourPaneMinimums();
    void commandCenterTimelineFixtureKeepsTimelineBoundedAndWorkspaceUsable();
};

void HidHideDoctorLayoutTests::focusMaximizedFixtureUsesTheWorkspaceWidth()
{
    DoctorSession session = createPhase0FixtureSession();
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(false);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY2(!engine.rootObjects().isEmpty(), "Focus layout fixture must load the native Doctor QML.");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1440, 900);
    QQuickItem *focus = item(window, "focusWorkspace");
    QVERIFY(focus);
    QVERIFY2(focus->width() >= 1300, "Focus narrative must use the wide workspace rather than collapse into columns.");
    QVERIFY(focus->height() >= 300);
    const QImage screenshot = window->grabWindow();
    QVERIFY(!screenshot.isNull());
    QVERIFY2(screenshot.width() >= 1440 && screenshot.height() >= 900,
        "The offscreen screenshot must cover the full Focus fixture at every supported scale factor.");
}

void HidHideDoctorLayoutTests::commandCenterFixtureHonorsFourPaneMinimums()
{
    DoctorSession session = createPhase0FixtureSession();
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY2(!engine.rootObjects().isEmpty(), "Command Center layout fixture must load the native Doctor QML.");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1680, 1024);
    QQuickItem *workspace = item(window, "commandPaneSplit");
    QQuickItem *plan = item(window, "commandPlanPane");
    QQuickItem *current = item(window, "commandCurrentPane");
    QQuickItem *findings = item(window, "commandFindingsPane");
    QQuickItem *action = item(window, "commandActionPane");
    QVERIFY(workspace && plan && current && findings && action);
    QVERIFY2(plan->width() >= 300, "Diagnostic plan pane must not collapse below its readable width.");
    QVERIFY2(current->width() >= 280, "Current operation pane must not collapse below its readable width.");
    QVERIFY2(findings->width() >= 340, "Findings pane must preserve diagnosis readability.");
    QVERIFY2(action->width() >= 260, "Action pane must preserve readable remediation guidance.");
    QVERIFY(workspace->width() >= plan->width() + current->width() + findings->width() + action->width());
    const QImage screenshot = window->grabWindow();
    QVERIFY(!screenshot.isNull());
}

void HidHideDoctorLayoutTests::commandCenterTimelineFixtureKeepsTimelineBoundedAndWorkspaceUsable()
{
    DoctorSession session = createPhase0FixtureSession();
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    model.setLiveEvidenceVisible(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY2(!engine.rootObjects().isEmpty(), "Timeline layout fixture must load the native Doctor QML.");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1680, 1024);
    QQuickItem *workspace = item(window, "commandPaneSplit");
    QQuickItem *timeline = item(window, "commandActivityTimeline");
    QVERIFY(workspace && timeline);
    QVERIFY2(timeline->isVisible() && timeline->height() >= 128 && timeline->height() <= 360,
        "The Command Center activity timeline must remain a bounded lower panel.");
    QVERIFY2(workspace->height() >= 200, "The four-pane workspace must remain usable with the activity timeline visible.");
    const QImage screenshot = window->grabWindow();
    QVERIFY(!screenshot.isNull());
}

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS BF6 Tests"));
    application.setApplicationName(QStringLiteral("HidHide Doctor Layout Fixtures"));
    HidHideDoctorLayoutTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "hidhide_doctor_layout_tests.moc"
