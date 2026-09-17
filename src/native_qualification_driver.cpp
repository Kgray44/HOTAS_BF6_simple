#include "native_qualification_driver.h"

#include "app_backend.h"
#include "contention_resilience_controller.h"
#include "controller_discovery.h"
#include "responsiveness_probe.h"
#include "theme_manager.h"
#include "vjoy_ownership.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTest>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <array>

namespace hotas {
namespace {

constexpr std::array<NativeQualificationDriver::Page, 10> kCampaignPages{{
    {8, "Overview"}, {2, "Devices"}, {0, "Axes"}, {1, "Buttons"},
    {6, "Curve Editor"}, {5, "Profiles"}, {9, "Adaptive Response"},
    {7, "Automation"}, {3, "Diagnostics"}, {4, "Settings"},
}};

struct ScrollPage {
    int page = -1;
    const char *name = "";
    const char *surfaceId = "";
};

constexpr std::array<ScrollPage, 10> kScrollPages{{
    {8, "Overview", "flightDeckOverview"},
    {2, "Devices", "flightDeckDevices"},
    {0, "Axes", "flightDeckAxes"},
    {1, "Buttons", "flightDeckButtons"},
    {6, "Curve Editor", "flightDeckCurveEditor"},
    {5, "Profiles", "flightDeckProfiles"},
    {9, "Adaptive Response", "flightDeckAdaptiveResponse"},
    {7, "Automation", "flightDeckAutomation"},
    {3, "Diagnostics", "flightDeckDiagnostics"},
    {4, "Settings", "flightDeckSettings"},
}};

struct ScrollPattern {
    const char *name = "";
    int events = 0;
    int intervalMs = 0;
    int reversalAt = -1;
};

constexpr std::array<ScrollPattern, 4> kScrollPatterns{{
    {"slow", 5, 100, -1},
    {"normal", 10, 35, -1},
    {"rapid", 16, 16, -1},
    {"reverse", 16, 16, 8},
}};

struct ScrollWindowProfile {
    const char *name = "";
    QSize size;
};

const std::array<ScrollWindowProfile, 3> kScrollWindowProfiles{{
    {"normal-1320x840", QSize(1320, 840)},
    {"compact-900x650", QSize(900, 650)},
    {"large-1600x1000", QSize(1600, 1000)},
}};

bool shouldMeasureScrollPage(int windowIndex, const ScrollPage &page)
{
    // Normal and compact cover every in-scope page. The large window is a
    // representative cross-section: static device cards, the telemetry-heavy
    // Adaptive page, and Diagnostics' mixed card/list layout.
    return windowIndex < 2 || page.page == 2 || page.page == 9 || page.page == 3;
}

const ScrollPage *scrollPageForId(int pageId)
{
    const auto it = std::find_if(kScrollPages.cbegin(), kScrollPages.cend(),
        [pageId](const ScrollPage &page) { return page.page == pageId; });
    return it == kScrollPages.cend() ? nullptr : &*it;
}

QString qualificationSummaryPath()
{
    const QString configured = qEnvironmentVariable("HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION_OUTPUT").trimmed();
    if (!configured.isEmpty()) return configured;
    const QString probePath = qEnvironmentVariable("HOTAS_RESPONSIVENESS_PROBE_OUTPUT").trimmed();
    if (probePath.isEmpty()) return {};
    const QFileInfo probeInfo(probePath);
    return probeInfo.dir().filePath(probeInfo.completeBaseName()
                                    + QStringLiteral("-native-qualification.json"));
}

QJsonArray stringArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) result.append(value);
    return result;
}

} // namespace

NativeQualificationDriver::NativeQualificationDriver(QObject *parent)
    : QObject(parent)
{
    m_pages.reserve(static_cast<qsizetype>(kCampaignPages.size()));
    for (const Page &page : kCampaignPages) m_pages.append(page);
    // This timer exists only while the opt-in native qualification session is
    // measuring a real Flickable. It observes the existing contentY property;
    // it neither forces frames nor writes per-movement data.
    m_scrollPositionSampler.setInterval(8);
    m_scrollPositionSampler.setTimerType(Qt::PreciseTimer);
    connect(&m_scrollPositionSampler, &QTimer::timeout, this, [this] {
        if (!m_scrollSessionActive || !m_scrollViewport) return;
        if (auto *probe = ResponsivenessProbe::active())
            probe->recordScrollPosition(m_scrollViewport->property("contentY").toReal());
    });
}

