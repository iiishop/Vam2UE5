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

// Build-facing serialization is taken from the actual mesh, not a guessed cache file.
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UVamStage06AssetEditor::DescribeMeshBinding(USkeletalMesh* Mesh)
{
    if (!Mesh) return TEXT("null");
    TArray<TSharedPtr<FJsonValue>> Bones;
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    for (int32 I=0;I<Ref.GetNum();++I)
    {
        TSharedRef<FJsonObject> Bone=MakeShared<FJsonObject>();
        const FTransform& T=Ref.GetRefBonePose()[I];
        const FVector P=T.GetTranslation(), S=T.GetScale3D(); const FQuat Q=T.GetRotation();
        auto Numbers=[](std::initializer_list<double> Values)
        { TArray<TSharedPtr<FJsonValue>> Result; for(double V:Values) Result.Add(MakeShared<FJsonValueNumber>(V)); return Result; };
        Bone->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());
        Bone->SetNumberField(TEXT("parent"),Ref.GetParentIndex(I));
        Bone->SetArrayField(TEXT("translation"),Numbers({P.X,P.Y,P.Z}));
        Bone->SetArrayField(TEXT("quaternion_xyzw"),Numbers({Q.X,Q.Y,Q.Z,Q.W}));
        Bone->SetArrayField(TEXT("scale"),Numbers({S.X,S.Y,S.Z}));
        Bones.Add(MakeShared<FJsonValueObject>(Bone));
    }
    FString Result; FJsonSerializer::Serialize(Bones,TJsonWriterFactory<>::Create(&Result)); return Result;
}

FString UVamStage06AssetEditor::ConfigureRuntimePhysicsAsset(UPhysicsAsset* Asset, USkeletalMesh* Mesh, const UVamRigProfile* Rig)
{
    if (!Asset || !Mesh || !Rig || Rig->Skeleton.LoadSynchronous()!=Mesh->GetSkeleton()) return TEXT("ERROR: incompatible physics inputs");
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    TArray<FTransform> CS=Ref.GetRefBonePose();
    for(int32 I=0;I<CS.Num();++I) if(Ref.GetParentIndex(I)>=0) CS[I]=CS[I]*CS[Ref.GetParentIndex(I)];
    // Validate before mutation. Unmapped rigid parts and collapsed body chains
    // remain explicitly locked; no semantic-name guesses select hinge axes.
    for (UPhysicsConstraintTemplate* Template:Asset->ConstraintSetup)
    {
        if(!Template) return TEXT("ERROR: null constraint");
        const auto& C=Template->DefaultInstance;
        int32 Child=Ref.FindBoneIndex(C.GetChildBoneName()), Parent=Ref.FindBoneIndex(C.GetParentBoneName());
        if(Child<0 || Parent<0 || !Ref.BoneIsChildOf(Child,Parent)) return TEXT("ERROR: invalid body ancestor chain");
    }
    TArray<FString> Locked;
    for (UPhysicsConstraintTemplate* Template:Asset->ConstraintSetup)
    {
        FConstraintInstance& C=Template->DefaultInstance;
        const int32 Child=Ref.FindBoneIndex(C.GetChildBoneName()), Parent=Ref.FindBoneIndex(C.GetParentBoneName());
        const FTransform Relative=CS[Child].GetRelativeTransform(CS[Parent]);
        // Anim pose controls use parent-local delta * reference rotation. Align
        // joint axes with that same parent basis, not the fitted capsule axis.
        C.SetRefFrame(EConstraintFrame::Frame1,FTransform(Relative.GetRotation().Inverse()));
        C.SetRefFrame(EConstraintFrame::Frame2,FTransform(FQuat::Identity,Relative.GetTranslation()));
        C.SetLinearXLimit(LCM_Locked,0); C.SetLinearYLimit(LCM_Locked,0); C.SetLinearZLimit(LCM_Locked,0);
        const FVamRigJoint* J=Rig->Joints.FindByPredicate([&C](const FVamRigJoint& Row){return Row.Bone==C.GetChildBoneName();});
        const bool Controlled=J && J->bPoseControl && J->bLimitRotation && Ref.GetParentIndex(Child)==Parent;
        if(Controlled)
        {
            const FRotator Half=(J->Maximum-J->Minimum)*.5;
            C.AngularRotationOffset=(J->Maximum+J->Minimum)*.5;
            C.SetAngularSwing1Limit(ACM_Limited,FMath::Abs(Half.Yaw));
            C.SetAngularSwing2Limit(ACM_Limited,FMath::Abs(Half.Pitch));
            C.SetAngularTwistLimit(ACM_Limited,FMath::Abs(Half.Roll));
        }
        else
        {
            C.AngularRotationOffset=FRotator::ZeroRotator;
            C.SetAngularSwing1Limit(ACM_Locked,0); C.SetAngularSwing2Limit(ACM_Locked,0); C.SetAngularTwistLimit(ACM_Locked,0);
            Locked.Add(C.GetChildBoneName().ToString());
        }
        Template->SetDefaultProfile(C);
    }
    Asset->MarkPackageDirty();
    return FString::Printf(TEXT("parent-local-v1; bodies=%d; constraints=%d; controlled_or_skipped_chain_locked=[%s]"),
        Asset->SkeletalBodySetups.Num(),Asset->ConstraintSetup.Num(),*FString::Join(Locked,TEXT(",")));
}

