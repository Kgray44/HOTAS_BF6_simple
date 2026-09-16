#include "doctor_diagnostics.h"
#include "doctor_fixtures.h"
#include "doctor_session.h"
#include "doctor_session_view_model.h"

#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QtTest>

#include <algorithm>
#include <utility>

using namespace hotas::doctor;

namespace {
QQuickItem *item(QObject *root, const char *objectName)
{
    return root->findChild<QQuickItem *>(QString::fromLatin1(objectName));
}

DoctorSession completedFixtureSession(bool includeLongDiagnosis = false)
{
    QString label;
    FixtureDiagnosticProvider provider(createDevelopmentFixture(QStringLiteral("GetWhitelist 0x57"), &label));
    DoctorDiagnosticEngine engine;
    DiagnosticRunOutcome outcome = engine.run(provider);
    outcome.session.setSessionLabel(label);
    if (includeLongDiagnosis) {
        Diagnosis diagnosis;
        diagnosis.id = DiagnosisId(QStringLiteral("HD-DIAG-LONG-LAYOUT-FIXTURE"));
        diagnosis.signatureId = KnowledgeSignatureId(QStringLiteral("HD-KB-LONG-LAYOUT-FIXTURE"));
        diagnosis.role = DiagnosisRole::Contributing;
        diagnosis.confidence = DiagnosisConfidence::High;
        diagnosis.severity = FindingSeverity::Warning;
        diagnosis.title = QStringLiteral("Long fixture diagnosis title proving that a readable engineering card continues to wrap without colliding with its confidence badge");
        diagnosis.humanExplanation = QStringLiteral("This synthetic layout-only fixture intentionally uses a long explanatory paragraph to exercise wrapping, spacing, and scrolling without changing any production diagnosis rule.");
        diagnosis.userImpact = QStringLiteral("Long impact text verifies that card borders, readable line height, and custom scroll margins continue to protect content at narrow and wide measures.");
        diagnosis.confidenceExplanation.score = 91;
        outcome.session.appendDiagnosis(std::move(diagnosis));
    }
    return outcome.session;
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
    QTest::qWait(160);
}

bool captureSnapshot(QQuickWindow *window, const QString &name)
{
    const QString directory = qEnvironmentVariable("HOTAS_DOCTOR_LAYOUT_CAPTURE_DIR");
    if (directory.isEmpty()) return true;
    if (!QDir().mkpath(directory)) return false;
    return window->grabWindow().save(QDir(directory).filePath(name + QStringLiteral(".png")));
}

bool containsItem(const QVariantList &items, const QVariant &needle)
{
    return std::any_of(items.cbegin(), items.cend(), [&](const QVariant &item) { return item == needle; });
}

bool containsDiagnosisCard(const QVariantList &cards, const QString &id)
{
    return std::any_of(cards.cbegin(), cards.cend(), [&](const QVariant &card) {
        return card.toMap().value(QStringLiteral("id")).toString() == id;
    });
}

void clearPresentationSettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("hidhideDoctorPhase2"));
    settings.sync();
}
} // namespace

class HidHideDoctorLayoutTests final : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void wideFocusCompletedUsesNarrativeMeasureAndReadableCards();
    void commandCenterUsesRebalancedFractionsAndReadableFindings();
    void commandCenterTimelineStaysBoundedAndAbsentFromFocus();
    void densityModesKeepDiagnosisCardsUsable();
    void narrowCommandCenterUsesFocusFallbackWithoutDeadTimelineSpace();
    void keyboardActivationUsesTheCustomPresentationAndPaneControls();
    void persistedSplitModeAndResetLayoutSurviveTheTortureSequence();
    void commandCenterDragOwnsLayoutAndBottomDocksCoexist();
};

void HidHideDoctorLayoutTests::init()
{
    clearPresentationSettings();
}

void HidHideDoctorLayoutTests::cleanup()
{
    clearPresentationSettings();
}

void HidHideDoctorLayoutTests::wideFocusCompletedUsesNarrativeMeasureAndReadableCards()
{
    DoctorSession session = completedFixtureSession(true);
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(false);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY2(!engine.rootObjects().isEmpty(), "Wide Focus fixture must load the native Doctor QML.");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1680, 1050);

    QQuickItem *focus = item(window, "focusWorkspace");
    QQuickItem *narrative = item(window, "focusNarrativeColumn");
    QQuickItem *findings = item(window, "focusFindingsPane");
    QQuickItem *completed = item(window, "completedOperationSummary");
    QVERIFY(focus && narrative && findings && completed);
    const qreal narrativeShare = narrative->width() / focus->width();
    QVERIFY2(narrativeShare >= 0.55 && narrativeShare <= 0.65,
        "Wide Focus must use a deliberate 55-65% report measure, not a narrow left rail or edge-to-edge cards.");
    QVERIFY2(qAbs(findings->width() - narrative->width()) <= 1.0,
        "The Focus findings pane must use the centered narrative measure without a rounding-induced layout drift.");
    QVERIFY(completed->height() >= 150);
    QVERIFY2(containsDiagnosisCard(model.diagnosisCards(), QStringLiteral("HD-DIAG-LONG-LAYOUT-FIXTURE")),
        "The long layout fixture must reach the real diagnosis-card model.");
    QVERIFY2(captureSnapshot(window, QStringLiteral("wide-focus-completed")), "Focus review capture could not be written.");
}

