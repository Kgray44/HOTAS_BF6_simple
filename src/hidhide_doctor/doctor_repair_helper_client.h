#pragma once

#include "doctor_repair_engine.h"

namespace hotas::doctor {

enum class RepairHelperOutcome {
    Accepted,
    AuthorizationCancelled,
    LaunchDenied,
    ConnectTimedOut,
    ProtocolRejected,
    MalformedResponse,
    Disconnected,
};

struct RepairHelperClientResult final {
    RepairHelperOutcome outcome = RepairHelperOutcome::LaunchDenied;
    QString detail;
    RepairTransactionId transactionId;
    RepairTransactionState state = RepairTransactionState::FailedSafely;
    bool mutated = false;
    bool connectivityOnly = false;
};

// Doctor is a client only: it starts one short-lived, elevated helper then
// sends one sealed request through a per-launch named pipe.  This API cannot
// accept arbitrary executable paths, commands, or untyped operations.
class RepairHelperClient final {
public:
    static RepairHelperClientResult invoke(const RepairPlan &authorizedPlan,
        const DoctorEnvironment &environment, const QString &doctorBuildId,
        const QString &helperBuildId, bool connectivityOnly,
        const RepairTransactionId &transactionId = {});
};

QString displayName(RepairHelperOutcome outcome);

} // namespace hotas::doctor
