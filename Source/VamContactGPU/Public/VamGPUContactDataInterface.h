#pragma once
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "VamGPUContact.h"
#include "VamGPUContactDataInterface.generated.h"
UCLASS(Category=ComputeFramework)
class VAMCONTACTGPU_API UVamGPUContactDataInterface : public UOptimusComputeDataInterface
{
 GENERATED_BODY()
public:
 FString GetDisplayName() const override{return TEXT("VaM GPU Contact");}
 TCHAR const* GetClassName() const override{return TEXT("VamGPUContact");}
 TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
 TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
 void GetSupportedInputs(TArray<FShaderFunctionDefinition>& Out) const override;
 void GetShaderParameters(TCHAR const* UID,FShaderParametersMetadataBuilder& Builder,FShaderParametersMetadataAllocations& Allocations) const override;
 void GetShaderHash(FString& Key) const override;
 void GetHLSL(FString& HLSL,FString const& Name) const override;
 UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> Binding,uint64 InputMask,uint64 OutputMask) const override;
};
UCLASS()
class UVamGPUContactDataProvider : public UComputeDataProvider
{
 GENERATED_BODY()
public:
 FVamGPUContactHandlePtr Handle;
 FComputeDataProviderRenderProxy* GetRenderProxy() override;
};