void HidHideDoctorLayoutTests::commandCenterUsesRebalancedFractionsAndReadableFindings()
{
    DoctorSession session = completedFixtureSession(true);
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY2(!engine.rootObjects().isEmpty(), "Wide Command Center fixture must load the native Doctor QML.");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 2200, 1100);

    QQuickItem *workspace = item(window, "commandPaneSplit");
    QQuickItem *plan = item(window, "commandPlanPane");
    QQuickItem *current = item(window, "commandCurrentPane");
    QQuickItem *findings = item(window, "commandFindingsPane");
    QQuickItem *action = item(window, "commandActionPane");
    QVERIFY(workspace && plan && current && findings && action);
    QVERIFY2(plan->width() > findings->width(), "The wide planning rail must receive the canonical first proportion.");
    QVERIFY2(findings->width() >= 400, "Findings must retain a readable card measure at a wide desktop size.");
    QVERIFY2(current->width() >= 280 && action->width() >= 260, "Operational and action panes must honor practical minimums.");
    QVERIFY(workspace->width() >= plan->width() + current->width() + findings->width() + action->width());
    QVERIFY2(containsDiagnosisCard(model.diagnosisCards(), QStringLiteral("HD-DIAG-LONG-LAYOUT-FIXTURE")),
        "The long layout fixture must reach the real diagnosis-card model.");
    QVERIFY2(findings->width() >= 400, "Long diagnosis text must have a useful card measure rather than a newspaper column.");
    QVERIFY2(captureSnapshot(window, QStringLiteral("wide-command-center")), "Command Center review capture could not be written.");
}

void HidHideDoctorLayoutTests::commandCenterTimelineStaysBoundedAndAbsentFromFocus()
{
    DoctorSession session = completedFixtureSession();
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    model.setLiveEvidenceVisible(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY2(!engine.rootObjects().isEmpty(), "Timeline fixture must load the native Doctor QML.");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1920, 1080);

    QQuickItem *workspace = item(window, "commandPaneSplit");
    QQuickItem *timeline = item(window, "commandActivityTimeline");
    QVERIFY(workspace && timeline);
    QVERIFY2(timeline->isVisible() && timeline->height() >= 128 && timeline->height() <= 360,
        "The Command Center activity timeline must remain a bounded lower panel.");
    QVERIFY2(workspace->height() >= 200, "The four-pane workspace must remain usable with the activity timeline visible.");
    QVERIFY2(captureSnapshot(window, QStringLiteral("wide-command-center-timeline")), "Timeline review capture could not be written.");

    model.setCommandCenter(false);
    QTest::qWait(180);
    QVERIFY(item(window, "focusWorkspace"));
    QVERIFY2(!item(window, "commandActivityTimeline"), "Focus must reclaim the timeline area; Activity remains Command Center-only.");
}

void HidHideDoctorLayoutTests::densityModesKeepDiagnosisCardsUsable()
{
    DoctorSession session = completedFixtureSession(true);
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1920, 1080);

    for (const QString &density : {QStringLiteral("Comfortable"), QStringLiteral("Compact"), QStringLiteral("Dense")}) {
        model.setDensity(density);
        QTest::qWait(120);
        QQuickItem *findings = item(window, "commandFindingsPane");
        QVERIFY2(findings && containsDiagnosisCard(model.diagnosisCards(), QStringLiteral("HD-DIAG-LONG-LAYOUT-FIXTURE")),
            qPrintable(density + QStringLiteral(" mode must retain findings and long-card content.")));
        QVERIFY2(findings->width() >= 360 && findings->height() > 80,
            qPrintable(density + QStringLiteral(" mode must retain a legible diagnosis-card measure rather than merely shrinking text.")));
    }
    model.setDensity(QStringLiteral("Compact"));
}

