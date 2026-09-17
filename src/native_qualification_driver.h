#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QCoreApplication;
class QQuickItem;
class QQuickWindow;

namespace hotas {

class AppBackend;
class ThemeManager;

// Explicit qualification-only driver for the shipped application executable.
// It is dormant unless HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION=1 and uses
// test-mode settings plus AppBackend's isolated-presentation boundary.
class NativeQualificationDriver final : public QObject {
public:
    struct Page {
        int id = -1;
        const char *name = "";
    };

    explicit NativeQualificationDriver(QObject *parent = nullptr);

    static bool requested();
    void start(QCoreApplication *application, AppBackend *backend, ThemeManager *themeManager,
               QQuickWindow *window);

private:
    void initializeSurface();
    void runNavigationStep();
    void startScrollCharacterization();
    void runScrollStep();
    void startControls();
    void runSliderBurst();
    void runTextControl();
    void startResizeSequence();
    void runResizeStep();
    void collectReadOnlyDeviceEvidence();
    void startTortureLoop();
    void runTortureStep();
    void finish();

    QObject *surface() const;
    QQuickItem *findItem(const QString &objectName) const;
    static QQuickItem *findItem(QQuickItem *root, const QString &objectName);
    bool click(QQuickItem *item, const QString &label);
    bool wheel(QQuickItem *item, int delta, const QString &label);
    bool navigate(int page, const QString &name);
    void bringIntoView(QQuickItem *item, QQuickItem *viewport) const;
    static QPoint viewportPoint(QQuickItem *item, QQuickItem *viewport, const QPointF &point);
    void writeSummary();
    void fail(const QString &message);

    QCoreApplication *m_application = nullptr;
    AppBackend *m_backend = nullptr;
    ThemeManager *m_themeManager = nullptr;
    QQuickWindow *m_window = nullptr;
    int m_navigationStep = 0;
    int m_scrollPage = 0;
    int m_scrollEvent = 0;
    int m_sliderBurst = 0;
    int m_resizeStep = 0;
    int m_tortureStep = 0;
    int m_navigationFallbacks = 0;
    int m_mouseEvents = 0;
    int m_wheelEvents = 0;
    int m_keyEvents = 0;
    int m_sliderNativeActivations = 0;
    int m_isolatedPersistenceEdits = 0;
    int m_tortureSeconds = 0;
    QStringList m_completedActions;
    QStringList m_failures;
    QList<Page> m_pages;
    QElapsedTimer m_tortureElapsed;
    QVariantList m_controllerSummary;
    QVariantMap m_vjoySummary;
    double m_controllerEnumerationMs = 0.0;
};

} // namespace hotas
