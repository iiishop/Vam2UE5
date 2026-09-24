#pragma once
#include "CoreMinimal.h"
#include "VamCharacterActor.h"
#include "VamFleshCapabilityActor.generated.h"

/** Explicit lab actor for backend qualification. Never selected by normal hosts. */
UCLASS(Blueprintable)
class VAMCHARACTERRUNTIME_API AVamFleshCapabilityActor : public AVamCharacterActor
{
    GENERATED_BODY()
public:
    AVamFleshCapabilityActor();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Capability") TSoftObjectPtr<class UVamFleshCapabilityAsset> Capability;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Capability") TSoftObjectPtr<class UMeshDeformer> SurfaceDeformer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") TObjectPtr<class UDeformableSolverComponent> FleshSolver;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") TObjectPtr<class UFleshComponent> Flesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") TObjectPtr<class UDeformableCollisionsComponent> FleshCollisions;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") TObjectPtr<class UStaticMeshComponent> ProbeSphere;
protected:
    virtual void Tick(float DeltaSeconds) override;
private:
    bool Started=false;
    double StartTime=0;
    int32 ScreenshotStage=0;
    FVector ProbeStart=FVector::ZeroVector;
    float ProbeRadius=1;
    float SurfacePeak=0,VolumeErrorPeak=0;
    int32 MeasuredFrames=0;
    void Finish(const FString& Reason);
};
