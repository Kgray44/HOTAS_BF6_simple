#include "setup_truth.h"

namespace hotas {

QString setupTruthStatusLabel(SetupTruthStatus status)
{
    switch (status) {
    case SetupTruthStatus::Ready: return QStringLiteral("READY");
    case SetupTruthStatus::Checking: return QStringLiteral("CHECKING");
    case SetupTruthStatus::Repairable: return QStringLiteral("ACTION NEEDED");
    case SetupTruthStatus::WaitingForUser: return QStringLiteral("WAITING FOR USER");
    case SetupTruthStatus::Attention: return QStringLiteral("ATTENTION");
    case SetupTruthStatus::Unknown: return QStringLiteral("UNKNOWN / INSPECTION FAILED");
    case SetupTruthStatus::Failed: return QStringLiteral("FAILED");
    case SetupTruthStatus::Unavailable: return QStringLiteral("UNAVAILABLE");
    }
    return QStringLiteral("UNKNOWN / INSPECTION FAILED");
}

int setupTruthStatusPriority(SetupTruthStatus status)
{
    switch (status) {
    case SetupTruthStatus::Failed: return 0;
    case SetupTruthStatus::Unknown: return 1;
    case SetupTruthStatus::Unavailable: return 2;
    case SetupTruthStatus::Repairable: return 3;
    case SetupTruthStatus::WaitingForUser: return 4;
    case SetupTruthStatus::Attention: return 5;
    case SetupTruthStatus::Checking: return 6;
    case SetupTruthStatus::Ready: return 7;
    }
    return 8;
}

} // namespace hotas
