#pragma once

#include <QString>

namespace hotas {

// These labels are deliberately narrower than the historical readiness
// severity strings. A setup result must distinguish an observed defect from
// a failed inspection; UI code receives the label but never infers it from a
// display sentence.
enum class SetupTruthStatus {
    Ready,
    // The selected Rig has passed every setup prerequisite but is only being
    // viewed. This is healthy, explicit work for the owner, not a defect.
    ReadyToActivate,
    Checking,
    Repairable,
    WaitingForUser,
    Attention,
    Unknown,
    Failed,
    Unavailable,
};

QString setupTruthStatusLabel(SetupTruthStatus status);
int setupTruthStatusPriority(SetupTruthStatus status);

} // namespace hotas
