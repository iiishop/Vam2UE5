#include "VamGluteCorrectiveProfile.h"
#include "VamCharacterDefinition.h"
#include "VamGluteStructureProfile.h"
#include "VamLegJiggleProfile.h"
#include "VamBreastJiggleProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/UnrealType.h"

namespace
{
struct FDigest
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer;
    FDigest():Writer(Bytes){}
    template<class T> void Add(const T& V){Writer.Serialize(const_cast<T*>(&V),sizeof(T));}
    void Text(const FString& V){FString S=V;Writer<<S;}
    void Vector(const FVector& V){Add(V.X);Add(V.Y);Add(V.Z);}
    void Object(UObject* O,const TSet<FName>* Only=nullptr)
    {
        if(!O){Text(TEXT("missing"));return;}
        Text(O->GetClass()->GetPathName());
        TArray<FProperty*> Fields;
        for(TFieldIterator<FProperty> It(O->GetClass());It;++It) if(!(Cast<UVamLegJiggleProfile>(O) && Cast<UVamLegJiggleProfile>(O)->SchemaVersion<3 && It->GetFName()==TEXT("ChainBoneIndices")) && !It->HasAnyPropertyFlags(CPF_Transient|CPF_EditorOnly)&&(!Only||Only->Contains(It->GetFName())))Fields.Add(*It);
        Fields.Sort([](const FProperty& A,const FProperty& B){return A.GetName()<B.GetName();});
        FObjectAndNameAsStringProxyArchive Archive(Writer,false);
        for(auto* P:Fields){Text(P->GetName());FStructuredArchiveFromArchive SA(Archive);P->SerializeItem(SA.GetSlot(),P->ContainerPtrToValuePtr<void>(O),nullptr);}
    }
    FString End(){FMD5 H;H.Update(Bytes.GetData(),Bytes.Num());uint8 D[16];H.Final(D);return BytesToHex(D,16);}
};
bool MeshData(USkeletalMesh* Mesh,TMap<FString,FString>& Out,const FString& Prefix,FString& Error)
{
    if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty()){Error=TEXT("mesh render data missing");return false;}
    FDigest Weights,Bind,Topology,Morphs;
    const auto& Ref=Mesh->GetRefSkeleton();Bind.Add(Ref.GetNum());
    for(int B=0;B<Ref.GetNum();++B){Bind.Text(Ref.GetBoneName(B).ToString());Bind.Add(Ref.GetParentIndex(B));const auto M=Ref.GetRefBonePose()[B].ToMatrixWithScale();for(int R=0;R<4;++R)for(int C=0;C<4;++C)Bind.Add(M.M[R][C]);}
    // Hash actual quantized render weights, including section-local bone maps and all LODs.
    for(const auto& L:Mesh->GetResourceForRendering()->LODRenderData)
    {
        const auto& W=L.SkinWeightVertexBuffer;
        if(!W.GetDataVertexBuffer()->GetWeightData()){Error=TEXT("actual CPU weight data unavailable; rebuild with CPU access");return false;}
        Weights.Add(W.GetNumVertices());Weights.Add(W.GetMaxBoneInfluences());
        for(const auto& Section:L.RenderSections){Weights.Add(Section.BaseVertexIndex);Weights.Add(Section.NumVertices);for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V)for(uint32 K=0;K<W.GetMaxBoneInfluences();++K){const auto Raw=W.GetBoneWeight(V,K);Weights.Add(Raw);const int Bone=Raw?Section.BoneMap[W.GetBoneIndex(V,K)]:INDEX_NONE;Weights.Add(Bone);}}
        for(const auto& Section:L.RenderSections){Topology.Add(bool(Section.bRecomputeTangent));Topology.Add(uint8(Section.RecomputeTangentsVertexMaskChannel));}
        const auto& P=L.StaticVertexBuffers.PositionVertexBuffer;Topology.Add(P.GetNumVertices());for(uint32 V=0;V<P.GetNumVertices();++V)Topology.Vector(FVector(P.VertexPosition(V)));
        const auto* Indices=L.MultiSizeIndexContainer.GetIndexBuffer();Topology.Add(Indices->Num());for(int I=0;I<Indices->Num();++I)Topology.Add(Indices->Get(I));
    }
    for(int32 LOD=0;LOD<Mesh->GetLODNum();++LOD)Topology.Add(uint8(Mesh->GetLODInfo(LOD)->SkinCacheUsage));
    TArray<UMorphTarget*> Targets=Mesh->GetMorphTargets();Targets.Sort([](const UMorphTarget& A,const UMorphTarget& B){return A.GetName()<B.GetName();});
    for(auto* M:Targets){Morphs.Text(M->GetName());for(const auto& L:M->GetMorphLODModels()){Morphs.Add(L.Vertices.Num());for(const auto& V:L.Vertices){Morphs.Add(V.SourceIdx);Morphs.Vector(FVector(V.PositionDelta));Morphs.Vector(FVector(V.TangentZDelta));}}}
    Out.Add(Prefix+TEXT("weights"),Weights.End());Out.Add(Prefix+TEXT("bind"),Bind.End());Out.Add(Prefix+TEXT("topology"),Topology.End());Out.Add(Prefix+TEXT("morphs"),Morphs.End());return true;
}
}
TMap<FString,FString> UVamGluteCorrectiveProfile::ComputeFinalData(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamLegJiggleProfile* L,UVamBreastJiggleProfile* B,FString& Error) const
{
    TMap<FString,FString> Out;Error.Reset();
    if(!D||!G||!L||!B){Error=TEXT("missing final definition/helper profiles");return Out;}
    if(!MeshData(D->Body.LoadSynchronous(),Out,TEXT("final_"),Error))return {};
    auto* Source=OriginalInfluenceDefinition.LoadSynchronous();if(!Source||!MeshData(Source->Body.LoadSynchronous(),Out,TEXT("source_"),Error)){Error=TEXT("retained original influence asset unavailable: ")+Error;return {};}
    FDigest Helpers;Helpers.Object(G);Helpers.Object(L);Helpers.Object(B);Out.Add(TEXT("helper_definitions"),Helpers.End());
    FDigest Shape;auto* S=D->Shape.LoadSynchronous();Shape.Object(S);if(S)Shape.Object(S->Geometry.LoadSynchronous());if(S && S->SchemaVersion>=2){const TSet<FName> FormulaFields={TEXT("Parameters")};Shape.Object(D,&FormulaFields);}Out.Add(TEXT("shape_reference_and_mapping"),Shape.End());
    FDigest SourceData;SourceData.Text(SourceFamilyJson);auto* SS=Source->Shape.LoadSynchronous();SourceData.Object(SS);if(SS)SourceData.Object(SS->Geometry.LoadSynchronous());Out.Add(TEXT("source_corrective_data"),SourceData.End());
    TSet<FName> PolicyFields={TEXT("Targets"),TEXT("MetricDegrees"),TEXT("BlendWidth"),TEXT("MaximumDimensionFraction"),TEXT("BuildDimensions"),TEXT("Bases"),TEXT("AuthoredReplay")};
    if(Algorithm==TEXT("glute-corrective-residual-only-v2")){PolicyFields.Add(TEXT("Algorithm"));PolicyFields.Add(TEXT("FixedAuthoredReplay"));}
    // Preserve old sealed assets; the new knee operator has its own actual data.
    if(!Knees.IsEmpty()){PolicyFields.Add(TEXT("Knees"));PolicyFields.Add(TEXT("KneeSourceDigest"));}
    FDigest Policy;Policy.Object(const_cast<UVamGluteCorrectiveProfile*>(this),&PolicyFields);Out.Add(TEXT("corrective_policy_and_targets"),Policy.End());
    return Out;
}
FString UVamGluteCorrectiveProfile::ValidateFinalData(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamLegJiggleProfile* L,UVamBreastJiggleProfile* B) const
{
    if(SchemaVersion<4||FinalDataDigests.IsEmpty()||SourceFamilyJson.IsEmpty())return TEXT("Corrective has no final-data seal; rebuild the runtime bundle (B1)");
    FString Error;const auto Actual=ComputeFinalData(D,G,L,B,Error);if(!Error.IsEmpty())return TEXT("Corrective identity unavailable; rebuild: ")+Error;
    if(Actual.Num()!=FinalDataDigests.Num())return TEXT("Corrective data seal incomplete; rebuild");
    TArray<FString> Keys;Actual.GetKeys(Keys);Keys.Sort();for(const auto& K:Keys)if(!FinalDataDigests.Contains(K)||FinalDataDigests[K]!=Actual[K])return TEXT("Corrective final-data mismatch: ")+K+TEXT("; rebuild before use");
    return FString();
}
