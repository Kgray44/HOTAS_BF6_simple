#pragma once

#include "doctor_diagnostics.h"

namespace hotas::doctor {

// Development-only snapshots are deliberately explicit. They make owner UI
// review repeatable without presenting simulated data as this PC's evidence.
QStringList developmentFixtureNames();
ReadOnlyDiagnosticSnapshot createDevelopmentFixture(const QString &name, QString *displayLabel = nullptr);

class FixtureDiagnosticProvider final : public IReadOnlyDiagnosticProvider {
public:
    explicit FixtureDiagnosticProvider(ReadOnlyDiagnosticSnapshot snapshot) : m_snapshot(std::move(snapshot)) {}
    ReadOnlyDiagnosticSnapshot observe(std::atomic_bool *, ObservationProgress) override { return m_snapshot; }
private:
    ReadOnlyDiagnosticSnapshot m_snapshot;
};

} // namespace hotas::doctor