#include "VamRuntimeConfiguration.h"
#include "VamCharacterDefinition.h"
#include "VamPhysicsShapeProfile.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Animation/MorphTarget.h"

bool UVamStage06AssetEditor::BuildPhysicsShapeProfile(UVamPhysicsShapeProfile* Profile, UVamCharacterDefinition* Definition, UPhysicsAsset* Physics, FString& Error)
{
    USkeletalMesh* Mesh=Definition ? Definition->Body.LoadSynchronous() : nullptr;
    const UVamShapeDefinition* Shape=Definition ? Definition->Shape.LoadSynchronous() : nullptr;
    if(!Profile || !Mesh || !Shape || !Physics || !Mesh->GetImportedModel() || Mesh->GetImportedModel()->LODModels.IsEmpty())
    { Error=TEXT("Missing native geometry/binding for collision fitting"); return false; }
    const auto& Ref=Mesh->GetRefSkeleton();
    TArray<FTransform> CS=Ref.GetRefBonePose();
    for(int32 I=0;I<CS.Num();++I) if(Ref.GetParentIndex(I)>=0) CS[I]=CS[I]*CS[Ref.GetParentIndex(I)];
    const auto& LOD=Mesh->GetImportedModel()->LODModels[0];
    Profile->BaselinePoints.SetNum(LOD.NumVertices); Profile->Morphs.Reset(); Profile->Fits.Reset();
    TMap<int32,int32> BoneToFit;
    for(const USkeletalBodySetup* Setup:Physics->SkeletalBodySetups)
    {
        if(!Setup || Setup->AggGeom.SphylElems.Num()!=1 || Setup->AggGeom.GetElementCount()!=1)
        { Error=TEXT("Collision fitting requires exactly one native capsule per body"); return false; }
        FVamCollisionFit Fit; Fit.Bone=Setup->BoneName; Fit.BoneIndex=Ref.FindBoneIndex(Fit.Bone);
        if(Fit.BoneIndex<0) { Error=TEXT("Unknown physics bone"); return false; }
        const auto& Capsule=Setup->AggGeom.SphylElems[0];
        Fit.Center=Capsule.Center; Fit.Rotation=Capsule.Rotation.Quaternion(); Fit.Radius=Capsule.Radius; Fit.Length=Capsule.Length;
        BoneToFit.Add(Fit.BoneIndex,Profile->Fits.Add(Fit));
    }
    for(const auto& Section:LOD.Sections) for(int32 I=0;I<Section.SoftVertices.Num();++I)
    {
        const auto& Vertex=Section.SoftVertices[I]; const int32 Point=Section.BaseVertexIndex+I;
        Profile->BaselinePoints[Point]=FVector(Vertex.Position);
        int32 Dominant=0;
        for(int32 J=1;J<MAX_TOTAL_INFLUENCES;++J) if(Vertex.InfluenceWeights[J]>Vertex.InfluenceWeights[Dominant]) Dominant=J;
        if(!Vertex.InfluenceWeights[Dominant] || !Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Dominant]))
        { Error=TEXT("Invalid native skin influence"); return false; }
        int32 Bone=Section.BoneMap[Vertex.InfluenceBones[Dominant]];
        while(Bone>=0 && !BoneToFit.Contains(Bone)) Bone=Ref.GetParentIndex(Bone);
        if(Bone>=0) Profile->Fits[BoneToFit[Bone]].Points.Add(Point);
    }
    for(const auto& Parameter:Definition->Parameters)
    {
        if(Parameter.Group==TEXT("Expression") && Parameter.BoneCenters.IsEmpty()) continue;
        // Fitting bounds and capsules are expressed in the imported mesh bind.
        // A neutral mesh has not had the authored default morph applied yet.
        FVamCollisionMorph Morph; Morph.Parameter=Parameter.Target;
        Morph.Baseline=Definition->ShapeConvention==TEXT("neutral_plus_parameters") ? 0.f : Parameter.DefaultValue;
        if(const UMorphTarget* Target=Mesh->FindMorphTarget(Parameter.Target))
            for(const auto& Delta:Target->GetMorphTargetDeltas(0))
            {
                if(!Profile->BaselinePoints.IsValidIndex(Delta.SourceIdx)) { Error=TEXT("Morph native index mismatch"); return false; }
                FVamCollisionPointDelta D; D.Point=Delta.SourceIdx; D.Delta=FVector(Delta.PositionDelta); Morph.Deltas.Add(D);
            }
        Profile->Morphs.Add(MoveTemp(Morph));
    }
    for(auto& Fit:Profile->Fits)
    {
        FBox Bounds(ForceInit);
        for(int32 Point:Fit.Points) Bounds+=Fit.Rotation.UnrotateVector(CS[Fit.BoneIndex].InverseTransformPosition(Profile->BaselinePoints[Point]));
        if(!Fit.Points.IsEmpty() && Bounds.GetSize().GetMin()<1.e-5)
        { Error=TEXT("Degenerate native collision region: ")+Fit.Bone.ToString(); return false; }
        Fit.BoundsMin=Bounds.Min; Fit.BoundsMax=Bounds.Max;
    }
    Profile->Physics=Physics; Profile->BindSignature=Definition->BindSignature; Profile->MorphSetLockDigest=Shape->MorphSetLockDigest;
    Profile->MarkPackageDirty(); return true;
}

