#pragma once
#include "VamRigProfile.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

// Explicit opt-in acceptance capture. Never runs during ordinary character play.
static bool VamRunPoseSuite(FAutomationTestBase& Test,AVamCharacterActor* Actor,UWorld* World,USceneCaptureComponent2D* Capture,const FString& Directory,TFunctionRef<void()> Tick)
{
 FString RecipePath;FParse::Value(FCommandLine::Get(),TEXT("VamPoseSuite="),RecipePath);FString Text;
 TSharedPtr<FJsonObject> Recipe;if(!FFileHelper::LoadFileToString(Text,*RecipePath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Recipe))return false;
 auto Save=[](const FString& Path,TSharedPtr<FJsonObject> O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return FFileHelper::SaveStringToFile(S,*Path);};
 auto Numbers=[](const FVector& V){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};};
 auto Matrix=[](const FTransform& T){TArray<TSharedPtr<FJsonValue>> V;const auto M=T.ToMatrixWithScale();for(int I=0;I<4;++I)for(int J=0;J<4;++J)V.Add(MakeShared<FJsonValueNumber>(M.M[I][J]));return V;};
 auto* Body=CastChecked<UVamLegSkeletalMeshComponent>(Actor->Character->Body);auto* Rig=Actor->Character->RigProfile.Get();auto* Anim=CastChecked<UVamShapeAnimInstance>(Body->GetAnimInstance());
 if(!Rig||Body->RestSides.IsEmpty())return false;
 const auto& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();const auto Reference=Actor->Character->GetShapeReferencePose();auto Neutral=Reference;
 for(int I=0;I<Neutral.Num();++I)if(Ref.GetParentIndex(I)>=0)Neutral[I]*=Neutral[Ref.GetParentIndex(I)];
 const int Head=Ref.FindBoneIndex(Rig->BoneForSemantic(TEXT("head")));FVector Feet=FVector::ZeroVector,Pelvis=FVector::ZeroVector;int Sides=0;
 for(const auto& S:Body->LegRest)if(!S.bCalf){Feet+=Neutral[S.Foot].GetLocation();Pelvis+=Neutral[S.Thigh].GetLocation();++Sides;}if(!Sides||Head<0)return false;Feet/=Sides;Pelvis/=Sides;
 const FVector Up=(Neutral[Head].GetLocation()-Feet).GetSafeNormal();FVector Front=Neutral[Body->RestSides[0].AnchorBone].GetUnitAxis(EAxis::X);Front=(Front-Up*FVector::DotProduct(Front,Up)).GetSafeNormal();const FVector Right=FVector::CrossProduct(Up,Front).GetSafeNormal();
 const FVector ViewCenter=Pelvis-Up*25;const FTransform Initial=Actor->GetActorTransform();
 TArray<UMaterialInterface*> OriginalMaterials;for(int I=0;I<Body->GetNumMaterials();++I)OriginalMaterials.Add(Body->GetMaterial(I));
 Body->SkinCacheUsage.Init(ESkinCacheUsage::Enabled,Body->GetSkeletalMeshAsset()->GetLODNum());Body->MarkRenderStateDirty();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 auto MakeMaterial=[&](bool Silhouette){auto* M=NewObject<UMaterial>(GetTransientPackage());M->SetShadingModel(Silhouette?MSM_Unlit:MSM_DefaultLit);auto* C=NewObject<UMaterialExpressionConstant3Vector>(M);C->Constant=Silhouette?FLinearColor::White:FLinearColor(.45,.45,.45);M->GetExpressionCollection().AddExpression(C);if(Silhouette)M->GetEditorOnlyData()->EmissiveColor.Expression=C;else{M->GetEditorOnlyData()->BaseColor.Expression=C;M->GetEditorOnlyData()->Roughness.UseConstant=true;M->GetEditorOnlyData()->Roughness.Constant=.8;}bool Recompile=false;M->SetMaterialUsage(Recompile,MATUSAGE_SkeletalMesh);M->SetMaterialUsage(Recompile,MATUSAGE_MorphTargets);M->SetFeatureLevelToCompile(World->GetFeatureLevel(),true);M->PostEditChange();M->ForceRecompileForRendering();return M;};
 TStrongObjectPtr<UMaterial> GrayOwner(MakeMaterial(false)),SilhouetteOwner(MakeMaterial(true));auto* Gray=GrayOwner.Get();auto* Silhouette=SilhouetteOwner.Get();Capture->PostProcessSettings.AutoExposureBias=2;FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 auto* SeatActor=World->SpawnActor<AActor>();auto* Seat=NewObject<UStaticMeshComponent>(SeatActor);SeatActor->SetRootComponent(Seat);Seat->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Seat->SetMobility(EComponentMobility::Movable);Seat->SetCollisionResponseToAllChannels(ECR_Block);Seat->RegisterComponent();Seat->SetWorldScale3D(FVector(1.1,1.1,.1));
 auto* Light=NewObject<UDirectionalLightComponent>(Actor);Light->Intensity=3;Light->RegisterComponent();
 Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);

 const auto Flags=Capture->ShowFlags;TArray<TSharedPtr<FJsonValue>> Results;
 int CaseIndex=0;FString Only;FParse::Value(FCommandLine::Get(),TEXT("VamPoseCase="),Only);
 for(const auto& Value:Recipe->GetArrayField(TEXT("cases"))){const auto C=Value->AsObject();const FString Id=C->GetStringField(TEXT("id"));if(!Only.IsEmpty()&&Only!=Id)continue;
  Actor->BreastContact->SetContactEnabled(false);Actor->BodyContactResponse->bEnabled=false;Body->bJiggleEnabled=Body->bGluteJiggleEnabled=Body->bLegJiggleEnabled=false;
  Actor->Character->ResetDebugBoneOffsets();Actor->Character->ResetPoseControlRotations();Actor->Character->SetFootLocked(TEXT("left_foot"),false);Actor->Character->SetFootLocked(TEXT("right_foot"),false);Actor->Character->ClearIKGoal(TEXT("pelvis"));
  Actor->ResetDeformationAuditState();auto Local=Reference;const auto Orient=C->GetArrayField(TEXT("orientation"));Actor->SetActorTransform(FTransform(FRotator(Orient[0]->AsNumber(),Orient[1]->AsNumber(),Orient[2]->AsNumber()).Quaternion()*Initial.GetRotation(),Initial.GetLocation(),Initial.GetScale3D()));
  auto Report=MakeShared<FJsonObject>();Report->SetObjectField(TEXT("request"),C);Report->SetStringField(TEXT("id"),Id);Report->SetStringField(TEXT("actor_class"),Actor->GetClass()->GetPathName());TArray<TSharedPtr<FJsonValue>> Limits;bool Clamped=false;
  for(const auto& S:Body->LegRest)if(!S.bCalf){const double H=FMath::DegreesToRadians(C->GetArrayField(TEXT("hip"))[S.Side]->AsNumber()),K=FMath::DegreesToRadians(C->GetArrayField(TEXT("knee"))[S.Side]->AsNumber());const double Sign=FMath::Sign(FVector::DotProduct(Neutral[S.Thigh].GetLocation()-Pelvis,Right));
   const FQuat Abd(Front,FMath::DegreesToRadians(C->GetArrayField(TEXT("abduction"))[S.Side]->AsNumber())*Sign);
   const FVector Thigh=Abd.RotateVector(Front*FMath::Sin(H)-Up*FMath::Cos(H));const FQuat Twist(Thigh,FMath::DegreesToRadians(C->GetArrayField(TEXT("twist"))[S.Side]->AsNumber())*Sign);
   const FVector Shin=Twist.RotateVector(Abd.RotateVector(Front*FMath::Sin(H-K)-Up*FMath::Cos(H-K)));
   const int Bones[]={S.Thigh,S.Shin,S.Foot};
   for(int J=0;J<3;++J){auto CS=Local;for(int I=0;I<CS.Num();++I)if(Ref.GetParentIndex(I)>=0)CS[I]*=CS[Ref.GetParentIndex(I)];const int B=Bones[J],Parent=Ref.GetParentIndex(B);
    FQuat Desired=Neutral[B].GetRotation();if(J<2){const FVector Direction=(CS[Bones[J+1]].GetLocation()-CS[B].GetLocation()).GetSafeNormal();Desired=FQuat::FindBetweenNormals(Direction,J==0?Thigh:Shin)*CS[B].GetRotation();if(J==0)Desired=Twist*Desired;}
    const FQuat Relative=(CS[Parent].GetRotation().Inverse()*Desired).GetNormalized();const FQuat Delta=(Relative*Reference[B].GetRotation().Inverse()).GetNormalized();const auto* Joint=Rig->Joints.FindByPredicate([&](const FVamRigJoint& X){return X.Bone==Ref.GetBoneName(B);});if(!Joint)return false;
    auto Admitted=[&](double Alpha){const auto Q=FQuat::Slerp(FQuat::Identity,Delta,Alpha).GetNormalized();return Q.Equals(VamPoseControl::Clamp(*Joint,Q.Rotator()).Quaternion(),1.e-5);};double Alpha=1;
    if(!Admitted(1)){double Lo=0,Hi=1;for(int N=0;N<30;++N){double Mid=(Lo+Hi)*.5;if(Admitted(Mid))Lo=Mid;else Hi=Mid;}Alpha=Lo;Clamped=true;}
    const FQuat Applied=FQuat::Slerp(FQuat::Identity,Delta,Alpha).GetNormalized();Anim->GeometricDebugBones.Add(B);Actor->Character->SetDebugBoneOffset(B,FTransform(Applied));Local[B].SetRotation((Applied*Reference[B].GetRotation()).GetNormalized());
    auto L=MakeShared<FJsonObject>();L->SetStringField(TEXT("bone"),Joint->Bone.ToString());L->SetNumberField(TEXT("rotation_fraction"),Alpha);FRotator Min,Max;VamPoseControl::GetLimits(*Joint,Min,Max);L->SetArrayField(TEXT("limit_min_pitch_yaw_roll"),Numbers(FVector(Min.Pitch,Min.Yaw,Min.Roll)));L->SetArrayField(TEXT("limit_max_pitch_yaw_roll"),Numbers(FVector(Max.Pitch,Max.Yaw,Max.Roll)));L->SetArrayField(TEXT("applied_local_delta_matrix"),Matrix(FTransform(Applied)));Limits.Add(MakeShared<FJsonValueObject>(L));
   }
  }
  Report->SetArrayField(TEXT("joint_limits"),Limits);Report->SetBoolField(TEXT("limited"),Clamped);
  for(int I=0;I<10;++I)Tick();
  const bool Contact=C->GetBoolField(TEXT("contact"));Seat->SetVisibility(Contact);Seat->SetCollisionEnabled(Contact?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
  FBox Region(ForceInit);for(const auto& S:Body->GluteRest)for(int I=0;I<S.RegionPoints.Num();++I)if(S.RegionWeights[I]>.35)Region+=Body->GetComponentSpaceTransforms()[S.AnchorBone].TransformPosition(S.RegionPoints[I]);
  FVector SeatLocal=Region.GetCenter();SeatLocal.Z=Region.Min.Z-5-2;Seat->SetWorldLocation(Body->GetComponentTransform().TransformPosition(SeatLocal));
  Actor->BreastContact->bForceFeedback=false;Actor->BreastContact->SetContactEnabled(Contact);Actor->ResetDeformationAuditState();
  const int Settle=Recipe->GetIntegerField(TEXT("settle_frames"));for(int I=0;I<Settle;++I){if(Contact){double T=FMath::Clamp(I/60.,0.,1.);T=T*T*(3-2*T);Seat->SetWorldLocation(Body->GetComponentTransform().TransformPosition(SeatLocal+FVector(0,0,4*T)));}Tick();}
  const FString CaseDir=Directory/Id;IFileManager::Get().MakeDirectory(*CaseDir,true);
  const auto Pose=Body->GetComponentSpaceTransforms();TArray<TSharedPtr<FJsonValue>> BoneData;for(int I=0;I<Pose.Num();++I){auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("bone"),Ref.GetBoneName(I).ToString());B->SetArrayField(TEXT("component_matrix"),Matrix(Pose[I]));B->SetArrayField(TEXT("local_matrix"),Matrix(Ref.GetParentIndex(I)>=0?Pose[I].GetRelativeTransform(Pose[Ref.GetParentIndex(I)]):Pose[I]));BoneData.Add(MakeShared<FJsonValueObject>(B));}Report->SetArrayField(TEXT("actual_bones"),BoneData);Report->SetArrayField(TEXT("body_to_world"),Matrix(Body->GetComponentTransform()));Report->SetArrayField(TEXT("gravity_in_body"),Numbers(Body->GetComponentTransform().InverseTransformVectorNoScale(FVector(0,0,World->GetGravityZ()))));Report->SetStringField(TEXT("contact_status"),Actor->BreastContact->Diagnostics());Report->SetBoolField(TEXT("contact_solver_active"),Actor->BreastContact->GetActiveSolverCount()>0);Report->SetArrayField(TEXT("seat_world"),Matrix(Seat->GetComponentTransform()));
  TInlineComponentArray<UActorComponent*> Components(Actor);TArray<bool> Enabled;for(auto* Comp:Components){Enabled.Add(Comp->IsComponentTickEnabled());Comp->SetComponentTickEnabled(false);}
  Actor->ExportDeformationAudit();FString Audit=Actor->DeformationAuditStatus;Audit.RemoveFromStart(TEXT("Capturing:"));Report->SetStringField(TEXT("audit_bundle"),Audit);for(int I=0;I<3;++I)Tick();
  TArray<TSharedPtr<FJsonValue>> Cameras;const FVector Directions[]={Right,-Front,Front,(Front+Right).GetSafeNormal()};const TCHAR* Names[]={TEXT("side"),TEXT("rear"),TEXT("front"),TEXT("oblique")};
  for(int V=0;V<4;++V){const FTransform WorldFrame=Body->GetComponentTransform();const FVector Center=WorldFrame.TransformPosition(ViewCenter),Eye=WorldFrame.TransformPosition(ViewCenter+Directions[V]*230);Capture->FOVAngle=42;Capture->SetWorldLocationAndRotation(Eye,FRotationMatrix::MakeFromXZ((Center-Eye).GetSafeNormal(),WorldFrame.TransformVectorNoScale(Up)).Rotator());Light->SetWorldRotation((Center-Eye+WorldFrame.TransformVectorNoScale(-Up)*90).Rotation());
   auto Cam=MakeShared<FJsonObject>();Cam->SetStringField(TEXT("view"),Names[V]);Cam->SetArrayField(TEXT("world_matrix"),Matrix(Capture->GetComponentTransform()));Cam->SetNumberField(TEXT("fov"),42);Cameras.Add(MakeShared<FJsonValueObject>(Cam));
   const TCHAR* Modes[]={TEXT("material"),TEXT("gray"),TEXT("silhouette"),TEXT("wireframe")};
   for(int Mode=0;Mode<4;++Mode){++GFrameCounter;for(int I=0;I<OriginalMaterials.Num();++I)Body->SetMaterial(I,Mode==0?OriginalMaterials[I]:Mode==2?Silhouette:Gray);Capture->ShowFlags=Flags;Capture->ShowFlags.SetWireframe(Mode==3);Capture->ShowFlags.SetDynamicShadows(Mode==0);Seat->SetVisibility(Contact&&Mode!=2);FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();Body->RefreshBoneTransforms();Body->MarkRenderDynamicDataDirty();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();Capture->CaptureScene();FlushRenderingCommands();FBufferArchive PNG;Test.TestTrue(TEXT("Pose suite PNG export"),FImageUtils::ExportRenderTarget2DAsPNG(Capture->TextureTarget,PNG));Test.TestTrue(TEXT("Pose suite PNG save"),FFileHelper::SaveArrayToFile(PNG,*(CaseDir/(FString(Names[V])+TEXT("-")+Modes[Mode]+TEXT(".png")))));}
  }
  Report->SetArrayField(TEXT("cameras"),Cameras);Report->SetNumberField(TEXT("width"),Capture->TextureTarget->SizeX);Report->SetNumberField(TEXT("height"),Capture->TextureTarget->SizeY);Report->SetBoolField(TEXT("temporal_aa"),false);Report->SetBoolField(TEXT("motion_blur"),false);Report->SetNumberField(TEXT("exposure_bias"),2);Report->SetNumberField(TEXT("fixed_dt"),1./60.);Report->SetNumberField(TEXT("settle_frames"),Settle);Report->SetStringField(TEXT("acceptance"),Contact&&Actor->BreastContact->GetActiveSolverCount()==0?TEXT("blocked_contact_solver_inactive"):Clamped?TEXT("captured_with_limit_reduction"):TEXT("captured"));Save(CaseDir/TEXT("state.json"),Report);Results.Add(MakeShared<FJsonValueObject>(Report));
  for(int I=0;I<OriginalMaterials.Num();++I)Body->SetMaterial(I,OriginalMaterials[I]);Capture->ShowFlags=Flags;for(int I=0;I<Components.Num();++I)Components[I]->SetComponentTickEnabled(Enabled[I]);UE_LOG(LogTemp,Display,TEXT("A2_POSE %s limited=%d contact=%d solver=%d"),*Id,Clamped,Contact,Actor->BreastContact->GetActiveSolverCount());++CaseIndex;
 }
 auto Summary=MakeShared<FJsonObject>();Summary->SetStringField(TEXT("schema"),TEXT("vam-pose-captures/1"));Summary->SetStringField(TEXT("recipe"),RecipePath);Summary->SetArrayField(TEXT("states"),Results);Summary->SetStringField(TEXT("comparison_contract"),TEXT("Compare converted actual local/component matrices and source mapping; UI Euler labels are requests only. All dynamics disabled except explicitly requested GPU contact. Failed solver captures are not contact acceptance."));Save(Directory/TEXT("suite.json"),Summary);SeatActor->Destroy();return CaseIndex>0;
}
