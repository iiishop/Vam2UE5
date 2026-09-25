#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamSoftTissueProfile.h"
#include "VamSoftTissueBuilder.generated.h"
UCLASS()
class VAMRESOURCEBROWSER_API UVamSoftTissueBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|SoftTissue")
    static FString Build(UVamSoftTissueProfile* Profile,class UVamCharacterDefinition* Definition,const TArray<FVamSoftTissueRegion>& Regions);
};