bool NativeQualificationDriver::requested()
{
    return qEnvironmentVariableIntValue("HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION") != 0;
}

void NativeQualificationDriver::start(QCoreApplication *application, AppBackend *backend,
                                      ThemeManager *themeManager, QQuickWindow *window)
{
    m_application = application;
    m_backend = backend;
    m_themeManager = themeManager;
    m_window = window;
    m_tortureSeconds = std::clamp(qEnvironmentVariableIntValue(
        "HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION_TORTURE_SECONDS"), 0, 180);
    if (!m_application || !m_backend || !m_themeManager || !m_window) {
        fail(QStringLiteral("native qualification did not receive the application, backend, theme manager, and QQuickWindow"));
        finish();
        return;
    }
    QTimer::singleShot(120, this, [this] { initializeSurface(); });
}

void NativeQualificationDriver::initializeSurface()
{
    m_themeManager->setCurrentTheme(QStringLiteral("Standard"));
    m_themeManager->setFlightDeckAppearance(QStringLiteral("Dark"));
    m_themeManager->setCurrentExperience(QStringLiteral("Flight Deck"));
    QTimer::singleShot(160, this, [this] {
        QObject *deck = surface();
        if (!deck) {
            fail(QStringLiteral("Flight Deck surface was not available in the real qualification window"));
            finish();
            return;
        }
        // A fresh test-mode installation has no editable profile/device
        // context. Create one isolated Profile so the subsequently injected
        // QML slider edits take the normal asynchronous persistence path.
        // The timestamp avoids reusing an owner name if test storage survives
        // a prior qualification run.
        const QString profileName = QStringLiteral("Native qualification %1")
                                        .arg(QDateTime::currentMSecsSinceEpoch());
        if (!m_backend->createProfile(profileName)) {
            fail(QStringLiteral("isolated test Profile could not be created for persistence qualification"));
            finish();
            return;
        }
        // A distinct route first ensures Overview's first pass produces a
        // navigation record instead of inheriting the shell's default page.
        deck->setProperty("currentPage", 4);
        m_completedActions.append(QStringLiteral("real-native-window-created"));
        m_completedActions.append(QStringLiteral("isolated-test-profile-created"));
        QTimer::singleShot(100, this, [this] { runNavigationStep(); });
    });
}

void NativeQualificationDriver::runNavigationStep()
{
    const int total = m_pages.size() * 2;
    if (m_navigationStep >= total) {
        startScrollCharacterization();
        return;
    }
    const Page &page = m_pages.at(m_navigationStep % m_pages.size());
    navigate(page.id, QString::fromLatin1(page.name));
    ++m_navigationStep;
    QTimer::singleShot(110, this, [this] { runNavigationStep(); });
}

void NativeQualificationDriver::startScrollCharacterization()
{
    m_scrollWindow = 0;
    m_scrollPage = 0;
    m_scrollPattern = 0;
    m_scrollEvent = 0;
    m_scrollSessionActive = false;
    m_window->resize(kScrollWindowProfiles[0].size);
    QTimer::singleShot(160, this, [this] { runScrollStep(); });
}