void HidHideDoctorLayoutTests::narrowCommandCenterUsesFocusFallbackWithoutDeadTimelineSpace()
{
    DoctorSession session = completedFixtureSession(true);
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    model.setLiveEvidenceVisible(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1180, 900);

    QQuickItem *focus = item(window, "focusWorkspace");
    QQuickItem *narrative = item(window, "focusNarrativeColumn");
    QVERIFY(focus && narrative);
    QVERIFY(!item(window, "commandWorkspace"));
    QVERIFY(!item(window, "commandActivityTimeline"));
    const qreal narrativeShare = narrative->width() / focus->width();
    QVERIFY(narrativeShare >= 0.55 && narrativeShare <= 0.70);
    QVERIFY2(captureSnapshot(window, QStringLiteral("narrow-focus-fallback")), "Narrow review capture could not be written.");
}

void HidHideDoctorLayoutTests::keyboardActivationUsesTheCustomPresentationAndPaneControls()
{
    DoctorSession session = completedFixtureSession();
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(false);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1680, 1024);

    QTest::keyClick(window, Qt::Key_Tab);
    QTest::keyClick(window, Qt::Key_Tab);
    QTest::keyClick(window, Qt::Key_Space);
    QTest::qWait(120);
    QVERIFY(model.commandCenter());
    QVERIFY(item(window, "commandWorkspace"));

}

void HidHideDoctorLayoutTests::persistedSplitModeAndResetLayoutSurviveTheTortureSequence()
{
    DoctorSession session = completedFixtureSession(true);
    DoctorSessionViewModel first(session, QStringLiteral("layout-fixture"));
    first.setCommandCenter(true);
    first.setDensity(QStringLiteral("Comfortable"));
    first.setLiveEvidenceVisible(true);
    const QVariantList draggedFractions{0.43, 0.14, 0.27, 0.16};
    first.savePaneFractions(draggedFractions);
    QCOMPARE(first.paneFractions(), draggedFractions);

    DoctorSession reopenedSession = completedFixtureSession(true);
    DoctorSessionViewModel restored(reopenedSession, QStringLiteral("layout-fixture"));
    QCOMPARE(restored.paneFractions(), draggedFractions);
    QVERIFY(restored.commandCenter());
    QCOMPARE(restored.density(), QStringLiteral("Comfortable"));
    QVERIFY(restored.liveEvidenceVisible());

    QQmlApplicationEngine engine;
    loadFixture(engine, restored);
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 1920, 1080);

    restored.setMaximizedPane(QStringLiteral("plan"));
    QTest::qWait(120);
    QVERIFY(item(window, "commandPlanPane")->isVisible());
    QVERIFY(!item(window, "commandFindingsPane")->isVisible());
    restored.setMaximizedPane(QString());
    QTest::qWait(120);
    QVERIFY(item(window, "commandFindingsPane")->isVisible());
    restored.setMaximizedPane(QStringLiteral("findings"));
    QTest::qWait(120);
    QVERIFY(item(window, "commandFindingsPane")->isVisible());
    QVERIFY(!item(window, "commandPlanPane")->isVisible());
    restored.setMaximizedPane(QString());

    restored.setCommandCenter(false);
    QTest::qWait(120);
    QVERIFY(item(window, "focusWorkspace"));
    QVERIFY(!item(window, "commandActivityTimeline"));
    restored.setCommandCenter(true);
    QTest::qWait(120);
    QVERIFY(item(window, "commandPaneSplit"));

    for (const QString &density : {QStringLiteral("Comfortable"), QStringLiteral("Compact"), QStringLiteral("Dense"), QStringLiteral("Compact")}) {
        restored.setDensity(density);
        QTest::qWait(80);
        QVERIFY(item(window, "commandFindingsPane"));
    }
    restored.setLiveEvidenceVisible(true);
    QTest::qWait(80);
    window->setWidth(1180);
    QTest::qWait(140);
    QVERIFY(item(window, "focusWorkspace"));
    QVERIFY(!item(window, "commandActivityTimeline"));
    window->setWidth(1920);
    QTest::qWait(160);
    QVERIFY(item(window, "commandPaneSplit"));
    QVERIFY(item(window, "commandActivityTimeline")->isVisible());

    restored.resetWorkspaceLayout();
    QTest::qWait(120);
    QCOMPARE(restored.paneFractions(), DoctorSessionViewModel::defaultPaneFractions());
    QCOMPARE(restored.density(), QStringLiteral("Compact"));
    QVERIFY(restored.commandCenter());
    QVERIFY(!restored.liveEvidenceVisible());
    QVERIFY(!containsItem(restored.paneFractions(), QVariant(0.43)));
    restored.setCommandCenter(false);
    QTest::qWait(120);
    QVERIFY(item(window, "focusWorkspace"));
    QVERIFY(!item(window, "commandActivityTimeline"));
}

