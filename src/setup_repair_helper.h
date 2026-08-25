#pragma once

#include <optional>

namespace hotas {

// Runs only when the primary executable is launched with the private repair
// transaction switch.  It deliberately creates no GUI or mapper worker.
std::optional<int> runElevatedRepairTransaction(int argc, char *argv[]);

} // namespace hotas
