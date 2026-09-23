#include "VamNativeBuilder.h"
#include "VamSourceMapping.h"
#include "AssetUtils/CreateSkeletalMeshUtil.h"
#include "Animation/Skeleton.h"
#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "SkeletalMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

USkeletalMesh* UVamNativeBuilder::BuildMesh(const FString& Path, const FVamNativeMeshInput& I, FString& Error)
{
    auto Fail = [&Error](const TCHAR* Reason) -> USkeletalMesh* { Error = Reason; return nullptr; };
    Error.Empty();
    const FString SkeletonPath = Path + TEXT("_Skeleton");
    if ((!Path.StartsWith(TEXT("/Game/")) && !Path.StartsWith(TEXT("/VamResourceBrowser/Examples/"))) || !FPackageName::IsValidLongPackageName(Path)) return Fail(TEXT("Invalid project or original example asset path"));
    if (FPackageName::DoesPackageExist(Path) || FPackageName::DoesPackageExist(SkeletonPath) ||
        FindPackage(nullptr, *Path) || FindPackage(nullptr, *SkeletonPath)) return Fail(TEXT("Asset conflict: existing packages are never overwritten"));
    const int32 Count = I.Vertices.Num();
    if (!Count || I.Normals.Num() != Count || I.UV.Num() != Count || I.SourceVertices.Num() != Count ||
        !I.Triangles.Num() || I.Triangles.Num() % 3 || I.TriangleMaterials.Num() != I.Triangles.Num()/3 ||
        !I.Bones.Num() || !I.Materials.Num()) return Fail(TEXT("Invalid mesh array lengths"));
    TSet<FName> Names;
    for (int32 B=0; B<I.Bones.Num(); ++B)
    {
        const auto& Bone = I.Bones[B];
        if (Bone.Name.IsNone() || Names.Contains(Bone.Name) || Bone.Parent >= B || Bone.Parent < -1 ||
            (B>0 && Bone.Parent<0) || Bone.LocalBind.ContainsNaN() || !Bone.LocalBind.IsRotationNormalized() ||
            !Bone.LocalBind.GetScale3D().Equals(FVector::OneVector, 1.e-6)) return Fail(TEXT("Invalid hierarchy or bind transform"));
        Names.Add(Bone.Name);
    }
    for (int32 V=0; V<Count; ++V)
        if (I.Vertices[V].ContainsNaN() || I.Normals[V].ContainsNaN() || I.Normals[V].IsNearlyZero() ||
            I.UV[V].ContainsNaN() || I.SourceVertices[V]<0) return Fail(TEXT("Invalid vertex, normal, UV or correspondence"));
    for (int32 T=0; T<I.Triangles.Num(); T+=3)
    {
        for (int32 K=0; K<3; ++K) if (!I.Vertices.IsValidIndex(I.Triangles[T+K])) return Fail(TEXT("Triangle index out of bounds"));
        if (!I.Materials.IsValidIndex(I.TriangleMaterials[T/3])) return Fail(TEXT("Material index out of bounds"));
        if (FVector::CrossProduct(I.Vertices[I.Triangles[T+1]]-I.Vertices[I.Triangles[T]], I.Vertices[I.Triangles[T+2]]-I.Vertices[I.Triangles[T]]).IsNearlyZero())
            return Fail(TEXT("Degenerate triangle must be diagnosed before native build"));
    }
    TArray<TArray<UE::AnimationCore::FBoneWeight>> Weights; Weights.SetNum(Count);
    TArray<double> Totals; Totals.Init(0., Count);
    TSet<uint64> Pairs;
    for (const auto& W : I.Influences)
    {
        if (!Weights.IsValidIndex(W.Vertex) || !I.Bones.IsValidIndex(W.Bone) || !FMath::IsFinite(W.Weight) || W.Weight<=0 || W.Weight>1)
            return Fail(TEXT("Invalid influence"));
        const uint64 Key = (uint64(W.Vertex)<<32) | uint32(W.Bone);
        if (Pairs.Contains(Key)) return Fail(TEXT("Duplicate influences require explicit source reduction"));
        Pairs.Add(Key); Totals[W.Vertex] += W.Weight;
        Weights[W.Vertex].Add(UE::AnimationCore::FBoneWeight(W.Bone, W.Weight));
    }
    for (int32 V=0; V<Count; ++V)
        if (Weights[V].Num()>8 || FMath::Abs(Totals[V]-1.)>1.e-5) return Fail(TEXT("Unnormalized, unweighted or >8-influence vertex; no guessed fallback"));
    Names.Reset();
    for (const auto& M : I.Morphs)
    {
        if (M.Name.IsNone() || Names.Contains(M.Name) || M.Deltas.Num()!=Count) return Fail(TEXT("Invalid morph name or domain"));
        Names.Add(M.Name);
        for (auto D : M.Deltas) if (D.ContainsNaN()) return Fail(TEXT("Non-finite morph delta"));
    }
    FMeshDescription Description;
    FSkeletalMeshAttributes A(Description); A.Register(); A.RegisterImportPointIndexAttribute();
    auto Positions = A.GetVertexPositions();
    auto ImportPoints = Description.VertexAttributes().GetAttributesRef<int32>(MeshAttribute::Vertex::ImportPointIndex);
    auto Skin = A.GetVertexSkinWeights();
    for (int32 V=0; V<Count; ++V)
    {
        auto Id = Description.CreateVertex(); Positions[Id] = FVector3f(I.Vertices[V]); ImportPoints[Id] = V;
        Skin.Set(Id, MakeArrayView(Weights[V]));
    }
    for (const auto& M : I.Morphs)
    {
        A.RegisterMorphTargetAttribute(M.Name, false);
        auto Deltas = A.GetVertexMorphPositionDelta(M.Name);
        for (int32 V=0; V<Count; ++V) Deltas[FVertexID(V)] = FVector3f(M.Deltas[V]);
    }
    A.GetVertexInstanceUVs().SetNumChannels(1);
    TArray<FPolygonGroupID> Groups;
    for (int32 M=0; M<I.Materials.Num(); ++M)
    {
        auto G = Description.CreatePolygonGroup(); Groups.Add(G);
        A.GetPolygonGroupMaterialSlotNames()[G] = FName(*FString::Printf(TEXT("SourceRegion_%d"), M));
    }
    for (int32 T=0; T<I.Triangles.Num(); T+=3)
    {
        TArray<FVertexInstanceID> Corners;
        for (int32 K=0; K<3; ++K)
        {
            int32 V=I.Triangles[T+K]; auto Corner=Description.CreateVertexInstance(FVertexID(V)); Corners.Add(Corner);
            A.GetVertexInstanceUVs().Set(Corner, 0, FVector2f(I.UV[V]));
            A.GetVertexInstanceNormals()[Corner] = FVector3f(I.Normals[V].GetSafeNormal());
            A.GetVertexInstanceColors()[Corner] = FVector4f(1,1,1,1);
        }
        Description.CreateTriangle(Groups[I.TriangleMaterials[T/3]], Corners);
    }
    // A new skeleton per build: CreateSkeletalMeshAsset mutates its skeleton, so sharing here is forbidden.
    auto* Skeleton = NewObject<USkeleton>(CreatePackage(*SkeletonPath), *FPackageName::GetLongPackageAssetName(SkeletonPath), RF_Public|RF_Standalone);
    FReferenceSkeleton Ref;
    {
        FReferenceSkeletonModifier Modifier(Ref, Skeleton);
        for (const auto& B : I.Bones) Modifier.Add(FMeshBoneInfo(B.Name, B.Name.ToString(), B.Parent), B.LocalBind);
    } // Modifier must rebuild final bone/name arrays before the asset utility reads Ref.
    UE::AssetUtils::FSkeletalMeshAssetOptions Options;
    Options.NewAssetPath=Path; Options.Skeleton=Skeleton; Options.RefSkeleton=&Ref;
    // Bootstrap topology without Morphs: UE's default threshold can destroy tiny
    // targets, leaving pending-kill objects that cannot be rebuilt under the same name.
    FMeshDescription GeometryDescription(Description);
    FSkeletalMeshAttributes GeometryAttributes(GeometryDescription);
    for (const auto& M:I.Morphs) GeometryAttributes.UnregisterMorphTargetAttribute(M.Name);
    Options.SourceMeshes.MeshDescriptions.Add(&GeometryDescription);
    Options.NumMaterialSlots=I.Materials.Num();
    for (int32 M=0; M<I.Materials.Num(); ++M)
    {
        const FName Slot(*FString::Printf(TEXT("SourceRegion_%d"),M));
        Options.SkeletalMaterials.Add(FSkeletalMaterial(I.Materials[M],Slot,Slot));
    }
    UE::AssetUtils::FSkeletalMeshResults Result;
    if (UE::AssetUtils::CreateSkeletalMeshAsset(Options, Result) != UE::AssetUtils::ECreateSkeletalMeshResult::Ok)
        return Fail(TEXT("UE skeletal mesh construction failed; not committed"));
    // UE's default 0.015 cm Morph threshold discards valid small clothing deltas.
    // Rebuild from the retained MeshDescription with our recorded precision policy.
    Result.SkeletalMesh->GetLODInfo(0)->BuildSettings.MorphThresholdPosition=1.e-6f;
    Result.SkeletalMesh->CreateMeshDescription(0,MoveTemp(Description));
    Result.SkeletalMesh->CommitMeshDescription(0);
    Result.SkeletalMesh->Build();
    const auto Map = GetRenderToInputMap(Result.SkeletalMesh);
    TArray<int32> Expected, Actual; Expected.Init(0,I.Materials.Num()); Actual.Init(0,I.Materials.Num());
    for (int32 Slot:I.TriangleMaterials) ++Expected[Slot];
    for (const auto& Section:Result.SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
    {
        if (!Actual.IsValidIndex(Section.MaterialIndex)) return Fail(TEXT("Built material index outside source slots"));
        Actual[Section.MaterialIndex]+=Section.NumTriangles;
    }
    if (Expected!=Actual) return Fail(TEXT("Built section material assignment differs from source triangle regions"));
    if (Map.IsEmpty()) return Fail(TEXT("Missing final render correspondence; not committed"));
    for (int32 V : Map) if (!I.Vertices.IsValidIndex(V)) return Fail(TEXT("Final render correspondence out of range; not committed"));
    if (Result.SkeletalMesh->GetMorphTargets().Num()!=I.Morphs.Num()) return Fail(TEXT("UE morph build mismatch; not committed"));
    // Validate in the FINAL split render domain, including UV/material copies.
    for (const auto& SourceMorph:I.Morphs)
    {
        const auto* Built=Result.SkeletalMesh->FindMorphTarget(SourceMorph.Name);
        if (!Built) return Fail(TEXT("Missing final morph"));
        TArray<FVector3f> FinalDeltas; FinalDeltas.Init(FVector3f::ZeroVector,Map.Num());
        for (const auto& D:Built->GetMorphTargetDeltas(0))
        {
            if (!Map.IsValidIndex(D.SourceIdx)) return Fail(TEXT("Morph render index outside correspondence"));
            FinalDeltas[D.SourceIdx]=D.PositionDelta;
        }
        for (int32 V=0;V<Map.Num();++V)
            // MeshDescription import identifies changed points at 1e-4 cm, even
            // when MorphThresholdPosition is smaller. Keep this one-micron bound explicit.
            if (!FVector(FinalDeltas[V]).Equals(SourceMorph.Deltas[Map[V]],1.e-4))
            {
                Error=FString::Printf(TEXT("Final morph %s vertex %d input %d expected %s actual %s"),*SourceMorph.Name.ToString(),V,Map[V],*SourceMorph.Deltas[Map[V]].ToString(),*FVector(FinalDeltas[V]).ToString());
                return nullptr;
            }
    }
    for (const auto& Section:Result.SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
        for (int32 V=0;V<Section.SoftVertices.Num();++V)
        {
            const auto& Final=Section.SoftVertices[V]; const int32 Input=Map[Section.BaseVertexIndex+V];
            if (!FVector(Final.Position).Equals(I.Vertices[Input],2.e-5) ||
                !FVector2D(Final.UVs[0]).Equals(I.UV[Input],1.e-6)) return Fail(TEXT("Final vertex/UV correspondence differs from source"));
            TMap<int32,double> ExpectedWeights,FinalWeights;
            for (const auto& Weight:Weights[Input]) ExpectedWeights.Add(Weight.GetBoneIndex(),Weight.GetWeight());
            for (int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if (Final.InfluenceWeights[K])
            {
                if (!Section.BoneMap.IsValidIndex(Final.InfluenceBones[K])) return Fail(TEXT("Final influence bone outside section map"));
                FinalWeights.Add(Section.BoneMap[Final.InfluenceBones[K]],double(Final.InfluenceWeights[K])/65535.);
            }
            // Up to eight packed influences are renormalized by UE during import.
            for (const auto& W:ExpectedWeights) if (FMath::Abs(FinalWeights.FindRef(W.Key)-W.Value)>8./65535.)
            {
                Error=FString::Printf(TEXT("Final skin input %d bone %d expected %.9g actual %.9g source count %d built count %d"),Input,W.Key,W.Value,FinalWeights.FindRef(W.Key),ExpectedWeights.Num(),FinalWeights.Num());
                return nullptr;
            }
            for (const auto& W:FinalWeights) if (!ExpectedWeights.Contains(W.Key)) return Fail(TEXT("Unexpected final skin influence"));
        }
    FAssetRegistryModule::AssetCreated(Skeleton);
    FAssetRegistryModule::AssetCreated(Result.SkeletalMesh);
    Skeleton->MarkPackageDirty(); Result.SkeletalMesh->MarkPackageDirty();
    return Result.SkeletalMesh;
}

TArray<int32> UVamNativeBuilder::GetRenderToInputMap(USkeletalMesh* Mesh)
{
    if (!Mesh || !Mesh->GetImportedModel() || !Mesh->GetImportedModel()->LODModels.Num()) return {};
    return Mesh->GetImportedModel()->LODModels[0].MeshToImportVertexMap;
}

bool UVamNativeBuilder::ShareCompatibleSkeleton(USkeletalMesh* Part, USkeletalMesh* Body)
{
    if (!Part || !Body || !Body->GetSkeleton()) return false;
    const auto& A=Part->GetRefSkeleton(); const auto& B=Body->GetRefSkeleton();
    if (A.GetNum()!=B.GetNum()) return false;
    for (int32 I=0; I<A.GetNum(); ++I)
        if (A.GetBoneName(I)!=B.GetBoneName(I) || A.GetParentIndex(I)!=B.GetParentIndex(I) ||
            !A.GetRefBonePose()[I].Equals(B.GetRefBonePose()[I],1.e-6)) return false;
    Part->SetSkeleton(Body->GetSkeleton()); Part->MarkPackageDirty(); return true;
}

void UVamNativeBuilder::SetBuildLimitations(UVamCharacterDefinition* Definition, const TArray<FString>& Limitations)
{
    if (Definition) { Definition->Limitations=Limitations; Definition->MarkPackageDirty(); }
}

void UVamNativeBuilder::SetAppearanceBaseline(UVamCharacterDefinition* Definition)
{
    if (Definition) { Definition->ShapeConvention=TEXT("appearance_plus_parameter_offsets"); Definition->MarkPackageDirty(); }
}

UVamSourceMapping* UVamNativeBuilder::CreateSourceMapping(const FString& Path, const FString& SourceDigest,
    const FString& BindSignature, const FString& SourceIR, const FString& MaterialIR,
    const FString& Contract, const TArray<int32>& RenderToInput, const TArray<int32>& InputToSource)
{
    if (!Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Path) ||
        FPackageName::DoesPackageExist(Path) || FindPackage(nullptr,*Path)) return nullptr;
    for (int32 V:RenderToInput) if (!InputToSource.IsValidIndex(V)) return nullptr;
    auto* Mapping=NewObject<UVamSourceMapping>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);
    Mapping->SourceDigest=SourceDigest; Mapping->BindSignature=BindSignature;
    Mapping->SourceIRJson=SourceIR; Mapping->MaterialIRJson=MaterialIR; Mapping->NativeContractJson=Contract;
    Mapping->RenderToInputVertex=RenderToInput; Mapping->InputToSourceVertex=InputToSource;
    FAssetRegistryModule::AssetCreated(Mapping); Mapping->MarkPackageDirty(); return Mapping;
}

UVamCharacterDefinition* UVamNativeBuilder::CreateDefinition(const FString& Path, USkeletalMesh* Body,
    const TArray<FVamMorphParameter>& Parameters, const FString& Identity, const FString& Digest, const FString& Bind)
{
    if (!Body || !Body->GetSkeleton() || GetRenderToInputMap(Body).IsEmpty() ||
        Identity.IsEmpty() || Digest.IsEmpty() || Bind.IsEmpty() ||
        (!Path.StartsWith(TEXT("/Game/")) && !Path.StartsWith(TEXT("/VamResourceBrowser/Examples/"))) || !FPackageName::IsValidLongPackageName(Path) ||
        FPackageName::DoesPackageExist(Path) || FindPackage(nullptr, *Path)) return nullptr;
    TSet<FName> Names;
    for (const auto& P : Parameters)
    {
        if (Names.Contains(P.Target) || (!Body->FindMorphTarget(P.Target) && P.BoneCenters.IsEmpty()) ||
            !FMath::IsFinite(P.Minimum) || !FMath::IsFinite(P.Maximum) || !FMath::IsFinite(P.DefaultValue) ||
            P.Minimum>P.DefaultValue || P.DefaultValue>P.Maximum) return nullptr;
        Names.Add(P.Target);
    }
    auto* Definition = NewObject<UVamCharacterDefinition>(CreatePackage(*Path), *FPackageName::GetLongPackageAssetName(Path), RF_Public|RF_Standalone);
    Definition->Body=Body; Definition->Skeleton=Body->GetSkeleton(); Definition->Parameters=Parameters;
    Definition->SourceIdentity=Identity; Definition->SourceDigest=Digest; Definition->BindSignature=Bind;
    Definition->bBuildVerified=true;
    FAssetRegistryModule::AssetCreated(Definition); Definition->MarkPackageDirty();
    return Definition;
}