void HidHideDoctorLayoutTests::commandCenterDragOwnsLayoutAndBottomDocksCoexist()
{
    DoctorSession session = completedFixtureSession(true);
    DoctorSessionViewModel model(session, QStringLiteral("layout-fixture"));
    model.setCommandCenter(true);
    QQmlApplicationEngine engine;
    loadFixture(engine, model);
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
    QVERIFY(window);
    sizeAndShow(window, 2200, 1100);

    QQuickItem *workspace = item(window, "commandPaneSplit");
    QQuickItem *handle = item(window, "commandPaneSplitHandle");
    QQuickItem *plan = item(window, "commandPlanPane");
    QQuickItem *current = item(window, "commandCurrentPane");
    QQuickItem *findings = item(window, "commandFindingsPane");
    QQuickItem *action = item(window, "commandActionPane");
    QVERIFY(workspace && handle && plan && current && findings && action);
    const QPoint dragStart = handle->mapToScene(handle->boundingRect().center()).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(window, dragStart + QPoint(90, 0), 80);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragStart + QPoint(90, 0));
    QTest::qWait(250);

    const QList<qreal> afterRelease{plan->width(), current->width(), findings->width(), action->width()};
    QVERIFY2(afterRelease[0] > 0 && afterRelease[1] > 0 && afterRelease[2] > 0 && afterRelease[3] > 0,
        "A released divider must retain four usable panes.");
    QTest::qWait(10000);
    const QList<qreal> afterIdle{plan->width(), current->width(), findings->width(), action->width()};
    for (int index = 0; index < afterRelease.size(); ++index)
        QVERIFY2(qAbs(afterIdle[index] - afterRelease[index]) <= 1.0,
            "No pane may drift or fight back after its divider is released.");

    model.setLiveEvidenceVisible(true);
    QObject *inspectorState = window->findChild<QObject *>(QStringLiteral("evidenceInspectorState"));
    QVERIFY(inspectorState);
    inspectorState->setProperty("visible", true);
    QTest::qWait(220);
    QQuickItem *activity = item(window, "commandActivityTimelineDual");
    QQuickItem *inspector = item(window, "commandEvidenceInspectorDual");
    QQuickItem *dockHandle = item(window, "commandBottomDockHandle");
    QVERIFY(activity && inspector && dockHandle && activity->isVisible() && inspector->isVisible());
    QVERIFY2(activity->x() + activity->width() <= inspector->x() + 1.0,
        "Activity and Inspector must share the bottom dock without overlap.");
    const qreal activityWidthBeforeDockDrag = activity->width();
    const QPoint dockDragStart = dockHandle->mapToScene(dockHandle->boundingRect().center()).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dockDragStart);
    QTest::mouseMove(window, dockDragStart + QPoint(70, 0), 80);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dockDragStart + QPoint(70, 0));
    QTest::qWait(120);
    QVERIFY2(qAbs(activity->width() - activityWidthBeforeDockDrag) > 1.0,
        "The shared bottom dock divider must be user-adjustable.");
    const QVariantList persistedDockFractions = model.bottomDockFractions();
    QCOMPARE(persistedDockFractions.size(), 2);
    QVERIFY2(qAbs(persistedDockFractions[0].toDouble() + persistedDockFractions[1].toDouble() - 1.0) <= 0.015,
        "A released bottom dock divider must persist normalized user intent.");
    const QList<qreal> afterDock{plan->width(), current->width(), findings->width(), action->width()};
    for (int index = 0; index < afterRelease.size(); ++index)
        QVERIFY2(qAbs(afterDock[index] - afterRelease[index]) <= 1.0,
            "Opening a bottom dock must not rebalance horizontal panes.");

    model.setDensity(QStringLiteral("Comfortable"));
    QTest::qWait(120);
    QQuickItem *planHeader = item(window, "doctorPaneHeaderTitle_DIAGNOSTIC PLAN");
    QVERIFY(planHeader);
    const int comfortableFont = planHeader->property("font").value<QFont>().pixelSize();
    model.setDensity(QStringLiteral("Dense"));
    QTest::qWait(120);
    const int denseFont = planHeader->property("font").value<QFont>().pixelSize();
    QVERIFY2(comfortableFont > denseFont, "Density modes must change actual Doctor typography, not only geometry.");
    const QList<qreal> afterDensity{plan->width(), current->width(), findings->width(), action->width()};
    for (int index = 0; index < afterRelease.size(); ++index)
        QVERIFY2(qAbs(afterDensity[index] - afterRelease[index]) <= 1.0,
            "Density is presentation-only and must not cause horizontal pane drift.");
    QVERIFY2(captureSnapshot(window, QStringLiteral("command-center-stable-dock")), "Stable dock review capture could not be written.");
}

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    application.setOrganizationName(QStringLiteral("HOTAS BF6 Tests"));
    application.setApplicationName(QStringLiteral("HidHide Doctor Layout Fixtures"));
    HidHideDoctorLayoutTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "hidhide_doctor_layout_tests.moc"
