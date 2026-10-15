#include "VamHairAsset.h"
#include "Misc/SecureHash.h"

FString UVamHairstyle::GetContentSignature() const
{
    FString Content;
    for(const auto& Entry:Entries)
    {
        Content+=Entry.Hair.ToSoftObjectPath().ToString()+TEXT("\n");
        const auto* Hair=Entry.Hair.LoadSynchronous();
        Content+=(Hair?Hair->SourceIdentity:TEXT("missing"))+TEXT("\n");
        Content+=(Entry.bEnabled?TEXT("1"):TEXT("0"));
        Content+=TEXT("\n")+Entry.AppearanceOverridesJson+TEXT("\n");
    }
    FTCHARToUTF8 Bytes(*Content);FSHAHash Hash;FSHA1::HashBuffer(Bytes.Get(),Bytes.Length(),Hash.Hash);
    return Hash.ToString();
}
