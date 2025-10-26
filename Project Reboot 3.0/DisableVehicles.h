#pragma once

// disableVehicles helper - runtime-only change to force vehicle spawn percents.
// This file intentionally does NOT require a separate FScalableFloat.h; it uses the
// project's FortAthenaMapInfo.h which already exposes the needed types.

#include "FortAthenaMapInfo.h"

/// Force all vehicle spawn percents on the supplied MapInfo to NewMinPercent / NewMaxPercent.
/// If you want no vehicles, call with (MapInfo, 0.f, 0.f).
/// This operates at runtime only (does not modify uasset files) by nulling the curve table
/// reference and setting the inline FScalableFloat value.
void disableVehicles(AFortAthenaMapInfo* MapInfo, float NewMinPercent = 0.f, float NewMaxPercent = 0.f);