bool UVamStage06AssetEditor::SetRuntimeConfigurationIdentity(UVamRuntimeConfiguration* Configuration, UVamCharacterDefinition* Definition, const FString& Identity, const FString& ReceiptJson)
{
    if(!Configuration || !Definition || Identity.Len()!=64) return false;
    const UVamShapeDefinition* Shape=Definition->Shape.LoadSynchronous();
    if(!Shape || Shape->MorphSetLockDigest.Len()!=64) return false;
    TSharedPtr<FJsonObject> Receipt;
    if(!ReceiptJson.IsEmpty()&&(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReceiptJson),Receipt)||!Receipt))return false;
    FString SourcePath;
    if(Receipt&&Receipt->TryGetStringField(TEXT("source_definition"),SourcePath))
    {
        auto* Source=LoadObject<UVamCharacterDefinition>(nullptr,*SourcePath);
        if(!Source||Source->SourceDigest!=Definition->SourceDigest)return false;
        Configuration->DiagnosticSourceDefinition=Source;
    }
    Configuration->SchemaVersion=3;
    Configuration->bIndependentReloadVerified=false;
    Configuration->BuildIdentity=Identity;
    Configuration->SourceDigest=Definition->SourceDigest;
    Configuration->BindSignature=Definition->BindSignature;
    Configuration->MorphSetLockDigest=Shape->MorphSetLockDigest;
    Configuration->ReceiptJson=ReceiptJson;
    Configuration->MarkPackageDirty();
    return true;
}

