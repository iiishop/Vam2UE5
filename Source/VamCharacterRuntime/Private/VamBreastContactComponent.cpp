#include "VamBreastContactComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "VamBodyContactResponseComponent.h"
#include "VamContactRigidSources.h"
#include "UObject/UObjectIterator.h"
#include "VamContactNativeExperiment.h"
#include "VamCharacterComponent.h"
#include "VamRuntimeConfiguration.h"
#include "VamBreastSkeletalMeshComponent.h"
#include "VamMotionComponent.h"
#include "ChaosFlesh/FleshCollection.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/PBDBendingConstraints.h"
#include "ChaosFlesh/FleshDynamicAsset.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "GeometryCollection/Facades/CollectionVertexBoneWeightsFacade.h"
#include "GeometryCollection/Facades/CollectionPositionTargetFacade.h"
#include "GeometryCollection/Facades/CollectionTetrahedralBindingsFacade.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
// Wake-up bounds must follow final helper motion; otherwise two breasts can
// touch through Jiggle while the standing-pose boxes keep contact asleep.
FBox ContactRegionBounds(UVamBreastSkeletalMeshComponent* Mesh,const FVamBreastSideProfile& Side,double Fraction)
{
    const auto& Pose=Mesh->GetComponentSpaceTransforms();if(!Pose.IsValidIndex(Side.AnchorBone))return FBox(ForceInit);
    const FTransform Anchor=Pose[Side.AnchorBone];const FBox Base(Side.COM-Side.SizeCm*Fraction,Side.COM+Side.SizeCm*Fraction);
    FBox Bounds=Base;double RotationMargin=0;
    for(const auto& Node:Side.Nodes)if(Pose.IsValidIndex(Node.BoneIndex)){
        const FTransform Local=Pose[Node.BoneIndex].GetRelativeTransform(Anchor);
        Bounds+=Base.ShiftBy(Local.GetLocation()-Node.Rest);
        const double Angle=2*FMath::Acos(FMath::Clamp(FMath::Abs(Local.GetRotation().W),0.,1.));
        RotationMargin=FMath::Max(RotationMargin,Side.SizeCm.Size()*FMath::Sin(Angle*.5));
    }
    return Bounds.ExpandBy(RotationMargin).TransformBy(Anchor*Mesh->GetComponentTransform());
}
TAutoConsoleVariable<int32> CVarContactGPU(TEXT("vam.Contact.GPU"),0,TEXT("1 opts into resident GPU contact for profiles with GPU deformer; unsupported collisions use CPU"));
TAutoConsoleVariable<int32> CVarContactRestMetrics(TEXT("vam.Contact.RestMetrics"),0,TEXT("Cache immutable per-cell displacement limits by component volume scale. Reset to apply."));
TAutoConsoleVariable<int32> CVarContactGSBatch(TEXT("vam.Contact.NativeGSBatch"),1,TEXT("Private fTetWild contact native GS batch; 0 uses original engine owner, 5 original batch. Reset to apply."));
TAutoConsoleVariable<int32> CVarContactNativeXPBD(TEXT("vam.Contact.NativeXPBD"),0,TEXT("Experimental UE XPBD corotated solver comparison; changes convergence, reset to apply."));
TAutoConsoleVariable<int32> CVarContactDirtyConstraints(TEXT("vam.Contact.DirtyConstraints"),1,TEXT("Skip already evaluated unchanged edge/tet constraints within one contact callback; reset to apply."));
TAutoConsoleVariable<int32> CVarContactBroadphase(TEXT("vam.Contact.Broadphase"),1,TEXT("Conservative bounds rejection for custom contact queries; reset to apply."));
TAutoConsoleVariable<int32> CVarContactExactQueryCache(TEXT("vam.Contact.ExactQueryCache"),0,TEXT("Reuse point contact only for identical particle positions within one callback; reset to apply."));
TAutoConsoleVariable<int32> CVarContactNeoHookean(TEXT("vam.Contact.NativeNeoHookean"),0,TEXT("Experimental native GS material comparison; applies when contact initializes."));
TAutoConsoleVariable<int32> CVarContactIterations(TEXT("vam.Contact.NativeIterations"),0,TEXT("Experimental native iteration override; zero uses Profile."));
TAutoConsoleVariable<int32> CVarContactBoundaryVolume(TEXT("vam.Contact.BoundaryVolume"),1,TEXT("Evaluate zonal volume and gradient through the oriented closed boundary."));
TAutoConsoleVariable<int32> CVarContactCacheStatic(TEXT("vam.Contact.CacheStaticData"),0,TEXT("Cache contact topology and per-iteration rest edge lengths."));
}

UVamBreastContactComponent::UVamBreastContactComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UVamBreastContactComponent::Release()
{
    if(Body && (SurfaceProducer || GPUHandle)) { if(bPreviousDeformerOverride) Body->SetMeshDeformer(PreviousDeformer);else Body->UnsetMeshDeformer(); }
    if(GPUHandle){VamGPUUnregister(Body);GPUHandle.Reset();}bGPUActive=false;GPUInverseMass.Reset();GPUDiagnosticFrame=0;GPULoadFrame=0;ReactionTargets.Reset();ReactionBones.Reset();ContactForceNewtons=FVector::ZeroVector;
    PreviousDeformer=nullptr;MaxContactResidualCm=0;ActivePressSphereCount=0;
    BoundSurfaceResidualCm=FVector2D::ZeroVector;VolumeState.Reset();
    InsideBefore=InsideAfter=MovableParticles=0;PenetrationBefore=PenetrationAfter=0;
    for(auto S:Solvers) if(S) S->SetSimulationTicking(false);
    for(auto F:Flesh) if(F) { F->DisableSimulation();F->DestroyComponent(); }
    if(Collisions) { Collisions->DisableSimulation();Collisions->DestroyComponent();Collisions=nullptr; }
    if(WorldCollisions) { WorldCollisions->DisableSimulation();WorldCollisions->DestroyComponent();WorldCollisions=nullptr; }
    WorldSources.Reset();
    if(SurfaceProducer) { SurfaceProducer->DestroyComponent();SurfaceProducer=nullptr; }
    for(auto S:Solvers) if(S) { S->ResetSimulationProxy();S->DestroyComponent(); }
    Flesh.Reset();Solvers.Reset();InstanceAssets.Reset();Accumulator=0;CompletedSteps=0;bConstraintsAdded=false;
}
void UVamBreastContactComponent::SetContactEnabled(bool bNewEnabled)
{
    bEnabled=bNewEnabled;
    if(!bEnabled) { Release();AppliedDebugDepth=0;Status=TEXT("Contact disabled"); }
    else { Status=TEXT("Contact enabled; initializes on demand"); }
}
void UVamBreastContactComponent::SetLowerBodyContactEnabled(bool bValue){if(bLowerBodyContactEnabled!=bValue){bLowerBodyContactEnabled=bValue;ResetContact();}}
void UVamBreastContactComponent::ResetContact() { bGPURejected=false;Release();AppliedDebugDepth=0;ShapeRevision=INDEX_NONE;Status=bEnabled?TEXT("Contact reset requested"):TEXT("Contact disabled"); }
void UVamBreastContactComponent::EndPlay(const EEndPlayReason::Type Reason) { Release();Super::EndPlay(Reason); }
void UVamBreastContactComponent::SetDebugPress(int32 Side,float DepthFraction)
{
    if(Side<0 && DebugSide>=0 && Profile && Profile->SchemaVersion>=2){bReleasingDebug=true;DebugDepth=0;return;}
    bReleasingDebug=false;
    if(DebugSide!=Side)AppliedDebugDepth=0;
    DebugSide=Side>=0 && Side<2?Side:INDEX_NONE;DebugDepth=FMath::Clamp(DepthFraction,0.f,.6f);
}

