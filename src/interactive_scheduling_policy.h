#pragma once

#include <QObject>

#include <memory>

class QJsonObject;
class QQuickWindow;

namespace hotas {

// A deliberately small Windows-only scheduler experiment and observer. It is
// created only by native responsiveness qualification, so normal launches and
// the DirectInput -> MappingWorker -> vJoy report path have no added work.
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
    explicit InteractiveSchedulingPolicy(QObject *parent);

    void applyGuiThreadPolicy();
    void applyRenderThreadPolicy();
    void attach(QQuickWindow *window);
    void recordCurrentThreadImpl(const QString &role);
    void recordGuiHeartbeatImpl(double wallStallMs);
    QJsonObject evidenceImpl() const;

    Mode m_mode = Mode::Current;
    std::unique_ptr<State> m_state;
};

} // namespace hotas