bool UVamStage06AssetEditor::PublishRuntimeConfiguration(UVamRuntimeConfiguration* Configuration)
{
    if(!Configuration || Configuration->SchemaVersion!=3 || Configuration->BuildIdentity.Len()!=64 || Configuration->ReceiptJson.IsEmpty()) return false;
    Configuration->bIndependentReloadVerified=true;Configuration->MarkPackageDirty();return true;
}

#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Engine/Texture2D.h"
FString UVamStage06AssetEditor::DescribeMaterialStreaming(UMaterialInterface* Material)
{
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Entries,References;
    if(Material)
    {
        Root->SetStringField(TEXT("material"),Material->GetPathName());
        for(const auto& D:Material->GetTextureStreamingData())
        {
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("texture"),D.TextureName.ToString());Row->SetNumberField(TEXT("uv_channel"),D.UVChannelIndex);Row->SetNumberField(TEXT("sampling_scale"),D.SamplingScale);Entries.Add(MakeShared<FJsonValueObject>(Row));
        }
        if(auto* M=Material->GetMaterial()) for(const auto& E:M->GetExpressionCollection().Expressions)
            if(auto* T=Cast<UMaterialExpressionTextureBase>(E);T&&T->Texture&&!T->Texture->NeverStream)
                References.Add(MakeShared<FJsonValueString>(T->Texture->GetPathName()));
    }
    Root->SetArrayField(TEXT("streaming_entries"),Entries);Root->SetArrayField(TEXT("graph_texture_references"),References);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}

