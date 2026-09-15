#pragma once

#include "doctor_repair_engine.h"

namespace hotas::doctor {

// This provider is compiled only into HidHideDoctorRepair.exe.  It is not
// linked into the diagnostics executable, which preserves the Phase 1
// read-only provider boundary even when a future owner authorizes R1 work.
class HidHideConfigurationMutator final : public IRepairConfigurationMutator {
public:
    HidHideConfigurationMutator(QString provider, QString providerVersion);
    HidHideConfigurationSnapshot readConfiguration() override;
    bool apply(const RepairOperation &operation, NativeError *error = nullptr) override;
private:
    QString m_provider;
    QString m_providerVersion;
};

} // namespace hotas::doctor
