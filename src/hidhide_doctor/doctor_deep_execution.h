#pragma once

#include "doctor_approved_package_runtime.h"
#include "doctor_repair_engine.h"

namespace hotas::doctor {

// Native helper-side executor for the sealed R2-R5 vocabulary. It is not a
// command runner: every switch arm has one fixed HidHide target and accepts
// no program path, registry path, INF name, service name, or arguments from
// the request. R5 has a deterministic fixture path only until an independently
// verified rollback package is catalogued.
class DeepRepairExecutor final {
public:
    static RepairExecutionResult execute(const RepairPlan &plan, const DoctorEnvironment &environment,
        RepairTransaction transaction, const RepairJournalStore &journal);

    // Restart is deliberately a separate explicit action. Deep execute only
    // schedules a RunOnce continuation and returns AwaitingReboot; it never
    // calls this method itself.
    static bool requestRestartNow(const RepairTransaction &transaction, QString *reason = nullptr);
};

} // namespace hotas::doctor