bool UVamBreastContactComponent::Initialize()
{
    if(!Profile || !Profile->SurfaceDeformer) { Status=TEXT("Contact profile/deformer absent; generate upgraded Runtime");return false; }
    Status=Profile->ValidateData();if(!Status.IsEmpty()) return false;
    if(Profile->Body.LoadSynchronous()!=Body->GetSkeletalMeshAsset()) { Status=TEXT("Contact body identity mismatch");return false; }
    const int32 N=Profile->Particles.Num();ShapedRest.Reset(N);
    for(const auto& P:Profile->Particles) ShapedRest.Add(P.Rest);
    for(const auto& M:Profile->Morphs)
    {
        // ApplyMorphWeights has already converted absolute Shape values into
        // mesh-relative Morph weights. Subtracting the authored default here
        // applies the appearance offset twice and can invert the rest cage.
        const double D=Body->GetMorphTarget(M.Parameter);
        for(int32 I=0;I<N;++I) ShapedRest[I]+=M.ParticleDeltas[I]*D;
    }
    TArray<FVamBreastContactVolumeState> RestCheck;
    if(!Profile->MeasureVolume(ShapedRest,ShapedRest,RestCheck)) { Status=TEXT("ERROR: Shape invalidates contact tetrahedra; reset required");
        for(int32 I=0;I<Profile->Tetrahedra.Num();++I)
        {
            const double V=Profile->SignedTetVolume(ShapedRest,Profile->Tetrahedra[I]);
            if(!FMath::IsFinite(V) || V<=1.e-8)
            { Status+=FString::Printf(TEXT(" | tet %d signed volume %.9g cm3"),I,V);break; }
        }
        bEnabled=false;return false; }
    ReferenceCS=Character->GetShapeReferencePose();
    const auto& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    if(ReferenceCS.Num()!=Ref.GetNum()) { Status=TEXT("Contact reference pose mismatch");return false; }
    for(int32 B=0;B<ReferenceCS.Num();++B) if(Ref.GetParentIndex(B)>=0) ReferenceCS[B]*=ReferenceCS[Ref.GetParentIndex(B)];
    // Start quasistatics in the CURRENT final pose. Initializing free particles
    // in imported pose while roots immediately use animation creates a spurious
    // relaxation that can be mistaken for pressure deformation.
    const auto CurrentPose=Body->GetComponentSpaceTransforms();
    for(int32 I=0;I<N;++I){const auto& P=Profile->Particles[I];FVector X=FVector::ZeroVector;
        for(int32 J=0;J<P.Bones.Num();++J){const int32 B=P.Bones[J];if(!CurrentPose.IsValidIndex(B) || !ReferenceCS.IsValidIndex(B)){Status=TEXT("Contact pose bone missing");return false;}
            X+=CurrentPose[B].TransformPosition(ReferenceCS[B].InverseTransformPosition(ShapedRest[I]))*P.Weights[J];}ShapedRest[I]=X;}
    ReferenceCS=CurrentPose;
    if(!Profile->MeasureVolume(ShapedRest,ShapedRest,RestCheck)){Status=TEXT("Current pose invalidates contact rest volume");return false;}
    if(bGPUActive)
    {
        FVamGPUContactTopology Topology;Topology.Tets=Profile->Tetrahedra;Topology.Parents=Profile->SurfaceParents;Topology.Weights=Profile->SurfaceWeights;Topology.Mask=Profile->SurfaceMask;
        Topology.RegionCount=Profile->EffectiveVolumeCm3.Num();Topology.ReactionRegionCount=2;
        for(const auto& P:Profile->Particles)Topology.Regions.Add(P.Side);
        if(!bLowerBodyContactEnabled)for(int V=0;V<Topology.Mask.Num();++V)if(Topology.Mask[V]>0 && Profile->Particles[Topology.Parents[V].X].Side>=2)Topology.Mask[V]=0;
        TSet<uint64> SeenEdges;
        for(const auto& Face:Profile->BoundaryTriangles)for(int J=0;J<3;++J)
        {
            const int A=FMath::Min(Face[J],Face[(J+1)%3]),B=FMath::Max(Face[J],Face[(J+1)%3]);const uint64 Key=(uint64(A)<<32)|uint32(B);if(SeenEdges.Contains(Key))continue;SeenEdges.Add(Key);
            const double Preserve=FMath::Clamp(Profile->NippleShapePreservation*NippleShapePreservationScale*FMath::Min(Profile->Particles[A].NippleSupport,Profile->Particles[B].NippleSupport),0.,1.);
            Topology.SurfaceEdges.Add(FIntPoint(A,B));Topology.EdgeLimits.Add(FVector2f(FMath::Lerp(Profile->SurfaceMinimumStretch,1-Profile->NippleAllowedStrain,Preserve),FMath::Lerp(Profile->SurfaceMaximumStretch,1+Profile->NippleAllowedStrain,Preserve)));
        }
        // Nonlocal feature distances retain a three-dimensional protrusion while
        // permitting rigid movement. Source semantic support defines membership.
        for(int Side=0;Side<2;++Side){TArray<int> Nodes;
            for(int I=0;I<N;++I)if(Profile->Particles[I].Side==Side && Profile->Particles[I].NippleSupport>.25 && !Profile->Particles[I].bKinematic)Nodes.Add(I);
            Nodes.Sort([&](int A,int B){return Profile->Particles[A].NippleSupport==Profile->Particles[B].NippleSupport?A<B:Profile->Particles[A].NippleSupport>Profile->Particles[B].NippleSupport;});
            if(Nodes.Num()>32)Nodes.SetNum(32);
            for(int I=0;I<Nodes.Num();++I)for(int J=I+1;J<Nodes.Num();++J){int A=FMath::Min(Nodes[I],Nodes[J]),B=FMath::Max(Nodes[I],Nodes[J]);uint64 Key=(uint64(A)<<32)|uint32(B);if(SeenEdges.Contains(Key))continue;SeenEdges.Add(Key);
                const double W=FMath::Clamp(Profile->NippleShapePreservation*NippleShapePreservationScale*FMath::Min(Profile->Particles[A].NippleSupport,Profile->Particles[B].NippleSupport),0.,1.);
                Topology.SurfaceEdges.Add(FIntPoint(A,B));Topology.EdgeLimits.Add(FVector2f(FMath::Lerp(Profile->SurfaceMinimumStretch,1-Profile->NippleAllowedStrain,W),FMath::Lerp(Profile->SurfaceMaximumStretch,1+Profile->NippleAllowedStrain,W)));}}
        for(const auto& F:Profile->BoundaryTriangles)if(bLowerBodyContactEnabled || Profile->Particles[F.X].Side<2)Topology.SkinFaces.Add(FIntVector4(F.X,F.Y,F.Z,F.Z));
        TMap<uint64,int> HingeOpposites;
        for(const auto& F:Profile->BoundaryTriangles)for(int J=0;J<3;++J){int A=FMath::Min(F[J],F[(J+1)%3]),B=FMath::Max(F[J],F[(J+1)%3]),C=F[(J+2)%3];uint64 K=(uint64(A)<<32)|uint32(B);
            if(const int* D=HingeOpposites.Find(K))Topology.SkinHinges.Add(FIntVector4(A,B,*D,C));else HingeOpposites.Add(K,C);}
        const double Mu=Profile->YoungModulusPa*.01/(2*(1+Profile->PoissonRatio));
        const double Lambda=Profile->YoungModulusPa*.01*Profile->PoissonRatio/((1+Profile->PoissonRatio)*(1-2*Profile->PoissonRatio));
        for(const auto& P:Profile->Particles){const double F=FMath::Clamp((double(P.RootSupport)-.65)/.35,0.,1.);
            Topology.Material.Add(FVector4f(Mu,Lambda,Profile->AttachmentStiffness*(Profile->SchemaVersion>=3?F*F*(3-2*F):(.05+.95*P.RootSupport*P.RootSupport)),FMath::Clamp(Profile->GPUSkinBendingRelaxation,0.,1.)));}
        GPUHandle=VamGPURegister(Body,MoveTemp(Topology));
        GPUInverseMass.Init(0,N);for(const auto& T:Profile->Tetrahedra){const float M=Profile->SignedTetVolume(ShapedRest,T)*Profile->DensityKgPerCm3/4;for(int J=0;J<4;++J)GPUInverseMass[T[J]]+=M;}
        for(int I=0;I<N;++I)GPUInverseMass[I]=(Profile->Particles[I].bKinematic || (!bLowerBodyContactEnabled&&Profile->Particles[I].Side>=2))?0:1/FMath::Max(GPUInverseMass[I],float(Profile->MinimumMovableMassKg));
        Collisions=NewObject<UVamBreastContactCollisions>(this,NAME_None,RF_Transient);
        PreviousDeformer=Body->GetComponentMeshDeformer();const auto* OverrideProperty=FindFProperty<FBoolProperty>(USkinnedMeshComponent::StaticClass(),TEXT("bSetMeshDeformer"));
        bPreviousDeformerOverride=OverrideProperty&&OverrideProperty->GetPropertyValue_InContainer(Body);
        Body->SetMeshDeformer(Profile->GPUSurfaceDeformer);AddTickPrerequisiteComponent(Body);
        ShapeRevision=Character->GetShapeState().Revision;Generation=Character->GetLoadGeneration();Status=TEXT("GPU contact initialized");return true;
    }
    TArray<FVector> Points=ShapedRest;Points.Append(ShapedRest); // weak targets are kinematic ghost particles
    for(int32 Pass=0;Pass<1;++Pass)
    {
        TUniquePtr<FFleshCollection> C(FFleshCollection::NewFleshCollection(Points,Profile->Tetrahedra,false));
        C->Mass.Fill(0);
        for(const auto& T:Profile->Tetrahedra)
        {
            const float Mass=Profile->SignedTetVolume(ShapedRest,T)*Profile->DensityKgPerCm3/4;
            for(int32 J=0;J<4;++J) C->Mass[T[J]]+=Mass;
        }
        // Pa -> kg/(cm s^2). Material coefficients are not Jiggle strength multipliers.
        C->AddAttribute<float>(TEXT("Stiffness"),FGeometryCollection::VerticesGroup).Fill(Profile->YoungModulusPa*.01);
        C->AddAttribute<float>(TEXT("Damping"),FGeometryCollection::VerticesGroup).Fill(0);
        C->AddAttribute<float>(TEXT("Incompressibility"),FGeometryCollection::VerticesGroup).Fill(Profile->PoissonRatio);
        C->AddAttribute<float>(TEXT("Inflation"),FGeometryCollection::VerticesGroup).Fill(1);
        GeometryCollection::Facades::FVertexBoneWeightsFacade Skin(*C,false);Skin.DefineSchema();
        GeometryCollection::Facades::FPositionTargetFacade Targets(*C);Targets.DefineSchema();
        for(int32 I=0;I<N;++I)
        {
            const auto& P=Profile->Particles[I];
            for(int32 B:P.Bones) if(!ReferenceCS.IsValidIndex(B)) { Status=TEXT("Contact attachment references absent bone");Release();return false; }
            for(int32 J:{I,I+N}) { Skin.ModifyBoneWeight(J,P.Bones,P.Weights);Skin.SetVertexKinematic(J,J>=N || P.bKinematic); }
            if(Profile->SchemaVersion>=2 && !P.bKinematic) C->Mass[I]=FMath::Max(C->Mass[I],float(Profile->MinimumMovableMassKg));
            if(P.bKinematic) C->Mass[I]=0;
            else
            {
                GeometryCollection::Facades::FPositionTargetsData T;
                T.SourceIndex={I};T.TargetIndex={I+N};T.SourceWeights={1};T.TargetWeights={1};T.bIsZeroRestLength=true;
                // C3 attaches only the chest-wall foundation. Interior material must
                // transmit contact load, rather than competing with ghost-position pins.
                const double Foundation=FMath::Clamp((double(P.RootSupport)-.65)/.35,0.,1.);
                T.Stiffness=Profile->AttachmentStiffness*(Profile->SchemaVersion>=3?Foundation*Foundation*(3-2*Foundation):(.05+.95*P.RootSupport*P.RootSupport));
                if(T.Stiffness>0)Targets.AddPositionTarget(T);
            }
        }
        auto* Asset=NewObject<UFleshAsset>(this,NAME_None,RF_Transient);Asset->SetFleshCollection(MoveTemp(C));InstanceAssets.Add(Asset);
        auto* Solver=NewObject<UDeformableSolverComponent>(GetOwner(),NAME_None,RF_Transient);
        Solver->SetupAttachment(Body);Solver->SolverTiming.bDoThreadedAdvance=false;Solver->SolverTiming.NumSubSteps=1;
        Solver->SolverTiming.NumSolverIterations=CVarContactIterations.GetValueOnGameThread()>0?FMath::Clamp(CVarContactIterations.GetValueOnGameThread(),1,64):Profile->SolverIterations;Solver->SolverEvolution.SolverQuasistatics.bDoQuasistatics=true;
        Solver->SolverConstraints.GaussSeidelConstraints.bUseGaussSeidelConstraints=CVarContactNativeXPBD.GetValueOnGameThread()==0;
        Solver->SolverConstraints.GaussSeidelConstraints.bUseGSNeohookean=CVarContactNeoHookean.GetValueOnGameThread()!=0;
        Solver->SolverForces.bEnableGravity=false;Solver->SolverCollisions.bUseFloor=false;
        Solver->RegisterComponent();Solver->SetComponentTickEnabled(false);Solvers.Add(Solver);
        auto* F=NewObject<UVamBreastContactFlesh>(GetOwner(),NAME_None,RF_Transient);F->SetupAttachment(Body);
        F->BodyForces.bApplyGravity=false;F->SetVisibility(false);F->ReferencePose=ReferenceCS;F->InputPose=Body->GetComponentSpaceTransforms();
        F->SetRestCollection(Asset);F->RegisterComponent();F->SetComponentTickEnabled(false);F->EnableSimulation(Solver);Flesh.Add(F);
    }
    Collisions=NewObject<UVamBreastContactCollisions>(GetOwner(),NAME_None,RF_Transient);Collisions->SetupAttachment(Body);
    Collisions->RegisterComponent();Collisions->SetComponentTickEnabled(false);Collisions->EnableSimulation(Solvers[0]);
    WorldCollisions=NewObject<UDeformableCollisionsComponent>(GetOwner(),NAME_None,RF_Transient);WorldCollisions->SetupAttachment(Body);
    WorldCollisions->RegisterComponent();WorldCollisions->SetComponentTickEnabled(false);WorldCollisions->EnableSimulation(Solvers[0]);
    // Chaos creates its particle/constraint ranges on the first advance. Warm up
    // without collision sources so even an initially overlapping world collider
    // sees the coupled volume constraints on its first contact step.
    if(Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild")))
    {
        for(int32 I=0;I<Solvers.Num();++I)
        {
            auto* S=Solvers[I].Get();S->WriteToSimulation(Profile->FixedStepSeconds,false);
            S->Simulate(Profile->FixedStepSeconds);S->ReadFromSimulation(Profile->FixedStepSeconds,false);
            AddVolumeConstraint(S,Flesh[I]);
        }
        bConstraintsAdded=true;
    }
    TArray<FVector> Base;for(const auto& P:Profile->Particles) Base.Add(P.Rest);
    TUniquePtr<FFleshCollection> Render(FFleshCollection::NewFleshCollection(Base,Profile->Tetrahedra,false));
    GeometryCollection::Facades::FTetrahedralBindings Bindings(*Render);Bindings.DefineSchema();
    const auto Id=Body->GetSkeletalMeshAsset()->GetPrimaryAssetId();
    const FName MeshId(*(Id.IsValid()?Id.ToString():Body->GetSkeletalMeshAsset()->GetName()));
    TArray<FVector3f> RenderOffsets=Profile->SurfaceOffsets;
    if(Profile->bResidualOnlySurface)for(auto& Offset:RenderOffsets)Offset=FVector3f::ZeroVector;
    Bindings.AddBindingsGroup(0,MeshId,0);Bindings.SetBindingsData(Profile->SurfaceParents,Profile->SurfaceWeights,RenderOffsets,Profile->SurfaceMask);
    auto* RenderAsset=NewObject<UFleshAsset>(this,NAME_None,RF_Transient);RenderAsset->SetFleshCollection(MoveTemp(Render));
    RenderAsset->TargetDeformationSkeleton=Body->GetSkeletalMeshAsset();InstanceAssets.Add(RenderAsset);
    SurfaceProducer=NewObject<UFleshComponent>(GetOwner(),NAME_None,RF_Transient);SurfaceProducer->SetupAttachment(Body);
    SurfaceProducer->SetVisibility(false);SurfaceProducer->SetRestCollection(RenderAsset);SurfaceProducer->RegisterComponent();SurfaceProducer->SetComponentTickEnabled(false);
    PreviousDeformer=Body->GetComponentMeshDeformer();const FBoolProperty* OverrideProperty=FindFProperty<FBoolProperty>(USkinnedMeshComponent::StaticClass(),TEXT("bSetMeshDeformer"));
    bPreviousDeformerOverride=OverrideProperty && OverrideProperty->GetPropertyValue_InContainer(Body);
    Body->SetMeshDeformer(Profile->SurfaceDeformer);AddTickPrerequisiteComponent(Body);
    ShapeRevision=Character->GetShapeState().Revision;Generation=Character->GetLoadGeneration();Status=TEXT("Contact solver warming up");return true;
}

void UVamBreastContactComponent::AddVolumeConstraint(UDeformableSolverComponent* Solver,UVamBreastContactFlesh* F)
{
    auto Access=Solver->PhysicsThreadAccess();auto* E=Access.GetEvolution();
    auto* Proxy=F->GetPhysicsProxy()?F->GetPhysicsProxy()->As<Chaos::Softs::FFleshThreadingProxy>():nullptr;
    if(!E || !Proxy) return;
    const int32 Start=Proxy->GetSolverParticleRange().Start,N=Profile->Particles.Num();
    // Chaos can execute separate constraint ranges concurrently. These constraints
    // touch the SAME particles as native GS and must be chained within its range.
    // Do not globally disable parallelism for unrelated characters/cloth solvers.
    const bool NativeXPBD=!Solver->SolverConstraints.GaussSeidelConstraints.bUseGaussSeidelConstraints;
    if(E->ConstraintRules().Num()<1 || (!NativeXPBD && E->ConstraintRules().Num()!=1)){Status=TEXT("ERROR: Unsupported native contact constraint layout");bEnabled=false;return;}
    // A 1 mm contact envelope covers the render/cage interpolation discrepancy.
    // This is geometric collision thickness, not allowed volume loss.
    if(Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild")))
        E->SetCollisionThickness(.1,E->ParticleGroupIds()[Start]);
    const int32 Rule=0;
    if(CVarContactGSBatch.GetValueOnGameThread()>0 && !NativeXPBD && Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild")))
    {
        if(!VamInstallContactNativeExperiment(*E,*Proxy,Access.GetProperties(),CVarContactGSBatch.GetValueOnGameThread()))
        { UE_LOG(LogTemp,Warning,TEXT("Vam contact GS adapter: unsupported layout; retaining native engine rule.")); }
    }
    auto NativeRule=MoveTemp(E->ConstraintRules()[Rule]);
    // Experimental XPBD has separate material/attachment ranges touching the same
    // particles. Serialize their original order inside one range before our volume rule.
    if(NativeXPBD)for(int I=1;I<E->ConstraintRules().Num();++I)
    {
        auto Previous=MoveTemp(NativeRule);auto Next=MoveTemp(E->ConstraintRules()[I]);
        NativeRule=[Previous=MoveTemp(Previous),Next=MoveTemp(Next)](Chaos::Softs::FSolverParticles& X,const Chaos::Softs::FSolverReal Dt){Previous(X,Dt);Next(X,Dt);};
        E->ConstraintRules()[I]=[](Chaos::Softs::FSolverParticles&,const Chaos::Softs::FSolverReal){};
    }
    // Zonal volume conservation is a constraint INSIDE every Chaos iteration, before
    // collision projection. No after-the-fact mesh inflation that pushes through a hand.
    TArray<double> Volumes;Volumes.Init(0,2);
    for(const auto& T:Profile->Tetrahedra) Volumes[Profile->Particles[T[0]].Side]+=Profile->SignedTetVolume(ShapedRest,T);
    TArray<double> TetRest;for(const auto& T:Profile->Tetrahedra)TetRest.Add(Profile->SignedTetVolume(ShapedRest,T));
    TArray<FIntPoint> Edges;TSet<uint64> Seen;
    for(const auto& Face:Profile->BoundaryTriangles)for(int32 J=0;J<3;++J){int32 A=Face[J],B=Face[(J+1)%3];if(A>B)Swap(A,B);
        const uint64 Key=(uint64(A)<<32)|uint32(B);if(!Seen.Contains(Key)){Seen.Add(Key);Edges.Add(FIntPoint(A,B));}}
    // A bounded local distance graph retains a protruding feature's 3D shape,
    // while allowing unrestricted rigid translation/rotation with surrounding tissue.
    // Membership comes from source bone support, never a world-space radius or character ID.
    for(int32 Side=0;Side<2;++Side){TArray<int32> Nodes;
        for(int32 I=0;I<N;++I)if(Profile->Particles[I].Side==Side && Profile->Particles[I].NippleSupport>.25 && !Profile->Particles[I].bKinematic)Nodes.Add(I);
        Nodes.Sort([&](int32 A,int32 B){const float WA=Profile->Particles[A].NippleSupport,WB=Profile->Particles[B].NippleSupport;return WA==WB?A<B:WA>WB;});
        if(Nodes.Num()>32)Nodes.SetNum(32);
        for(int32 I=0;I<Nodes.Num();++I)for(int32 J=I+1;J<Nodes.Num();++J){int32 A=Nodes[I],B=Nodes[J];if(A>B)Swap(A,B);
            const uint64 Key=(uint64(A)<<32)|uint32(B);if(!Seen.Contains(Key)){Seen.Add(Key);Edges.Add(FIntPoint(A,B));}}}
    TSharedPtr<Chaos::Softs::FPBDBendingConstraints> SkinBending;
    if(Profile->SurfaceBending>0)
    {
        TMap<uint64,FIntVector> FirstFace;TArray<Chaos::TVec4<int32>> Hinges;
        for(const auto& T:Profile->BoundaryTriangles)for(int32 J=0;J<3;++J)
        {
            int32 A=T[J],B=T[(J+1)%3],Opp=T[(J+2)%3];if(A>B)Swap(A,B);const uint64 Key=(uint64(A)<<32)|uint32(B);
            if(const auto* First=FirstFace.Find(Key))Hinges.Add(Chaos::TVec4<int32>(Start+A,Start+B,Start+First->Z,Start+Opp));
            else FirstFace.Add(Key,FIntVector(A,B,Opp));
        }
        using namespace Chaos::Softs;
        SkinBending=MakeShared<FPBDBendingConstraints>(E->Particles(),Start,N,MoveTemp(Hinges),
            TConstArrayView<float>(),TConstArrayView<float>(),TConstArrayView<float>(),TConstArrayView<float>(),
            FSolverVec2(Profile->SurfaceBending),FSolverVec2(0),FSolverVec2(Profile->SurfaceBending),FSolverVec2(0),
            FPBDBendingConstraintsBase::ERestAngleConstructionType::Use3DRestAngles,true);
        SkinBending->ApplyProperties(Profile->FixedStepSeconds,Solver->SolverTiming.NumSolverIterations);SkinBending->Init(E->Particles());
    }
    TArray<TArray<int32>> IncidentTets;IncidentTets.SetNum(N);
    for(int32 K=0;K<Profile->Tetrahedra.Num();++K)for(int32 J=0;J<4;++J)IncidentTets[Profile->Tetrahedra[K][J]].Add(K);
    TArray<TArray<int32>> IncidentEdges;IncidentEdges.SetNum(N);
    for(int I=0;I<Edges.Num();++I){IncidentEdges[Edges[I].X].Add(I);IncidentEdges[Edges[I].Y].Add(I);}
    const bool DirtyConstraints=CVarContactDirtyConstraints.GetValueOnGameThread()!=0;
    const bool Broadphase=CVarContactBroadphase.GetValueOnGameThread()!=0;
    const bool ExactQueryCache=CVarContactExactQueryCache.GetValueOnGameThread()!=0;
    const bool CacheStatic=CVarContactCacheStatic.GetValueOnGameThread()!=0;
    TArray<TArray<int32>> FaceIncident;FaceIncident.SetNum(Profile->BoundaryTriangles.Num());
    if(CacheStatic)for(int32 F=0;F<FaceIncident.Num();++F)for(int32 J=0;J<3;++J)
        for(int32 K:IncidentTets[Profile->BoundaryTriangles[F][J]])FaceIncident[F].AddUnique(K);
    const bool BoundaryVolume=CVarContactBoundaryVolume.GetValueOnGameThread()!=0 && Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild"));
    struct FRestMetrics { double Scale=-1;TArray<double> Limits; };
    auto Metrics=MakeShared<FRestMetrics>();
    const bool CacheRestMetrics=CVarContactRestMetrics.GetValueOnGameThread()!=0;
    TArray<FVector> Scratch;Scratch.Init(FVector::ZeroVector,N);
    E->ConstraintRules()[Rule]=[this,Start,N,Volumes,TetRest,BoundaryVolume,Metrics,CacheRestMetrics,Scratch=MoveTemp(Scratch),NativeRule=MoveTemp(NativeRule)](Chaos::Softs::FSolverParticles& X,const Chaos::Softs::FSolverReal Dt) mutable
    {
        { TRACE_CPUPROFILER_EVENT_SCOPE(VamContactNativeMaterial);const double T=FPlatformTime::Seconds();NativeRule(X,Dt);NativeMaterialMs+=(FPlatformTime::Seconds()-T)*1000; }
        TRACE_CPUPROFILER_EVENT_SCOPE(VamContactVolumeConstraints);
        const double VolumeStart=FPlatformTime::Seconds();ON_SCOPE_EXIT { VolumeConstraintMs+=(FPlatformTime::Seconds()-VolumeStart)*1000; };
        const FVector ComponentScale=Body->GetComponentScale();
        const double VolumeScale=FMath::Abs(ComponentScale.X*ComponentScale.Y*ComponentScale.Z);
        if(CacheRestMetrics && Metrics->Scale!=VolumeScale)
        {Metrics->Scale=VolumeScale;Metrics->Limits.SetNum(TetRest.Num());for(int K=0;K<TetRest.Num();++K)Metrics->Limits[K]=FMath::Pow(TetRest[K]*VolumeScale,1./3.)*.25;}
        if(Profile->SchemaVersion>=2)
        {
                TRACE_CPUPROFILER_EVENT_SCOPE(VamContactLocalCompression);
            // Cell compression is distinct from zonal volume. Surface strain is
            // coupled to contact in the post-collision rule of this same iteration.
            for(int32 K=0;K<Profile->Tetrahedra.Num();++K)
            {
                const auto& T=Profile->Tetrahedra[K];FVector P[4],G[4];
                for(int32 J=0;J<4;++J)P[J]=FVector(X.P(Start+T[J]));
                const FVector A=P[1]-P[0],B=P[2]-P[0],C=P[3]-P[0];
                const double V=FVector::DotProduct(A,FVector::CrossProduct(B,C))/6;
                const double Correction=Profile->CompressionCorrection(TetRest[K]*VolumeScale,V);
                if(Correction<=1.e-10)continue;
                G[1]=FVector::CrossProduct(B,C)/6;G[2]=FVector::CrossProduct(C,A)/6;G[3]=FVector::CrossProduct(A,B)/6;G[0]=-G[1]-G[2]-G[3];
                double Den=0;for(int32 J=0;J<4;++J)Den+=X.InvM(Start+T[J])*G[J].SizeSquared();
                if(Den<=1.e-12)continue;
                double Lambda=Correction/Den*Profile->ConstraintRelaxation;
                double MaxMove=0;for(int32 J=0;J<4;++J)MaxMove=FMath::Max(MaxMove,Lambda*X.InvM(Start+T[J])*G[J].Size());
                const double Limit=CacheRestMetrics?Metrics->Limits[K]:FMath::Pow(TetRest[K]*VolumeScale,1./3.)*.25;if(MaxMove>Limit)Lambda*=Limit/MaxMove;
                for(int32 J=0;J<4;++J)X.P(Start+T[J])+=Chaos::Softs::FSolverVec3(G[J]*(X.InvM(Start+T[J])*Lambda));
            }
        }
        for(int32 Side=0;Side<2;++Side)
        {
                TRACE_CPUPROFILER_EVENT_SCOPE(VamContactZonalVolume);
            auto& G=Scratch;for(auto& V:G)V=FVector::ZeroVector;double Volume=0,RestVolume=Volumes[Side];
            if(BoundaryVolume)
            {
                // Internal faces cancel exactly in a conforming closed tetrahedral mesh.
                // Retain cell-wise safety below; this only changes the zonal sum.
                const FVector Origin(X.P(Start));
                for(const auto& F:Profile->BoundaryTriangles)if(Profile->Particles[F.X].Side==Side)
                {
                    const FVector A=FVector(X.P(Start+F.X))-Origin,B=FVector(X.P(Start+F.Y))-Origin,C=FVector(X.P(Start+F.Z))-Origin;
                    const FVector GA=FVector::CrossProduct(B,C)/6,GB=FVector::CrossProduct(C,A)/6,GC=FVector::CrossProduct(A,B)/6;
                    Volume+=FVector::DotProduct(A,GA);G[F.X]+=GA;G[F.Y]+=GB;G[F.Z]+=GC;
                }
            }
            else
            {
            for(const auto& T:Profile->Tetrahedra) if(Profile->Particles[T[0]].Side==Side)
            {
                FVector P[4];for(int32 J=0;J<4;++J) P[J]=FVector(X.P(Start+T[J]));
                const FVector A=P[1]-P[0],B=P[2]-P[0],C=P[3]-P[0];
                const FVector G1=FVector::CrossProduct(B,C)/6,G2=FVector::CrossProduct(C,A)/6,G3=FVector::CrossProduct(A,B)/6;
                G[T[1]]+=G1;G[T[2]]+=G2;G[T[3]]+=G3;G[T[0]]-=G1+G2+G3;
                Volume+=FVector::DotProduct(A,FVector::CrossProduct(B,C))/6;
            }
            }
            // Chaos's default simulation space is world; authored rest volumes are
            // component-space cm^3. Translation/rotation do not change volume.
            const FVector Scale=Body->GetComponentScale();
            RestVolume*=FMath::Abs(Scale.X*Scale.Y*Scale.Z);
            double Den=0;for(int32 I=0;I<N;++I) Den+=X.InvM(Start+I)*G[I].SizeSquared();
            if(Den<1.e-12 || !FMath::IsFinite(Volume)) continue;
            double Lambda=(RestVolume-Volume)/Den;
            if(Profile->SchemaVersion>=2)
            {
                // Backtrack zonal correction when it would crush another cell.
                // Collision can still violate the bound; the post-solve diagnostics remain authoritative.
                for(int32 Attempt=0;Attempt<8;++Attempt)
                {
                    bool Safe=true;
                    for(int32 K=0;K<Profile->Tetrahedra.Num();++K){const auto& T=Profile->Tetrahedra[K];if(Profile->Particles[T[0]].Side!=Side)continue;
                        FVector Q[4];for(int32 J=0;J<4;++J)Q[J]=FVector(X.P(Start+T[J]))+G[T[J]]*(X.InvM(Start+T[J])*Lambda);
                        const double V=FVector::DotProduct(Q[1]-Q[0],FVector::CrossProduct(Q[2]-Q[0],Q[3]-Q[0]))/6;
                        if(V<TetRest[K]*VolumeScale*.08){Safe=false;break;}}
                    if(Safe)break;Lambda*=.5;if(Attempt==7)Lambda=0;
                }
            }
            for(int32 I=0;I<N;++I) X.P(Start+I)+=Chaos::Softs::FSolverVec3(G[I]*(X.InvM(Start+I)*Lambda));
        }
    };
    if(Profile->SchemaVersion>=2)
    {
        const int32 Post=E->AddPostCollisionConstraintRuleRange(1,true);
        E->PostCollisionConstraintRules()[Post]=[this,Start,N,Edges,E,TetRest,SkinBending,IncidentTets,FaceIncident=MoveTemp(FaceIncident),CacheStatic,Broadphase,ExactQueryCache,IncidentEdges=MoveTemp(IncidentEdges),DirtyConstraints,Metrics,CacheRestMetrics](Chaos::Softs::FSolverParticles& X,const Chaos::Softs::FSolverReal Dt)
        {
            // Couple membrane strain to the ACTUAL active Chaos rigid geometry.
            // This remains inside each solver iteration, not a render-mesh correction.
            TRACE_CPUPROFILER_EVENT_SCOPE(VamContactPostCollision);
            const double PostStart=FPlatformTime::Seconds();ON_SCOPE_EXIT { PostContactMs+=(FPlatformTime::Seconds()-PostStart)*1000; };
            TArray<int32> Active;
            E->CollisionParticlesActiveView().RangeFor([&](Chaos::Softs::FSolverCollisionParticles& C,int32 Offset,int32 End){for(int32 I=Offset;I<End;++I)if(C.GetGeometry(I))Active.Add(I);},true);
            // Bounds are immutable during this callback. Test the CURRENT node/face
            // positions on every query so projection cannot invalidate a candidate list.
            TMap<int32,FBox> ColliderBounds;
            if(Broadphase)for(int32 Collider:Active)
            {
                const auto& C=E->CollisionParticles();const auto& Geometry=C.GetGeometry(Collider);
                if(!Geometry->HasBoundingBox())continue; // Planes/unbounded shapes use exact queries.
                const Chaos::Softs::FSolverRigidTransform3 Frame(C.GetX(Collider),C.GetR(Collider));
                const auto B=Geometry->BoundingBox().TransformedAABB(FTransform(FQuat(C.GetR(Collider)),FVector(C.GetX(Collider))));
                const double Margin=E->GetCollisionThickness(E->ParticleGroupIds()[Start])+.02;
                ColliderBounds.Add(Collider,FBox(FVector(B.Min()),FVector(B.Max())).ExpandBy(Margin));
            }
            struct FQueryCache { Chaos::Softs::FSolverVec3 Position;FVector Normal;double Phi=0;bool Valid=false; };
            TMap<int32,TArray<FQueryCache>> QueryCache;
            if(ExactQueryCache)for(int32 Collider:Active)QueryCache.Add(Collider).SetNum(N);
            auto Contact=[&](int32 Particle,int32 Collider,FVector& Normal)->double
            {
                const uint32 Group=E->ParticleGroupIds()[Particle],Other=E->CollisionParticleGroupIds()[Collider];
                if(Other!=uint32(INDEX_NONE) && Group!=Other)return DBL_MAX;
                if(Broadphase)if(const auto* Bounds=ColliderBounds.Find(Collider))if(!Bounds->IsInsideOrOn(FVector(X.P(Particle))))return DBL_MAX;
                FQueryCache* Cached=ExactQueryCache?&QueryCache.FindChecked(Collider)[Particle-Start]:nullptr;
                if(Cached && Cached->Valid && Cached->Position==X.P(Particle)){Normal=Cached->Normal;return Cached->Phi;}
                const auto& C=E->CollisionParticles();
                const Chaos::Softs::FSolverRigidTransform3 Frame(C.GetX(Collider),C.GetR(Collider));
                Chaos::FVec3 LocalNormal;
                const double Phi=C.GetGeometry(Collider)->PhiWithNormal(Chaos::FVec3(Frame.InverseTransformPosition(X.P(Particle))),LocalNormal)-E->GetCollisionThickness(Group);
                Normal=FVector(Frame.TransformVector(Chaos::Softs::FSolverVec3(LocalNormal)));
                if(Cached){Cached->Position=X.P(Particle);Cached->Normal=Normal;Cached->Phi=Phi;Cached->Valid=true;}return Phi;
            };
            auto Project=[&](int32 I){if(X.InvM(I)<=0)return;for(int32 C:Active){FVector Normal;const double Phi=Contact(I,C,Normal);if(Phi<0)X.P(I)-=Chaos::Softs::FSolverVec3(Normal*Phi);}};
            if(SkinBending)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(VamContactSkinBending);
                // The cloth bending operator does not know about tetrahedra. A
                // volume-aware line search prevents its surface step inverting
                // interior cells before the coupled bulk solve can react.
                TArray<FVector> Before,After;Before.SetNumUninitialized(Profile->Particles.Num());After.SetNumUninitialized(Before.Num());
                for(int32 I=0;I<Before.Num();++I)Before[I]=FVector(X.P(Start+I));
                SkinBending->Apply(X,Dt);for(int32 I=0;I<After.Num();++I)After[I]=FVector(X.P(Start+I));
                double Alpha=1;bool Safe=false;
                for(int32 Attempt=0;Attempt<9;++Attempt)
                {
                    Safe=true;
                    for(const auto& T:Profile->Tetrahedra)
                    {
                        FVector P[4];for(int32 J=0;J<4;++J)P[J]=FMath::Lerp(Before[T[J]],After[T[J]],Alpha);
                        const double V=FVector::DotProduct(P[1]-P[0],FVector::CrossProduct(P[2]-P[0],P[3]-P[0]))/6;
                        const double B=Profile->SignedTetVolume(Before,T);
                        if(!FMath::IsFinite(V) || V<FMath::Min(B*.5,B)){Safe=false;break;}
                    }
                    if(Safe)break;Alpha*=.5;
                }
                if(!Safe)Alpha=0;
                for(int32 I=0;I<Before.Num();++I)X.P(Start+I)=Chaos::Softs::FSolverVec3(FMath::Lerp(Before[I],After[I],Alpha));
            }
            const FVector Scale=Body->GetComponentScale();
            // Vertex-only contact misses an indenter between cage vertices. Add a
            // barycentric surface contact, with no new physical degrees of freedom.
            // Closest-to-center sampling is exact for spheres on a planar triangle;
            // it supplements (does not replace) native vertex contacts for other shapes.
            TArray<double> RestEdgeLengths;
            if(CacheStatic){RestEdgeLengths.Reserve(Edges.Num());for(const auto& Edge:Edges)RestEdgeLengths.Add(((ShapedRest[Edge.X]-ShapedRest[Edge.Y])*Scale).Size());}
            auto ProjectFaces=[&]()
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(VamContactFaceProjection);
                for(int32 FaceIndex=0;FaceIndex<Profile->BoundaryTriangles.Num();++FaceIndex)
                {
                    const auto& Face=Profile->BoundaryTriangles[FaceIndex];
                    const int32 Id[3]={Start+Face.X,Start+Face.Y,Start+Face.Z};
                    if(X.InvM(Id[0])+X.InvM(Id[1])+X.InvM(Id[2])<=0)continue;
                    for(int32 Collider:Active)
                    {
                        const uint32 Group=E->ParticleGroupIds()[Id[0]],Other=E->CollisionParticleGroupIds()[Collider];
                        if(Other!=uint32(INDEX_NONE) && Group!=Other)continue;
                        const auto& C=E->CollisionParticles();const auto& Geometry=C.GetGeometry(Collider);
                        const Chaos::Softs::FSolverRigidTransform3 Frame(C.GetX(Collider),C.GetR(Collider));
                        FVector P[3];for(int32 J=0;J<3;++J)P[J]=FVector(X.P(Id[J]));
                        if(Broadphase)if(const auto* Bounds=ColliderBounds.Find(Collider))
                        {
                            FBox TriangleBounds(ForceInit);for(int J=0;J<3;++J)TriangleBounds+=P[J];
                            if(!Bounds->Intersect(TriangleBounds))continue;
                        }
                        if(FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).SizeSquared()<1.e-12)continue;
                        const FVector Q=FMath::ClosestPointOnTriangleToPoint(FVector(C.GetX(Collider)),P[0],P[1],P[2]);
                        Chaos::FVec3 LN;const double Phi=Geometry->PhiWithNormal(Chaos::FVec3(Frame.InverseTransformPosition(Chaos::Softs::FSolverVec3(Q))),LN)-E->GetCollisionThickness(Group);
                        if(Phi>=-1.e-5)continue;
                        const FVector W=FMath::ComputeBaryCentric2D(Q,P[0],P[1],P[2]);
                        const FVector Normal(Frame.TransformVector(Chaos::Softs::FSolverVec3(LN)));
                        double Den=0;for(int32 J=0;J<3;++J)Den+=X.InvM(Id[J])*W[J]*W[J];
                        if(Den<1.e-12)continue;
                        const double Lambda=-Phi/Den;
                        // Bounded increments allow bulk and contact to converge together.
                        double MaxMove=0;for(int32 J=0;J<3;++J)MaxMove=FMath::Max(MaxMove,Lambda*X.InvM(Id[J])*W[J]);
                        const double Limit=.1*FMath::Min3((P[1]-P[0]).Size(),(P[2]-P[0]).Size(),(P[2]-P[1]).Size());
                        double Relax=MaxMove>Limit?Limit/MaxMove:1.;
                        TArray<int32,TInlineAllocator<64>> Affected;
                        if(CacheStatic)Affected.Append(FaceIncident[FaceIndex]);
                        else for(int32 J=0;J<3;++J)for(int32 K:IncidentTets[Face[J]])Affected.AddUnique(K);
                        bool Safe=false;
                        for(int32 Attempt=0;Attempt<9;++Attempt)
                        {
                            Safe=true;
                            for(int32 K:Affected){const auto& T=Profile->Tetrahedra[K];FVector Before[4],After[4];
                                for(int32 J=0;J<4;++J){Before[J]=After[J]=FVector(X.P(Start+T[J]));for(int32 Node=0;Node<3;++Node)if(T[J]==Face[Node])After[J]+=Normal*(Lambda*Relax*X.InvM(Id[Node])*W[Node]);}
                                const double BV=FVector::DotProduct(Before[1]-Before[0],FVector::CrossProduct(Before[2]-Before[0],Before[3]-Before[0]))/6;
                                const double AV=FVector::DotProduct(After[1]-After[0],FVector::CrossProduct(After[2]-After[0],After[3]-After[0]))/6;
                                const bool Refined=Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild"));
                                const double Floor=BV>0?FMath::Min(BV*(Refined?1.:.5),TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z)*(Refined?.2:.1)):BV;
                                if(!FMath::IsFinite(AV) || AV<Floor){Safe=false;break;}}
                            if(Safe)break;Relax*=.5;
                        }
                        if(!Safe)continue;
                        for(int32 J=0;J<3;++J)X.P(Id[J])+=Chaos::Softs::FSolverVec3(Normal*(Lambda*Relax*X.InvM(Id[J])*W[J]));
                    }
                }
            };
            const int32 ContactSweeps=Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild"))?24:4;
            TArray<uint8> DirtyEdges,DirtyTets;
            if(DirtyConstraints){DirtyEdges.Init(1,Edges.Num());DirtyTets.Init(1,TetRest.Num());}
            auto MarkMoved=[&](int32 Local){if(DirtyConstraints){for(int K:IncidentEdges[Local])DirtyEdges[K]=1;for(int K:IncidentTets[Local])DirtyTets[K]=1;}};
            for(int32 Sweep=0;Sweep<ContactSweeps;++Sweep)
            {
                if(Profile->SchemaVersion>=4 && Sweep==0)ProjectFaces();
                { TRACE_CPUPROFILER_EVENT_SCOPE(VamContactEdgeSweep);
                for(int32 Index=0;Index<Edges.Num();++Index)
                {
                    const int32 EdgeIndex=Sweep%2==0?Index:Edges.Num()-1-Index;
                    if(DirtyConstraints){if(!DirtyEdges[EdgeIndex])continue;DirtyEdges[EdgeIndex]=0;}
                    const auto& Edge=Edges[EdgeIndex];const int32 A=Start+Edge.X,B=Start+Edge.Y;
                    const FVector D=FVector(X.P(A)-X.P(B));const double L=D.Size();if(L<1.e-8)continue;
                    const double Rest=CacheStatic?RestEdgeLengths[Sweep%2==0?Index:Edges.Num()-1-Index]:((ShapedRest[Edge.X]-ShapedRest[Edge.Y])*Scale).Size();
                    const double Support=FMath::Min(Profile->Particles[Edge.X].NippleSupport,Profile->Particles[Edge.Y].NippleSupport);
                    const double Preserve=FMath::Clamp(Profile->NippleShapePreservation*NippleShapePreservationScale*Support,0.,1.);
                    const double Lower=FMath::Lerp(Profile->SurfaceMinimumStretch,1-Profile->NippleAllowedStrain,Preserve);
                    const double Upper=FMath::Lerp(Profile->SurfaceMaximumStretch,1+Profile->NippleAllowedStrain,Preserve);
                    const double C=L-FMath::Clamp(L,Rest*Lower,Rest*Upper);if(FMath::Abs(C)<1.e-9)continue;
                    FVector GA=D*(FMath::Sign(C)/L),GB=-GA;
                    auto Restrict=[&](int32 I,FVector& G){for(int32 Collider:Active){FVector Normal;if(Contact(I,Collider,Normal)>.01)continue;const double Inward=FVector::DotProduct(G,Normal);if(Inward>0)G-=Normal*Inward;}};
                    Restrict(A,GA);Restrict(B,GB);
                    const double Den=X.InvM(A)*GA.SizeSquared()+X.InvM(B)*GB.SizeSquared();if(Den<1.e-12)continue;
                    double Lambda=FMath::Abs(C)/Den;
                    const double Move=Lambda*FMath::Max(GA.Size()*X.InvM(A),GB.Size()*X.InvM(B));
                    if(Move>Rest*.25)Lambda*=Rest*.25/Move;
                    X.P(A)-=Chaos::Softs::FSolverVec3(GA*(Lambda*X.InvM(A)));X.P(B)-=Chaos::Softs::FSolverVec3(GB*(Lambda*X.InvM(B)));
                    Project(A);Project(B);MarkMoved(Edge.X);MarkMoved(Edge.Y);
                }
                }
                // Collision and membrane projection can compress a cell AFTER
                // the native solve. Restore its barrier in contact-tangent space,
                // so neighboring tissue takes the load instead of penetrating.
                { TRACE_CPUPROFILER_EVENT_SCOPE(VamContactCellBarrierSweep);
                for(int32 K=0;K<Profile->Tetrahedra.Num();++K){
                    // The final sweep has a different bulk correction, so evaluate all cells there.
                    if(DirtyConstraints){if(!DirtyTets[K] && Sweep!=ContactSweeps-1)continue;DirtyTets[K]=0;}
                    const auto& T=Profile->Tetrahedra[K];FVector P[4],G[4];
                    for(int32 J=0;J<4;++J)P[J]=FVector(X.P(Start+T[J]));
                    const FVector A=P[1]-P[0],B=P[2]-P[0],C=P[3]-P[0];
                    const double V=FVector::DotProduct(A,FVector::CrossProduct(B,C))/6;
                    const double RestVolume=TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z);
                    const double Barrier=FMath::Max(0.,RestVolume*Profile->CompressionBarrierRatio-V);
                    const double Correction=Sweep==ContactSweeps-1?FMath::Max(Barrier,Profile->CompressionCorrection(RestVolume,V)*.25):Barrier;
                    // Ignore sub-tolerance bulk corrections (0.01% cell volume);
                    // do not run contact queries for essentially undeformed cells.
                    if(Correction<=FMath::Max(1.e-10,RestVolume*1.e-4))continue;
                    G[1]=FVector::CrossProduct(B,C)/6;G[2]=FVector::CrossProduct(C,A)/6;G[3]=FVector::CrossProduct(A,B)/6;G[0]=-G[1]-G[2]-G[3];
                    double Den=0,MaxMove=0;
                    for(int32 J=0;J<4;++J){for(int32 Collider:Active){FVector Normal;if(Contact(Start+T[J],Collider,Normal)>.01)continue;const double Inward=FVector::DotProduct(G[J],Normal);if(Inward<0)G[J]-=Normal*Inward;}
                        Den+=X.InvM(Start+T[J])*G[J].SizeSquared();}
                    if(Den<=1.e-12)continue;double Lambda=Correction/Den*Profile->ConstraintRelaxation;
                    for(int32 J=0;J<4;++J)MaxMove=FMath::Max(MaxMove,Lambda*X.InvM(Start+T[J])*G[J].Size());
                    const double Limit=CacheRestMetrics?Metrics->Limits[K]:FMath::Pow(TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z),1./3.)*.25;if(MaxMove>Limit)Lambda*=Limit/MaxMove;
                    for(int32 J=0;J<4;++J){X.P(Start+T[J])+=Chaos::Softs::FSolverVec3(G[J]*(Lambda*X.InvM(Start+T[J])));Project(Start+T[J]);MarkMoved(T[J]);}
                }
                }
                // Stop local refinement once the cell barrier has converged;
                // difficult contacts retain the bounded iteration budget.
                if(ContactSweeps>4 && Sweep>=3)
                {
                TRACE_CPUPROFILER_EVENT_SCOPE(VamContactConvergenceScan);
                    bool Supported=true;
                    for(int32 K=0;K<Profile->Tetrahedra.Num();++K){const auto& T=Profile->Tetrahedra[K];const FVector A(X.P(Start+T[0]));
                        const double V=FVector::DotProduct(FVector(X.P(Start+T[1]))-A,FVector::CrossProduct(FVector(X.P(Start+T[2]))-A,FVector(X.P(Start+T[3]))-A))/6;
                        if(!FMath::IsFinite(V) || V<TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z)*.25){Supported=false;break;}}
                    if(Supported)break;
                }
            }
            // End the coupled iteration with surface contact as well: volume and
            // edge constraints can move a triangle into a sphere without any
            // individual vertex penetrating it.
            if(Profile->SchemaVersion>=4)ProjectFaces();

        };
    }

}

void UVamBreastContactComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VamBreastContact);
    const double TickStart=FPlatformTime::Seconds();SolveMs=PublishMs=InitMs=NativeMaterialMs=VolumeConstraintMs=PostContactMs=0;
    ON_SCOPE_EXIT { TickMs=(FPlatformTime::Seconds()-TickStart)*1000; };
    Super::TickComponent(Dt,TickType,TickFunction);
    if(!GetWorld() || !GetWorld()->IsGameWorld()) return;
    Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    if(!Character || !Character->Body || !bEnabled) { Release();if(!Status.StartsWith(TEXT("ERROR:"))) Status=TEXT("Contact disabled");return; }
    if(Body!=Character->Body || Generation!=Character->GetLoadGeneration())
    {
        Release();LowerWakeRevision=INDEX_NONE;LowerWakeBoneBounds.Reset();Body=Character->Body;
        const auto* RC=Character->RuntimeConfiguration.LoadSynchronous();Profile=RC?RC->BreastContact.LoadSynchronous():nullptr;
        Generation=Character->GetLoadGeneration();
    }
    const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>();
    const int32 Teleport=Motion?Motion->GetClock().TeleportRevision:0;
    if(GetWorld()->IsPaused() || (Motion && Motion->GetClock().bPaused) || Dt<=0) { Accumulator=0;return; }
    if(Teleport!=TeleportRevision || ShapeRevision!=Character->GetShapeState().Revision) { Release();TeleportRevision=Teleport; }
    // Do not allocate/step contact solvers when there is no contact source nearby.
    bool NearbyWorld=false;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> Sources;
    FBox ContactBounds(ForceInit);
    if(auto* B=Cast<UVamBreastSkeletalMeshComponent>(Body))
        for(const auto& R:B->RestSides) if(Body->GetComponentSpaceTransforms().IsValidIndex(R.AnchorBone))
        {
            ContactBounds+=ContactRegionBounds(B,R,.6);
        }
    // Cache Shape-dependent bone-local boxes; positive skin weights keep the
    // skinned points inside their union. Per-frame wake work scales with bones,
    // not particle count times Morph count.
    if(Profile && bLowerBodyContactEnabled && Profile->EffectiveVolumeCm3.Num()>2){
        if(LowerWakeRevision!=Character->GetShapeState().Revision || LowerWakeGeneration!=Generation){
            LowerWakeBoneBounds.Reset();TArray<FTransform> Ref=Character->GetShapeReferencePose();const auto& Skeleton=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
            for(int B=0;B<Ref.Num();++B)if(Skeleton.GetParentIndex(B)>=0)Ref[B]*=Ref[Skeleton.GetParentIndex(B)];
            TArray<FVector> Rest;for(const auto& P:Profile->Particles)Rest.Add(P.Rest);
            for(const auto& M:Profile->Morphs){const double W=Body->GetMorphTarget(M.Parameter);if(FMath::Abs(W)>1e-8)for(int I=0;I<Rest.Num();++I)Rest[I]+=M.ParticleDeltas[I]*W;}
            for(int I=0;I<Profile->Particles.Num();++I){const auto& P=Profile->Particles[I];if(P.Side<2)continue;
                for(int J=0;J<P.Bones.Num();++J)if(P.Weights[J]>0&&Ref.IsValidIndex(P.Bones[J])){FBox* Box=LowerWakeBoneBounds.Find(P.Bones[J]);if(!Box)Box=&LowerWakeBoneBounds.Add(P.Bones[J],FBox(ForceInit));*Box+=Ref[P.Bones[J]].InverseTransformPosition(Rest[I]);}}
            LowerWakeRevision=Character->GetShapeState().Revision;LowerWakeGeneration=Generation;}
        const auto& Pose=Body->GetComponentSpaceTransforms();for(const auto& Pair:LowerWakeBoneBounds)if(Pose.IsValidIndex(Pair.Key))ContactBounds+=Pair.Value.TransformBy(Pose[Pair.Key]*Body->GetComponentTransform());
    }
    if(bWorldCollision && ContactBounds.IsValid)
    {
        TArray<FOverlapResult> Hits;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(VamContactWake),false,GetOwner());
        GetWorld()->OverlapMultiByObjectType(Hits,ContactBounds.GetCenter(),FQuat::Identity,Objects,FCollisionShape::MakeBox(ContactBounds.GetExtent()),Query);
        for(const auto& H:Hits) if(auto* Mesh=H.GetComponent())
            if(Mesh->IsCollisionEnabled() && Mesh->GetCollisionResponseToChannel(Body->GetCollisionObjectType())==ECR_Block) Sources.AddUnique(Mesh);
        Sources.AddUnique(Body);Sources.Sort([](const auto& A,const auto& B){return A->GetUniqueID()<B->GetUniqueID();});
    }
    TArray<FVamContactRigidSource> RigidSources;
    if(bWorldCollision)for(const auto& S:Sources)if(S.IsValid())VamGatherRigidSources(S.Get(),Cast<UVamBreastSkeletalMeshComponent>(Body),ContactBounds,RigidSources);
    NearbyWorld=!RigidSources.IsEmpty();
    if(CompletedSteps<2 && FParse::Param(FCommandLine::Get(),TEXT("VamContactTraceRigid")))for(const auto& Source:RigidSources)UE_LOG(LogTemp,Display,TEXT("CONTACT_RIGID owner=%s target=%s bone=%s"),*GetOwner()->GetName(),*Source.Component->GetOwner()->GetName(),*Source.Bone.ToString());
    bool NearbySoft=false;
    auto SoftBounds=[](UVamBreastSkeletalMeshComponent* Mesh,TArray<FBox>& Sides){if(!Mesh)return;for(const auto& R:Mesh->RestSides)if(Mesh->GetComponentSpaceTransforms().IsValidIndex(R.AnchorBone)){
        Sides.Add(ContactRegionBounds(Mesh,R,.55));}};
    if(bSoftCollision){TArray<FBox> Own;SoftBounds(Cast<UVamBreastSkeletalMeshComponent>(Body),Own);NearbySoft=Own.Num()==2&&Own[0].Intersect(Own[1]);
        for(TObjectIterator<UVamBreastContactComponent> It;It&&!NearbySoft;++It){if(*It==this || It->GetWorld()!=GetWorld() || !It->IsRegistered() || !It->bEnabled || !It->bSoftCollision)continue;
            auto* Other=It->GetOwner()->FindComponentByClass<UVamCharacterComponent>();TArray<FBox> Their;SoftBounds(Other?Cast<UVamBreastSkeletalMeshComponent>(Other->Body):nullptr,Their);for(const auto& A:Own)for(const auto& B:Their)NearbySoft|=A.ExpandBy(3).Intersect(B);}}
    if(DebugSide==INDEX_NONE && PressSpheres.IsEmpty() && !NearbyWorld && !NearbySoft)
    { if(!Solvers.IsEmpty() || GPUHandle) Release();Status=TEXT("Contact idle: no sources; native skinning; zero solver steps");return; }
    AppliedDebugDepth=FMath::FInterpConstantTo(AppliedDebugDepth,DebugDepth,Dt,.4f);
    if(bReleasingDebug && AppliedDebugDepth<=KINDA_SMALL_NUMBER){DebugSide=INDEX_NONE;bReleasingDebug=false;}
    bool SupportedGPU=Profile && Profile->GPUSurfaceDeformer && Profile->bResidualOnlySurface && !bDebugPlaten;
    for(const auto& S:PressSpheres)SupportedGPU&=!S.bPlaten;
    for(const auto& Source:RigidSources)SupportedGPU&=VamSupportsGPURigid(Source);
    const FVector GPUScale=Body->GetComponentScale();
    SupportedGPU &= GPUScale.GetMin()>0 && GPUScale.GetMax()-GPUScale.GetMin()<.001;
    if(Profile && Profile->EffectiveVolumeCm3.Num()>2 && (!SupportedGPU || bGPURejected || (!bUseGPU && CVarContactGPU.GetValueOnGameThread()==0))){Release();Status=TEXT("Lower-body volume contact requires the GPU backend and supported simple colliders; no bilateral CPU fallback");return;}
    const bool WantGPU=!bGPURejected && (bUseGPU || CVarContactGPU.GetValueOnGameThread()!=0)&&SupportedGPU;
    if((GPUHandle || !Solvers.IsEmpty()) && WantGPU!=bGPUActive)Release();
    if(Solvers.IsEmpty() && !GPUHandle) { bGPUActive=WantGPU;const double T=FPlatformTime::Seconds();const bool Ready=Initialize();InitMs=(FPlatformTime::Seconds()-T)*1000;if(!Ready)return; }
    const int32 N=Profile->Particles.Num();
    for(auto F:Flesh) F->InputPose=Body->GetComponentSpaceTransforms();
    TArray<FVector> AnimatedRest;AnimatedRest.SetNum(N);
    const auto& FinalPose=Body->GetComponentSpaceTransforms();
    for(int32 I=0;I<N;++I)
    {
        FVector P=FVector::ZeroVector;const auto& Particle=Profile->Particles[I];
        for(int32 J=0;J<Particle.Bones.Num();++J)
        {
            const int32 B=Particle.Bones[J];
            P+=FinalPose[B].TransformPosition(ReferenceCS[B].InverseTransformPosition(ShapedRest[I]))*Particle.Weights[J];
        }
        AnimatedRest[I]=P;
    }
    Collisions->Spheres=PressSpheres;
    if(WorldCollisions)for(const auto& Old:WorldSources) if(Old.IsValid() && !Sources.Contains(Old)) if(auto* Mesh=Cast<UStaticMeshComponent>(Old.Get()))WorldCollisions->RemoveStaticMeshComponent(Mesh);
    if(WorldCollisions)for(const auto& Source:Sources) if(!WorldSources.Contains(Source)) if(auto* Mesh=Cast<UStaticMeshComponent>(Source.Get()))WorldCollisions->AddStaticMeshComponent(Mesh);
    WorldSources=MoveTemp(Sources);
    if(auto* Breast=Cast<UVamBreastSkeletalMeshComponent>(Body)) if(Breast->RestSides.IsValidIndex(DebugSide))
    {
        const auto& Side=Breast->RestSides[DebugSide];const auto& Pose=Body->GetComponentSpaceTransforms();
        if(Pose.IsValidIndex(Side.AnchorBone))
        {
            FVamBreastPressSphere S;S.RadiusCm=FMath::Max(1.,Side.EffectiveRadiusCm*(Profile->SchemaVersion>=2?Profile->DebugPressRadiusFraction:.3));
            // Contact onset is derived from the whole support patch. Using only
            // its most anterior point would spend the stroke moving the nipple.
            const FTransform Anchor=Pose[Side.AnchorBone];
            S.bPlaten=bDebugPlaten;// The platen must extend farther than its stroke. A thin slab lets
            // an elastic iteration cross the back face and escape discrete contact.
            S.HalfExtentCm=FVector(Side.EffectiveDepthCm,S.RadiusCm,S.RadiusCm);
            FVector Target=Side.COM;Target.Y+=DebugPressOffset.X*Side.EffectiveRadiusCm;Target.Z+=DebugPressOffset.Y*Side.EffectiveRadiusCm;
            double Front=-DBL_MAX,BodyFront=-DBL_MAX;
            for(int32 I=0;I<N;++I)if(Profile->Particles[I].Side==DebugSide && !Profile->Particles[I].bKinematic)
            {
                const FVector Local=Anchor.InverseTransformPosition(AnimatedRest[I]);
                const double R2=FMath::Square(Local.Y-Target.Y)+FMath::Square(Local.Z-Target.Z);
                if(FMath::Abs(Local.Y-Target.Y)>S.RadiusCm || FMath::Abs(Local.Z-Target.Z)>S.RadiusCm)continue;
                if(!S.bPlaten && R2>=FMath::Square(S.RadiusCm))continue;
                const double Sag=S.bPlaten?0:S.RadiusCm-FMath::Sqrt(FMath::Square(S.RadiusCm)-R2);
                Front=FMath::Max(Front,Local.X-Sag);
                if(Profile->Particles[I].NippleSupport<.1)BodyFront=FMath::Max(BodyFront,Local.X-Sag);
            }
            if(Profile->SchemaVersion>=4)
            {
                Front=Profile->ProbeFront(AnimatedRest,Anchor,Target,S.RadiusCm,S.bPlaten,false,DebugSide);
                BodyFront=Profile->ProbeFront(AnimatedRest,Anchor,Target,S.RadiusCm,S.bPlaten,true,DebugSide);
            }
            if(Front==-DBL_MAX)Front=Target.X;
            if(BodyFront==-DBL_MAX)BodyFront=Front;
            // Ramp from first contact to the prescribed depth below BODY contact.
            // This is an indenter stroke, not a claim of uniform tissue strain.
            const double Progress=FMath::Clamp(double(AppliedDebugDepth)/.05,0.,1.);
            const double Stroke=AppliedDebugDepth*Side.EffectiveDepthCm+Progress*(S.bPlaten?FMath::Max(0.,Front-BodyFront):0.);
            Target.X=Front+(S.bPlaten?S.HalfExtentCm.X:S.RadiusCm)-Stroke+(Profile->BuildAlgorithmVersion.Contains(TEXT("ftetwild"))?.1:0.);
            const FTransform WorldFrame=Anchor*Body->GetComponentTransform();
            S.WorldCenter=WorldFrame.TransformPosition(Target);S.WorldRotation=WorldFrame.GetRotation();Collisions->Spheres.Add(S);
        }
    }
    ActivePressSphereCount=Collisions->Spheres.Num();
    if(bShowContacts)for(const auto& S:Collisions->Spheres){if(S.bPlaten)DrawDebugBox(GetWorld(),S.WorldCenter,S.HalfExtentCm,S.WorldRotation,FColor::Orange,false,0);else DrawDebugSphere(GetWorld(),S.WorldCenter,S.RadiusCm,20,FColor::Orange,false,0);}
    if(bGPUActive && GPUHandle)
    {
        TArray<FVamGPUContactLoad> Loads;double Submitted=0;const uint64 Packet=VamGPUReadLoads(GPUHandle,Loads,Submitted);
        if(Packet>0){GPULoadFrame=Packet;ContactForceNewtons=FVector::ZeroVector;
            if(bForceFeedback && FPlatformTime::Seconds()-Submitted<.1){auto* Response=GetOwner()->FindComponentByClass<UVamBodyContactResponseComponent>();
                for(const auto& L:Loads){if(FVector::DistSquared(L.Origin,Body->GetComponentLocation())>2500)continue;
                    // A vanished collider invalidates its delayed packet.
                    UPrimitiveComponent* Target=L.ColliderId?ReactionTargets.FindRef(L.ColliderId).Get():nullptr;if(L.ColliderId&&(!Target||!Target->IsCollisionEnabled()))continue;
                    const double Cap=1000,Scale=FMath::Min(1.,Cap/FMath::Max(L.ForceNewtons.Size(),1e-9));const FVector F=L.ForceNewtons*Scale,T=L.TorqueNewtonMeters*Scale;
                    if(!Target || Target->GetOwner()!=GetOwner() || Target->IsSimulatingPhysics(ReactionBones.FindRef(L.ColliderId))){ContactForceNewtons+=F;if(Response)Response->AddContactWrench(F,T,L.Origin);}
                    const FName TargetBone=ReactionBones.FindRef(L.ColliderId);
                    if(Target&&Target->IsSimulatingPhysics(TargetBone)){Target->AddForce(-F*100,TargetBone);const FVector TorqueAtCOM=-T+FVector::CrossProduct((L.Origin-Target->GetCenterOfMass(TargetBone))*.01,-F);Target->AddTorqueInRadians(TorqueAtCOM*10000,TargetBone);}
                    else if(Target&&Target->GetOwner()!=GetOwner())if(auto* Other=Target->GetOwner()->FindComponentByClass<UVamBodyContactResponseComponent>())Other->AddContactWrench(-F,-T,L.Origin);
                }}}
        FVamGPUContactScene Scene;Scene.WorldId=GetWorld()->GetUniqueID();Scene.BodyToWorld=Body->GetComponentTransform();Scene.bSoftCollision=bSoftCollision;
        if(auto* Breast=Cast<UVamBreastSkeletalMeshComponent>(Body))if(!Breast->RestSides.IsEmpty() && FinalPose.IsValidIndex(Breast->RestSides[0].ChestBone))Scene.PairFrameToWorld=FinalPose[Breast->RestSides[0].ChestBone]*Body->GetComponentTransform();
        ReactionTargets.Reset();ReactionBones.Reset();TArray<FVector4f> Rest,Spheres;Rest.Reserve(N);
        for(int I=0;I<N;++I)Rest.Add(FVector4f(FVector3f(AnimatedRest[I]),GPUInverseMass[I]));
        const auto BodyWorld=Body->GetComponentTransform();
        for(const auto& S:Collisions->Spheres){Spheres.Add(FVector4f(FVector3f(BodyWorld.InverseTransformPosition(S.WorldCenter)),S.RadiusCm/BodyWorld.GetScale3D().GetAbsMax()));Scene.ColliderIds.Add(0);}
        Scene.ShapeRotations.Init(FVector4f(0,0,0,1),Spheres.Num());Scene.ShapeExtents.Init(FVector4f(0,0,0,0),Spheres.Num());
        for(const auto& Source:RigidSources){
            uint32 Ordinal=1;const double Scale=Source.World.GetScale3D().GetAbsMax()/BodyWorld.GetScale3D().GetAbsMax();
            auto AddShape=[&](FVector Center,FQuat Rotation,FVector Extent,float Radius,float Kind){const FVector C=BodyWorld.InverseTransformPosition(Source.World.TransformPosition(Center));
                const FQuat Q=BodyWorld.GetRotation().Inverse()*Source.World.GetRotation()*Rotation;Spheres.Add(FVector4f(FVector3f(C),Radius*Scale));Scene.ShapeRotations.Add(FVector4f(Q.X,Q.Y,Q.Z,Q.W));Scene.ShapeExtents.Add(FVector4f(FVector3f(Extent*Scale),Kind));
                const uint64 Id=(uint64(Source.Component->GetUniqueID())<<32)|(Source.BodyTag<<16)|Ordinal++;Scene.ColliderIds.Add(Id);ReactionTargets.Add(Id,Source.Component);ReactionBones.Add(Id,Source.Bone);};
            for(const auto& Sphere:Source.Setup->AggGeom.SphereElems)AddShape(Sphere.Center,FQuat::Identity,FVector::ZeroVector,Sphere.Radius,0);
            for(const auto& Box:Source.Setup->AggGeom.BoxElems)AddShape(Box.Center,Box.Rotation.Quaternion(),FVector(Box.X,Box.Y,Box.Z)*.5f*(Source.World.GetScale3D()/Source.World.GetScale3D().GetAbsMax()),FMath::Max3(Box.X,Box.Y,Box.Z)*.5f,1);
            for(const auto& Capsule:Source.Setup->AggGeom.SphylElems)AddShape(Capsule.Center,Capsule.Rotation.Quaternion(),FVector(0,0,Capsule.Length*.5f),Capsule.Radius,2);
            for(const auto& Convex:Source.Setup->AggGeom.ConvexElems){TArray<FPlane> Planes;Convex.GetPlanes(Planes);const auto Local=Convex.GetTransform();const FVector LocalScale=Local.GetScale3D();
                const int Begin=Scene.ShapePlanes.Num();for(const auto& Plane:Planes){const FVector PlaneNormal=FVector(Plane.X,Plane.Y,Plane.Z)/LocalScale;const double Length=PlaneNormal.Size();if(Length>1e-8)Scene.ShapePlanes.Add(FVector4f(FVector3f(PlaneNormal/Length),Plane.W/Length*Scale));}
                AddShape(Local.GetTranslation(),Local.GetRotation(),FVector::ZeroVector,FMath::Max(.1,Convex.ElemBox.GetExtent().Size()*LocalScale.GetAbsMax()),3);
                Scene.ShapeExtents.Last()=FVector4f(Begin,Scene.ShapePlanes.Num()-Begin,0,3);
            }
        }
        TArray<FVector4f> Starts;
        for(auto S:Spheres)
        {
            FVector COM=FVector::ZeroVector;double Best=DBL_MAX;
            if(auto* Breast=Cast<UVamBreastSkeletalMeshComponent>(Body))for(const auto& Side:Breast->RestSides)if(FinalPose.IsValidIndex(Side.AnchorBone)){
                const FVector C=FinalPose[Side.AnchorBone].TransformPosition(Side.COM);const double D=(FVector(FVector3f(S))-C).SizeSquared();if(D<Best){Best=D;COM=C;}}
            if(Profile->EffectiveVolumeCm3.Num()>2){for(int I=0;I<AnimatedRest.Num();++I){const double D=(FVector(FVector3f(S))-AnimatedRest[I]).SizeSquared();if(D<Best){Best=D;COM=AnimatedRest[I];}}}
            const FVector Direction=(FVector(FVector3f(S))-COM).GetSafeNormal();
            Starts.Add(FVector4f(FVector3f(FVector(FVector3f(S))+Direction*S.W*4),S.W));
        }
        ActivePressSphereCount=Spheres.Num();VamGPUSetScene(GPUHandle,MoveTemp(Scene));VamGPUUpdate(GPUHandle,MoveTemp(Rest),MoveTemp(Spheres),MoveTemp(Starts));++CompletedSteps;
        Status=VamGPUDiagnostics(GPUHandle,MaxContactResidualCm);
        TArray<FVector> DiagnosticRest,DiagnosticCurrent;const uint64 Frame=VamGPUReadDiagnostic(GPUHandle,DiagnosticRest,DiagnosticCurrent);
        if(Frame>GPUDiagnosticFrame && DiagnosticRest.Num()==N && DiagnosticCurrent.Num()==N){GPUDiagnosticFrame=Frame;
            bool Safe=Profile->MeasureVolume(DiagnosticRest,DiagnosticCurrent,VolumeState);
            for(const FVector& P:DiagnosticCurrent)Safe &= !P.ContainsNaN();
            for(const auto& V:VolumeState)Safe &= V.InvertedTetrahedra==0;
            if(!Safe){for(const auto& V:VolumeState)UE_LOG(LogTemp,Warning,TEXT("GPU_SAFETY frame=%llu volume=%g rest=%g minJ=%g inversions=%d"),Frame,V.CurrentVolumeCm3,V.RestVolumeCm3,V.MinimumTetRatio,V.InvertedTetrahedra);bGPURejected=true;Release();Status=TEXT("GPU numerical safety rejected output; Reset to retry (lower-body has no CPU fallback)");return;}}
        if(bShowCage&&DiagnosticCurrent.Num()==N)for(const auto& T:Profile->BoundaryTriangles)for(int J=0;J<3;++J)DrawDebugLine(GetWorld(),BodyWorld.TransformPosition(DiagnosticCurrent[T[J]]),BodyWorld.TransformPosition(DiagnosticCurrent[T[(J+1)%3]]),FColor::Cyan,false,0,0,.3f);
        return;
    }
    auto MeasurePress=[&](bool After)
    {
        int32& Count=After?InsideAfter:InsideBefore;double& Depth=After?PenetrationAfter:PenetrationBefore;
        Count=0;Depth=0;MovableParticles=0;
        auto Access=Solvers[0]->PhysicsThreadAccess();auto* E=Access.GetEvolution();
        auto* Proxy=Flesh[0]->GetPhysicsProxy()?Flesh[0]->GetPhysicsProxy()->As<Chaos::Softs::FFleshThreadingProxy>():nullptr;
        if(!E || !Proxy)return;
        const int32 Start=Proxy->GetSolverParticleRange().Start;
        if(Start<0 || Proxy->GetSolverParticleRange().Count<N || Start+N>int32(E->Particles().Size()))return;
        for(int32 I=0;I<N;++I) if(E->Particles().InvM(Start+I)>0)
        {
            ++MovableParticles;const FVector P(E->Particles().GetX(Start+I));
            for(const auto& S:Collisions->Spheres) { const FVector Q=S.WorldRotation.UnrotateVector(P-S.WorldCenter).GetAbs()-S.HalfExtentCm;
                const double BoxPhi=Q.ComponentMax(FVector::ZeroVector).Size()+FMath::Min(FMath::Max3(Q.X,Q.Y,Q.Z),0.);
                const double D=S.bPlaten?-BoxPhi:S.RadiusCm-FVector::Distance(P,S.WorldCenter);if(D>0){++Count;Depth=FMath::Max(Depth,D);} }
        }
    };
    MeasurePress(false);
    const double SolveStart=FPlatformTime::Seconds();
    Accumulator=FMath::Min(Accumulator+Dt,Profile->FixedStepSeconds);
    while(Accumulator>=Profile->FixedStepSeconds)
    {
        for(int32 I=0;I<Solvers.Num();++I)
        {
            auto* S=Solvers[I].Get();S->WriteToSimulation(Profile->FixedStepSeconds,false);S->Simulate(Profile->FixedStepSeconds);S->ReadFromSimulation(Profile->FixedStepSeconds,false);
            if(!bConstraintsAdded) AddVolumeConstraint(S,Flesh[I]);
        }
        bConstraintsAdded=true;Accumulator-=Profile->FixedStepSeconds;++CompletedSteps;
    }
    SolveMs=(FPlatformTime::Seconds()-SolveStart)*1000;MeasurePress(true);
    const double PublishStart=FPlatformTime::Seconds();
    if(Flesh[0]->OutputPositions.Num()<N) return;
    TArray<FVector> Current;Current.Append(Flesh[0]->OutputPositions.GetData(),N);
    if(!Profile->MeasureVolume(ShapedRest,Current,VolumeState)) { Status=TEXT("ERROR: Contact non-finite/inverted rest state; reset required");bEnabled=false;Release();return; }
    bool Inverted=false;for(const auto& V:VolumeState) Inverted|=V.InvertedTetrahedra>0;
    if(Inverted) { UE_LOG(LogTemp,Warning,TEXT("CONTACT_INVERSION step=%d teleport=%d minJ=%.8f/%.8f"),CompletedSteps,TeleportRevision,VolumeState[0].MinimumTetRatio,VolumeState[1].MinimumTetRatio);Status=TEXT("ERROR: Contact tetrahedron inversion; reset required");bEnabled=false;Release();return; }
    for(int32 I=0;I<N;++I) if(AnimatedRest[I].ContainsNaN())
    { Status=TEXT("ERROR: Contact baseline non-finite; reset required");bEnabled=false;Release();return; }
    const auto* Proxy=Flesh[0]->GetPhysicsProxy()->As<Chaos::Softs::FFleshThreadingProxy>();
    UDeformablePhysicsComponent::FDataMapValue RenderBuffer(new Chaos::Softs::FFleshThreadingProxy::FFleshOutputBuffer(*Proxy));
    auto& Render=RenderBuffer->As<Chaos::Softs::FFleshThreadingProxy::FFleshOutputBuffer>()->Dynamic.AddAttribute<FVector3f>(TEXT("Vertex"),FGeometryCollection::VerticesGroup);
    MaxContactResidualCm=0;
    for(int32 I=0;I<N;++I)
    {
        const FVector Delta=Flesh[0]->OutputPositions[I]-AnimatedRest[I];
        MaxContactResidualCm=FMath::Max(MaxContactResidualCm,Delta.Size());
        Render[I]=FVector3f(Profile->bResidualOnlySurface?Delta:Profile->Particles[I].Rest+Delta);
    }
    // CPU prediction through the SAME tetrahedral bindings and mask as the GPU.
    // This measures transfer coverage, not GPU readback or visual acceptance.
    BoundSurfaceResidualCm=FVector2D::ZeroVector;
    for(int32 V=0;V<Profile->SurfaceParents.Num();++V) if(Profile->SurfaceMask[V]>0)
    {
        const auto& Parents=Profile->SurfaceParents[V];FVector D=FVector::ZeroVector;int32 Side=INDEX_NONE;
        for(int32 J=0;J<4;++J) if(Profile->Particles.IsValidIndex(Parents[J]))
        {
            const int32 P=Parents[J];Side=Profile->Particles[P].Side;
            D+=(Current[P]-AnimatedRest[P])*Profile->SurfaceWeights[V][J];
        }
        if(Side>=0 && Side<2) BoundSurfaceResidualCm[Side]=FMath::Max(BoundSurfaceResidualCm[Side],D.Size()*Profile->SurfaceMask[V]);
    }
    // The exported component API updates the GPU manager; its UpdateGPUBuffers
    // implementation itself is deliberately not exported by the engine DLL.
    SurfaceProducer->UpdateFromSimulation(&RenderBuffer);Status=TEXT("Chaos contact active (one-way world simple collision + press spheres)");
    for(const auto& V:VolumeState) if(FMath::Abs(V.RelativeVolumeError)>Profile->VolumeErrorTolerance || V.MinimumTetRatio<Profile->MinimumSupportedTetRatio)
        Status=TEXT("Contact active: volume/compression tolerance exceeded; reduce penetration");
    if((bUseGPU || CVarContactGPU.GetValueOnGameThread()!=0)&&!bGPUActive)Status+=TEXT(" | GPU fallback: missing profile, unsupported geometry, or numerical safety latch");
    PublishMs=(FPlatformTime::Seconds()-PublishStart)*1000;
    if(bShowCage) for(const auto& T:Profile->BoundaryTriangles) for(int32 J=0;J<3;++J)
        DrawDebugLine(GetWorld(),Body->GetComponentTransform().TransformPosition(Current[T[J]]),Body->GetComponentTransform().TransformPosition(Current[T[(J+1)%3]]),FColor::Cyan,false,0,0,.3f);
}

