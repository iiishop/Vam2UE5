#include "VamBreastContactBuilder.h"
#include "VamBreastContactProfile.h"
#include "VamBreastJiggleProfile.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "VamCharacterDefinition.h"
#include "VamShapeData.h"
#include "VamGluteStructureProfile.h"
#include "VamLegJiggleProfile.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace
{
uint64 EdgeKey(int32 A,int32 B) { return (uint64(FMath::Min(A,B))<<32)|uint32(FMath::Max(A,B)); }
template<class T> T* CopyRefinementAsset(T* Source,const FString& Path)
{
    if(!Source || FPackageName::DoesPackageExist(Path) || FindPackage(nullptr,*Path))return nullptr;
    auto* Out=DuplicateObject<T>(Source,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path));
    Out->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Out);Out->MarkPackageDirty();return Out;
}
// Cubic Hermite edge midpoint with tangent-plane projected derivatives.
// It changes only the NEW samples; original source vertices are never moved.
FVector CurvedMidpoint(const FVector& A,const FVector& B,const FVector& NA,const FVector& NB)
{
    const FVector E=B-A;
    const FVector Bend=(-FVector::DotProduct(E,NA)*NA+FVector::DotProduct(E,NB)*NB)*.125;
    return (A+B)*.5+Bend.GetClampedToMaxSize(E.Size()*.125);
}
FVector4f TetCoordinates(const FVector& P,const FIntVector4& T,const TArray<FVamBreastContactParticle>& X)
{
    const FVector A=X[T[0]].Rest,B=X[T[1]].Rest-A,C=X[T[2]].Rest-A,D=X[T[3]].Rest-A,Q=P-A;
    auto Det=[](const FVector& U,const FVector& V,const FVector& W){return FVector::DotProduct(U,FVector::CrossProduct(V,W));};
    const double Den=Det(B,C,D),Y=Det(Q,C,D)/Den,Z=Det(B,Q,D)/Den,W=Det(B,C,Q)/Den;
    return FVector4f(1-Y-Z-W,Y,Z,W);
}
}

