#pragma once

namespace frost {
// Runs the normal boot composition; returns without starting any radio when
// external configuration is absent. Does not run diagnostic probes.
void run_production_boot();
}  // namespace frost
