#include "VamGPUContactDataInterface.h"
#include "VamGPUContactState.h"
#include "Components/SkinnedMeshComponent.h"
#include "OptimusDataDomain.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ShaderParameterMetadataBuilder.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "RenderGraphBuilder.h"
BEGIN_SHADER_PARAMETER_STRUCT(FVamContactDIParameters,)
 SHADER_PARAMETER(uint32,NumVertices)
 SHADER_PARAMETER(uint32,ParticleOffset)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Positions)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Rest)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int4>,Parents)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Weights)
 SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>,Mask)
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
{auto* P=NewObject<UVamGPUContactDataProvider>();P->Handle=VamGPUFind(B);return P;}
class FVamContactDIProxy : public FComputeDataProviderRenderProxy
{
 FVamGPUContactHandlePtr H;FVamContactDIParameters Params;
public:
 explicit FVamContactDIProxy(FVamGPUContactHandlePtr In):H(In){}
 bool IsValid(FValidationData const& D)const override{return H&&H->Positions&&H->Rest&&D.ParameterStructSize==sizeof(FVamContactDIParameters);}
 void AllocateResources(FRDGBuilder& G,FAllocationData const&)override
 {
  Params.NumVertices=H->Topology.Parents.Num();Params.ParticleOffset=H->Offset;
  Params.Positions=G.CreateSRV(G.RegisterExternalBuffer(H->Positions));Params.Rest=G.CreateSRV(G.RegisterExternalBuffer(H->Rest));
  Params.Parents=G.CreateSRV(G.RegisterExternalBuffer(H->Parents));Params.Weights=G.CreateSRV(G.RegisterExternalBuffer(H->Weights));Params.Mask=G.CreateSRV(G.RegisterExternalBuffer(H->Mask));
 }
 void GatherDispatchData(FDispatchData const& D)override{for(auto& P:MakeStridedParameterView<FVamContactDIParameters>(D))P=Params;}
};
FComputeDataProviderRenderProxy* UVamGPUContactDataProvider::GetRenderProxy(){return new FVamContactDIProxy(Handle);}