void NativeQualificationDriver::runScrollStep()
{
    if (m_scrollWindow >= static_cast<int>(kScrollWindowProfiles.size())) {
        startControls();
        return;
    }
    if (m_scrollPage >= static_cast<int>(kScrollPages.size())) {
        ++m_scrollWindow;
        m_scrollPage = 0;
        m_scrollPattern = 0;
        m_scrollEvent = 0;
        m_scrollSessionActive = false;
        if (m_scrollWindow >= static_cast<int>(kScrollWindowProfiles.size())) {
            startControls();
            return;
        }
        m_window->resize(kScrollWindowProfiles[static_cast<size_t>(m_scrollWindow)].size);
        QTimer::singleShot(160, this, [this] { runScrollStep(); });
        return;
    }
    const ScrollPage &page = kScrollPages[static_cast<size_t>(m_scrollPage)];
    const ScrollWindowProfile &windowProfile =
        kScrollWindowProfiles[static_cast<size_t>(m_scrollWindow)];
    if (!shouldMeasureScrollPage(m_scrollWindow, page)) {
        ++m_scrollPage;
        m_scrollPattern = 0;
        m_scrollEvent = 0;
        QTimer::singleShot(0, this, [this] { runScrollStep(); });
        return;
    }
    if (m_scrollPattern >= static_cast<int>(kScrollPatterns.size())) {
        ++m_scrollPage;
        m_scrollPattern = 0;
        m_scrollEvent = 0;
        QTimer::singleShot(80, this, [this] { runScrollStep(); });
        return;
    }
    if (!m_scrollSessionActive && m_scrollEvent == 0) {
        navigate(page.page, QString::fromLatin1(page.name));
        m_scrollEvent = -1;
        QTimer::singleShot(140, this, [this] { runScrollStep(); });
        return;
    }

    QQuickItem *viewport = findItem(QString::fromLatin1(page.surfaceId));
    if (!viewport) {
        fail(QStringLiteral("scroll viewport was unavailable for %1").arg(QString::fromLatin1(page.name)));
        ++m_scrollPage;
        m_scrollPattern = 0;
        m_scrollEvent = 0;
        QTimer::singleShot(80, this, [this] { runScrollStep(); });
        return;
    }
    const qreal maximumContentY = std::max<qreal>(0.0, viewport->property("contentHeight").toReal()
                                                         - viewport->height());
    if (maximumContentY <= 0.5) {
        if (auto *probe = ResponsivenessProbe::active()) {
            probe->recordNotScrollable(QString::fromLatin1(page.name),
                                       QString::fromLatin1(page.surfaceId),
                                       QString::fromLatin1(windowProfile.name),
                                       m_backend->contentionResilienceController()->levelName(),
                                       QStringLiteral("not scrollable in this fixture"));
        }
        ++m_scrollPage;
        m_scrollPattern = 0;
        m_scrollEvent = 0;
        QTimer::singleShot(80, this, [this] { runScrollStep(); });
        return;
    }
    const ScrollPattern &pattern = kScrollPatterns[static_cast<size_t>(m_scrollPattern)];
    if (!m_scrollSessionActive) {
        const qreal initialContentY = pattern.reversalAt >= 0 ? maximumContentY * 0.55 : 0.0;
        viewport->setProperty("contentY", initialContentY);
        if (auto *probe = ResponsivenessProbe::active()) {
            probe->beginScrollSession(QString::fromLatin1(page.name),
                                      QString::fromLatin1(page.surfaceId),
                                      QString::fromLatin1(pattern.name),
                                      QString::fromLatin1(windowProfile.name),
                                      m_backend->contentionResilienceController()->levelName(),
                                      viewport->property("contentY").toReal());
        }
        m_completedActions.append(QStringLiteral("scroll:%1:%2")
                                      .arg(QString::fromLatin1(page.name), QString::fromLatin1(pattern.name)));
        m_scrollSessionActive = true;
        m_scrollViewport = viewport;
        m_scrollPositionSampler.start();
        m_scrollEvent = 0;
        QTimer::singleShot(30, this, [this] { runScrollStep(); });
        return;
    }
    if (m_scrollEvent >= pattern.events) {
        QTimer::singleShot(225, this, &NativeQualificationDriver::finishScrollSession);
        return;
    }
    const int delta = pattern.reversalAt >= 0 && m_scrollEvent >= pattern.reversalAt ? 120 : -120;
    wheel(viewport, delta, QStringLiteral("scroll:%1:%2")
                                .arg(QString::fromLatin1(page.name), QString::fromLatin1(pattern.name)));
    ++m_scrollEvent;
    QTimer::singleShot(pattern.intervalMs, this, [this] { runScrollStep(); });
}

void NativeQualificationDriver::sampleScrollPosition(const QString &surfaceId)
{
    if (auto *probe = ResponsivenessProbe::active()) {
        if (QQuickItem *viewport = findItem(surfaceId))
            probe->recordScrollPosition(viewport->property("contentY").toReal());
    }
}