FString UVamBreastContactComponent::Diagnostics() const
{
    FString Text=GetOwner()->GetPathName()+FString::Printf(TEXT(" | world %s | solvers %d | deformer %d\n"),*GetWorld()->GetName(),Solvers.Num(),Body && Body->HasMeshDeformer()?1:0)+Status+FString::Printf(TEXT("\nSteps %d | residual from final animated skin | no additional gravity"),CompletedSteps);
    if(Profile)Text+=FString::Printf(TEXT("\nContact schema %d particles %d tetrahedra %d"),Profile->SchemaVersion,Profile->Particles.Num(),Profile->Tetrahedra.Num());
    Text+=FString::Printf(TEXT("\nPress spheres %d | selected side %d | max contact residual %.4f cm"),ActivePressSphereCount,DebugSide,MaxContactResidualCm);
    if(!bGPUActive)Text+=FString::Printf(TEXT("\nBound surface prediction L %.4f R %.4f cm (CPU binding check)"),BoundSurfaceResidualCm.X,BoundSurfaceResidualCm.Y);
    for(int32 I=0;I<VolumeState.Num();++I) { const auto& V=VolumeState[I];Text+=FString::Printf(TEXT("\n%s cage V %.2f / %.2f cm3 error %.2f%% min J %.4f inverted %d"),*(Profile&&Profile->RegionNames.IsValidIndex(I)?Profile->RegionNames[I].ToString():FString::Printf(TEXT("Side %d"),I)),V.CurrentVolumeCm3,V.RestVolumeCm3,V.RelativeVolumeError*100,V.MinimumTetRatio,V.InvertedTetrahedra);Text+=FString::Printf(TEXT(" max J %.3f surface stretch %.3f..%.3f"),V.MaximumTetRatio,V.MinimumSurfaceStretch,V.MaximumSurfaceStretch);Text+=FString::Printf(TEXT(" worst edge rest %.6f cm extension max %.4f cm"),V.WorstStretchRestLengthCm,V.MaximumEdgeExtensionCm);Text+=FString::Printf(TEXT(" nipple RMS strain %.5f pairs %d"),V.NippleShapeRmsStrain,V.NippleShapePairCount); }
    if(bGPUActive)Text+=FString::Printf(TEXT("\nGPU async cage diagnostics; surface penetration not measured | component CPU %.3f ms"),TickMs);
    else Text+=FString::Printf(TEXT("\nCPU ms total %.3f init %.3f solve %.3f publish %.3f | movable %d inside %d -> %d penetration %.3f -> %.3f cm"),TickMs,InitMs,SolveMs,PublishMs,MovableParticles,InsideBefore,InsideAfter,PenetrationBefore,PenetrationAfter);
    Text+=FString::Printf(TEXT("\nSolver detail ms native %.3f volume %.3f post-contact %.3f"),NativeMaterialMs,VolumeConstraintMs,PostContactMs);
    return Text;
}


