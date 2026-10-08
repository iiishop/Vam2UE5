#include "VamBreastContactComponent.h"
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
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UVamBreastContactComponent::UVamBreastContactComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UVamBreastContactComponent::Release()
{
    if(Body && SurfaceProducer) { if(bPreviousDeformerOverride) Body->SetMeshDeformer(PreviousDeformer);else Body->UnsetMeshDeformer(); }
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
void UVamBreastContactComponent::ResetContact() { Release();AppliedDebugDepth=0;ShapeRevision=INDEX_NONE;bEnabled=true;Status=TEXT("Contact reset requested"); }
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
        Solver->SolverTiming.NumSolverIterations=Profile->SolverIterations;Solver->SolverEvolution.SolverQuasistatics.bDoQuasistatics=true;
        Solver->SolverConstraints.GaussSeidelConstraints.bUseGaussSeidelConstraints=true;
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
    if(E->ConstraintRules().Num()!=1){Status=TEXT("ERROR: Unsupported native contact constraint layout");bEnabled=false;return;}
    const int32 Rule=0;
    auto NativeRule=MoveTemp(E->ConstraintRules()[Rule]);
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
        SkinBending->ApplyProperties(Profile->FixedStepSeconds,Profile->SolverIterations);SkinBending->Init(E->Particles());
    }
    TArray<TArray<int32>> IncidentTets;IncidentTets.SetNum(N);
    for(int32 K=0;K<Profile->Tetrahedra.Num();++K)for(int32 J=0;J<4;++J)IncidentTets[Profile->Tetrahedra[K][J]].Add(K);
    TArray<FVector> Scratch;Scratch.Init(FVector::ZeroVector,N);
    E->ConstraintRules()[Rule]=[this,Start,N,Volumes,TetRest,Scratch=MoveTemp(Scratch),NativeRule=MoveTemp(NativeRule)](Chaos::Softs::FSolverParticles& X,const Chaos::Softs::FSolverReal Dt) mutable
    {
        NativeRule(X,Dt);
        const FVector ComponentScale=Body->GetComponentScale();
        const double VolumeScale=FMath::Abs(ComponentScale.X*ComponentScale.Y*ComponentScale.Z);
        if(Profile->SchemaVersion>=2)
        {
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
                const double Limit=FMath::Pow(TetRest[K]*VolumeScale,1./3.)*.25;if(MaxMove>Limit)Lambda*=Limit/MaxMove;
                for(int32 J=0;J<4;++J)X.P(Start+T[J])+=Chaos::Softs::FSolverVec3(G[J]*(X.InvM(Start+T[J])*Lambda));
            }
        }
        for(int32 Side=0;Side<2;++Side)
        {
            auto& G=Scratch;for(auto& V:G)V=FVector::ZeroVector;double Volume=0,RestVolume=Volumes[Side];
            for(const auto& T:Profile->Tetrahedra) if(Profile->Particles[T[0]].Side==Side)
            {
                FVector P[4];for(int32 J=0;J<4;++J) P[J]=FVector(X.P(Start+T[J]));
                const FVector A=P[1]-P[0],B=P[2]-P[0],C=P[3]-P[0];
                const FVector G1=FVector::CrossProduct(B,C)/6,G2=FVector::CrossProduct(C,A)/6,G3=FVector::CrossProduct(A,B)/6;
                G[T[1]]+=G1;G[T[2]]+=G2;G[T[3]]+=G3;G[T[0]]-=G1+G2+G3;
                Volume+=FVector::DotProduct(A,FVector::CrossProduct(B,C))/6;
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
        E->PostCollisionConstraintRules()[Post]=[this,Start,Edges,E,TetRest,SkinBending,IncidentTets](Chaos::Softs::FSolverParticles& X,const Chaos::Softs::FSolverReal Dt)
        {
            // Couple membrane strain to the ACTUAL active Chaos rigid geometry.
            // This remains inside each solver iteration, not a render-mesh correction.
            TArray<int32> Active;
            E->CollisionParticlesActiveView().RangeFor([&](Chaos::Softs::FSolverCollisionParticles& C,int32 Offset,int32 End){for(int32 I=Offset;I<End;++I)if(C.GetGeometry(I))Active.Add(I);},true);
            auto Contact=[&](int32 Particle,int32 Collider,FVector& Normal)->double
            {
                const uint32 Group=E->ParticleGroupIds()[Particle],Other=E->CollisionParticleGroupIds()[Collider];
                if(Other!=uint32(INDEX_NONE) && Group!=Other)return DBL_MAX;
                const auto& C=E->CollisionParticles();
                const Chaos::Softs::FSolverRigidTransform3 Frame(C.GetX(Collider),C.GetR(Collider));
                Chaos::FVec3 LocalNormal;
                const double Phi=C.GetGeometry(Collider)->PhiWithNormal(Chaos::FVec3(Frame.InverseTransformPosition(X.P(Particle))),LocalNormal)-E->GetCollisionThickness(Group);
                Normal=FVector(Frame.TransformVector(Chaos::Softs::FSolverVec3(LocalNormal)));return Phi;
            };
            auto Project=[&](int32 I){if(X.InvM(I)<=0)return;for(int32 C:Active){FVector Normal;const double Phi=Contact(I,C,Normal);if(Phi<0)X.P(I)-=Chaos::Softs::FSolverVec3(Normal*Phi);}};
            if(SkinBending)
            {
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
            auto ProjectFaces=[&]()
            {
                for(const auto& Face:Profile->BoundaryTriangles)
                {
                    const int32 Id[3]={Start+Face.X,Start+Face.Y,Start+Face.Z};
                    if(X.InvM(Id[0])+X.InvM(Id[1])+X.InvM(Id[2])<=0)continue;
                    for(int32 Collider:Active)
                    {
                        const uint32 Group=E->ParticleGroupIds()[Id[0]],Other=E->CollisionParticleGroupIds()[Collider];
                        if(Other!=uint32(INDEX_NONE) && Group!=Other)continue;
                        const auto& C=E->CollisionParticles();const auto& Geometry=C.GetGeometry(Collider);
                        const Chaos::Softs::FSolverRigidTransform3 Frame(C.GetX(Collider),C.GetR(Collider));
                        FVector P[3];for(int32 J=0;J<3;++J)P[J]=FVector(X.P(Id[J]));
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
                        for(int32 J=0;J<3;++J)for(int32 K:IncidentTets[Face[J]])Affected.AddUnique(K);
                        bool Safe=false;
                        for(int32 Attempt=0;Attempt<9;++Attempt)
                        {
                            Safe=true;
                            for(int32 K:Affected){const auto& T=Profile->Tetrahedra[K];FVector Before[4],After[4];
                                for(int32 J=0;J<4;++J){Before[J]=After[J]=FVector(X.P(Start+T[J]));for(int32 Node=0;Node<3;++Node)if(T[J]==Face[Node])After[J]+=Normal*(Lambda*Relax*X.InvM(Id[Node])*W[Node]);}
                                const double BV=FVector::DotProduct(Before[1]-Before[0],FVector::CrossProduct(Before[2]-Before[0],Before[3]-Before[0]))/6;
                                const double AV=FVector::DotProduct(After[1]-After[0],FVector::CrossProduct(After[2]-After[0],After[3]-After[0]))/6;
                                const double Floor=BV>0?FMath::Min(BV*.5,TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z)*.1):BV;
                                if(!FMath::IsFinite(AV) || AV<Floor){Safe=false;break;}}
                            if(Safe)break;Relax*=.5;
                        }
                        if(!Safe)continue;
                        for(int32 J=0;J<3;++J)X.P(Id[J])+=Chaos::Softs::FSolverVec3(Normal*(Lambda*Relax*X.InvM(Id[J])*W[J]));
                    }
                }
            };
            for(int32 Sweep=0;Sweep<4;++Sweep)
            {
                if(Profile->SchemaVersion>=4 && Sweep==0)ProjectFaces();
                for(int32 Index=0;Index<Edges.Num();++Index)
                {
                    const auto& Edge=Edges[Sweep%2==0?Index:Edges.Num()-1-Index];const int32 A=Start+Edge.X,B=Start+Edge.Y;
                    const FVector D=FVector(X.P(A)-X.P(B));const double L=D.Size();if(L<1.e-8)continue;
                    const double Rest=((ShapedRest[Edge.X]-ShapedRest[Edge.Y])*Scale).Size();
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
                    Project(A);Project(B);
                }
                // Collision and membrane projection can compress a cell AFTER
                // the native solve. Restore its barrier in contact-tangent space,
                // so neighboring tissue takes the load instead of penetrating.
                for(int32 K=0;K<Profile->Tetrahedra.Num();++K){const auto& T=Profile->Tetrahedra[K];FVector P[4],G[4];
                    for(int32 J=0;J<4;++J)P[J]=FVector(X.P(Start+T[J]));
                    const FVector A=P[1]-P[0],B=P[2]-P[0],C=P[3]-P[0];
                    const double V=FVector::DotProduct(A,FVector::CrossProduct(B,C))/6;
                    const double RestVolume=TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z);
                    const double Barrier=FMath::Max(0.,RestVolume*Profile->CompressionBarrierRatio-V);
                    const double Correction=Sweep==3?FMath::Max(Barrier,Profile->CompressionCorrection(RestVolume,V)*.25):Barrier;
                    // Ignore sub-tolerance bulk corrections (0.01% cell volume);
                    // do not run contact queries for essentially undeformed cells.
                    if(Correction<=FMath::Max(1.e-10,RestVolume*1.e-4))continue;
                    G[1]=FVector::CrossProduct(B,C)/6;G[2]=FVector::CrossProduct(C,A)/6;G[3]=FVector::CrossProduct(A,B)/6;G[0]=-G[1]-G[2]-G[3];
                    double Den=0,MaxMove=0;
                    for(int32 J=0;J<4;++J){for(int32 Collider:Active){FVector Normal;if(Contact(Start+T[J],Collider,Normal)>.01)continue;const double Inward=FVector::DotProduct(G[J],Normal);if(Inward<0)G[J]-=Normal*Inward;}
                        Den+=X.InvM(Start+T[J])*G[J].SizeSquared();}
                    if(Den<=1.e-12)continue;double Lambda=Correction/Den*Profile->ConstraintRelaxation;
                    for(int32 J=0;J<4;++J)MaxMove=FMath::Max(MaxMove,Lambda*X.InvM(Start+T[J])*G[J].Size());
                    const double Limit=FMath::Pow(TetRest[K]*FMath::Abs(Scale.X*Scale.Y*Scale.Z),1./3.)*.25;if(MaxMove>Limit)Lambda*=Limit/MaxMove;
                    for(int32 J=0;J<4;++J){X.P(Start+T[J])+=Chaos::Softs::FSolverVec3(G[J]*(Lambda*X.InvM(Start+T[J])));Project(Start+T[J]);}
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
    const double TickStart=FPlatformTime::Seconds();SolveMs=PublishMs=InitMs=0;
    ON_SCOPE_EXIT { TickMs=(FPlatformTime::Seconds()-TickStart)*1000; };
    Super::TickComponent(Dt,TickType,TickFunction);
    if(!GetWorld() || !GetWorld()->IsGameWorld()) return;
    Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    if(!Character || !Character->Body || !bEnabled) { Release();if(!Status.StartsWith(TEXT("ERROR:"))) Status=TEXT("Contact disabled");return; }
    if(Body!=Character->Body || Generation!=Character->GetLoadGeneration())
    {
        Release();Body=Character->Body;
        const auto* RC=Character->RuntimeConfiguration.LoadSynchronous();Profile=RC?RC->BreastContact.LoadSynchronous():nullptr;
        Generation=Character->GetLoadGeneration();
    }
    const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>();
    const int32 Teleport=Motion?Motion->GetClock().TeleportRevision:0;
    if(GetWorld()->IsPaused() || (Motion && Motion->GetClock().bPaused) || Dt<=0) { Accumulator=0;return; }
    if(Teleport!=TeleportRevision || ShapeRevision!=Character->GetShapeState().Revision) { Release();TeleportRevision=Teleport; }
    // Do not allocate/step contact solvers when there is no contact source nearby.
    bool NearbyWorld=false;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Sources;
    FBox ContactBounds(ForceInit);
    if(auto* B=Cast<UVamBreastSkeletalMeshComponent>(Body))
        for(const auto& R:B->RestSides) if(Body->GetComponentSpaceTransforms().IsValidIndex(R.AnchorBone))
        {
            const FTransform W=Body->GetComponentSpaceTransforms()[R.AnchorBone]*Body->GetComponentTransform();
            ContactBounds+=FBox(R.COM-R.SizeCm*.6,R.COM+R.SizeCm*.6).TransformBy(W);
        }
    if(bWorldCollision && ContactBounds.IsValid)
    {
        TArray<FOverlapResult> Hits;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(VamContactWake),false,GetOwner());
        GetWorld()->OverlapMultiByObjectType(Hits,ContactBounds.GetCenter(),FQuat::Identity,Objects,FCollisionShape::MakeBox(ContactBounds.GetExtent()),Query);
        for(const auto& H:Hits) if(auto* Mesh=Cast<UStaticMeshComponent>(H.GetComponent()))
            if(Mesh->IsCollisionEnabled()) Sources.AddUnique(Mesh);
        NearbyWorld=!Sources.IsEmpty();
    }
    if(DebugSide==INDEX_NONE && PressSpheres.IsEmpty() && !NearbyWorld)
    { if(!Solvers.IsEmpty()) Release();Status=TEXT("Contact idle: no sources; native skinning; zero solver steps");return; }
    AppliedDebugDepth=FMath::FInterpConstantTo(AppliedDebugDepth,DebugDepth,Dt,.4f);
    if(bReleasingDebug && AppliedDebugDepth<=KINDA_SMALL_NUMBER){DebugSide=INDEX_NONE;bReleasingDebug=false;}
    if(Solvers.IsEmpty()) { const double T=FPlatformTime::Seconds();const bool Ready=Initialize();InitMs=(FPlatformTime::Seconds()-T)*1000;if(!Ready)return; }
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
    for(const auto& Old:WorldSources) if(Old.IsValid() && !Sources.Contains(Old)) WorldCollisions->RemoveStaticMeshComponent(Old.Get());
    for(const auto& Source:Sources) if(!WorldSources.Contains(Source)) WorldCollisions->AddStaticMeshComponent(Source.Get());
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
            Target.X=Front+(S.bPlaten?S.HalfExtentCm.X:S.RadiusCm)-Stroke;
            const FTransform WorldFrame=Anchor*Body->GetComponentTransform();
            S.WorldCenter=WorldFrame.TransformPosition(Target);S.WorldRotation=WorldFrame.GetRotation();Collisions->Spheres.Add(S);
        }
    }
    ActivePressSphereCount=Collisions->Spheres.Num();
    if(bShowContacts)for(const auto& S:Collisions->Spheres){if(S.bPlaten)DrawDebugBox(GetWorld(),S.WorldCenter,S.HalfExtentCm,S.WorldRotation,FColor::Orange,false,0);else DrawDebugSphere(GetWorld(),S.WorldCenter,S.RadiusCm,20,FColor::Orange,false,0);}
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
    PublishMs=(FPlatformTime::Seconds()-PublishStart)*1000;
    if(bShowCage) for(const auto& T:Profile->BoundaryTriangles) for(int32 J=0;J<3;++J)
        DrawDebugLine(GetWorld(),Body->GetComponentTransform().TransformPosition(Current[T[J]]),Body->GetComponentTransform().TransformPosition(Current[T[(J+1)%3]]),FColor::Cyan,false,0,0,.3f);
}