void NativeQualificationDriver::finishScrollSession()
{
    if (!m_scrollSessionActive) return;
    m_scrollPositionSampler.stop();
    const ScrollPage &page = kScrollPages[static_cast<size_t>(m_scrollPage)];
    sampleScrollPosition(QString::fromLatin1(page.surfaceId));
    if (auto *probe = ResponsivenessProbe::active()) probe->endScrollSession();
    m_scrollSessionActive = false;
    m_scrollViewport = nullptr;
    ++m_scrollPattern;
    m_scrollEvent = 0;
    QTimer::singleShot(80, this, [this] { runScrollStep(); });
}

void NativeQualificationDriver::startControls()
{
    navigate(9, QStringLiteral("Adaptive Response controls"));
    QTimer::singleShot(130, this, [this] {
        QQuickItem *adaptive = findItem(QStringLiteral("flightDeckAdaptiveResponse"));
        if (adaptive) {
            // This is a presentation-source choice in the isolated test
            // window. It lets qualification count the real live-graph cadence
            // without changing Adaptive Response runtime or mapper behavior.
            adaptive->setProperty("responseLabSource", QStringLiteral("live"));
        }
        if (QQuickItem *advanced = findItem(QStringLiteral("flightDeckAdaptiveAdvancedToggle"))) {
            // This page is deliberately long. Make the control visible in the
            // actual Flickable before injecting the native-window click rather
            // than treating a clipped item's scene coordinate as interactive.
            bringIntoView(advanced, adaptive);
            QTimer::singleShot(120, this, [this, advanced] {
                if (QQuickItem *adaptiveSurface = findItem(QStringLiteral("flightDeckAdaptiveResponse"))) {
                    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier,
                        viewportPoint(advanced, adaptiveSurface,
                                      QPointF(advanced->width() * 0.5, advanced->height() * 0.5)));
                    ++m_mouseEvents;
                    m_completedActions.append(QStringLiteral("adaptive-advanced-toggle"));
                }
                QTimer::singleShot(220, this, [this] {
                    QQuickItem *adaptiveSurface = findItem(QStringLiteral("flightDeckAdaptiveResponse"));
                    if (!adaptiveSurface || !adaptiveSurface->property("advancedExpanded").toBool()) {
                        fail(QStringLiteral("synthetic native Advanced disclosure did not expand"));
                        runTextControl();
                        return;
                    }
                    // Device scope legitimately has no target in this
                    // test-mode process. The profile just created above is a
                    // harmless isolated persistence target for the real QML
                    // slider, while production scope semantics remain intact.
                    m_backend->setSelectedAxis(0);
                    adaptiveSurface->setProperty("editScope", QStringLiteral("profile"));
                    adaptiveSurface->setProperty("targetId", QString());
                    QTimer::singleShot(160, this, [this] { runSliderBurst(); });
                });
            });
        } else {
            fail(QStringLiteral("safe Adaptive Response disclosure was unavailable"));
            runTextControl();
        }
    });
}

void NativeQualificationDriver::runSliderBurst()
{
    QQuickItem *adaptive = findItem(QStringLiteral("flightDeckAdaptiveResponse"));
    QQuickItem *slider = findItem(QStringLiteral("flightDeckAdaptiveSlider_maximumHorizonMs"));
    if (!adaptive || !slider) {
        fail(QStringLiteral("safe Adaptive Response slider was unavailable in isolated qualification"));
        runTextControl();
        return;
    }
    bringIntoView(slider, adaptive);
    if (m_sliderBurst < 100) {
        const qreal fraction = 0.12 + static_cast<qreal>(m_sliderBurst % 76) / 100.0;
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier,
            viewportPoint(slider, adaptive,
                          QPointF(slider->width() * fraction, slider->height() * 0.5)));
        ++m_mouseEvents;
        // Preserve the native pointer exercise above while ensuring every
        // sample also crosses the actual, isolated Profile persistence
        // boundary. This profile cannot be the owner's active mapping profile
        // because native qualification runs in QStandardPaths test mode.
        if (!m_backend->setAdaptiveResponsePropertyAtContext(
                QStringLiteral("profile"), m_backend->selectedProfileId(), 0,
                QStringLiteral("maximumHorizonMs"), 30.0 * fraction)) {
            fail(QStringLiteral("isolated Profile persistence edit was rejected"));
        } else {
            ++m_isolatedPersistenceEdits;
        }
        ++m_sliderBurst;
        QTimer::singleShot(16, this, [this] { runSliderBurst(); });
        return;
    }
    // Pointer-handler state is finalized by Qt Quick after the injected
    // press/release delivery returns, so sample it on a later event-loop turn
    // rather than falsely treating synchronous QTest return as a rejected
    // TapHandler event.
    QTimer::singleShot(100, this, [this] {
        QQuickItem *completedSlider = findItem(QStringLiteral("flightDeckAdaptiveSlider_maximumHorizonMs"));
        if (completedSlider)
            m_sliderNativeActivations = completedSlider->property("pointerPresses").toInt();
        if (m_sliderNativeActivations == 0)
            fail(QStringLiteral("synthetic native slider input did not reach the QML control"));
        m_completedActions.append(QStringLiteral("native-slider-events-%1").arg(m_sliderNativeActivations));
        m_completedActions.append(QStringLiteral("isolated-persistence-slider-burst-%1")
                                      .arg(m_isolatedPersistenceEdits));
        recordChildControlWheel(QStringLiteral("Adaptive Response slider"), completedSlider,
                                findItem(QStringLiteral("flightDeckAdaptiveResponse")));
        QTimer::singleShot(700, this, [this] { runTextControl(); });
    });
}

