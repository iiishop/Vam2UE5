#pragma once
#include "CoreMinimal.h"
#include "Chaos/Deformable/ChaosDeformableSolver.h"
bool VamInstallContactNativeExperiment(Chaos::Softs::FPBDEvolution& Evolution,
    Chaos::Softs::FFleshThreadingProxy& Proxy,
    const Chaos::Softs::FDeformableSolverProperties& Properties, int32 Batch);
