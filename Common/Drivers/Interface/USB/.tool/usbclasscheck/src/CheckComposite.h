#pragma once

#include <vector>

#include "DeviceIdentity.h"
#include "Options.h"
#include "Report.h"

// Runs the composite checks the options name (all three, or one) on one board, and adds a report for each to reports.
void RunCompositeChecks(CompositeCheck which, const BoardInfo& board, std::vector<CheckReport>& reports);
