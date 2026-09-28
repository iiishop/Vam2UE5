#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "VamBreastJiggleProfile.h"
#include "VamBreastCalibration.h"
#include "VamBreastWeightUtils.h"
#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/MorphTarget.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
template<class T> T* CopyAsset(T* Source,const FString& Path)
{
    if(!Source || FPackageName::DoesPackageExist(Path) || FindPackage(nullptr,*Path)) return nullptr;
    auto* Result=DuplicateObject<T>(Source,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path));
    if(Result) { Result->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Result);Result->MarkPackageDirty(); }
    return Result;
}
bool Extract(USkeletalMesh* Mesh,FVamNativeMeshInput& I,FString& Error)
{
    const FMeshDescription* D=Mesh ? Mesh->GetMeshDescription(0) : nullptr;
    if(!D) { Error=TEXT("Source has no retained native MeshDescription; reimport required");return false; }
    FSkeletalMeshConstAttributes A(*D);
    const int32 Count=D->Vertices().Num();
    I.Vertices.SetNum(Count);I.Normals.Init(FVector::UpVector,Count);I.UV.Init(FVector2D::ZeroVector,Count);I.SourceVertices.SetNum(Count);
    for(const auto V:D->Vertices().GetElementIDs())
    {
        const int32 Id=V.GetValue();if(!I.Vertices.IsValidIndex(Id)) { Error=TEXT("Noncompact source vertex domain unsupported");return false; }
        I.Vertices[Id]=FVector(A.GetVertexPositions()[V]);I.SourceVertices[Id]=Id;
        for(const auto& W:A.GetVertexSkinWeights().Get(V)) { FVamBuildInfluence F;F.Vertex=Id;F.Bone=W.GetBoneIndex();F.Weight=W.GetWeight();I.Influences.Add(F); }
    }
    TSet<int32> ReferencedVertices;
    for(const auto V:D->VertexInstances().GetElementIDs())
    {
        const int32 Id=D->GetVertexInstanceVertex(V).GetValue();
        ReferencedVertices.Add(Id);
        I.Normals[Id]=FVector(A.GetVertexInstanceNormals()[V]);I.UV[Id]=FVector2D(A.GetVertexInstanceUVs().Get(V,0));
    }
    if(ReferencedVertices.Num()<Count) UE_LOG(LogTemp,Display,TEXT("VAM_NATIVE_EXTRACT: %s retains %d unused vertices; UV initialized deterministically"),*Mesh->GetPathName(),Count-ReferencedVertices.Num());
    for(const auto T:D->Triangles().GetElementIDs())
    {
        for(const auto V:D->GetTriangleVertices(T)) I.Triangles.Add(V.GetValue());
        I.TriangleMaterials.Add(D->GetTrianglePolygonGroup(T).GetValue());
    }
    for(const auto& M:Mesh->GetMaterials()) I.Materials.Add(M.MaterialInterface);
    const auto& Ref=Mesh->GetRefSkeleton();
    for(int32 B=0;B<Ref.GetRawBoneNum();++B) { FVamBuildBone Bone;Bone.Name=Ref.GetBoneName(B);Bone.Parent=Ref.GetParentIndex(B);Bone.LocalBind=Ref.GetRefBonePose()[B];I.Bones.Add(Bone); }
    for(FName Name:A.GetMorphTargetNames())
    {
        FVamBuildMorph M;M.Name=Name;M.Deltas.SetNum(Count);
        const auto Delta=A.GetVertexMorphPositionDelta(Name);
        for(const auto V:D->Vertices().GetElementIDs()) M.Deltas[V.GetValue()]=FVector(Delta[V]);
        I.Morphs.Add(M);
    }
    return true;
}
double Smooth(double X) { X=FMath::Clamp(X,0.,1.);return X*X*(3-2*X); }
struct FMeasure
{
    double Volume=0,Area=0,Radius=0,Depth=0;
    FVector COM=FVector::ZeroVector;
    TArray<FVector> Nodes,MassCenters;
    TArray<double> NodeVolumes;
    FVector Size=FVector::ZeroVector,RootSize=FVector::ZeroVector;
};
FMeasure Measure(const FVamNativeMeshInput& I,const TArray<FVector>& V,const TArray<float>& Region,const FTransform& Anchor,double SideSign)
{
    FMeasure M;M.Nodes.Init(FVector::ZeroVector,5);TArray<double> Sum;Sum.Init(0,5);
    TArray<FVector> Local;for(const auto& P:V) Local.Add(Anchor.InverseTransformPosition(P));
    double Weight=0;FVector Mean=FVector::ZeroVector;
    for(int32 J=0;J<V.Num();++J) { Mean+=Local[J]*Region[J];Weight+=Region[J]; }
    Mean/=FMath::Max(Weight,1.e-12);
    for(int32 T=0;T<I.Triangles.Num();T+=3)
    {
        const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];
        const double W=(Region[A]+Region[B]+Region[C])/3.;
        const FVector Center=(Local[A]+Local[B]+Local[C])/3.;
        const FVector Cross=FVector::CrossProduct(Local[B]-Local[A],Local[C]-Local[A]);
        const double Projected=FMath::Abs(Cross.X)*.5*W;
        const double Volume=Projected*FMath::Max(0.,Center.X)/3.;
        M.Area+=Projected;M.Volume+=Volume;M.COM+=Center*.75*Volume;
    }
    M.COM/=FMath::Max(M.Volume,1.e-12);
    double R2=0;
    for(int32 J=0;J<V.Num();++J) R2+=Region[J]*(FMath::Square(Local[J].Y-Mean.Y)+FMath::Square(Local[J].Z-Mean.Z));
    M.Radius=FMath::Sqrt(R2/FMath::Max(Weight,1.e-12));M.Depth=M.Volume*3/FMath::Max(M.Area,1.e-12);
    const double Scale=FMath::Max(.1,M.Radius);
    const FVector Centers[5]={Mean,Mean+FVector(0,0,Scale*.6),Mean-FVector(0,0,Scale*.6),Mean-FVector(0,SideSign*Scale*.6,0),Mean+FVector(0,SideSign*Scale*.6,0)};
    for(int32 J=0;J<V.Num();++J) for(int32 N=0;N<5;++N)
    {
        const double W=Region[J]*FMath::Exp(-(Local[J]-Centers[N]).SizeSquared()/(Scale*Scale*.6));
        M.Nodes[N]+=Local[J]*W;Sum[N]+=W;
    }
    for(int32 N=0;N<5;++N) M.Nodes[N]/=FMath::Max(Sum[N],1.e-12);
    // Partition each support cone by normalized semantic kernels; no fixed mass fractions.
    M.NodeVolumes.Init(0,5);M.MassCenters.Init(FVector::ZeroVector,5);
    FVector Variance=FVector::ZeroVector;double RootWeight=0;FVector RootVariance=FVector::ZeroVector;
    for(int32 J=0;J<V.Num();++J)
    {
        const FVector D=Local[J]-Mean;Variance+=D*D*Region[J];
        const double W=Region[J]*FMath::Exp(-FMath::Square(Local[J].X/FMath::Max(.1,M.Depth*.5)));
        RootVariance+=D*D*W;RootWeight+=W;
    }
    for(int32 A=0;A<3;++A) { M.Size[A]=FMath::Max(.1,4*FMath::Sqrt(Variance[A]/FMath::Max(Weight,1.e-12)));M.RootSize[A]=FMath::Max(.1,4*FMath::Sqrt(RootVariance[A]/FMath::Max(RootWeight,1.e-12))); }
    M.Size.X=FMath::Max(M.Size.X,M.Depth);
    for(int32 T=0;T<I.Triangles.Num();T+=3)
    {
        const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];
        const FVector Center=(Local[A]+Local[B]+Local[C])/3.;
        const double W=(Region[A]+Region[B]+Region[C])/3.;
        const double Volume=FMath::Abs(FVector::CrossProduct(Local[B]-Local[A],Local[C]-Local[A]).X)*.5*W*FMath::Max(0.,Center.X)/3.;
        double Kernels[5],Total=0;
        for(int32 N=0;N<5;++N) { Kernels[N]=FMath::Exp(-(Center-Centers[N]).SizeSquared()/FMath::Square(Scale*.65));Total+=Kernels[N]; }
        if(Total<1.e-30) continue;
        for(int32 N=0;N<5;++N) { const double Part=Volume*Kernels[N]/Total;M.NodeVolumes[N]+=Part;M.MassCenters[N]+=Center*.75*Part; }
    }
    for(int32 N=0;N<5;++N) M.MassCenters[N]/=FMath::Max(1.e-12,M.NodeVolumes[N]);
    return M;
}
}
bool UVamBreastJiggleBuilder::ExtractNative(USkeletalMesh* Mesh,FVamNativeMeshInput& Input,FString& Error) { return Extract(Mesh,Input,Error); }
UVamCharacterDefinition* UVamBreastJiggleBuilder::Build(const FString& Root,UVamCharacterDefinition* Source,UVamBreastJiggleProfile* P,const FString& FamilyJson,FString& Error)
{
    Error.Reset();auto Fail=[&Error](const FString& Why)->UVamCharacterDefinition*{Error=Why;return nullptr;};
    if(!Source || !P) return Fail(TEXT("Missing native source/profile"));
    TSharedPtr<FJsonObject> Family;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FamilyJson),Family) || !Family) return Fail(TEXT("Invalid family JSON"));
    const TSharedPtr<FJsonObject>* Mapping=nullptr;
    if(!Family->TryGetObjectField(TEXT("breast_jiggle"),Mapping)) return Fail(TEXT("Breast Jiggle unsupported family: no semantic mapping"));
    FVamNativeMeshInput I;
    if(!Extract(Source->Body.LoadSynchronous(),I,Error)) return nullptr;
    auto* Shape=Source->Shape.LoadSynchronous();auto* Geometry=Shape ? Shape->Geometry.LoadSynchronous() : nullptr;
    if(!Geometry || Geometry->InputToSource.Num()!=I.Vertices.Num()) return Fail(TEXT("Source topology correspondence unavailable"));
    P->SourceTopologyIdentity=Geometry->TopologyDigest;P->SkeletonFamily=Family->GetStringField(TEXT("family"));
    P->SchemaVersion=3;P->BuildAlgorithmVersion=TEXT("breast-modal-v3");P->DensityKgPerCm3=.00102;
    P->SourceBoneCount=I.Bones.Num();P->Sides.Reset();
    P->CompressedDonorVertices=P->SaturatedVerticesWithHelpers=P->SaturatedVerticesWithoutEligibleDonors=0;P->MeanHelperWeight=0;
    TArray<FTransform> CS;for(int32 B=0;B<I.Bones.Num();++B) CS.Add(I.Bones[B].Parent<0 ? I.Bones[B].LocalBind : I.Bones[B].LocalBind*CS[I.Bones[B].Parent]);
    auto Bone=[&](const TCHAR* Key)->int32 { FString Name; if(!(*Mapping)->TryGetStringField(Key,Name)) return INDEX_NONE;return I.Bones.IndexOfByPredicate([&](const FVamBuildBone& B){return B.Name==FName(*Name);}); };
    const int32 Chest=Bone(TEXT("chest")),Superior=Bone(TEXT("superior")),Left=Bone(TEXT("left_pectoral")),Right=Bone(TEXT("right_pectoral"));
    if(Chest<0 || Superior<0 || Left<0 || Right<0) return Fail(TEXT("Breast Jiggle unsupported: required family semantics missing"));
    TSet<int32> Donors;const TArray<TSharedPtr<FJsonValue>>* Support=nullptr;
    if(!(*Mapping)->TryGetArrayField(TEXT("support"),Support)) return Fail(TEXT("Family support bones missing"));
    for(const auto& V:*Support) { const int32 B=I.Bones.IndexOfByPredicate([&](const FVamBuildBone& X){return X.Name==FName(*V->AsString());});if(B<0) return Fail(TEXT("Invalid support bone"));Donors.Add(B); }
    Donors.Add(Left);Donors.Add(Right);
    TArray<TMap<int32,double>> Weights;Weights.SetNum(I.Vertices.Num());
    for(const auto& W:I.Influences) Weights[W.Vertex].Add(W.Bone,W.Weight);
    TArray<TArray<int32>> Adj;Adj.SetNum(I.Vertices.Num());
    for(int32 T=0;T<I.Triangles.Num();T+=3) for(int32 K=0;K<3;++K)
    { const int32 A=I.Triangles[T+K],B=I.Triangles[T+(K+1)%3];Adj[A].AddUnique(B);Adj[B].AddUnique(A); }
    // Weld only the persisted source identity (UV/material splits), never proximity guesses.
    TMap<int32,int32> First;
    for(int32 V=0;V<I.Vertices.Num();++V)
    { const int32 SourceId=Geometry->InputToSource[V];if(const int32* Other=First.Find(SourceId)) { Adj[V].AddUnique(*Other);Adj[*Other].AddUnique(V); } else First.Add(SourceId,V); }
    FVector Z=(CS[Superior].GetLocation()-CS[Chest].GetLocation()).GetSafeNormal();
    FVector Y=(CS[Left].GetLocation()-CS[Right].GetLocation());Y=(Y-Z*FVector::DotProduct(Y,Z)).GetSafeNormal();
    FVector X=FVector::CrossProduct(Y,Z).GetSafeNormal();
    FVector Front=FVector::ZeroVector;double TotalSupport=0;
    for(int32 V=0;V<I.Vertices.Num();++V) { double W=Weights[V].FindRef(Left)+Weights[V].FindRef(Right);Front+=W*(I.Vertices[V]-CS[Chest].GetLocation());TotalSupport+=W; }
    const int32 LF=Bone(TEXT("left_front_reference")),RF=Bone(TEXT("right_front_reference"));
    if(LF>=0 && RF>=0) Front=(CS[LF].GetLocation()+CS[RF].GetLocation()-CS[Left].GetLocation()-CS[Right].GetLocation())*.5*TotalSupport;
    if(TotalSupport<1 || X.IsNearlyZero() || FMath::Abs(FVector::DotProduct(Front/TotalSupport,X))<.01) return Fail(TEXT("Ambiguous chest frame/front evidence"));
    if(FVector::DotProduct(Front,X)<0) { X=-X;Y=-Y; }
    const FQuat Frame=FRotationMatrix::MakeFromXY(X,Y).ToQuat();
    const FName Semantics[5]={TEXT("Core"),TEXT("Upper"),TEXT("Lower"),TEXT("Medial"),TEXT("Lateral")};
    for(int32 Side=0;Side<2;++Side)
    {
        const int32 Pec=Side==0 ? Left : Right,Other=Side==0 ? Right : Left;
        FVamBreastSideProfile R;R.Side=Side==0 ? TEXT("L") : TEXT("R");R.ChestBone=Chest;
        const FTransform Anchor(Frame,CS[Pec].GetLocation());R.AnchorLocal=Anchor.GetRelativeTransform(CS[Chest]);
        const double Sign=FVector::DotProduct(CS[Pec].GetLocation()-CS[Other].GetLocation(),Y)>0 ? 1 : -1;
        R.RegionWeights.Init(0,I.Vertices.Num());
        double Weight=0;FVector Mean=FVector::ZeroVector;
        for(int32 V=0;V<I.Vertices.Num();++V) { const double W=Weights[V].FindRef(Pec);Mean+=Anchor.InverseTransformPosition(I.Vertices[V])*W;Weight+=W; }
        if(Weight<1) return Fail(TEXT("Insufficient pectoral skin support"));Mean/=Weight;
        double Radius2=0;
        for(int32 V=0;V<I.Vertices.Num();++V) Radius2+=Weights[V].FindRef(Pec)*(Anchor.InverseTransformPosition(I.Vertices[V])-Mean).SizeSquared();
        const double Radius=FMath::Max(.1,FMath::Sqrt(Radius2/Weight));
        TArray<double> MorphEvidence;MorphEvidence.Init(0,I.Vertices.Num());
        for(const auto& M:I.Morphs)
        {
            const auto* Param=Source->Parameters.FindByPredicate([&](const FVamMorphParameter& V){return V.Target==M.Name;});
            if(!Param || Param->Group==TEXT("Expression")) continue;
            double TotalDelta=0,SupportedDelta=0;
            for(int32 V=0;V<I.Vertices.Num();++V) { const double D=M.Deltas[V].SizeSquared();TotalDelta+=D;SupportedDelta+=D*(Weights[V].FindRef(Left)+Weights[V].FindRef(Right)); }
            if(TotalDelta<1.e-10 || SupportedDelta/TotalDelta<.35) continue;
            for(int32 V=0;V<I.Vertices.Num();++V) MorphEvidence[V]=FMath::Max(MorphEvidence[V],M.Deltas[V].Size()/Radius);
        }
        for(int32 V=0;V<I.Vertices.Num();++V)
        {
            const FVector Local=Anchor.InverseTransformPosition(I.Vertices[V]);
            double Allowed=0;for(const auto& W:Weights[V]) if(Donors.Contains(W.Key) && W.Key!=Other) Allowed+=W.Value;
            const double SideGate=Smooth(FVector::DotProduct(I.Vertices[V]-(CS[Left].GetLocation()+CS[Right].GetLocation())*.5,Y)*Sign/(Radius*.4));
            const double FrontGate=Smooth((Local.X+Radius*.12)/(Radius*.4));
            const double Distance=FMath::Exp(-(Local-Mean).SizeSquared()/(Radius*Radius*3));
            R.RegionWeights[V]=FMath::Clamp((Weights[V].FindRef(Pec)*1.5+MorphEvidence[V]*.15)*Allowed*SideGate*FrontGate*Distance,0.,1.);
        }
        for(int32 Pass=0;Pass<8;++Pass)
        {
            const auto Previous=R.RegionWeights;
            for(int32 V=0;V<I.Vertices.Num();++V)
            {
                double Sum=Previous[V],Count=1;for(int32 N:Adj[V]) { Sum+=Previous[N];++Count; }
                // Diffusion cannot spread into vertices without source pectoral support.
                R.RegionWeights[V]=FMath::Min(float(.5*Previous[V]+.5*Sum/Count),float(Smooth(Weights[V].FindRef(Pec)*8)));
            }
        }
        const FMeasure M=Measure(I,I.Vertices,R.RegionWeights,Anchor,Sign);
        if(M.Volume<=.01 || M.Radius<=.01) return Fail(TEXT("Degenerate effective breast geometry"));
        R.COM=M.COM;R.EffectiveVolumeCm3=M.Volume;R.EffectiveRadiusCm=M.Radius;R.EffectiveDepthCm=M.Depth;R.SupportAreaCm2=M.Area;R.MassKg=M.Volume*P->DensityKgPerCm3;R.ReferenceMassKg=R.MassKg;
        // Imported p0 is the zero-offset shape in its authored upright orientation.
        R.ImportedGravityLocal=Anchor.InverseTransformVectorNoScale(-Z*980.);
        R.AnchorBone=I.Bones.Num();FVamBuildBone AB;AB.Name=FName(*(R.Side.ToString()+TEXT("_BreastAnchor")));AB.Parent=Chest;AB.LocalBind=R.AnchorLocal;I.Bones.Add(AB);
        R.SizeCm=M.Size;R.RootSizeCm=M.RootSize;
        for(int32 N=0;N<5;++N)
        {
            FVamBreastNodeParameters Node;Node.Semantic=Semantics[N];Node.BoneIndex=I.Bones.Num();Node.Rest=M.Nodes[N];Node.EffectiveVolumeCm3=M.NodeVolumes[N];Node.MassCenter=M.MassCenters[N];
            R.Nodes.Add(Node);FVamBuildBone B;B.Name=FName(*(R.Side.ToString()+TEXT("_Breast_")+Semantics[N].ToString()));B.Parent=R.AnchorBone;B.LocalBind=FTransform(Node.Rest);I.Bones.Add(B);
        }
        VamBreastCalibration::Calibrate(R,P->DensityKgPerCm3,P->EffectiveModulusPa);
        for(int32 V=0;V<I.Vertices.Num();++V) R.RegionPoints.Add(Anchor.InverseTransformPosition(I.Vertices[V]));
        for(const auto& Parameter:Source->Parameters)
        {
            if(Parameter.Group==TEXT("Expression")) continue;
            const auto* Morph=I.Morphs.FindByPredicate([&](const FVamBuildMorph& M){return M.Name==Parameter.Target;});
            TArray<FVector> Changed=I.Vertices;double Evidence=0;
            if(Morph) for(int32 V=0;V<Changed.Num();++V) { Changed[V]+=Morph->Deltas[V];Evidence+=Morph->Deltas[V].SizeSquared()*R.RegionWeights[V]; }
            TArray<FTransform> NextCS;
            for(int32 B=0;B<P->SourceBoneCount;++B)
            {
                FTransform Local=I.Bones[B].LocalBind;
                for(const auto& Delta:Parameter.BoneCenters) if(Delta.BoneIndex==B) Local.AddToTranslation(Delta.LocalTranslation);
                NextCS.Add(I.Bones[B].Parent<0 ? Local : Local*NextCS[I.Bones[B].Parent]);
            }
            const FTransform NextAnchor(Frame,NextCS[Pec].GetLocation());
            const FVector AnchorDelta=NextAnchor.GetRelativeTransform(NextCS[Chest]).GetLocation()-R.AnchorLocal.GetLocation();
            if(Evidence<1.e-10 && AnchorDelta.IsNearlyZero()) continue;
            const FMeasure Next=Measure(I,Changed,R.RegionWeights,NextAnchor,Sign);
            FVamBreastShapeResponse Response;Response.Parameter=Parameter.Target;Response.DefaultValue=Parameter.DefaultValue;
            Response.AnchorTranslationDelta=AnchorDelta;Response.DepthDelta=Next.Depth-M.Depth;Response.SupportAreaDelta=Next.Area-M.Area;
            Response.LogVolumeSlope=FMath::Loge(FMath::Max(1.e-6,Next.Volume/M.Volume));Response.COMDelta=Next.COM-M.COM;Response.RadiusDelta=Next.Radius-M.Radius;
            for(int32 N=0;N<5;++N) Response.NodeDeltas.Add(Next.Nodes[N]-M.Nodes[N]);
            Response.SizeDelta=Next.Size-M.Size;Response.RootSizeDelta=Next.RootSize-M.RootSize;
            for(int32 N=0;N<5;++N) { Response.NodeVolumeLogSlopes.Add(FMath::Loge(FMath::Max(1.e-8,Next.NodeVolumes[N]/FMath::Max(1.e-8,M.NodeVolumes[N]))));Response.NodeMassCenterDeltas.Add(Next.MassCenters[N]-M.MassCenters[N]); }
            R.ShapeResponses.Add(Response);
        }
        P->Sides.Add(R);
    }
    // Continuous geometry-driven redistribution. Only approved donor weights may be compressed.
    for(int32 V=0;V<I.Vertices.Num();++V)
    {
        for(int32 Side=0;Side<2;++Side)
        {
            const auto& R=P->Sides[Side];const double Region=R.RegionWeights[V];if(Region<1.e-5) continue;
            const int32 Other=Side==0 ? Right : Left;
            const FVector Local=R.RegionPoints[V];
            const double RootFade=Smooth(Local.X/FMath::Max(.1,R.EffectiveDepthCm));
            const double Lower=Smooth((R.COM.Z-Local.Z)/FMath::Max(.1,R.SizeCm.Z*.4));
            const double Lateral=Smooth(FMath::Abs(Local.Y-R.COM.Y)/FMath::Max(.1,R.SizeCm.Y*.5));
            const double Transfer=Region*RootFade*(.55+.25*RootFade+.1*FMath::Max(Lower,Lateral));
            TArray<TPair<int32,double>> Added;double Sum=0;
            for(int32 N=0;N<5;++N)
            {
                const double Bias=N==0 ? 2. : (N==1 || N==3 ? .5 : 1.2);
                const double W=Bias*FMath::Exp(-(Local-R.Nodes[N].Rest).SizeSquared()/FMath::Square(R.EffectiveRadiusCm*.7));
                Added.Emplace(R.Nodes[N].BoneIndex,W);Sum+=W;
            }
            Added.Sort([](const auto& A,const auto& B){return A.Value>B.Value;});
            TSet<int32> AllowedDonors=Donors;AllowedDonors.Remove(Other);
            const bool Saturated=Weights[V].Num()>=8;
            const int32 Compressed=VamBreastWeights::Redistribute(Weights[V],AllowedDonors,Added,Transfer);
            if(Compressed>0) ++P->CompressedDonorVertices;
            if(Saturated && Compressed>=0) ++P->SaturatedVerticesWithHelpers;
            if(Saturated && Compressed<0) ++P->SaturatedVerticesWithoutEligibleDonors;
        }
        double Sum=0;for(const auto& W:Weights[V]) Sum+=W.Value;
        for(auto& W:Weights[V]) { W.Value/=Sum;if(W.Key>=P->SourceBoneCount) P->MeanHelperWeight+=W.Value; }
    }
    P->MeanHelperWeight/=Weights.Num();
    I.Influences.Reset();for(int32 V=0;V<Weights.Num();++V) for(const auto& W:Weights[V]) { FVamBuildInfluence F;F.Vertex=V;F.Bone=W.Key;F.Weight=W.Value;I.Influences.Add(F); }
    P->RegionProvenance=TEXT("Source pectoral support + persisted topology adjacency/seam identity + morph delta evidence + chest frame and mirrored side gates; 8 diffusion passes. Effective volume is a surface-to-support cone proxy, not anatomical volume.");
    P->SkinWeightIdentity=FMD5::HashAnsiString(*(P->BuildAlgorithmVersion+P->SourceTopologyIdentity+Source->SourceDigest+Source->BindSignature));
    auto* Body=UVamNativeBuilder::BuildMesh(Root+TEXT("/SK_Body"),I,Error);if(!Body) return nullptr;
    auto* Result=CopyAsset(Source,Root+TEXT("/CD_Character"));auto* NewShape=CopyAsset(Shape,Root+TEXT("/SD_Shape"));auto* NewGeometry=CopyAsset(Geometry,Root+TEXT("/GD_Bindings"));
    if(!Result || !NewShape || !NewGeometry) return Fail(TEXT("Immutable destination conflict"));
    Result->Body=Body;Result->Skeleton=Body->GetSkeleton();Result->Shape=NewShape;Result->SkeletonExtensionVersion=P->BuildAlgorithmVersion;Result->Parts.Reset();
    NewShape->Geometry=NewGeometry;NewGeometry->RenderToInput=UVamNativeBuilder::GetRenderToInputMap(Body);
    if(NewShape->NeutralLocalBind.Num()!=P->SourceBoneCount) return Fail(TEXT("Neutral source bind count mismatch"));
    for(int32 B=P->SourceBoneCount;B<I.Bones.Num();++B) NewShape->NeutralLocalBind.Add(I.Bones[B].LocalBind);
    for(int32 Part=0;Part<Source->Parts.Num();++Part)
    {
        FVamNativeMeshInput PartInput;if(!Extract(Source->Parts[Part].LoadSynchronous(),PartInput,Error)) return nullptr;
        PartInput.Bones=I.Bones;
        auto* Mesh=UVamNativeBuilder::BuildMesh(Root+FString::Printf(TEXT("/SK_Part_%d"),Part),PartInput,Error);
        if(!Mesh || !UVamNativeBuilder::ShareCompatibleSkeleton(Mesh,Body)) return Fail(TEXT("Part helper hierarchy build failed: ")+Source->Parts[Part].ToString()+TEXT(": ")+Error);
        Result->Parts.Add(Mesh);
    }
    P->MarkPackageDirty();Result->MarkPackageDirty();
    Error=Validate(Result,P);return Error.IsEmpty() ? Result : nullptr;
}
UAnimSequence* UVamBreastJiggleBuilder::CopyAnimation(const FString& Path,UAnimSequence* Source,USkeleton* Skeleton)
{
    auto* A=CopyAsset(Source,Path);if(A) { A->SetSkeleton(Skeleton);A->MarkPackageDirty(); }return A;
}
FString UVamBreastJiggleBuilder::Validate(UVamCharacterDefinition* D,UVamBreastJiggleProfile* P)
{
    if(!D || !P || !P->IsValidProfile()) return TEXT("Invalid breast profile");
    auto* Mesh=D->Body.LoadSynchronous();if(!Mesh) return TEXT("Missing body");
    const auto& Ref=Mesh->GetRefSkeleton();
    if(Ref.GetRawBoneNum()<P->SourceBoneCount+12) return TEXT("Helper count mismatch");
    for(const auto& S:P->Sides)
    {
        if(Ref.GetParentIndex(S.AnchorBone)!=S.ChestBone) return TEXT("Invalid helper anchor parent");
        for(const auto& N:S.Nodes) if(Ref.GetParentIndex(N.BoneIndex)!=S.AnchorBone) return TEXT("Invalid semantic node parent");
    }
    TArray<FTransform> BindCS=Ref.GetRefBonePose();
    for(int32 I=0;I<BindCS.Num();++I) if(Ref.GetParentIndex(I)>=0) BindCS[I]=BindCS[I]*BindCS[Ref.GetParentIndex(I)];
    for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections) for(const auto& V:Section.SoftVertices)
    {
        int32 Count=0;uint32 Sum=0;for(int32 I=0;I<MAX_TOTAL_INFLUENCES;++I) if(V.InfluenceWeights[I]) { ++Count;Sum+=V.InfluenceWeights[I]; }
        if(Count>8 || FMath::Abs(int32(Sum)-65535)>8) return TEXT("Invalid final influence count/normalization");
        FVector Reconstructed=FVector::ZeroVector;
        for(int32 I=0;I<MAX_TOTAL_INFLUENCES;++I) if(V.InfluenceWeights[I])
        {
            const int32 Bone=Section.BoneMap[V.InfluenceBones[I]];
            const FVector Local=FVector(Mesh->GetRefBasesInvMatrix()[Bone].TransformPosition(V.Position));
            Reconstructed+=BindCS[Bone].TransformPosition(Local)*(double(V.InfluenceWeights[I])/Sum);
        }
        if(!Reconstructed.Equals(FVector(V.Position),.001)) return TEXT("Zero-offset bind reconstruction error > 0.001 cm");
    }
    if(!D->Shape.LoadSynchronous() || D->Shape.Get()->NeutralLocalBind.Num()!=Ref.GetRawBoneNum()) return TEXT("Shape helper identity mismatch");
    return FString();
}