bool UVamBreastContactComponent::SuppliesContactForceFor(UPrimitiveComponent* Component) const
{
    if(!bEnabled || !bWorldCollision || !bForceFeedback || bGPURejected || !Component)return false;
    for(const auto& Pair:ReactionTargets)if(Pair.Value.Get()==Component)return true;
    // Reserve an imminent GPU contact before its first PostUpdateWork tick.
    // Otherwise the PrePhysics proxy can inject a one-frame duplicate impact.
    if(!(bUseGPU || CVarContactGPU.GetValueOnGameThread()!=0) || !Profile || !Profile->GPUSurfaceDeformer)return false;
    auto* Breast=Cast<UVamBreastSkeletalMeshComponent>(Body);if(!Breast)return false;
    FBox Bounds(ForceInit);for(const auto& R:Breast->RestSides)if(Body->GetComponentSpaceTransforms().IsValidIndex(R.AnchorBone)){
        Bounds+=ContactRegionBounds(Breast,R,.6);}
    if(Bounds.IsValid && Bounds.Intersect(Component->Bounds.GetBox())){TArray<FVamContactRigidSource> Sources;VamGatherRigidSources(Component,Breast,Bounds,Sources);
        if(!Sources.IsEmpty()){for(const auto& Source:Sources)if(!VamSupportsGPURigid(Source))return false;return true;}}
    return false;
}
