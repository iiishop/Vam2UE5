#include "VamNativeBuilder.h"
#include "AssetUtils/CreateSkeletalMeshUtil.h"
#include "Animation/Skeleton.h"
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
    if (!Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Path)) return Fail(TEXT("Invalid /Game asset path"));
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
    Options.SourceMeshes.MeshDescriptions.Add(&Description);
    Options.NumMaterialSlots=I.Materials.Num();
    for (auto Material : I.Materials) Options.AssetMaterials.Add(Material);
    UE::AssetUtils::FSkeletalMeshResults Result;
    if (UE::AssetUtils::CreateSkeletalMeshAsset(Options, Result) != UE::AssetUtils::ECreateSkeletalMeshResult::Ok)
        return Fail(TEXT("UE skeletal mesh construction failed; not committed"));
    const auto Map = GetRenderToInputMap(Result.SkeletalMesh);
    if (Map.IsEmpty()) return Fail(TEXT("Missing final render correspondence; not committed"));
    for (int32 V : Map) if (!I.Vertices.IsValidIndex(V)) return Fail(TEXT("Final render correspondence out of range; not committed"));
    if (Result.SkeletalMesh->GetMorphTargets().Num()!=I.Morphs.Num()) return Fail(TEXT("UE morph build mismatch; not committed"));
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

UVamCharacterDefinition* UVamNativeBuilder::CreateDefinition(const FString& Path, USkeletalMesh* Body,
    const TArray<FVamMorphParameter>& Parameters, const FString& Identity, const FString& Digest, const FString& Bind)
{
    if (!Body || !Body->GetSkeleton() || GetRenderToInputMap(Body).IsEmpty() ||
        Identity.IsEmpty() || Digest.IsEmpty() || Bind.IsEmpty() ||
        !Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Path) ||
        FPackageName::DoesPackageExist(Path) || FindPackage(nullptr, *Path)) return nullptr;
    TSet<FName> Names;
    for (const auto& P : Parameters)
    {
        if (Names.Contains(P.Target) || !Body->FindMorphTarget(P.Target) ||
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