FString UVamBreastContactComponent::Diagnostics() const
{
    FString Text=GetOwner()->GetPathName()+FString::Printf(TEXT(" | world %s | solvers %d | deformer %d\n"),*GetWorld()->GetName(),Solvers.Num(),Body && Body->HasMeshDeformer()?1:0)+Status+FString::Printf(TEXT("\nSteps %d | residual from final animated skin | no additional gravity"),CompletedSteps);
    if(Profile)Text+=FString::Printf(TEXT("\nContact schema %d particles %d tetrahedra %d"),Profile->SchemaVersion,Profile->Particles.Num(),Profile->Tetrahedra.Num());
    Text+=FString::Printf(TEXT("\nPress spheres %d | selected side %d | max contact residual %.4f cm"),ActivePressSphereCount,DebugSide,MaxContactResidualCm);
    Text+=FString::Printf(TEXT("\nBound surface prediction L %.4f R %.4f cm (CPU binding check)"),BoundSurfaceResidualCm.X,BoundSurfaceResidualCm.Y);
    for(int32 I=0;I<VolumeState.Num();++I) { const auto& V=VolumeState[I];Text+=FString::Printf(TEXT("\nSide %d cage V %.2f / %.2f cm3 error %.2f%% min J %.4f inverted %d"),I,V.CurrentVolumeCm3,V.RestVolumeCm3,V.RelativeVolumeError*100,V.MinimumTetRatio,V.InvertedTetrahedra);Text+=FString::Printf(TEXT(" max J %.3f surface stretch %.3f..%.3f"),V.MaximumTetRatio,V.MinimumSurfaceStretch,V.MaximumSurfaceStretch);Text+=FString::Printf(TEXT(" worst edge rest %.6f cm extension max %.4f cm"),V.WorstStretchRestLengthCm,V.MaximumEdgeExtensionCm);Text+=FString::Printf(TEXT(" nipple RMS strain %.5f pairs %d"),V.NippleShapeRmsStrain,V.NippleShapePairCount); }
    Text+=FString::Printf(TEXT("\nCPU ms total %.3f init %.3f solve %.3f publish %.3f | movable %d inside %d -> %d penetration %.3f -> %.3f cm"),TickMs,InitMs,SolveMs,PublishMs,MovableParticles,InsideBefore,InsideAfter,PenetrationBefore,PenetrationAfter);
    return Text;
}

