#include "DisableVehicles.h"

void disableVehicles(AFortAthenaMapInfo* MapInfo, float NewMinPercent, float NewMaxPercent)
{
    if (!MapInfo)
        return;

    auto& VehicleDetails = MapInfo->GetVehicleClassDetails();

    for (int i = 0; i < VehicleDetails.Num(); ++i)
    {
        FVehicleClassDetails& Details = VehicleDetails.at(i);

        FScalableFloat* MinPtr = Details.GetVehicleMinSpawnPercent();
        if (MinPtr)
        {
            MinPtr->GetCurve().CurveTable = nullptr;
            MinPtr->GetCurve().RowName = FName(0);
            MinPtr->GetValue() = NewMinPercent;
        }

        FScalableFloat* MaxPtr = Details.GetVehicleMaxSpawnPercent();
        if (MaxPtr)
        {
            MaxPtr->GetCurve().CurveTable = nullptr;
            MaxPtr->GetCurve().RowName = FName(0);
            MaxPtr->GetValue() = NewMaxPercent;
        }
    }
}