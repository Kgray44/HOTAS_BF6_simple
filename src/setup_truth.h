#pragma once

#include <QString>

namespace hotas {

// These labels are deliberately narrower than the historical readiness
// severity strings. A setup result must distinguish an observed defect from
// a failed inspection; UI code receives the label but never infers it from a
// display sentence.
enum class SetupTruthStatus {
    Ready,
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
