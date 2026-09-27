#pragma once
#include "VamBreastJiggleProfile.h"

/** Version 2 geometry -> material/elastic baseline, shared by builder and Shape runtime. */
namespace VamBreastCalibration
{
    VAMCHARACTERRUNTIME_API void Calibrate(FVamBreastSideProfile& Side,double DensityKgPerCm3,double EffectiveModulusPa);
}
