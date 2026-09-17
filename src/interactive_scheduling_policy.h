#pragma once

#include <QObject>

#include <memory>

class QJsonObject;
class QQuickWindow;

namespace hotas {

// A deliberately small Windows-only interactive scheduler policy. Production
// applies the selected GUI boost; native qualification additionally captures
// evidence. Neither mode changes the MappingWorker scheduling contract.
class InteractiveSchedulingPolicy final : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Current,
        Gui,
        Render,
        GuiAndRender,
        ProcessAboveNormal,
    };

    static void installProduction(QObject *parent);
    static void installForQualification(QObject *parent);
    static InteractiveSchedulingPolicy *active();
    static void attachWindow(QQuickWindow *window);
    static void recordCurrentThread(const char *role);
    static void recordGuiHeartbeat(double wallStallMs);
    static QJsonObject evidence();

    ~InteractiveSchedulingPolicy() override;

    // Opaque implementation detail; public only so platform helpers in the
    // translation unit can name the incomplete type without exposing data.
    struct State;

private:
    explicit InteractiveSchedulingPolicy(QObject *parent, Mode mode, bool captureEvidence);

    void applyGuiThreadPolicy();
    void applyRenderThreadPolicy();
    void attach(QQuickWindow *window);
    void recordCurrentThreadImpl(const QString &role);
    void recordGuiHeartbeatImpl(double wallStallMs);
    QJsonObject evidenceImpl() const;

    Mode m_mode = Mode::Current;
    bool m_captureEvidence = false;
    std::unique_ptr<State> m_state;
};

} // namespace hotas
