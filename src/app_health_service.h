#pragma once

#include <QVariantList>
#include <QVariantMap>

namespace hotas {

// Pure aggregation boundary for compact, low-frequency application health.
// It deliberately consumes already-built AppIssue projections; no telemetry
// or mapper state is sampled here.
class AppHealthService final {
public:
    static QVariantMap summarize(const QVariantList &issues);
};

} // namespace hotas