UVamCharacterDefinition* UVamBreastContactBuilder::RefineRenderSurface(const FString& Root,UVamCharacterDefinition* Source,UVamBreastJiggleProfile* Breast,UVamBreastContactProfile* P,UVamGluteStructureProfile* Glute,UVamLegJiggleProfile* Leg,FString& Error)
{
    Error.Reset();auto Fail=[&](const FString& Why)->UVamCharacterDefinition*{Error=Why;return nullptr;};
    if(!Source || !Breast || !Glute || !Leg || !P || !P->ValidateData().IsEmpty())return Fail(TEXT("Render refinement needs valid contact data"));
    auto* OldMesh=Source->Body.LoadSynchronous();auto* Shape=Source->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;
    FVamNativeMeshInput I;if(!UVamBreastJiggleBuilder::ExtractNative(OldMesh,I,Error))return nullptr;
    const int32 OriginalCount=I.Vertices.Num(),OriginalTriangles=I.Triangles.Num()/3,ParticleCount=P->Particles.Num(),TetCount=P->Tetrahedra.Num();
    if(!Geometry || Geometry->InputToSource.Num()!=OriginalCount || Geometry->RenderRefinementVersion!=0)return Fail(TEXT("Render refinement requires unrefined native input"));
    const auto OldMap=UVamNativeBuilder::GetRenderToInputMap(OldMesh);
    if(OldMap.Num()!=P->SurfaceMask.Num())return Fail(TEXT("Render refinement contact correspondence mismatch"));
    TArray<float> Mask,Nipple;Mask.Init(0,OriginalCount);Nipple.Init(0,OriginalCount);
    TArray<FIntVector4> Parents;Parents.Init(FIntVector4(0,0,0,0),OriginalCount);
    TArray<FVector4f> BindWeights;BindWeights.Init(FVector4f(1,0,0,0),OriginalCount);
    TArray<FVector3f> Offsets;Offsets.Init(FVector3f::ZeroVector,OriginalCount);
    for(int32 R=0;R<OldMap.Num();++R){const int32 V=OldMap[R];Mask[V]=P->SurfaceMask[R];Nipple[V]=P->SurfaceNippleSupport[R];Parents[V]=P->SurfaceParents[R];BindWeights[V]=P->SurfaceWeights[R];Offsets[V]=P->SurfaceOffsets[R];}
    TArray<TMap<int32,double>> Skin;Skin.SetNum(OriginalCount);for(const auto& W:I.Influences)Skin[W.Vertex].Add(W.Bone,W.Weight);
    TArray<FIntPoint> Provenance;Provenance.Init(FIntPoint(INDEX_NONE,INDEX_NONE),OriginalCount);
    TArray<int32> Representatives;Representatives.SetNum(OriginalCount);TMap<int32,int32> SourceRepresentative;
    for(int32 V=0;V<OriginalCount;++V){const int32 Id=Geometry->InputToSource[V];if(const int32* Found=SourceRepresentative.Find(Id))Representatives[V]=*Found;else{SourceRepresentative.Add(Id,V);Representatives[V]=V;}}
    // A single normal per source point keeps coincident UV/material copies coincident.
    TArray<FVector> Sums;Sums.Init(FVector::ZeroVector,OriginalCount);
    for(int32 V=0;V<OriginalCount;++V)Sums[Representatives[V]]+=I.Normals[V];
    TArray<FVector> CurveNormals=I.Normals;
    for(int32 V=0;V<OriginalCount;++V)CurveNormals[V]=Sums[Representatives[V]].GetSafeNormal();
    const double TargetEdge=FMath::Max(.1,FMath::Min(Breast->Sides[0].EffectiveRadiusCm,Breast->Sides[1].EffectiveRadiusCm)*.06);
    for(int32 Pass=0;Pass<2;++Pass)
    {
        TMap<uint64,int32> Midpoints;TMap<uint64,int32> WeldedMidpoints;
        // Collect before editing triangles so both adjacent faces split every selected edge.
        TArray<FIntPoint> Selected;TSet<uint64> Seen;
        for(int32 T=0;T<I.Triangles.Num();T+=3)for(int32 K=0;K<3;++K){const int32 A=I.Triangles[T+K],B=I.Triangles[T+(K+1)%3];const uint64 Key=EdgeKey(A,B);
            if(FMath::Max(Mask[A],Mask[B])>.02 && FVector::Distance(I.Vertices[A],I.Vertices[B])>TargetEdge && !Seen.Contains(Key)){Seen.Add(Key);Selected.Add(FIntPoint(A,B));}}
        for(const auto& Edge:Selected)
        {
            const int32 A=Edge.X,B=Edge.Y,V=I.Vertices.Num();const FVector Position=CurvedMidpoint(I.Vertices[A],I.Vertices[B],CurveNormals[A],CurveNormals[B]);
            I.Vertices.Add(Position);CurveNormals.Add((CurveNormals[A]+CurveNormals[B]).GetSafeNormal());I.Normals.Add(CurveNormals.Last());I.UV.Add((I.UV[A]+I.UV[B])*.5);I.SourceVertices.Add(V);
            Provenance.Add(Edge);Mask.Add((Mask[A]+Mask[B])*.5);Nipple.Add((Nipple[A]+Nipple[B])*.5);
            const uint64 WeldKey=EdgeKey(Representatives[A],Representatives[B]);const int32* Weld=WeldedMidpoints.Find(WeldKey);
            Representatives.Add(Weld?*Weld:V);if(!Weld)WeldedMidpoints.Add(WeldKey,V);
            Skin.AddDefaulted();for(const auto& W:Skin[A])Skin[V].FindOrAdd(W.Key)+=W.Value*.5;for(const auto& W:Skin[B])Skin[V].FindOrAdd(W.Key)+=W.Value*.5;
            TArray<int32> Bones;Skin[V].GetKeys(Bones);Bones.Sort([&](int32 L,int32 R){const double D=Skin[V][L]-Skin[V][R];return D==0?L<R:D>0;});
            double Total=0;for(int32 K=0;K<FMath::Min(8,Bones.Num());++K)Total+=Skin[V][Bones[K]];
            for(int32 K=8;K<Bones.Num();++K)Skin[V].Remove(Bones[K]);for(auto& W:Skin[V])W.Value/=Total;
            // Evaluate the same bounded Hermite operator on each Morph.
            // The normals define a fixed rest patch; Morph normals are rebuilt by UE.
            for(auto& M:I.Morphs)M.Deltas.Add(CurvedMidpoint(I.Vertices[A]+M.Deltas[A],I.Vertices[B]+M.Deltas[B],CurveNormals[A],CurveNormals[B])-Position);
            Parents.Add(FIntVector4(0,0,0,0));BindWeights.Add(FVector4f(1,0,0,0));Offsets.Add(FVector3f::ZeroVector);
            if(Mask[V]>0)
            {
                const int32 Side=P->Particles[Parents[Mask[A]>=Mask[B]?A:B][0]].Side;
                double Best=DBL_MAX;FIntVector4 BestTet(0,0,0,0);FVector4f BestWeights(1,0,0,0);FVector BestOffset=FVector::ZeroVector;
                // Positive weights only: curved samples outside the convex cage carry a
                // small detail offset, rather than unstable extrapolation into a sliver.
                for(const auto& T:P->Tetrahedra)if(P->Particles[T[0]].Side==Side){auto W=TetCoordinates(Position,T,P->Particles);double Sum=0;for(int32 J=0;J<4;++J){W[J]=FMath::Max(0.f,W[J]);Sum+=W[J];}if(Sum<=0)continue;
                    FVector Q=FVector::ZeroVector;for(int32 J=0;J<4;++J){W[J]/=Sum;Q+=P->Particles[T[J]].Rest*W[J];}
                    const double Distance=FVector::DistSquared(Position,Q);if(Distance<Best){Best=Distance;BestTet=T;BestWeights=W;BestOffset=Position-Q;}if(Best<1.e-12)break;}
                if(!FMath::IsFinite(Best) || Best>FMath::Square(TargetEdge))return Fail(TEXT("Refined render sample too far outside existing cage"));
                Parents[V]=BestTet;BindWeights[V]=BestWeights;Offsets[V]=FVector3f(BestOffset);
            }
            Midpoints.Add(EdgeKey(A,B),V);
        }
        TArray<int32> Triangles,Materials;
        for(int32 T=0;T<I.Triangles.Num();T+=3)
        {
            const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];TArray<FIntVector> Pieces;Pieces.Add(FIntVector(A,B,C));
            for(const auto E:{FIntPoint(A,B),FIntPoint(B,C),FIntPoint(C,A)})if(const int32* M=Midpoints.Find(EdgeKey(E.X,E.Y)))
            {
                for(int32 K=0;K<Pieces.Num();++K){const FIntVector F=Pieces[K];bool Split=false;for(int32 J=0;J<3;++J)if(EdgeKey(F[J],F[(J+1)%3])==EdgeKey(E.X,E.Y)){
                    Pieces[K]=FIntVector(F[J],*M,F[(J+2)%3]);Pieces.Add(FIntVector(*M,F[(J+1)%3],F[(J+2)%3]));Split=true;break;}if(Split)break;}
            }
            for(const auto& F:Pieces){Triangles.Append({F.X,F.Y,F.Z});Materials.Add(I.TriangleMaterials[T/3]);}
        }
        I.Triangles=MoveTemp(Triangles);I.TriangleMaterials=MoveTemp(Materials);
    }
    I.Influences.Reset();for(int32 V=0;V<Skin.Num();++V){TArray<int32> Bones;Skin[V].GetKeys(Bones);Bones.Sort();for(int32 B:Bones){FVamBuildInfluence W;W.Vertex=V;W.Bone=B;W.Weight=Skin[V][B];I.Influences.Add(W);}}
    auto* Mesh=UVamNativeBuilder::BuildMesh(Root+TEXT("/SK_Body"),I,Error);if(!Mesh)return nullptr;
    auto* D=CopyRefinementAsset(Source,Root+TEXT("/CD_Character"));auto* S=CopyRefinementAsset(Shape,Root+TEXT("/SD_Shape"));auto* G=CopyRefinementAsset(Geometry,Root+TEXT("/GD_Bindings"));
    if(!D || !S || !G)return Fail(TEXT("Immutable refinement destination already exists"));
    // The new mesh uses the identical append-only skeleton, including all other systems.
    if(!UVamNativeBuilder::ShareCompatibleSkeleton(Mesh,OldMesh))return Fail(TEXT("Refined skeleton identity mismatch"));
    D->Body=Mesh;D->Skeleton=OldMesh->GetSkeleton();D->Shape=S;S->Geometry=G;
    G->RenderRefinementVersion=1;G->RenderRefinementParents=Provenance;G->InputToSource.SetNum(I.Vertices.Num());
    for(int32 V=OriginalCount;V<I.Vertices.Num();++V)G->InputToSource[V]=INDEX_NONE;
    for(auto& Region:G->Regions){const FString Id=Region.Id.ToString();if(Id.StartsWith(TEXT("source_material_"))){
        const int32 Slot=FCString::Atoi(*Id.RightChop(15));Region.InputTriangles.Reset();
        for(int32 T=0;T<I.TriangleMaterials.Num();++T)if(I.TriangleMaterials[T]==Slot)Region.InputTriangles.Add(T);}}
    G->InputTriangles=I.Triangles;G->RenderToInput=UVamNativeBuilder::GetRenderToInputMap(Mesh);
    // Profiles retain their calibrated dynamics. Only append diagnostic/source-domain
    // correspondence; no anatomical mass/support is recalibrated from denser samples.
    auto ExtendRegion=[&](auto& Region){for(int32 V=OriginalCount;V<Provenance.Num();++V){const auto E=Provenance[V];
        Region.RegionWeights.Add((Region.RegionWeights[E.X]+Region.RegionWeights[E.Y])*.5f);
        Region.RegionPoints.Add((Region.RegionPoints[E.X]+Region.RegionPoints[E.Y])*.5);}};
    for(auto& R:Breast->Sides)ExtendRegion(R);
    for(auto& R:Glute->Sides)ExtendRegion(R);
    for(auto& R:Leg->Segments)ExtendRegion(R);
    Breast->MarkPackageDirty();Glute->MarkPackageDirty();Leg->MarkPackageDirty();
    P->bResidualOnlySurface=true;
    P->BuildAlgorithmVersion+=TEXT("-render-curve-v1-direct-residual");
    P->Body=Mesh;P->SurfaceParents.Reset();P->SurfaceWeights.Reset();P->SurfaceOffsets.Reset();P->SurfaceMask.Reset();P->SurfaceNippleSupport.Reset();P->MeasurementTriangles.Reset();
    TArray<int32> FirstRender;FirstRender.Init(INDEX_NONE,I.Vertices.Num());
    for(int32 R=0;R<G->RenderToInput.Num();++R){const int32 V=G->RenderToInput[R];if(FirstRender[V]<0)FirstRender[V]=R;
        P->SurfaceParents.Add(Parents[V]);P->SurfaceWeights.Add(BindWeights[V]);P->SurfaceOffsets.Add(Offsets[V]);P->SurfaceMask.Add(Mask[V]);P->SurfaceNippleSupport.Add(Nipple[V]);}
    for(int32 T=0;T<I.Triangles.Num();T+=3){const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];if(FMath::Min3(Mask[A],Mask[B],Mask[C])>.5)P->MeasurementTriangles.Add(FIntVector(FirstRender[A],FirstRender[B],FirstRender[C]));}
    if(P->Particles.Num()!=ParticleCount || P->Tetrahedra.Num()!=TetCount)return Fail(TEXT("Render refinement changed physical topology"));
    Error=P->ValidateData();if(!Error.IsEmpty())return nullptr;
    P->RegionProvenance+=TEXT("; render-only curved-edge refinement v1; stable original vertices; native Morph/UV/skin interpolation; unchanged physical cage");
    P->MarkPackageDirty();D->MarkPackageDirty();S->MarkPackageDirty();G->MarkPackageDirty();
    UE_LOG(LogTemp,Display,TEXT("CONTACT_RENDER_REFINEMENT input %d -> %d triangles %d -> %d particles %d tets %d target_edge_cm %.4f"),OriginalCount,I.Vertices.Num(),OriginalTriangles,I.Triangles.Num()/3,ParticleCount,TetCount,TargetEdge);
    return D;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamContactRenderCurveTest,"Vam.Breast.ContactRenderCurve",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamContactRenderCurveTest::RunTest(const FString&)
{
    const FVector A(1,0,0),B(0,1,0),N(0,0,1);
    TestTrue(TEXT("Plane midpoint unchanged"),CurvedMidpoint(A,B,N,N).Equals((A+B)*.5,1.e-10));
    const FVector P=CurvedMidpoint(A,B,A,B);
    TestTrue(TEXT("Curved midpoint closer to unit circle"),FMath::Abs(P.Size()-1)<FMath::Abs(((A+B)*.5).Size()-1));
    TestTrue(TEXT("Bounded curvature"),FVector::Distance(P,(A+B)*.5)<=FVector::Distance(A,B)*.125+1.e-10);
    TestTrue(TEXT("Edge direction independent"),CurvedMidpoint(B,A,B,A).Equals(P,1.e-10));
    const FQuat Q(FVector(1,2,3).GetSafeNormal(),.7);const FVector Offset(7,-2,5);
    TestTrue(TEXT("Pose frame independent"),CurvedMidpoint(Q.RotateVector(A)+Offset,Q.RotateVector(B)+Offset,Q.RotateVector(A),Q.RotateVector(B)).Equals(Q.RotateVector(P)+Offset,1.e-9));
    TestTrue(TEXT("Collapsed edge finite"),!CurvedMidpoint(A,A,A,-A).ContainsNaN());
    return true;
}
#endif