void NativeQualificationDriver::runTextControl()
{
    navigate(5, QStringLiteral("Profiles text control"));
    QTimer::singleShot(130, this, [this] {
        if (QQuickItem *search = findItem(QStringLiteral("flightDeckProfileSearch"))) {
            click(search, QStringLiteral("profile-search-focus"));
            constexpr char text[] = "phase2";
            for (const char character : text) {
                if (character == '\0') break;
                QTest::keyClick(m_window, character);
                ++m_keyEvents;
            }
            m_completedActions.append(QStringLiteral("profile-search-keyboard-edit"));
            recordChildControlWheel(QStringLiteral("Profiles search text"), search,
                                    findItem(QStringLiteral("flightDeckProfiles")));
        } else {
            fail(QStringLiteral("safe Profile search text field was unavailable"));
        }
        startResizeSequence();
    });
}

void NativeQualificationDriver::startResizeSequence()
{
    m_resizeStep = 0;
    QTimer::singleShot(80, this, [this] { runResizeStep(); });
}

void NativeQualificationDriver::runResizeStep()
{
    static constexpr std::array<QSize, 4> sizes{{QSize(900, 650), QSize(1320, 840),
                                                   QSize(1600, 1000), QSize(1320, 840)}};
    if (m_resizeStep >= static_cast<int>(sizes.size())) {
        m_completedActions.append(QStringLiteral("native-window-resize-min-normal-large-normal"));
        QTimer::singleShot(120, this, [this] { collectReadOnlyDeviceEvidence(); });
        return;
    }
    m_window->resize(sizes[static_cast<size_t>(m_resizeStep)]);
    ++m_resizeStep;
    QTimer::singleShot(140, this, [this] { runResizeStep(); });
}

void NativeQualificationDriver::collectReadOnlyDeviceEvidence()
{
    QElapsedTimer timer;
    timer.start();
    const QList<DiscoveredController> controllers = ControllerDiscovery::enumerate();
    m_controllerEnumerationMs = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    for (const DiscoveredController &controller : controllers) {
        QVariantMap summary;
        summary.insert(QStringLiteral("name"), controller.name);
        summary.insert(QStringLiteral("virtualDevice"), controller.virtualDevice);
        summary.insert(QStringLiteral("axisCount"), controller.axisCount);
        summary.insert(QStringLiteral("buttonCount"), controller.buttonCount);
        summary.insert(QStringLiteral("povCount"), controller.povCount);
        int namedAxes = 0;
        for (bool available : controller.axes) {
            if (available) ++namedAxes;
        }
        summary.insert(QStringLiteral("recognizedAxisCapabilities"), namedAxes);
        m_controllerSummary.append(summary);
    }
    const VJoyOwnershipEvidence ownership = queryVJoyOwnership(1);
    m_vjoySummary.insert(QStringLiteral("deviceId"), ownership.deviceId);
    m_vjoySummary.insert(QStringLiteral("rawStatus"), ownership.rawStatus);
    m_vjoySummary.insert(QStringLiteral("state"), vjoyOwnershipStateName(ownership.state));
    m_vjoySummary.insert(QStringLiteral("queryCompleted"), true);
    m_completedActions.append(QStringLiteral("read-only-directinput-enumeration"));
    m_completedActions.append(QStringLiteral("read-only-vjoy-ownership-query"));
    startTortureLoop();
}

