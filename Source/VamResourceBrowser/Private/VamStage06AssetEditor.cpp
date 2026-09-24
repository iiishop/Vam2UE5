#include "VamStage06AssetEditor.h"
#include "VamRigProfile.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsAssetUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"

UPhysicsAsset* UVamStage06AssetEditor::BuildPhysicsAsset(const FString& AssetPath, USkeletalMesh* Mesh, float MinimumBoneSizeCm, FString& Error)
{
    if (!Mesh || !FPackageName::IsValidLongPackageName(AssetPath))
    { Error=TEXT("Invalid input or destination already exists"); return nullptr; }
    UPackage* Package=CreatePackage(*AssetPath);
    const FName Name(*FPackageName::GetShortName(AssetPath));
    if (FindObject<UPhysicsAsset>(Package,*Name.ToString()))
    { Error=TEXT("Destination asset already exists"); return nullptr; }
    UPhysicsAsset* Asset=NewObject<UPhysicsAsset>(Package,Name,RF_Public|RF_Standalone);
    FPhysAssetCreateParams Params;
    Params.MinBoneSize=FMath::Clamp(MinimumBoneSizeCm,2.f,30.f);
    Params.GeomType=EFG_Sphyl;
    Params.bCreateConstraints=true;
    FText GenerationError;
    if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset,Mesh,Params,GenerationError,false,false))
    { Error=GenerationError.ToString(); return nullptr; }
    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    return Asset;
}

FString UVamStage06AssetEditor::ConfigurePhysicsAsset(UPhysicsAsset* Asset, const UVamRigProfile* Rig)
{
    if (!Asset || !Rig) return TEXT("Missing physics asset or rig profile");
    TMap<FName,FName> Semantics;
    for (const FVamRigJoint& J:Rig->Joints) Semantics.Add(J.Bone,J.Semantic);
    TArray<FString> Bodies;
    for (const USkeletalBodySetup* Setup:Asset->SkeletalBodySetups)
        if (Setup) Bodies.Add(Setup->BoneName.ToString());
    TArray<FString> Constraints;
    for (UPhysicsConstraintTemplate* Template:Asset->ConstraintSetup)
    {
        if (!Template) continue;
        FConstraintInstance& Joint=Template->DefaultInstance;
        const FName Semantic=Semantics.FindRef(Joint.GetChildBoneName());
        float Swing1=45.f,Swing2=25.f,Twist=25.f;
        const FString Label=Semantic.ToString();
        if (Label.EndsWith(TEXT("_knee")) || Label.EndsWith(TEXT("_elbow"))) { Swing1=130.f; Swing2=12.f; Twist=12.f; }
        else if (Label==TEXT("neck") || Label==TEXT("head")) { Swing1=45.f; Swing2=35.f; Twist=30.f; }
        else if (Label==TEXT("pelvis") || Label==TEXT("chest")) { Swing1=35.f; Swing2=30.f; Twist=25.f; }
        Joint.SetLinearXLimit(ELinearConstraintMotion::LCM_Locked,0);
        Joint.SetLinearYLimit(ELinearConstraintMotion::LCM_Locked,0);
        Joint.SetLinearZLimit(ELinearConstraintMotion::LCM_Locked,0);
        Joint.SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited,Swing1);
        Joint.SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited,Swing2);
        Joint.SetAngularTwistLimit(EAngularConstraintMotion::ACM_Limited,Twist);
        Template->SetDefaultProfile(Joint);
        if (!Template->ContainsConstraintProfile(TEXT("Stage06_Limited"))) Template->AddConstraintProfile(TEXT("Stage06_Limited"));
        Constraints.Add(FString::Printf(TEXT("%s:%s/%s:%s %.0f %.0f %.0f"),*Joint.GetParentBoneName().ToString(),
            *Joint.GetChildBoneName().ToString(),*Label,*Template->GetName(),Swing1,Swing2,Twist));
    }
    Asset->MarkPackageDirty();
    return FString::Printf(TEXT("bodies=%d constraints=%d body_bones=[%s] joints=[%s]"),
        Bodies.Num(),Constraints.Num(),*FString::Join(Bodies,TEXT(",")),*FString::Join(Constraints,TEXT(";")));
}