bool UVamBreastJiggleBuilder::ExcludeRigidHelpers(UPhysicsAsset* Physics,USkeletalMesh* Mesh,UVamBreastJiggleProfile* Profile)
{
    if(!Physics || !Mesh || !Profile) return false;
    const auto& Ref=Mesh->GetRefSkeleton();
    TMap<int32,int32> IndexMap;int32 Next=0;
    for(int32 I=0;I<Physics->SkeletalBodySetups.Num();++I)
        if(Ref.FindBoneIndex(Physics->SkeletalBodySetups[I]->BoneName)<Profile->SourceBoneCount) IndexMap.Add(I,Next++);
    TMap<FRigidBodyIndexPair,bool> Disabled;
    for(const auto& Pair:Physics->CollisionDisableTable)
    {
        const int32* A=IndexMap.Find(Pair.Key.Indices[0]);const int32* B=IndexMap.Find(Pair.Key.Indices[1]);
        if(A && B) Disabled.Add(FRigidBodyIndexPair(*A,*B),Pair.Value);
    }
    Physics->ConstraintSetup.RemoveAll([&](const UPhysicsConstraintTemplate* C){return Ref.FindBoneIndex(C->DefaultInstance.GetChildBoneName())>=Profile->SourceBoneCount || Ref.FindBoneIndex(C->DefaultInstance.GetParentBoneName())>=Profile->SourceBoneCount;});
    Physics->SkeletalBodySetups.RemoveAll([&](const USkeletalBodySetup* B){return Ref.FindBoneIndex(B->BoneName)>=Profile->SourceBoneCount;});
    Physics->CollisionDisableTable=MoveTemp(Disabled);Physics->UpdateBodySetupIndexMap();Physics->UpdateBoundsBodiesArray();Physics->MarkPackageDirty();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastExtractTest,"Vam.Breast.OrphanVertexExtraction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastExtractTest::RunTest(const FString& Parameters)
{
    FMeshDescription D;FSkeletalMeshAttributes A(D);A.Register();A.GetVertexInstanceUVs().SetNumChannels(1);
    const auto Used=D.CreateVertex(),Orphan=D.CreateVertex();A.GetVertexPositions()[Used]=FVector3f(0,0,0);A.GetVertexPositions()[Orphan]=FVector3f(1,0,0);
    const auto Instance=D.CreateVertexInstance(Used);A.GetVertexInstanceNormals()[Instance]=FVector3f(0,0,1);A.GetVertexInstanceUVs().Set(Instance,0,FVector2f(.25,.75));
    auto* Mesh=NewObject<USkeletalMesh>();Mesh->AddLODInfo();Mesh->CreateMeshDescription(0,MoveTemp(D));
    FVamNativeMeshInput Input;FString Error;
    // Seed the target with invalid old values so this test cannot pass by allocator luck.
    Input.UV.Init(FVector2D(std::numeric_limits<double>::quiet_NaN()),2);
    TestTrue(TEXT("Extract retains unused source correspondence"),Extract(Mesh,Input,Error) && Input.Vertices.Num()==2 && Input.SourceVertices[1]==1);
    TestTrue(TEXT("Used vertex UV preserved"),Input.UV[0].Equals(FVector2D(.25,.75),1.e-8));
    TestTrue(TEXT("Unused UV deterministic and finite"),Input.UV[1]==FVector2D::ZeroVector);
    return true;
}
#endif
