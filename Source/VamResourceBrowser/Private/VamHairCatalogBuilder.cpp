#include "VamHairBuilder.h"
#include "VamHairAsset.h"
#include "VamHairProfile.h"
#include "VamRuntimeConfiguration.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace {
bool SaveHairObject(UObject* Object)
{
    Object->MarkPackageDirty();FAssetRegistryModule::AssetCreated(Object);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Object->GetOutermost(),Object,*FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
FString Json(const TSharedPtr<FJsonObject>& Object){FString Text;FJsonSerializer::Serialize(Object.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return Text;}
}

UVamHairstyle* UVamHairBuilder::BuildHairstyle(const FString& File,const FString& Root,FString& Error)
{
    FString Text;TSharedPtr<FJsonObject> Catalog;
    if(!Root.StartsWith(TEXT("/Game/"))||!FFileHelper::LoadFileToString(Text,*File)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Catalog))
    {Error=TEXT("Invalid hair catalog");return nullptr;}
    if(Catalog->HasTypedField<EJson::Object>(TEXT("catalog")))Catalog=Catalog->GetObjectField(TEXT("catalog"));
    TMap<FString,UVamHairAsset*> Assets;
    for(const auto& Value:Catalog->GetArrayField(TEXT("assets")))
    {
        const auto Row=Value->AsObject();const FString ID=Row->GetStringField(TEXT("identity"));
        if(ID.Len()!=64||Row->GetStringField(TEXT("schema"))!=TEXT("vam-hair-asset/1")){Error=TEXT("Invalid hair resource identity");return nullptr;}
        const FString Path=Root/TEXT("Hair")/(TEXT("H_")+ID)/TEXT("HA_Hair");
        auto* Asset=LoadObject<UVamHairAsset>(nullptr,*Path);
        auto Payload=MakeShared<FJsonObject>();Payload->SetStringField(TEXT("schema"),TEXT("vam-hair-asset/1"));Payload->SetField(TEXT("source"),Row->TryGetField(TEXT("source")));Payload->SetField(TEXT("scalp"),Row->TryGetField(TEXT("scalp")));const FString Data=Json(Payload);
        if(Asset){if(Asset->SourceIdentity!=ID||Asset->SourceDataJson!=Data){Error=TEXT("Shared hair resource content mismatch");return nullptr;}}
        else
        {
            Asset=NewObject<UVamHairAsset>(CreatePackage(*Path),TEXT("HA_Hair"),RF_Public|RF_Standalone);
            Asset->SourceIdentity=ID;Asset->DisplayName=Row->GetStringField(TEXT("display_name"));Asset->SourcePath=Row->GetStringField(TEXT("source_path"));Asset->SourceDataJson=Data;
            if(!SaveHairObject(Asset)){Error=TEXT("Hair resource save failed");return nullptr;}
        }
        Assets.Add(ID,Asset);
    }
    const auto Row=Catalog->GetObjectField(TEXT("hairstyle"));const FString ID=Row->GetStringField(TEXT("identity"));
    if(ID.Len()!=64||Row->GetStringField(TEXT("schema"))!=TEXT("vam-hairstyle/1")){Error=TEXT("Invalid hairstyle identity");return nullptr;}
    TArray<FVamHairstyleEntry> Entries;
    for(const auto& Value:Row->GetArrayField(TEXT("entries")))
    {
        const auto Entry=Value->AsObject();auto** Asset=Assets.Find(Entry->GetStringField(TEXT("hair_identity")));
        if(!Asset){Error=TEXT("Hairstyle references missing hair");return nullptr;}
        auto& Item=Entries.AddDefaulted_GetRef();Item.Hair=*Asset;Item.bEnabled=Entry->GetBoolField(TEXT("enabled"));Item.AppearanceOverridesJson=Json(Entry->GetObjectField(TEXT("appearance_overrides")));
    }
    if(Entries.IsEmpty()){Error=TEXT("Empty hairstyle");return nullptr;}
    const FString Path=Root/TEXT("Styles")/(TEXT("S_")+ID)/TEXT("HS_Style");
    if(auto* Existing=LoadObject<UVamHairstyle>(nullptr,*Path))
    {
        auto* Expected=NewObject<UVamHairstyle>();Expected->Entries=Entries;
        if(Existing->BuildIdentity!=ID||Existing->GetContentSignature()!=Expected->GetContentSignature()){Error=TEXT("Existing hairstyle has changed");return nullptr;}
        return Existing;
    }
    auto* Style=NewObject<UVamHairstyle>(CreatePackage(*Path),TEXT("HS_Style"),RF_Public|RF_Standalone);Style->BuildIdentity=ID;Style->Entries=MoveTemp(Entries);
    if(!SaveHairObject(Style)){Error=TEXT("Hairstyle save failed");return nullptr;}return Style;
}

bool UVamHairBuilder::BindHairstyle(UVamHairProfile* Cache,UVamRuntimeConfiguration* Configuration,UVamHairstyle* Style)
{
    if(!Cache||!Configuration||!Style)return false;
    // A cache is valid only for the exact composition it was built from. Binding
    // must not certify edited overrides or a reordered/duplicated composition.
    if(Cache->Hairstyle.Get()!=Style||Cache->HairstyleSignature!=Style->GetContentSignature())return false;
    TSharedPtr<FJsonObject> Source;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Cache->SourceReceiptJson),Source))return false;
    TSet<FString> Actual,Expected;
    for(const auto& Group:Source->GetArrayField(TEXT("groups")))Actual.Add(Group->AsObject()->GetStringField(TEXT("source_path")));
    for(const auto& Entry:Style->Entries)if(Entry.bEnabled){auto* Hair=Entry.Hair.LoadSynchronous();if(!Hair)return false;Expected.Add(Hair->SourcePath);}
    if(Actual.Num()!=Expected.Num())return false;for(const auto& Path:Expected)if(!Actual.Contains(Path))return false;
    Cache->Hairstyle=Style;Cache->HairstyleSignature=Style->GetContentSignature();Configuration->Hairstyle=Style;Configuration->Hair=Cache;
    return SaveHairObject(Cache)&&SaveHairObject(Configuration);
}