void NativeQualificationDriver::startTortureLoop()
{
    if (m_tortureSeconds <= 0) {
        finish();
        return;
    }
    m_tortureElapsed.start();
    QTimer::singleShot(1, this, [this] { runTortureStep(); });
}

void NativeQualificationDriver::runTortureStep()
{
    if (m_tortureElapsed.elapsed() >= static_cast<qint64>(m_tortureSeconds) * 1000) {
        m_completedActions.append(QStringLiteral("combined-torture-loop-%1s").arg(m_tortureSeconds));
        finish();
        return;
    }
    const Page &page = m_pages.at(m_tortureStep % m_pages.size());
    navigate(page.id, QString::fromLatin1(page.name));
    if (const ScrollPage *scrollPage = scrollPageForId(page.id)) {
        if (QQuickItem *viewport = findItem(QString::fromLatin1(scrollPage->surfaceId))) {
            // This keeps the established torture cadence, but targets the
            // real page Flickable instead of the shell and alternates the
            // wheel direction over the page cycle. Session distributions are
            // captured by the dedicated matrix above; this is sustained
            // native-window scroll activity during the unchanged 90-second
            // persistence/navigation torture.
            const int delta = m_tortureStep % 20 < 10 ? -120 : 120;
            wheel(viewport, delta, QStringLiteral("scroll:%1:torture")
                                      .arg(QString::fromLatin1(scrollPage->name)));
        }
    }
    if (m_tortureStep % 13 == 0) {
        if (QQuickItem *slider = findItem(QStringLiteral("flightDeckAdaptiveSlider_maximumHorizonMs")))
            click(slider, QStringLiteral("torture-safe-slider"));
    }
    if (m_tortureStep % 17 == 0) {
        const QSize size = m_tortureStep % 34 == 0 ? QSize(980, 700) : QSize(1320, 840);
        m_window->resize(size);
    }
    ++m_tortureStep;
    QTimer::singleShot(65, this, [this] { runTortureStep(); });
}

void NativeQualificationDriver::finish()
{
    if (!m_application) return;
    writeSummary();
    QTimer::singleShot(250, this, [this] {
        m_application->exit(m_failures.isEmpty() ? 0 : 1);
    });
}

QObject *NativeQualificationDriver::surface() const
{
    return m_window ? m_window->findChild<QObject *>(QStringLiteral("flightDeckSurface")) : nullptr;
}

QQuickItem *NativeQualificationDriver::findItem(const QString &objectName) const
{
    return m_window ? findItem(m_window->contentItem(), objectName) : nullptr;
}

QQuickItem *NativeQualificationDriver::findItem(QQuickItem *root, const QString &objectName)
{
    if (!root) return nullptr;
    if (root->objectName() == objectName && root->isVisible() && root->width() > 0.0 && root->height() > 0.0)
        return root;
    const auto children = root->childItems();
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        if (QQuickItem *found = findItem(*it, objectName)) return found;
    }
    return nullptr;
}

bool NativeQualificationDriver::click(QQuickItem *item, const QString &label)
{
    if (!item || !m_window) return false;
    const QPointF point = item->mapToScene(QPointF(item->width() * 0.5, item->height() * 0.5));
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    ++m_mouseEvents;
    m_completedActions.append(label);
    return true;
}

bool NativeQualificationDriver::wheel(QQuickItem *item, int delta, const QString &label)
{
    if (!item || !m_window) return false;
    const QPointF point = item->mapToScene(QPointF(item->width() * 0.5, item->height() * 0.5));
    if (m_scrollSessionActive) {
        if (auto *probe = ResponsivenessProbe::active()) probe->recordScrollWheel();
    }
    // Route through Qt's window-system test seam rather than directly sending
    // a QWheelEvent to QQuickWindow. That preserves the same delivery path as
    // native-window input, including Flickable's normal wheel coalescing.
    QTest::wheelEvent(m_window, point, QPoint(0, delta));
    if (m_scrollSessionActive) {
        const QString surfaceId = item->objectName();
        sampleScrollPosition(surfaceId);
        QTimer::singleShot(0, this, [this, surfaceId] { sampleScrollPosition(surfaceId); });
    }
    ++m_wheelEvents;
    if (m_wheelEvents == 1) m_completedActions.append(label);
    return true;
}

