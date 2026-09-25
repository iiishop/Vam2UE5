#include "VamSoftTissueComponent.h"
#include "VamTissueBackend.h"
#include "VamCharacterComponent.h"
#include "VamRuntimeConfiguration.h"
#include "VamMotionComponent.h"
#include "VamPhysicsOutputComponent.h"
#include "ChaosFlesh/FleshAsset.h"
#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/FleshDynamicAsset.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "GeometryCollection/Facades/CollectionVertexBoneWeightsFacade.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ProceduralMeshComponent.h"

namespace
{
double TetVolume(const TArray<FVector>& P,const FIntVector4& T)
{return FVector::DotProduct(P[T[1]]-P[T[0]],FVector::CrossProduct(P[T[2]]-P[T[0]],P[T[3]]-P[T[0]]))/6;}
FVector SkinPoint(const FVector& P,const FVamTissueSkinWeight& W,const TArray<FTransform>& Ref,const TArray<FTransform>& Pose)
{
    FVector Result=FVector::ZeroVector;
    for(int32 J=0;J<W.Bones.Num();++J) Result+=Pose[W.Bones[J]].TransformPosition(Ref[W.Bones[J]].InverseTransformPosition(P))*W.Weights[J];
    return Result;
}
template<typename T> TArray<T> Slice(const TArray<T>& Values,int32 First,int32 Num)
{TArray<T> Result;Result.Append(Values.GetData()+First,Num);return Result;}
}
UVamSoftTissueComponent::UVamSoftTissueComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    Output.Instance=FGuid::NewGuid();
}
void UVamSoftTissueComponent::BeginPlay()
{
    Super::BeginPlay();Output.Instance=FGuid::NewGuid();
    Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    if(Character) Character->OnShapeChanged.AddUniqueDynamic(this,&UVamSoftTissueComponent::ShapeChanged);
    if(auto* Physics=GetOwner()->FindComponentByClass<UVamPhysicsOutputComponent>()) AddTickPrerequisiteComponent(Physics);
}
void UVamSoftTissueComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    CharacterUnloading();if(Character) Character->OnShapeChanged.RemoveDynamic(this,&UVamSoftTissueComponent::ShapeChanged);
    ExplicitColliders.Reset();Character=nullptr;Super::EndPlay(Reason);
}
void UVamSoftTissueComponent::ReleaseRuntime()
{
    Output.Valid=false;Output.SurfaceResource=nullptr;Output.CollisionVertices.Reset();Output.CollisionTriangles.Reset();
    if(Solver) Solver->SetSimulationTicking(false);
    if(Flesh) {Flesh->DisableSimulation();Flesh->DestroyComponent();Flesh=nullptr;}
    if(Collisions) {Collisions->DisableSimulation();Collisions->DestroyComponent();Collisions=nullptr;}
    if(Solver) {Solver->ResetSimulationProxy();Solver->DestroyComponent();Solver=nullptr;}
    if(Surface) {Surface->DestroyComponent();Surface=nullptr;if(Character && Character->Body) Character->Body->SetVisibility(OriginalVisible);}
    InstanceRest=nullptr;Warmup=0;SolverTime=0;
}
void UVamSoftTissueComponent::CharacterUnloading()
{
    ++LoadTicket;if(Pending) {Pending->CancelHandle();Pending.Reset();}
    ReleaseRuntime();Profile=nullptr;State=EVamSoftTissueState::Disabled;Generation=MAX_uint64;
}
void UVamSoftTissueComponent::Fail(const FString& Message)
{ReleaseRuntime();State=EVamSoftTissueState::Error;LastError=Message;UE_LOG(LogTemp,Error,TEXT("SoftTissue %s: %s"),*GetPathName(),*Message);}
void UVamSoftTissueComponent::SetSoftTissueQuality(EVamSoftTissueQuality Value)
{
    QualityOverride=true;Quality=Value;if(Value!=EVamSoftTissueQuality::Off) LastEnabledQuality=Value;
    ResetSoftTissue();
}
void UVamSoftTissueComponent::SetSoftTissueEnabled(bool Enabled)
{SetSoftTissueQuality(Enabled?LastEnabledQuality:EVamSoftTissueQuality::Off);}
void UVamSoftTissueComponent::ResetSoftTissue()
{
    ReleaseRuntime();LastError.Reset();State=Quality==EVamSoftTissueQuality::Off?EVamSoftTissueState::Disabled:EVamSoftTissueState::Resetting;
    if(!Profile && !Pending && Character && Character->Body && Quality!=EVamSoftTissueQuality::Off) LoadProfile();
}
void UVamSoftTissueComponent::ShapeChanged(const FVamShapeChange& Change)
{Output.Valid=false;if(Profile && Quality!=EVamSoftTissueQuality::Off) ResetSoftTissue();}
void UVamSoftTissueComponent::RegisterInteractionCollider(UPrimitiveComponent* Collider)
{if(IsValid(Collider)) ExplicitColliders.AddUnique(Collider);}
void UVamSoftTissueComponent::UnregisterInteractionCollider(UPrimitiveComponent* Collider)
{ExplicitColliders.Remove(Collider);}
FVamBodySurfaceOutput UVamSoftTissueComponent::GetBodySurfaceOutput() const
{
    auto Result=Output;
    Result.Valid=Result.Valid && State==EVamSoftTissueState::Ready && Character && Character->Body &&
        Character->GetLoadGeneration()==Generation && Character->GetShapeState().Revision==ShapeRevision &&
        Result.ToWorld.Equals(Character->GetComponentTransform()) &&
        GetWorld() && Result.PublishedWorldTimeSeconds>=GetWorld()->GetTimeSeconds()-.1;
    if(const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>()) Result.Valid=Result.Valid && Result.TeleportRevision==Motion->GetClock().TeleportRevision;
    return Result;
}
void UVamSoftTissueComponent::LoadProfile()
{
    const auto* Config=Character?Character->RuntimeConfiguration.Get():nullptr;
    if(!Config || Config->SoftTissueProfile.IsNull()) {State=EVamSoftTissueState::Disabled;return;}
    State=EVamSoftTissueState::Loading;const auto Path=Config->SoftTissueProfile.ToSoftObjectPath();const uint64 Ticket=++LoadTicket;
    Pending=UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,FStreamableDelegate::CreateWeakLambda(this,[this,Path,Ticket]()
    {
        if(Ticket!=LoadTicket) return;
        Profile=Cast<UVamSoftTissueProfile>(Path.ResolveObject());Pending.Reset();
        if(!Profile) {Fail(TEXT("SoftTissueProfile failed to load; character remains available"));return;}
        State=Quality==EVamSoftTissueQuality::Off?EVamSoftTissueState::Disabled:EVamSoftTissueState::Building;
    }));
}
bool UVamSoftTissueComponent::BuildRuntime()
{
    auto* Body=Character->Body.Get();const auto* Config=Character->RuntimeConfiguration.Get();
    if(!Profile || !Config || Profile->Body.Get()!=Body->GetSkeletalMeshAsset() || Profile->BindSignature!=Config->BindSignature ||
        Profile->MorphSetLockDigest!=Config->MorphSetLockDigest || Profile->BackendVersion!=Config->SoftTissueBackendVersion)
    {Fail(TEXT("Soft tissue source/bind/MorphSet/backend identity mismatch"));return false;}
    const int32 N=Profile->Positions.Num(),P=Profile->RestParticles.Num();
    if(N==0 || P==0 || Profile->Skin.Num()!=N || Profile->SurfaceMask.Num()!=N || Profile->SurfaceParents.Num()!=N ||
       Profile->SurfaceWeights.Num()!=N || Profile->ParticleSkin.Num()!=P || Profile->Supports.Num()!=P ||
       Profile->ParticleRegions.Num()!=P || Profile->ShapeSourceVertices.Num()!=P || Profile->Tetrahedra.IsEmpty())
    {Fail(TEXT("Incomplete cooked soft tissue mapping"));return false;}
    if(Profile->SurfaceRegions.Num()!=N || Profile->NormalParents.Num()!=N || Profile->UV0.Num()!=N || Profile->UV1.Num()!=N || Profile->UV2.Num()!=N || Profile->UV3.Num()!=N || Profile->Colors.Num()!=N ||
       !FMath::IsFinite(Profile->MaximumStepSeconds) || Profile->MaximumStepSeconds<=0 || Profile->MaximumStepSeconds>.1f)
    {Fail(TEXT("Invalid cooked surface attributes or solver timestep"));return false;}
    for(int32 Parent:Profile->NormalParents) if(!Profile->Positions.IsValidIndex(Parent)) {Fail(TEXT("Invalid cooked normal group"));return false;}
    for(const FName Region:Config->EnabledRegions) if(!Profile->Regions.ContainsByPredicate([Region](const auto& R){return R.Name==Region;}))
    {Fail(TEXT("Enabled region is absent from profile"));return false;}
    for(int32 V=0;V<N;++V) if(Profile->SurfaceMask[V]>0)
    {
        if(!Profile->Regions.IsValidIndex(Profile->SurfaceRegions[V])) {Fail(TEXT("Invalid surface region index"));return false;}
        for(int32 J=0;J<4;++J) if(!Profile->RestParticles.IsValidIndex(Profile->SurfaceParents[V][J])) {Fail(TEXT("Invalid surface parent index"));return false;}
    }
    for(const auto& S:Profile->Sections)
    {
        if(S.FirstVertex<0 || S.NumVertices<=0 || S.FirstVertex+S.NumVertices>N) {Fail(TEXT("Invalid surface section bounds"));return false;}
        for(int32 I:S.Triangles) if(I<0 || I>=S.NumVertices) {Fail(TEXT("Invalid surface triangle"));return false;}
    }
    ReferenceCS=Character->GetShapeReferencePose();const auto& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    if(ReferenceCS.Num()!=Ref.GetNum()) {Fail(TEXT("Soft tissue reference pose size mismatch"));return false;}
    for(int32 I=0;I<ReferenceCS.Num();++I) if(Ref.GetParentIndex(I)>=0) ReferenceCS[I]*=ReferenceCS[Ref.GetParentIndex(I)];
    for(const auto* Weights:{&Profile->Skin,&Profile->ParticleSkin}) for(const auto& W:*Weights)
    {
        float Sum=0;if(W.Bones.Num()!=W.Weights.Num()) {Fail(TEXT("Invalid skin weight mapping"));return false;}
        for(int32 J=0;J<W.Bones.Num();++J) {if(!ReferenceCS.IsValidIndex(W.Bones[J])) {Fail(TEXT("Invalid attachment bone"));return false;} Sum+=W.Weights[J];}
        if(!FMath::IsNearlyEqual(Sum,1.f,.001f)) {Fail(TEXT("Unnormalized attachment weights"));return false;}
    }
    TArray<FVector> Shaped=Profile->Positions;const auto Values=Character->GetShapeState().Values;
    for(const auto& Morph:Profile->Morphs) if(!Profile->ExpressionMorphs.Contains(Morph.Parameter))
    {
        const float* Value=Values.Find(Morph.Parameter);const float Weight=(Value?*Value:Morph.Baseline)-Morph.Baseline;
        for(const auto& D:Morph.Deltas) {if(!Shaped.IsValidIndex(D.Point)) {Fail(TEXT("Invalid morph source index"));return false;} Shaped[D.Point]+=D.Delta*Weight;}
    }
    ShapedParticles=Profile->RestParticles;
    for(int32 I=0;I<P;++I)
    {
        const int32 V=Profile->ShapeSourceVertices[I];
        if(!Shaped.IsValidIndex(V) || !Profile->Regions.IsValidIndex(Profile->ParticleRegions[I])) {Fail(TEXT("Invalid proxy shape mapping"));return false;}
        ShapedParticles[I]+=Shaped[V]-Profile->Positions[V];
    }
    for(const auto& T:Profile->Tetrahedra)
    {
        for(int32 J=0;J<4;++J) if(!ShapedParticles.IsValidIndex(T[J])) {Fail(TEXT("Invalid tetrahedron index"));return false;}
        const double Ratio=TetVolume(ShapedParticles,T)/TetVolume(Profile->RestParticles,T);
        if(!FMath::IsFinite(Ratio) || Ratio<Profile->MinimumTetVolumeRatio || Ratio>Profile->MaximumTetVolumeRatio)
        {Fail(TEXT("Shape outside cooked volume validity range; soft tissue disabled until a valid shape/reset"));return false;}
    }
    TUniquePtr<FFleshCollection> Collection(FFleshCollection::NewFleshCollection(ShapedParticles,Profile->Tetrahedra,false));
    Collection->Mass.Fill(0);
    for(const auto& T:Profile->Tetrahedra)
    {
        const auto& Region=Profile->Regions[Profile->ParticleRegions[T[0]]];
        const float Mass=TetVolume(ShapedParticles,T)*Region.DensityKgPerCm3/4;
        for(int32 J=0;J<4;++J) Collection->Mass[T[J]]+=Mass;
    }
    auto& Stiffness=Collection->AddAttribute<float>(TEXT("Stiffness"),FGeometryCollection::VerticesGroup);
    auto& Damping=Collection->AddAttribute<float>(TEXT("Damping"),FGeometryCollection::VerticesGroup);
    auto& Incompressibility=Collection->AddAttribute<float>(TEXT("Incompressibility"),FGeometryCollection::VerticesGroup);
    Collection->AddAttribute<float>(TEXT("Inflation"),FGeometryCollection::VerticesGroup).Fill(1);
    GeometryCollection::Facades::FVertexBoneWeightsFacade Attachments(*Collection,false);Attachments.DefineSchema();
    for(int32 I=0;I<P;++I)
    {
        const auto& Region=Profile->Regions[Profile->ParticleRegions[I]];
        const bool Enabled=Config->EnabledRegions.Contains(Region.Name);
        const bool Fixed=Profile->Supports[I] || !Enabled;
        if(Fixed) Collection->Mass[I]=0;
        Attachments.ModifyBoneWeight(I,Profile->ParticleSkin[I].Bones,Profile->ParticleSkin[I].Weights);Attachments.SetVertexKinematic(I,Fixed);
        Stiffness[I]=Region.Stiffness;Damping[I]=Region.Damping;Incompressibility[I]=Region.Incompressibility;
    }
    InstanceRest=NewObject<UFleshAsset>(this,NAME_None,RF_Transient);InstanceRest->SetFleshCollection(MoveTemp(Collection));
    InstanceRest->SkeletalMesh=Body->GetSkeletalMeshAsset();InstanceRest->TargetDeformationSkeleton=Body->GetSkeletalMeshAsset();
    Solver=NewObject<UDeformableSolverComponent>(GetOwner(),NAME_None,RF_Transient);Solver->SetupAttachment(Character);
    Solver->SolverTiming.bDoThreadedAdvance=false;Solver->SolverTiming.NumSubSteps=Quality==EVamSoftTissueQuality::High?4:2;
    Solver->SolverTiming.NumSolverIterations=Quality==EVamSoftTissueQuality::High?10:5;
    Solver->SolverForces.bEnableGravity=Profile->bGravity;Solver->SolverCollisions.bUseFloor=false;
    Solver->RegisterComponent();Solver->SetComponentTickEnabled(false);
    Flesh=NewObject<UVamTissueFleshComponent>(GetOwner(),NAME_None,RF_Transient);Flesh->SetupAttachment(Character);Flesh->SetVisibility(false);
    Flesh->InputBody=Body;Flesh->ReferenceCS=ReferenceCS;Flesh->BodyForces.bApplyGravity=Profile->bGravity;
    Flesh->SetRestCollection(InstanceRest);Flesh->RegisterComponent();Flesh->EnableSimulation(Solver);
    Collisions=NewObject<UVamTissueCollisionComponent>(GetOwner(),NAME_None,RF_Transient);Collisions->SetupAttachment(Character);Collisions->RegisterComponent();Collisions->EnableSimulation(Solver);
    Surface=NewObject<UProceduralMeshComponent>(GetOwner(),NAME_None,RF_Transient);Surface->SetupAttachment(Character);Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Surface->RegisterComponent();
    OriginalVisible=Body->IsVisible();ShapeRevision=Character->GetShapeState().Revision;
    Output.ShapeRevision=ShapeRevision;Output.CharacterGeneration=Generation;Output.SurfaceResource=Surface;
    Output.CollisionTriangles=Profile->BoundaryTriangles;Output.Valid=false;Warmup=0;State=EVamSoftTissueState::WarmingUp;
    UpdateSurface();if(State==EVamSoftTissueState::Error) return false;Body->SetVisibility(false);return true;
}
void UVamSoftTissueComponent::UpdateSurface()
{
    auto* Body=Character->Body.Get();const auto& Pose=Body->GetComponentSpaceTransforms();
    if(Pose.Num()!=ReferenceCS.Num()) {Fail(TEXT("Final pose/reference size mismatch"));return;}
    Vertices=Profile->Positions;
    for(const auto& Morph:Profile->Morphs)
    {
        const float Weight=Body->GetMorphTarget(Morph.Parameter);
        for(const auto& D:Morph.Deltas) Vertices[D.Point]+=D.Delta*Weight;
    }
    for(int32 I=0;I<Vertices.Num();++I) Vertices[I]=SkinPoint(Vertices[I],Profile->Skin[I],ReferenceCS,Pose);
    SkinnedParticles.SetNum(ShapedParticles.Num());
    for(int32 I=0;I<ShapedParticles.Num();++I) SkinnedParticles[I]=SkinPoint(ShapedParticles[I],Profile->ParticleSkin[I],ReferenceCS,Pose);
    const auto* Positions=Flesh->GetDynamicCollection()?Flesh->GetDynamicCollection()->FindPositions():nullptr;
    if(Positions && Positions->Num()==ShapedParticles.Num() && Flesh->CompletedOutputs>0)
    {
        Output.CollisionVertices.Reset(Positions->Num());
        for(int32 I=0;I<Positions->Num();++I) {if(FVector((*Positions)[I]).ContainsNaN()) {Fail(TEXT("Non-finite Flesh output"));return;} Output.CollisionVertices.Add(FVector((*Positions)[I]));}
        const auto* Config=Character->RuntimeConfiguration.Get();
        for(int32 V=0;V<Vertices.Num();++V) if(Profile->SurfaceMask[V]>0)
        {
            const int32 R=Profile->SurfaceRegions[V];if(!Config->EnabledRegions.Contains(Profile->Regions[R].Name)) continue;
            const auto& T=Profile->SurfaceParents[V];const auto& W=Profile->SurfaceWeights[V];FVector Delta=FVector::ZeroVector;
            for(int32 J=0;J<4;++J) Delta+=(Output.CollisionVertices[T[J]]-SkinnedParticles[T[J]])*W[J];
            Vertices[V]+=Delta*Profile->SurfaceMask[V];
        }
    }
    // Linear triangle accumulation over cooked smoothing groups. The engine's
    // convenience helper searches all coincident vertices for every triangle.
    TArray<FVector> NormalSums,TangentX,TangentY,AllNormals;TArray<FProcMeshTangent> AllTangents;
    NormalSums.Init(FVector::ZeroVector,Vertices.Num());TangentX=NormalSums;TangentY=NormalSums;
    for(const auto& Section:Profile->Sections) for(int32 I=0;I+2<Section.Triangles.Num();I+=3)
    {
        const int32 A=Section.FirstVertex+Section.Triangles[I],B=Section.FirstVertex+Section.Triangles[I+1],C=Section.FirstVertex+Section.Triangles[I+2];
        const FVector E1=Vertices[B]-Vertices[A],E2=Vertices[C]-Vertices[A];
        const FVector Normal=FVector::CrossProduct(E2,E1).GetSafeNormal();
        const FVector2D U1=Profile->UV0[B]-Profile->UV0[A],U2=Profile->UV0[C]-Profile->UV0[A];
        const double D=U1.X*U2.Y-U1.Y*U2.X;
        const FVector X=FMath::Abs(D)>UE_SMALL_NUMBER?((E1*U2.Y-E2*U1.Y)/D).GetSafeNormal():E1.GetSafeNormal();
        const FVector Y=FMath::Abs(D)>UE_SMALL_NUMBER?((E2*U1.X-E1*U2.X)/D).GetSafeNormal():FVector::CrossProduct(Normal,X);
        for(int32 V:{A,B,C}) {NormalSums[Profile->NormalParents[V]]+=Normal;TangentX[V]+=X;TangentY[V]+=Y;}
    }
    AllNormals.SetNum(Vertices.Num());AllTangents.SetNum(Vertices.Num());
    for(int32 V=0;V<Vertices.Num();++V)
    {
        const FVector N=NormalSums[Profile->NormalParents[V]].GetSafeNormal();
        const FVector X=(TangentX[V]-N*FVector::DotProduct(N,TangentX[V])).GetSafeNormal();
        AllNormals[V]=N;AllTangents[V]=FProcMeshTangent(X,FVector::DotProduct(FVector::CrossProduct(N,X),TangentY[V])<0);
    }
    for(int32 S=0;S<Profile->Sections.Num();++S)
    {
        const auto& Section=Profile->Sections[S];const int32 F=Section.FirstVertex,N=Section.NumVertices;
        auto P=Slice(Vertices,F,N);auto UV=Slice(Profile->UV0,F,N);auto Normals=Slice(AllNormals,F,N);auto Tangents=Slice(AllTangents,F,N);
        if(!Surface->GetProcMeshSection(S)) Surface->CreateMeshSection_LinearColor(S,P,Section.Triangles,Normals,UV,Slice(Profile->UV1,F,N),Slice(Profile->UV2,F,N),Slice(Profile->UV3,F,N),Slice(Profile->Colors,F,N),Tangents,false,false);
        else Surface->UpdateMeshSection_LinearColor(S,P,Normals,UV,Slice(Profile->UV1,F,N),Slice(Profile->UV2,F,N),Slice(Profile->UV3,F,N),Slice(Profile->Colors,F,N),Tangents,false);
        Surface->SetMaterial(S,Body->GetMaterial(Section.MaterialSlot));
    }
    Output.ToWorld=Character->GetComponentTransform();Output.PublishedWorldTimeSeconds=GetWorld()->GetTimeSeconds();
    if(auto* Physics=GetOwner()->FindComponentByClass<UVamPhysicsOutputComponent>()) Output.PoseRevision=Physics->GetCollisionOutput().FinalPoseAnimationRevision;
}
void UVamSoftTissueComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(!Character || !Character->Body) return;
    if(Generation!=Character->GetLoadGeneration())
    {
        CharacterUnloading();Generation=Character->GetLoadGeneration();
        if(!QualityOverride) if(const auto* Config=Character->RuntimeConfiguration.Get()) Quality=Config->SoftTissueQuality;
        if(Quality!=EVamSoftTissueQuality::Off) {LastEnabledQuality=Quality;LoadProfile();}return;
    }
    if(Quality==EVamSoftTissueQuality::Off || State==EVamSoftTissueState::Loading || State==EVamSoftTissueState::Error) return;
    if(auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>())
        if(TeleportRevision!=Motion->GetClock().TeleportRevision) {TeleportRevision=Motion->GetClock().TeleportRevision;Output.TeleportRevision=TeleportRevision;ResetSoftTissue();}
    if(Profile && (!Solver || ShapeRevision!=Character->GetShapeState().Revision)) {ReleaseRuntime();if(!BuildRuntime()) return;}
    if(!Solver || DeltaTime<=0) return;
    UpdateCollisions();const int64 Before=Flesh->CompletedOutputs;const float Step=FMath::Min(DeltaTime,Profile->MaximumStepSeconds);
    // Advance this private solver once, after the final animated/physical pose. Never tick the world manually.
    Solver->WriteToSimulation(Step,false);Solver->Simulate(Step);Solver->ReadFromSimulation(Step,false);
    if(Flesh->CompletedOutputs>Before)
    {
        SolverTime+=Step;++Warmup;++Output.SolverRevision;Output.SolverTimeSeconds=SolverTime;
        UpdateSurface();if(State==EVamSoftTissueState::Error) return;
        if(Warmup>=FMath::Max(1,Profile->WarmupFrames)) {State=EVamSoftTissueState::Ready;Output.Valid=true;}
    }
}