#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDivide.h"
namespace
{
struct FVamAffineUV
{
    int32 Channel=INDEX_NONE;
    FVector2D Scale=FVector2D::ZeroVector,Offset=FVector2D::ZeroVector;
};
bool VamIdentityUVMask(const FExpressionInput& Input)
{
    return !Input.Mask||(Input.MaskR&&Input.MaskG&&!Input.MaskB&&!Input.MaskA)||
        (Cast<UMaterialExpressionConstant>(Input.Expression)&&Input.MaskR&&!Input.MaskG&&!Input.MaskB&&!Input.MaskA);
}
bool VamReadAffineUV(UMaterialExpression* E,FVamAffineUV& Out,int32 Depth=0)
{
    if(!E||Depth>32)return false;
    if(auto* T=Cast<UMaterialExpressionTextureCoordinate>(E)){Out.Channel=T->CoordinateIndex;Out.Scale=FVector2D(T->UTiling,T->VTiling);return true;}
    if(auto* C=Cast<UMaterialExpressionConstant>(E)){Out.Offset=FVector2D(C->R,C->R);return true;}
    if(auto* C=Cast<UMaterialExpressionConstant2Vector>(E)){Out.Offset=FVector2D(C->R,C->G);return true;}
    FExpressionInput A,B;double CA=0,CB=0;bool Multiply=false,Divide=false;
    if(auto* Add=Cast<UMaterialExpressionAdd>(E)){A=Add->A;B=Add->B;CA=Add->ConstA;CB=Add->ConstB;}
    else if(auto* Mul=Cast<UMaterialExpressionMultiply>(E)){A=Mul->A;B=Mul->B;CA=Mul->ConstA;CB=Mul->ConstB;Multiply=true;}
    else if(auto* Div=Cast<UMaterialExpressionDivide>(E)){A=Div->A;B=Div->B;CA=Div->ConstA;CB=Div->ConstB;Divide=true;}
    else return false;
    // Masked/swizzled or parameterized coordinates require UE's general GPU exporter.
    if(!VamIdentityUVMask(A)||!VamIdentityUVMask(B))return false;
    FVamAffineUV X,Y;
    if(A.Expression){if(!VamReadAffineUV(A.Expression,X,Depth+1))return false;}else X.Offset=FVector2D(CA,CA);
    if(B.Expression){if(!VamReadAffineUV(B.Expression,Y,Depth+1))return false;}else Y.Offset=FVector2D(CB,CB);
    if(Divide)
    {
        if(Y.Channel!=INDEX_NONE||FMath::Abs(Y.Offset.X)<=UE_SMALL_NUMBER||FMath::Abs(Y.Offset.Y)<=UE_SMALL_NUMBER)return false;
        Out.Channel=X.Channel;Out.Scale=X.Scale/Y.Offset;Out.Offset=X.Offset/Y.Offset;
    }
    else if(Multiply)
    {
        if(X.Channel!=INDEX_NONE&&Y.Channel!=INDEX_NONE)return false;
        Out.Channel=X.Channel!=INDEX_NONE?X.Channel:Y.Channel;
        Out.Scale=X.Channel!=INDEX_NONE?X.Scale*Y.Offset:Y.Scale*X.Offset;Out.Offset=X.Offset*Y.Offset;
    }
    else
    {
        if(X.Channel!=INDEX_NONE&&Y.Channel!=INDEX_NONE&&X.Channel!=Y.Channel)return false;
        Out.Channel=X.Channel!=INDEX_NONE?X.Channel:Y.Channel;Out.Scale=X.Scale+Y.Scale;Out.Offset=X.Offset+Y.Offset;
    }
    return !Out.Scale.ContainsNaN()&&!Out.Offset.ContainsNaN();
}
}
FString UVamStage06AssetEditor::BuildAffineMaterialStreaming(UMaterial* Material)
{
    if(!Material)return TEXT("Missing generated material");
    TArray<FMaterialTextureInfo> Data;
    for(const auto& Expression:Material->GetExpressionCollection().Expressions)
    {
        auto* Sample=Cast<UMaterialExpressionTextureSample>(Expression);if(!Sample||!Sample->Texture||Sample->Texture->NeverStream)continue;
        FVamAffineUV UV;
        if(Sample->Coordinates.Expression)
        {
            if(!VamIdentityUVMask(Sample->Coordinates)||!VamReadAffineUV(Sample->Coordinates.Expression,UV))
                return TEXT("Unsupported non-affine texture coordinates; use UE material UV-density export: ")+Sample->GetPathName();
        }
        else{UV.Channel=Sample->ConstCoordinate;UV.Scale=FVector2D(1,1);}
        if(UV.Channel==INDEX_NONE)continue; // Constant sampling has no surface-frequency requirement.
        const double SX=FMath::Abs(UV.Scale.X),SY=FMath::Abs(UV.Scale.Y);
        // Use the smaller nonzero derivative for a conservative anisotropic footprint.
        const double Scale=SX>UE_SMALL_NUMBER&&SY>UE_SMALL_NUMBER?FMath::Min(SX,SY):FMath::Max(SX,SY);
        if(!FMath::IsFinite(Scale)||Scale<=UE_SMALL_NUMBER||UV.Channel<0||UV.Channel>=MAX_TEXCOORDS)
            return TEXT("Invalid generated texture sampling footprint");
        FMaterialTextureInfo Info;Info.TextureName=Sample->Texture->GetFName();Info.TextureReference=FSoftObjectPath(Sample->Texture);Info.UVChannelIndex=UV.Channel;Info.SamplingScale=Scale;
        Data.Add(Info);
    }
    Material->SetTextureStreamingData(Data);Material->MarkPackageDirty();return FString();
}