bool NativeQualificationDriver::navigate(int page, const QString &name)
{
    QObject *deck = surface();
    if (!deck) {
        fail(QStringLiteral("navigation surface disappeared before %1").arg(name));
        return false;
    }
    const QString objectName = QStringLiteral("flightDeckNav_%1").arg(page);
    const bool clicked = click(findItem(objectName), QStringLiteral("navigate-%1").arg(name));
    if (deck->property("currentPage").toInt() != page) {
        // The fallback remains within the same QML page host and emits its
        // normal navigation lifecycle records. It only covers a window-system
        // injection rejection; the synthetic input event was still delivered
        // to the real QQuickWindow and retained by the probe.
        deck->setProperty("currentPage", page);
        ++m_navigationFallbacks;
    }
    if (!clicked) fail(QStringLiteral("synthetic native navigation target was unavailable for %1").arg(name));
    if (page == 5) {
        // Profiles-only construction evidence.  The page itself owns the
        // timestamps/counters and this qualification driver only captures the
        // published snapshot after its normal loader turn.
        QTimer::singleShot(0, this, [this] { captureProfilesConstruction(); });
    }
    return clicked;
}

void NativeQualificationDriver::captureProfilesConstruction()
{
    if (!m_window || m_profilesConstructionSamples.size() >= 64) return;
    QObject *profiles = m_window->findChild<QObject *>(QStringLiteral("flightDeckProfiles"));
    if (!profiles) return;
    const QVariantMap metrics = profiles->property("constructionMetrics").toMap();
    if (metrics.isEmpty()) return;
    QVariantMap sample = metrics;
    sample.insert(QStringLiteral("categoryDelegateCount"),
                  profiles->property("libraryCategoryDelegateCount"));
    sample.insert(QStringLiteral("profileDelegateCount"),
                  profiles->property("libraryProfileDelegateCount"));
    sample.insert(QStringLiteral("detailDelegateCount"), profiles->property("detailDelegateCount"));
    sample.insert(QStringLiteral("capturedDuring"), QStringLiteral("Profiles navigation"));
    m_profilesConstructionSamples.append(sample);
}

void NativeQualificationDriver::recordChildControlWheel(const QString &label, QQuickItem *control,
                                                         QQuickItem *viewport)
{
    if (!control || !viewport || !m_window) return;
    const QVariant valueBefore = control->property("value");
    const QVariant textBefore = control->property("text");
    const qreal contentYBefore = viewport->property("contentY").toReal();
    const QPointer<QQuickItem> guardedControl(control);
    const QPointer<QQuickItem> guardedViewport(viewport);
    QTest::wheelEvent(m_window, viewportPoint(control, viewport,
                                               QPointF(control->width() * 0.5, control->height() * 0.5)),
                      QPoint(0, -120));
    QTimer::singleShot(0, this, [this, label, guardedControl, guardedViewport, valueBefore, textBefore,
                                  contentYBefore] {
        QVariantMap result;
        result.insert(QStringLiteral("label"), label);
        result.insert(QStringLiteral("interactionSource"),
                      QStringLiteral("native-window synthetic wheel-angle-delta scroll"));
        result.insert(QStringLiteral("controlAvailableAfterEvent"), !guardedControl.isNull());
        result.insert(QStringLiteral("viewportAvailableAfterEvent"), !guardedViewport.isNull());
        result.insert(QStringLiteral("contentYBefore"), contentYBefore);
        if (!guardedControl || !guardedViewport) {
            result.insert(QStringLiteral("outcome"), QStringLiteral("unavailable after native wheel"));
        } else {
            const QVariant valueAfter = guardedControl->property("value");
            const QVariant textAfter = guardedControl->property("text");
            const qreal contentYAfter = guardedViewport->property("contentY").toReal();
            const bool valueChanged = valueBefore.isValid() && valueAfter.isValid()
                && valueBefore != valueAfter;
            const bool textChanged = textBefore.isValid() && textAfter.isValid()
                && textBefore != textAfter;
            const bool viewportMoved = !qFuzzyCompare(contentYBefore + 1.0, contentYAfter + 1.0);
            result.insert(QStringLiteral("valueChanged"), valueChanged);
            result.insert(QStringLiteral("textChanged"), textChanged);
            result.insert(QStringLiteral("contentYAfter"), contentYAfter);
            result.insert(QStringLiteral("viewportMoved"), viewportMoved);
            result.insert(QStringLiteral("outcome"), valueChanged || textChanged
                ? QStringLiteral("child control changed")
                : viewportMoved ? QStringLiteral("viewport scrolled without child mutation")
                                : QStringLiteral("no observable control or viewport change"));
        }
        if (m_childControlWheelTests.size() < 16) m_childControlWheelTests.append(result);
    });
}

