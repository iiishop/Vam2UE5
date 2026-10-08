#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamBreastContactProfile.generated.h"

/** Small simulation-domain data only; render vertices stay on the GPU. Units cm, kg, s. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastContactParticle
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") FVector Rest=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") int32 Side=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TArray<int32> Bones;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TArray<float> Weights;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") float RootSupport=0;
    /** Source nipple-bone support, smoothly normalized per side. Not a kinematic pin. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") float NippleSupport=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") bool bKinematic=false;
};

USTRUCT()
struct VAMCHARACTERRUNTIME_API FVamBreastContactMorph
{
    GENERATED_BODY()
    UPROPERTY() FName Parameter;
    UPROPERTY() float Baseline=0;
    UPROPERTY() TArray<FVector> ParticleDeltas;
};

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastContactVolumeState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double RestVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double CurrentVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double RelativeVolumeError=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double MinimumTetRatio=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") int32 InvertedTetrahedra=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double MaximumTetRatio=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double MaximumSurfaceStretch=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double MinimumSurfaceStretch=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double WorstStretchRestLengthCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double MaximumEdgeExtensionCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") double NippleShapeRmsStrain=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") int32 NippleShapePairCount=0;
};

/** Immutable, versioned contact cage. It is independent of Jiggle strength and bone topology. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamBreastContactProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") FString BuildAlgorithmVersion=TEXT("breast-contact-c1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") FString BindSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") FString RegionProvenance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TSoftObjectPtr<class USkeletalMesh> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TObjectPtr<class UMeshDeformer> SurfaceDeformer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TObjectPtr<class UMeshDeformer> GPUSurfaceDeformer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TArray<FVamBreastContactParticle> Particles;
    UPROPERTY() TArray<FIntVector4> Tetrahedra;
    UPROPERTY() TArray<FIntVector> BoundaryTriangles;
    UPROPERTY() TArray<FVamBreastContactMorph> Morphs;
    // New graphs consume a displacement field directly. Old assets retain their
    // reference-plus-residual contract; never reinterpret an existing graph.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") bool bResidualOnlySurface=false;
    UPROPERTY() TArray<FIntVector4> SurfaceParents;
    UPROPERTY() TArray<FVector4f> SurfaceWeights;
    UPROPERTY() TArray<float> SurfaceMask;
    /** Source evidence and unique triangles in render-index domain for body-only audits. */
    UPROPERTY() TArray<float> SurfaceNippleSupport;
    UPROPERTY() TArray<FIntVector> MeasurementTriangles;
    UPROPERTY() TArray<FVector3f> SurfaceOffsets;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Contact") TArray<double> EffectiveVolumeCm3;
    /** Elastic material tuning, separate from density and from Breast Jiggle. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material",meta=(ClampMin="1")) double YoungModulusPa=3000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material",meta=(ClampMin="0",ClampMax="0.49")) double PoissonRatio=.45;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material",meta=(ClampMin="0.0001")) double DensityKgPerCm3=.001;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact",meta=(ClampMin="0")) double AttachmentStiffness=500;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact",meta=(ClampMin="0.001",ClampMax="0.02")) double FixedStepSeconds=1./120.;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact",meta=(ClampMin="1",ClampMax="32")) int32 MaxSubsteps=8;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact",meta=(ClampMin="1",ClampMax="64")) int32 SolverIterations=12;
    /** C2 resolution is relative to the calibrated anatomy, with a hard budget. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double SurfaceSpacingRadiusFraction=.35;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") int32 MaxAdditionalSurfaceParticlesPerSide=96;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double CompressionBarrierRatio=.35;
    /** Continuous cell compression response. Zero retains legacy barrier-only behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|Bulk",meta=(ClampMin="0",ClampMax="1")) double LocalCompressionResistance=0;
    /** Dimensionless PBD skin curvature retention, separate from bulk stiffness. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|Bulk",meta=(ClampMin="0",ClampMax="1")) double SurfaceBending=0;
    /** GPU Jacobi hinge relaxation, not the CPU PBD authored stiffness or a tissue modulus. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|GPU",meta=(ClampMin="0",ClampMax="1")) double GPUSkinBendingRelaxation=.2;
    /** Volume increment before relaxation, in the same units as RestVolume. */
    double CompressionCorrection(double RestVolume,double CurrentVolume) const;
    double ProbeFront(const TArray<FVector>& Positions,const FTransform& Frame,const FVector& Center,double Radius,bool bPlaten,bool bBodyOnly,int32 Side) const;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double SurfaceMinimumStretch=.65;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double SurfaceMaximumStretch=1.30;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double ConstraintRelaxation=.65;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double DebugPressRadiusFraction=.55;
    /** Quasistatic numerical floor: prevents tiny positive masses becoming kinematic in Chaos. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double MinimumMovableMassKg=.0002;
    /** Relative engineering control, not a measured tissue modulus. Zero restores uniform material. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double NippleShapePreservation=1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Contact|C2") double NippleAllowedStrain=.03;
    /** Reported tolerances, never an assertion of anatomical accuracy. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Diagnostics") double VolumeErrorTolerance=.05;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Diagnostics") double MinimumSupportedTetRatio=.1;
    UFUNCTION(BlueprintPure, Category="Contact") FString ValidateData() const;
    static double SignedTetVolume(const TArray<FVector>& Positions,const FIntVector4& Tet);
    bool MeasureVolume(const TArray<FVector>& Rest,const TArray<FVector>& Current,TArray<FVamBreastContactVolumeState>& Out) const;
};
