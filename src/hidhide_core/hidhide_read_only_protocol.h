#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace hotas {

// A deliberately narrow, reusable read-only subset of HidHide's documented
// control protocol.  This type represents only GET observations.  It defines
// no SET operation and is therefore safe to use for in-app diagnosis.
enum class HidHideReadState {
    Pass,
    Failed,
    TimedOut,
    PermissionLimited,
    Unavailable,
    Cancelled,
};

struct HidHideNativeError final {
    QString domain;
    quint32 code = 0;
    QString operation;
    QString message;
};

struct HidHideReadObservation final {
    QString id;
    QString operation;
    HidHideReadState state = HidHideReadState::Unavailable;
    QString summary;
    QString value;
    QStringList values;
    HidHideNativeError nativeError;
    bool hasNativeError = false;
    qint64 durationMs = 0;
    bool sizeNegotiation = false;
};

QString hidHideReadStateLabel(HidHideReadState state);

class HidHideReadOnlyProtocol final {
public:
    // Performs bounded, overlapped GET observations against HidHide. Every
    // operation is independent: one failed query never erases other results.
    // The optional cancellation flag is checked between operations; an active
    // DeviceIoControl is separately bounded and cancelled on timeout.
    using ObservationCallback = std::function<void(const HidHideReadObservation &)>;
    static QList<HidHideReadObservation> inspect(std::atomic_bool *cancelled = nullptr,
                                                 ObservationCallback observation = {});
};

} // namespace hotas
