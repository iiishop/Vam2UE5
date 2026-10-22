#include "VamGPUContactDataInterface.h"
#include "VamGPUContactState.h"
#include "Components/SkinnedMeshComponent.h"
#include "OptimusDataDomain.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ShaderParameterMetadataBuilder.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
namespace { TMap<TWeakObjectPtr<UObject>,TSharedPtr<TArray<FVector4f>,ESPMode::ThreadSafe>> SkinCorrections; }
void VamGPUSetSkinCorrection(UObject* B,TArray<FVector4f>&& V)
{
 check(IsInGameThread());
 for(auto It=SkinCorrections.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
 if(V.IsEmpty()){SkinCorrections.Remove(B);return;}
 SkinCorrections.Add(B,MakeShared<TArray<FVector4f>,ESPMode::ThreadSafe>(MoveTemp(V)));
}
BEGIN_SHADER_PARAMETER_STRUCT(FVamContactDIParameters,)
 SHADER_PARAMETER(uint32,NumVertices)
 SHADER_PARAMETER(uint32,ParticleOffset)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Positions)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Rest)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int4>,Parents)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Weights)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>,Mask)
 SHADER_PARAMETER(uint32,CorrectionVertices)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,SkinCorrection)
END_SHADER_PARAMETER_STRUCT()
TArray<FOptimusCDIPinDefinition> UVamGPUContactDataInterface::GetPinDefinitions() const
{
 return {{"Mask","ReadMask",Optimus::DomainName::Vertex,"ReadNumVertices"},{"Parents","ReadParents",Optimus::DomainName::Vertex,"ReadNumVertices"},{"Weights","ReadWeights",Optimus::DomainName::Vertex,"ReadNumVertices"},{"Offset","ReadOffset",Optimus::DomainName::Vertex,"ReadNumVertices"},{"EmbeddedPos","GetEmbeddedPos",Optimus::DomainName::Vertex,"ReadNumVertices"}};
}
TSubclassOf<UActorComponent> UVamGPUContactDataInterface::GetRequiredComponentClass() const{return USkinnedMeshComponent::StaticClass();}
void UVamGPUContactDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& Out) const
{
 Out.AddDefaulted_GetRef().SetName(TEXT("ReadNumVertices")).AddReturnType(EShaderFundamentalType::Uint);
 Out.AddDefaulted_GetRef().SetName(TEXT("ReadMask")).AddReturnType(EShaderFundamentalType::Float).AddParam(EShaderFundamentalType::Uint);
 Out.AddDefaulted_GetRef().SetName(TEXT("ReadParents")).AddReturnType(EShaderFundamentalType::Int,4).AddParam(EShaderFundamentalType::Uint);
 Out.AddDefaulted_GetRef().SetName(TEXT("ReadWeights")).AddReturnType(EShaderFundamentalType::Float,4).AddParam(EShaderFundamentalType::Uint);
 Out.AddDefaulted_GetRef().SetName(TEXT("ReadOffset")).AddReturnType(EShaderFundamentalType::Float,3).AddParam(EShaderFundamentalType::Uint);
 Out.AddDefaulted_GetRef().SetName(TEXT("GetEmbeddedPos")).AddReturnType(EShaderFundamentalType::Float,3).AddParam(EShaderFundamentalType::Uint);
}
void UVamGPUContactDataInterface::GetShaderParameters(TCHAR const* UID,FShaderParametersMetadataBuilder& B,FShaderParametersMetadataAllocations&)const{B.AddNestedStruct<FVamContactDIParameters>(UID);}
void UVamGPUContactDataInterface::GetShaderHash(FString& Key)const{GetShaderFileHash(TEXT("/Plugin/VamContactGPU/Private/VamGPUContactDI.ush"),SP_PCD3D_SM5).AppendString(Key);}
void UVamGPUContactDataInterface::GetHLSL(FString& H,FString const& Name)const
{
 FString Source;LoadShaderSourceFile(TEXT("/Plugin/VamContactGPU/Private/VamGPUContactDI.ush"),SP_PCD3D_SM5,&Source,nullptr);
 H+=Source.Replace(TEXT("DI_NAME"),*Name);
}
UComputeDataProvider* UVamGPUContactDataInterface::CreateDataProvider(TObjectPtr<UObject> B,uint64,uint64)const
{auto* P=NewObject<UVamGPUContactDataProvider>();P->Handle=VamGPUFind(B);P->SkinBinding=B;return P;}
class FVamContactDIProxy : public FComputeDataProviderRenderProxy
{
 FVamGPUContactHandlePtr H;FVamContactDIParameters Params;
 TSharedPtr<TArray<FVector4f>,ESPMode::ThreadSafe> Correction;
public:
 explicit FVamContactDIProxy(FVamGPUContactHandlePtr In,TSharedPtr<TArray<FVector4f>,ESPMode::ThreadSafe> C):H(In),Correction(C){}
 bool IsValid(FValidationData const& D)const override{return D.ParameterStructSize==sizeof(FVamContactDIParameters);}
 void AllocateResources(FRDGBuilder& G,FAllocationData const&)override
 {
  Params.CorrectionVertices=Correction?Correction->Num():0;
  const FVector4f Zero(0,0,0,0);const FIntVector4 ZeroInt(0,0,0,0);const float ZeroFloat=0;
  Params.SkinCorrection=G.CreateSRV(CreateStructuredBuffer(G,TEXT("Vam.SkinCorrection"),sizeof(FVector4f),FMath::Max(1u,Params.CorrectionVertices),Correction?Correction->GetData():&Zero,Correction?Correction->Num()*sizeof(FVector4f):sizeof(FVector4f)));
  if(!H||!H->Positions||!H->Rest){
   Params.NumVertices=0;Params.ParticleOffset=0;
   Params.Positions=Params.Rest=Params.Weights=G.CreateSRV(CreateStructuredBuffer(G,TEXT("Vam.SkinZero"),sizeof(FVector4f),1,&Zero,sizeof(Zero)));
   Params.Parents=G.CreateSRV(CreateStructuredBuffer(G,TEXT("Vam.SkinZeroParents"),sizeof(FIntVector4),1,&ZeroInt,sizeof(ZeroInt)));
   Params.Mask=G.CreateSRV(CreateStructuredBuffer(G,TEXT("Vam.SkinZeroMask"),sizeof(float),1,&ZeroFloat,sizeof(float)));return;
  }
  Params.NumVertices=H->Topology.Parents.Num();Params.ParticleOffset=H->Offset;
  Params.Positions=G.CreateSRV(G.RegisterExternalBuffer(H->Positions));Params.Rest=G.CreateSRV(G.RegisterExternalBuffer(H->Rest));
  Params.Parents=G.CreateSRV(G.RegisterExternalBuffer(H->Parents));Params.Weights=G.CreateSRV(G.RegisterExternalBuffer(H->Weights));Params.Mask=G.CreateSRV(G.RegisterExternalBuffer(H->Mask));
 }
 void GatherDispatchData(FDispatchData const& D)override{for(auto& P:MakeStridedParameterView<FVamContactDIParameters>(D))P=Params;}
};
FComputeDataProviderRenderProxy* UVamGPUContactDataProvider::GetRenderProxy(){return new FVamContactDIProxy(VamGPUFind(SkinBinding),SkinCorrections.FindRef(SkinBinding));}