void NativeQualificationDriver::bringIntoView(QQuickItem *item, QQuickItem *viewport) const
{
    if (!item || !viewport) return;
    const qreal contentHeight = viewport->property("contentHeight").toReal();
    const qreal viewportHeight = viewport->height();
    if (contentHeight <= viewportHeight) return;
    QQuickItem *contentItem = qvariant_cast<QQuickItem *>(viewport->property("contentItem"));
    if (!contentItem) contentItem = viewport;
    const QPointF relative = item->mapToItem(contentItem, QPointF{});
    const qreal maximum = std::max<qreal>(0.0, contentHeight - viewportHeight);
    viewport->setProperty("contentY", std::clamp(relative.y() - 96.0, 0.0, maximum));
}

QPoint NativeQualificationDriver::viewportPoint(QQuickItem *item, QQuickItem *viewport,
                                                const QPointF &point)
{
    if (!item || !viewport) return {};
    QQuickItem *contentItem = qvariant_cast<QQuickItem *>(viewport->property("contentItem"));
    if (!contentItem) return item->mapToScene(point).toPoint();
    const QPointF inContent = item->mapToItem(contentItem, point);
    const QPointF inViewport = inContent - QPointF(viewport->property("contentX").toReal(),
                                                    viewport->property("contentY").toReal());
    return viewport->mapToScene(inViewport).toPoint();
}

void NativeQualificationDriver::writeSummary()
{
    const QString path = qualificationSummaryPath();
    if (path.isEmpty()) return;
    QJsonObject report{{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("mode"), QStringLiteral("native-window-synthetic")},
                       {QStringLiteral("startedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                       {QStringLiteral("navigationPageCycles"), 2},
                       {QStringLiteral("navigationFallbacks"), m_navigationFallbacks},
                       {QStringLiteral("syntheticMouseEvents"), m_mouseEvents},
                       {QStringLiteral("syntheticWheelEvents"), m_wheelEvents},
                       {QStringLiteral("syntheticKeyEvents"), m_keyEvents},
                       {QStringLiteral("nativeSliderActivations"), m_sliderNativeActivations},
                       {QStringLiteral("isolatedPersistenceEdits"), m_isolatedPersistenceEdits},
                       {QStringLiteral("tortureSeconds"), m_tortureSeconds},
                       {QStringLiteral("completedActions"), stringArray(m_completedActions)},
                       {QStringLiteral("failures"), stringArray(m_failures)},
                       {QStringLiteral("controllerEnumerationMs"), m_controllerEnumerationMs},
                       {QStringLiteral("controllers"), QJsonArray::fromVariantList(m_controllerSummary)},
                       {QStringLiteral("profilesConstruction"),
                        QJsonArray::fromVariantList(m_profilesConstructionSamples)},
                       {QStringLiteral("childControlWheelTests"),
                        QJsonArray::fromVariantList(m_childControlWheelTests)},
                       {QStringLiteral("contentionResilience"),
                        QJsonObject::fromVariantMap(m_backend
                            ? m_backend->contentionResilienceController()->evidence() : QVariantMap{})},
                       {QStringLiteral("vjoyReadOnly"), QJsonObject::fromVariantMap(m_vjoySummary)}};
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QSaveFile output(path);
    if (output.open(QIODevice::WriteOnly)) {
        output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        output.commit();
    }
}

void NativeQualificationDriver::fail(const QString &message)
{
    if (!m_failures.contains(message)) m_failures.append(message);
}

} // namespace hotas
