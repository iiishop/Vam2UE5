#include "VamGluteJiggleBuilder.h"
#include "VamGluteJiggleProfile.h"
FString UVamGluteJiggleBuilder::Build(UVamGluteStructureProfile* G,UVamGluteJiggleProfile* P)
{
    if(!G || !P || !G->IsValidProfile() || G->RefinementVersion!=1) return TEXT("G1 requires valid G0.5 structural calibration");
    P->SchemaVersion=2;P->Algorithm=TEXT("glute-dual-attachment-g1.1-reference-gravity-v1");P->bNormalizedAttachmentDamping=true;
    P->AuthoredGravityWorld=FVector(0,0,-980);
    P->GravityPolicy=TEXT("body-attached-imported-1g-v1");
    P->SourceTopologyIdentity=G->SourceTopologyIdentity;P->SkeletonFamily=G->SkeletonFamily;P->DensityKgPerCm3=G->DensityCandidateKgPerCm3;P->Sides.Reset();P->bBilateralMaterialCalibration=false;
    for(const auto& S:G->Sides)
    {
        auto Side=VamGluteDynamics::Calibrate(*P,S,VamGluteStructure::Evaluate(*G,S,S.RestThighInAnchor));
        Side.ReferenceGravityLocal=(S.AnchorLocal*G->RestPelvisComponent).GetRotation().UnrotateVector(P->AuthoredGravityWorld);
        P->Sides.Add(Side);
    }
    // Infer one material response from both noisy region estimates, without mirroring shape.
    for(int32 N=0;N<5;++N)
    {
        auto& L=P->Sides[0].Nodes[N];auto& R=P->Sides[1].Nodes[N];
        const FVector Rate=(L.Support/L.MassKg+R.Support/R.MassKg)*.5;
        const FVector Travel=(L.PositiveTravel/P->Sides[0].Dimensions+R.PositiveTravel/P->Sides[1].Dimensions)*.5;
        const FVector Negative=(L.NegativeTravel/P->Sides[0].Dimensions+R.NegativeTravel/P->Sides[1].Dimensions)*.5;
        L.Support=Rate*L.MassKg;R.Support=Rate*R.MassKg;
        L.PositiveTravel=Travel*P->Sides[0].Dimensions;R.PositiveTravel=Travel*P->Sides[1].Dimensions;
        L.NegativeTravel=Negative*P->Sides[0].Dimensions;R.NegativeTravel=Negative*P->Sides[1].Dimensions;
    }
    P->bBilateralMaterialCalibration=true;
    for(int32 SideIndex=0;SideIndex<2;++SideIndex)
        P->Sides[SideIndex]=VamGluteDynamics::Calibrate(*P,G->Sides[SideIndex],VamGluteStructure::Evaluate(*G,G->Sides[SideIndex],G->Sides[SideIndex].RestThighInAnchor));
    if(!P->IsValidProfile()) return TEXT("Invalid G1 automatic dynamics calibration");P->MarkPackageDirty();return FString();
}

#include "VamCharacterDefinition.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "Engine/SkeletalMesh.h"
FString UVamGluteJiggleBuilder::BuildSurfaceGuard(UVamCharacterDefinition* Definition,UVamGluteJiggleProfile* P)
{
    if(!Definition || !P || P->Sides.Num()!=2) return TEXT("Missing final surface guard inputs");
    FVamNativeMeshInput Mesh;FString Error;
    if(!UVamBreastJiggleBuilder::ExtractNative(Definition->Body.LoadSynchronous(),Mesh,Error)) return Error;
    TArray<TMap<int32,double>> Weights;Weights.SetNum(Mesh.Vertices.Num());
    for(const auto& W:Mesh.Influences) Weights[W.Vertex].Add(W.Bone,W.Weight);
    P->SurfaceGradients.Reset();
    for(int32 T=0;T<Mesh.Triangles.Num();T+=3)
    {
        const int32 A=Mesh.Triangles[T],B=Mesh.Triangles[T+1],C=Mesh.Triangles[T+2];
        const FVector E1=Mesh.Vertices[B]-Mesh.Vertices[A],E2=Mesh.Vertices[C]-Mesh.Vertices[A];
        const double Length=E1.Size();if(Length<1.e-6) continue;
        const double X=FVector::DotProduct(E2,E1)/Length,Y=FVector::CrossProduct(E1,E2).Size()/Length;
        if(Y<1.e-6) continue;
        for(int32 Side=0;Side<2;++Side)
        {
            FVamGluteSurfaceGradient G;G.Side=Side;double Norm=0;
            for(const auto& Node:P->Sides[Side].Nodes)
            {
                const double WA=Weights[A].FindRef(Node.BoneIndex),WB=Weights[B].FindRef(Node.BoneIndex),WC=Weights[C].FindRef(Node.BoneIndex);
                const double GX=(WB-WA)/Length,GY=(WC-WA-GX*X)/Y;
                G.WeightGradients.Add(FVector2D(GX,GY));Norm+=GX*GX+GY*GY;
            }
            if(Norm>1.e-16) P->SurfaceGradients.Add(MoveTemp(G));
        }
    }
    if(P->SurfaceGradients.IsEmpty()) return TEXT("Empty final glute surface gradients");
    P->SurfaceGuardVersion=1;P->MarkPackageDirty();
    UE_LOG(LogTemp,Display,TEXT("GLUTE_SURFACE_GUARD triangles/sides=%d"),P->SurfaceGradients.Num());return FString();
}