void UVamSoftTissueComponent::UpdateCollisions()
{
    Collisions->Shapes.Reset();UnsupportedColliders=0;
    TSet<UPrimitiveComponent*> Sources;
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    Objects.AddObjectTypesToQuery(ECC_PhysicsBody);Objects.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(VamSoftTissueEnvironment),false,GetOwner());
    TArray<FOverlapResult> Overlaps;
    const auto Bounds=Character->Body->Bounds;
    GetWorld()->OverlapMultiByObjectType(Overlaps,Bounds.Origin,FQuat::Identity,Objects,
        FCollisionShape::MakeSphere(Bounds.SphereRadius+Profile->CollisionQueryMarginCm),Query);
    for(const auto& Hit:Overlaps) if(auto* Component=Hit.GetComponent()) Sources.Add(Component);
    Sources.Add(Character->Body);
    ExplicitColliders.RemoveAll([](const auto& P){return !P.IsValid();});
    for(const auto& Component:ExplicitColliders) Sources.Add(Component.Get());
    auto AddGeometry=[this](UPrimitiveComponent* Owner,const UBodySetup* Setup,const FTransform& Base,int32& Index)
    {
        using namespace Chaos::Softs;
        if(!Setup) {++UnsupportedColliders;return;}
        const FVector Scale=Base.GetScale3D().GetAbs();FTransform NoScale=Base;NoScale.RemoveScaling();
        const int32 Before=Collisions->Shapes.Num();
        auto Add=[&](ERigidCollisionShapeType Type,const FVector& Center,const FQuat& Rotation,const FVector& Size,const TArray<Chaos::FConvex::FVec3Type>& Convex)
        {
            if(Index>127 || Scale.GetMin()<UE_SMALL_NUMBER) {++UnsupportedColliders;return;}
            FVamTissueCollider Shape;Shape.Key={Owner,Type,int8(Index++)};Shape.Transform=FTransform(Rotation,Center*Scale)*NoScale;Shape.Size=Size;Shape.Convex=Convex;
            Collisions->Shapes.Add(MoveTemp(Shape));
        };
        for(const auto& E:Setup->AggGeom.SphereElems) Add(ERigidCollisionShapeType::Sphere,E.Center,FQuat::Identity,FVector(E.Radius*Scale.GetMax(),0,0),{});
        for(const auto& E:Setup->AggGeom.BoxElems) Add(ERigidCollisionShapeType::Box,E.Center,E.Rotation.Quaternion(),FVector(E.X,E.Y,E.Z)*Scale*.5,{});
        for(const auto& E:Setup->AggGeom.SphylElems) Add(ERigidCollisionShapeType::Sphyl,E.Center,E.Rotation.Quaternion(),FVector(E.Radius*FMath::Max(Scale.X,Scale.Y),0,E.Length*Scale.Z*.5),{});
        for(const auto& E:Setup->AggGeom.ConvexElems)
        {
            if(E.VertexData.IsEmpty()) {++UnsupportedColliders;continue;}
            TArray<Chaos::FConvex::FVec3Type> Points;for(const auto& V:E.VertexData) Points.Add(Chaos::FConvex::FVec3Type(V*Scale));
            Add(ERigidCollisionShapeType::Convex,E.GetTransform().GetTranslation(),E.GetTransform().GetRotation(),Scale,Points);
        }
        if(Collisions->Shapes.Num()==Before) ++UnsupportedColliders;
    };
    for(auto* Source:Sources)
    {
        if(!IsValid(Source) || Source==Surface || Source==Flesh || Source==Collisions) continue;
        int32 Index=0;
        if(auto* Mesh=Cast<USkeletalMeshComponent>(Source))
        {
            if(const auto* Physics=Mesh->GetPhysicsAsset()) for(const USkeletalBodySetup* Body:Physics->SkeletalBodySetups)
            {
                if(!Body) continue;
                // The mapped tissue bone is not simultaneously driven by its own rigid envelope.
                if(Mesh==Character->Body && Profile->Regions.ContainsByPredicate([&](const auto& R){return R.Bone==Body->BoneName;})) continue;
                AddGeometry(Source,Body,Mesh->GetSocketTransform(Body->BoneName),Index);
            }
        }
        else AddGeometry(Source,Source->GetBodySetup(),Source->GetComponentTransform(),Index);
    }
}
