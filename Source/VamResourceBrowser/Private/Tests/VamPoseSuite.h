#pragma once
#include "VamRigProfile.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "ContentStreaming.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureStreamingTypes.h"

#include "VamUnsafeMorphDiagnostic.h"
#include "VamLocomotionCapture.h"
#include "VamHairComponent.h"

// Explicit opt-in acceptance capture. Never runs during ordinary character play.
static bool VamRunPoseSuite(FAutomationTestBase& Test,AVamCharacterActor* Actor,UWorld* World,USceneCaptureComponent2D* Capture,const FString& Directory,TFunctionRef<void()> Tick)
{
 FString RecipePath;FParse::Value(FCommandLine::Get(),TEXT("VamPoseSuite="),RecipePath);FString Text;
 TSharedPtr<FJsonObject> Recipe;if(!FFileHelper::LoadFileToString(Text,*RecipePath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Recipe))return false;
 auto Save=[](const FString& Path,TSharedPtr<FJsonObject> O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return FFileHelper::SaveStringToFile(S,*Path);};
 auto Numbers=[](const FVector& V){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};};
 auto Matrix=[](const FTransform& T){TArray<TSharedPtr<FJsonValue>> V;const auto M=T.ToMatrixWithScale();for(int I=0;I<4;++I)for(int J=0;J<4;++J)V.Add(MakeShared<FJsonValueNumber>(M.M[I][J]));return V;};
 auto* Body=CastChecked<UVamLegSkeletalMeshComponent>(Actor->Character->Body);FString UnsafeFields;FParse::Value(FCommandLine::Get(),TEXT("VamUnsafeCorrectiveFields="),UnsafeFields);if(!UnsafeFields.IsEmpty()&&!VamInstallUnsafeMorphDiagnostic(Test,Body,UnsafeFields))return false;auto* Rig=Actor->Character->RigProfile.Get();auto* Anim=CastChecked<UVamShapeAnimInstance>(Body->GetAnimInstance());
 if(!Rig||Body->RestSides.IsEmpty())return false;
 const auto& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();const auto Reference=Actor->Character->GetShapeReferencePose();auto Neutral=Reference;
 for(int I=0;I<Neutral.Num();++I)if(Ref.GetParentIndex(I)>=0)Neutral[I]*=Neutral[Ref.GetParentIndex(I)];
 const int Head=Ref.FindBoneIndex(Rig->BoneForSemantic(TEXT("head")));FVector Feet=FVector::ZeroVector,Pelvis=FVector::ZeroVector;int Sides=0;
 for(const auto& S:Body->LegRest)if(!S.bCalf){Feet+=Neutral[S.Foot].GetLocation();Pelvis+=Neutral[S.Thigh].GetLocation();++Sides;}if(!Sides||Head<0)return false;Feet/=Sides;Pelvis/=Sides;
 const FVector Up=(Neutral[Head].GetLocation()-Feet).GetSafeNormal();FVector Front=Neutral[Body->RestSides[0].AnchorBone].GetUnitAxis(EAxis::X);Front=(Front-Up*FVector::DotProduct(Front,Up)).GetSafeNormal();const FVector Right=FVector::CrossProduct(Up,Front).GetSafeNormal();
 const bool HairView=FParse::Param(FCommandLine::Get(),TEXT("VamHairView"));const FVector ViewCenter=HairView?Neutral[Head].GetLocation()+Up*5:Pelvis-Up*25;const FTransform Initial=Actor->GetActorTransform();
 TArray<UMaterialInterface*> OriginalMaterials;for(int I=0;I<Body->GetNumMaterials();++I)OriginalMaterials.Add(Body->GetMaterial(I));
 Body->SkinCacheUsage.Init(ESkinCacheUsage::Enabled,Body->GetSkeletalMeshAsset()->GetLODNum());Body->MarkRenderStateDirty();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 auto MakeMaterial=[&](bool Silhouette,FLinearColor DebugColor=FLinearColor::White){auto* M=NewObject<UMaterial>(GetTransientPackage());M->SetShadingModel(Silhouette?MSM_Unlit:MSM_DefaultLit);auto* C=NewObject<UMaterialExpressionConstant3Vector>(M);C->Constant=Silhouette?DebugColor:FLinearColor(.45,.45,.45);M->GetExpressionCollection().AddExpression(C);if(Silhouette)M->GetEditorOnlyData()->EmissiveColor.Expression=C;else{M->GetEditorOnlyData()->BaseColor.Expression=C;M->GetEditorOnlyData()->Roughness.UseConstant=true;M->GetEditorOnlyData()->Roughness.Constant=.8;}M->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);M->SetUsageByFlag(MATUSAGE_MorphTargets,true);M->SetUsageByFlag(MATUSAGE_MeshDeformer,true);M->SetFeatureLevelToCompile(World->GetFeatureLevel(),true);M->PostEditChange();M->ForceRecompileForRendering();return M;};
 TStrongObjectPtr<UMaterial> GrayOwner(MakeMaterial(false)),SilhouetteOwner(MakeMaterial(true));auto* Gray=GrayOwner.Get();auto* Silhouette=SilhouetteOwner.Get();Capture->PostProcessSettings.AutoExposureBias=2;FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 TArray<TStrongObjectPtr<UMaterial>> SectionColors;for(const auto& Color:{FLinearColor(.8,.15,.1),FLinearColor(.1,.7,.25),FLinearColor(.1,.25,.8)})SectionColors.Emplace(MakeMaterial(true,Color));
 auto* SeatActor=World->SpawnActor<AActor>();auto* Seat=NewObject<UStaticMeshComponent>(SeatActor);SeatActor->SetRootComponent(Seat);Seat->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Seat->SetMobility(EComponentMobility::Movable);Seat->SetCollisionResponseToAllChannels(ECR_Block);Seat->RegisterComponent();Seat->SetWorldScale3D(FVector(1.1,1.1,.1));
 auto* Light=NewObject<UDirectionalLightComponent>(Actor);Light->Intensity=3;Light->RegisterComponent();
 Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);

 const auto Flags=Capture->ShowFlags;TArray<TSharedPtr<FJsonValue>> Results;
 int CaseIndex=0;FString Only;FParse::Value(FCommandLine::Get(),TEXT("VamPoseCase="),Only);
 for(const auto& Value:Recipe->GetArrayField(TEXT("cases"))){const auto C=Value->AsObject();const FString Id=C->GetStringField(TEXT("id"));if(!Only.IsEmpty()&&Only!=Id)continue;
  FString HairQuality;if(C->TryGetStringField(TEXT("hair_quality"),HairQuality)&&Actor->Hair)Actor->Hair->SetHairQuality(HairQuality==TEXT("coarse")?EVamHairQuality::Coarse:EVamHairQuality::Fine);
  bool Dynamic=false;C->TryGetBoolField(TEXT("jiggle"),Dynamic);Actor->BreastContact->SetContactEnabled(false);Actor->BodyContactResponse->bEnabled=false;Body->bJiggleEnabled=Body->bGluteJiggleEnabled=Body->bLegJiggleEnabled=Dynamic;
  C->TryGetBoolField(TEXT("breast_jiggle"),Body->bJiggleEnabled);C->TryGetBoolField(TEXT("glute_jiggle"),Body->bGluteJiggleEnabled);C->TryGetBoolField(TEXT("leg_jiggle"),Body->bLegJiggleEnabled);
  double Coherence=.75;C->TryGetNumberField(TEXT("glute_coherence"),Coherence);Body->GluteCoherence=FMath::Clamp(Coherence,0.,1.);
  Actor->Character->ResetDebugBoneOffsets();Actor->Character->ResetPoseControlRotations();Actor->Character->SetFootLocked(TEXT("left_foot"),false);Actor->Character->SetFootLocked(TEXT("right_foot"),false);Actor->Character->ClearIKGoal(TEXT("pelvis"));
  bool Structure=true,Corrective=true;C->TryGetBoolField(TEXT("structure"),Structure);C->TryGetBoolField(TEXT("corrective"),Corrective);Body->bGluteEnabled=Structure;Body->bCorrectiveEnabled=Corrective;
  Actor->ResetDeformationAuditState();auto Local=Reference;const auto Orient=C->GetArrayField(TEXT("orientation"));Actor->SetActorTransform(FTransform(FRotator(Orient[0]->AsNumber(),Orient[1]->AsNumber(),Orient[2]->AsNumber()).Quaternion()*Initial.GetRotation(),Initial.GetLocation(),Initial.GetScale3D()));
  auto Report=MakeShared<FJsonObject>();Report->SetObjectField(TEXT("request"),C);Report->SetStringField(TEXT("id"),Id);Report->SetStringField(TEXT("unsafe_corrective_diagnostic"),UnsafeFields);Report->SetBoolField(TEXT("jiggle_enabled"),Dynamic);Report->SetNumberField(TEXT("glute_coherence"),Body->GluteCoherence);Report->SetBoolField(TEXT("structure_enabled"),Structure);Report->SetBoolField(TEXT("corrective_enabled"),Corrective);Report->SetStringField(TEXT("actor_class"),Actor->GetClass()->GetPathName());Report->SetStringField(TEXT("world_type"),World->WorldType==EWorldType::PIE?TEXT("PIE"):World->WorldType==EWorldType::Editor?TEXT("Editor"):TEXT("Game"));TArray<TSharedPtr<FJsonValue>> Limits;bool Clamped=false;
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
  Report->SetBoolField(TEXT("glute_jiggle_enabled"),Body->bGluteJiggleEnabled);Report->SetBoolField(TEXT("leg_jiggle_enabled"),Body->bLegJiggleEnabled);Report->SetBoolField(TEXT("breast_jiggle_enabled"),Body->bJiggleEnabled);
  for(int I=0;I<10;++I)Tick();
  bool SoftContact=false;C->TryGetBoolField(TEXT("soft_contact"),SoftContact);const bool Contact=C->GetBoolField(TEXT("contact"));const bool SolveContact=Contact||SoftContact;Actor->BreastContact->bWorldCollision=!SoftContact;Seat->SetVisibility(Contact);Seat->SetCollisionEnabled(Contact?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
  FBox Region(ForceInit);for(const auto& S:Body->GluteRest)for(int I=0;I<S.RegionPoints.Num();++I)if(S.RegionWeights[I]>.35)Region+=Body->GetComponentSpaceTransforms()[S.AnchorBone].TransformPosition(S.RegionPoints[I]);
  FVector SeatLocal=Region.GetCenter();SeatLocal.Z=Region.Min.Z-5-2;Seat->SetWorldLocation(Body->GetComponentTransform().TransformPosition(SeatLocal));
  Actor->BreastContact->bForceFeedback=false;Actor->BreastContact->SetContactEnabled(SolveContact);Actor->ResetDeformationAuditState();
  double MotionAmplitude=2;C->TryGetNumberField(TEXT("motion_amplitude_cm"),MotionAmplitude);Report->SetNumberField(TEXT("motion_amplitude_cm"),MotionAmplitude);
  const int Settle=Recipe->GetIntegerField(TEXT("settle_frames"));for(int I=0;I<Settle;++I){if(Dynamic)Actor->SetActorLocation(Initial.GetLocation()+FVector(0,0,MotionAmplitude*FMath::Sin(2*PI*I/60.)));if(Contact){double T=FMath::Clamp(I/60.,0.,1.);T=T*T*(3-2*T);Seat->SetWorldLocation(Body->GetComponentTransform().TransformPosition(SeatLocal+FVector(0,0,4*T)));}Tick();}
  const FString CaseDir=Directory/Id;IFileManager::Get().MakeDirectory(*CaseDir,true);
  const auto Pose=Body->GetComponentSpaceTransforms();TArray<TSharedPtr<FJsonValue>> BoneData;for(int I=0;I<Pose.Num();++I){auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("bone"),Ref.GetBoneName(I).ToString());B->SetArrayField(TEXT("component_matrix"),Matrix(Pose[I]));B->SetArrayField(TEXT("local_matrix"),Matrix(Ref.GetParentIndex(I)>=0?Pose[I].GetRelativeTransform(Pose[Ref.GetParentIndex(I)]):Pose[I]));BoneData.Add(MakeShared<FJsonValueObject>(B));}Report->SetArrayField(TEXT("actual_bones"),BoneData);Report->SetArrayField(TEXT("body_to_world"),Matrix(Body->GetComponentTransform()));Report->SetArrayField(TEXT("gravity_in_body"),Numbers(Body->GetComponentTransform().InverseTransformVectorNoScale(FVector(0,0,World->GetGravityZ()))));Report->SetStringField(TEXT("contact_status"),Actor->BreastContact->Diagnostics());Report->SetBoolField(TEXT("contact_solver_active"),Actor->BreastContact->GetActiveSolverCount()>0);Report->SetArrayField(TEXT("seat_world"),Matrix(Seat->GetComponentTransform()));
  TInlineComponentArray<UActorComponent*> Components(Actor);TArray<bool> Enabled;for(auto* Comp:Components){Enabled.Add(Comp->IsComponentTickEnabled());Comp->SetComponentTickEnabled(false);}
  Actor->ExportDeformationAudit();FString Audit=Actor->DeformationAuditStatus;Audit.RemoveFromStart(TEXT("Capturing:"));Report->SetStringField(TEXT("audit_bundle"),Audit);for(int I=0;I<3;++I)Tick();
  TArray<TSharedPtr<FJsonValue>> Cameras;const FVector Directions[]={Right,-Front,Front,(Front+Right).GetSafeNormal()};const TCHAR* Names[]={TEXT("side"),TEXT("rear"),TEXT("front"),TEXT("oblique")};
  for(int V=0;V<4;++V){const FTransform WorldFrame=Body->GetComponentTransform();const FVector Center=WorldFrame.TransformPosition(ViewCenter),Eye=WorldFrame.TransformPosition(ViewCenter+Directions[V]*(HairView?100:230));Capture->FOVAngle=42;Capture->SetWorldLocationAndRotation(Eye,FRotationMatrix::MakeFromXZ((Center-Eye).GetSafeNormal(),WorldFrame.TransformVectorNoScale(Up)).Rotator());const bool LightFromBelow=FParse::Param(FCommandLine::Get(),TEXT("VamPoseLightFromBelow"));Light->SetWorldRotation((Center-Eye+WorldFrame.TransformVectorNoScale(LightFromBelow?Up:-Up)*90).Rotation());Report->SetBoolField(TEXT("light_from_below"),LightFromBelow);
   float LightYaw=0;FParse::Value(FCommandLine::Get(),TEXT("VamPoseLightYaw="),LightYaw);Light->AddWorldRotation(FRotator(0,LightYaw,0));Report->SetNumberField(TEXT("light_yaw_offset"),LightYaw);
   for(int I=0;I<OriginalMaterials.Num();++I)Body->SetMaterial(I,OriginalMaterials[I]);World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
   if(FParse::Param(FCommandLine::Get(),TEXT("VamPoseStreaming")))
   {
    // A manually ticked capture world has no player viewport to supply streaming views.
    IStreamingManager::Get().AddViewInformation(Eye,Capture->TextureTarget->SizeX,Capture->TextureTarget->SizeX/FMath::Tan(FMath::DegreesToRadians(21.f)),1.f,false,5.f,nullptr,World);
    IStreamingManager::Get().NotifyPrimitiveUpdated(Body);
    IStreamingManager::Get().UpdateResourceStreaming(0,true);
    IStreamingManager::Get().StreamAllResources(30.f);
   }
   auto Cam=MakeShared<FJsonObject>();Cam->SetStringField(TEXT("view"),Names[V]);Cam->SetArrayField(TEXT("world_matrix"),Matrix(Capture->GetComponentTransform()));Cam->SetNumberField(TEXT("fov"),42);Cameras.Add(MakeShared<FJsonValueObject>(Cam));
   const TCHAR* Modes[]={TEXT("material"),TEXT("gray"),TEXT("silhouette"),TEXT("wireframe"),TEXT("unlit"),TEXT("world-normal"),TEXT("base-color"),TEXT("section-boundary")};
   for(int Mode=0;Mode<(FParse::Param(FCommandLine::Get(),TEXT("VamSeamAudit"))?8:4);++Mode){++GFrameCounter;for(int I=0;I<OriginalMaterials.Num();++I)Body->SetMaterial(I,Mode==7?SectionColors[I%SectionColors.Num()].Get():(Mode==0||Mode>=4)?OriginalMaterials[I]:Mode==2?Silhouette:Gray);Capture->ShowFlags=Flags;Capture->CaptureSource=Mode==5?SCS_Normal:Mode==6?SCS_BaseColor:SCS_FinalColorLDR;Capture->ShowFlags.SetLighting(Mode!=4);Capture->ShowFlags.SetWireframe(Mode==3);Capture->ShowFlags.SetDynamicShadows(Mode==0);Seat->SetVisibility(Contact&&Mode!=2);FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();Body->RefreshBoneTransforms();Body->MarkRenderDynamicDataDirty();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();Capture->CaptureScene();ENQUEUE_RENDER_COMMAND(VamPoseCaptureFence)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();});FlushRenderingCommands();FBufferArchive PNG;Test.TestTrue(TEXT("Pose suite PNG export"),FImageUtils::ExportRenderTarget2DAsPNG(Capture->TextureTarget,PNG));Test.TestTrue(TEXT("Pose suite PNG save"),FFileHelper::SaveArrayToFile(PNG,*(CaseDir/(FString(Names[V])+TEXT("-")+Modes[Mode]+TEXT(".png")))));}
  }
  FString InvestigateTexture;if(FParse::Value(FCommandLine::Get(),TEXT("VamStreamingProbe="),InvestigateTexture))GEngine->Exec(World,*(TEXT("InvestigateTexture ")+InvestigateTexture));
  TArray<TSharedPtr<FJsonValue>> StreamingRequests;FStreamingTextureLevelContext StreamingContext(EMaterialQualityLevel::High,Body);TArray<FStreamingRenderAssetPrimitiveInfo> StreamingInfos;Body->GetStreamingRenderAssetInfo(StreamingContext,StreamingInfos);
  for(const auto& Info:StreamingInfos){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("asset"),GetPathNameSafe(Info.RenderAsset));R->SetNumberField(TEXT("texel_factor"),Info.TexelFactor);R->SetNumberField(TEXT("radius"),Info.Bounds.SphereRadius);StreamingRequests.Add(MakeShared<FJsonValueObject>(R));}Report->SetArrayField(TEXT("streaming_requests"),StreamingRequests);
  TArray<TSharedPtr<FJsonValue>> SamplingCaches;for(int M=0;M<OriginalMaterials.Num();++M)if(auto* Material=OriginalMaterials[M]){auto R=MakeShared<FJsonObject>();const auto Cache=Material->GetCachedTexturesSamplingInfo();R->SetNumberField(TEXT("material"),M);R->SetStringField(TEXT("path"),Material->GetPathName());R->SetNumberField(TEXT("cache_entries"),Cache?Cache->TextureSamplingInfos.Num():-1);R->SetBoolField(TEXT("mesh_uses_material"),Body->GetSkinnedAsset()->IsMaterialUsed(M));SamplingCaches.Add(MakeShared<FJsonValueObject>(R));}Report->SetArrayField(TEXT("sampling_caches"),SamplingCaches);
  TArray<TSharedPtr<FJsonValue>> TextureState;TSet<UTexture*> SeenTextures;
  for(auto* Material:OriginalMaterials) if(Material){TArray<UTexture*> Textures;Material->GetUsedTextures(Textures);for(auto* Texture:Textures)if(auto* T=Cast<UTexture2D>(Texture);T&&!SeenTextures.Contains(T)){SeenTextures.Add(T);auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("texture"),T->GetPathName());R->SetNumberField(TEXT("mips"),T->GetNumMips());R->SetNumberField(TEXT("resident"),T->GetNumResidentMips());R->SetNumberField(TEXT("size_x"),T->GetSizeX());TextureState.Add(MakeShared<FJsonValueObject>(R));}}
  TArray<TSharedPtr<FJsonValue>> Density;for(int32 M=0;M<Body->GetSkeletalMeshAsset()->GetMaterials().Num();++M){const auto* UV=Body->GetSkeletalMeshAsset()->GetUVChannelData(M);auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("material"),M);R->SetBoolField(TEXT("initialized"),UV&&UV->bInitialized);R->SetNumberField(TEXT("density"),UV?UV->LocalUVDensities[0]:-1);Density.Add(MakeShared<FJsonValueObject>(R));}Report->SetArrayField(TEXT("uv_densities"),Density);
  Report->SetArrayField(TEXT("texture_residency"),TextureState);Report->SetBoolField(TEXT("streaming_view_supplied"),FParse::Param(FCommandLine::Get(),TEXT("VamPoseStreaming")));
  Report->SetArrayField(TEXT("cameras"),Cameras);Report->SetNumberField(TEXT("width"),Capture->TextureTarget->SizeX);Report->SetNumberField(TEXT("height"),Capture->TextureTarget->SizeY);Report->SetBoolField(TEXT("temporal_aa"),false);Report->SetBoolField(TEXT("motion_blur"),false);Report->SetNumberField(TEXT("exposure_bias"),2);Report->SetNumberField(TEXT("fixed_dt"),1./60.);Report->SetNumberField(TEXT("settle_frames"),Settle);Report->SetStringField(TEXT("acceptance"),SolveContact&&Actor->BreastContact->GetActiveSolverCount()==0?TEXT("blocked_contact_solver_inactive"):Clamped?TEXT("captured_with_limit_reduction"):TEXT("captured"));Save(CaseDir/TEXT("state.json"),Report);Results.Add(MakeShared<FJsonValueObject>(Report));
  for(int I=0;I<OriginalMaterials.Num();++I)Body->SetMaterial(I,OriginalMaterials[I]);Capture->CaptureSource=SCS_FinalColorLDR;Capture->ShowFlags=Flags;for(int I=0;I<Components.Num();++I)Components[I]->SetComponentTickEnabled(Enabled[I]);UE_LOG(LogTemp,Display,TEXT("A2_POSE %s limited=%d contact=%d solver=%d"),*Id,Clamped,Contact,Actor->BreastContact->GetActiveSolverCount());++CaseIndex;
  // Opt-in real-time-sequence evidence using the same normal character, camera
  // and solver. Encoding uses fixed simulated time, not capture wall time.
  int32 VideoFrames=0;C->TryGetNumberField(TEXT("video_frames"),VideoFrames);
  if(VideoFrames>0)
  {
   FString LocomotionAction;C->TryGetStringField(TEXT("video_action"),LocomotionAction);if(LocomotionAction==TEXT("locomotion")){if(!VamCaptureLocomotion(Test,Actor,World,Capture,CaseDir,Tick))return false;continue;}
   bool LegPipeline=false;C->TryGetBoolField(TEXT("leg_pipeline"),LegPipeline);FString Direction;C->TryGetStringField(TEXT("video_direction"),Direction);const FVector MotionDirection=Direction==TEXT("front")?Initial.TransformVectorNoScale(Front):FVector::UpVector;
   const FVector Start=Initial.GetLocation();Actor->SetActorLocation(Start);Actor->ResetDeformationAuditState();
   bool PosePreloadProbe=false;C->TryGetBoolField(TEXT("leg_pose_preload_probe"),PosePreloadProbe);
   if(PosePreloadProbe)for(auto& S:Body->LegRest){const FTransform Anchor=S.AnchorLocal*Body->GetComponentSpaceTransforms()[S.bCalf?S.Shin:S.Thigh]*Body->GetComponentTransform();S.Dynamics.ReferenceGravityLocal=Anchor.GetRotation().UnrotateVector(FVector(0,0,World->GetGravityZ()));}
   for(int I=0;I<120;++I)Tick();
   const FVector Center=Initial.TransformPosition(ViewCenter),Eye=Initial.TransformPosition(ViewCenter+(Front+Right).GetSafeNormal()*230);
   Capture->SetWorldLocationAndRotation(Eye,FRotationMatrix::MakeFromXZ((Center-Eye).GetSafeNormal(),Initial.TransformVectorNoScale(Up)).Rotator());
   const FString VideoDir=CaseDir/TEXT("video");IFileManager::Get().MakeDirectory(*VideoDir,true);TArray<TSharedPtr<FJsonValue>> Samples;
   for(int I=0;I<VideoFrames;++I)
   {
    const double Ramp=FMath::Clamp(I/30.,0.,1.),Z=I<180?MotionAmplitude*Ramp*Ramp*(3-2*Ramp)*FMath::Sin(2*PI*I/60.):0;
    FString Action;C->TryGetStringField(TEXT("video_action"),Action);
    if(Action==TEXT("flexion") || Action==TEXT("step"))
    {
     auto Animated=Reference;
     for(const auto& S:Body->LegRest)if(!S.bCalf)
     {
      const double Wave=I<240?FMath::Sin(2*PI*I/120.+(Action==TEXT("step")?S.Side*PI:0)):0;
      const double H=FMath::DegreesToRadians(Action==TEXT("step")?25*Wave:Ramp*35*(1-Wave));
      const double K=FMath::DegreesToRadians(Action==TEXT("step")?45*FMath::Max(0.,Wave):Ramp*35*(1-Wave));
      const FVector Targets[]={Front*FMath::Sin(H)-Up*FMath::Cos(H),Front*FMath::Sin(H-K)-Up*FMath::Cos(H-K)};const int Bones[]={S.Thigh,S.Shin,S.Foot};
      for(int J=0;J<3;++J){auto CS=Animated;for(int B=0;B<CS.Num();++B)if(Ref.GetParentIndex(B)>=0)CS[B]*=CS[Ref.GetParentIndex(B)];const int B=Bones[J],Parent=Ref.GetParentIndex(B);FQuat Desired=Neutral[B].GetRotation();if(J<2)Desired=FQuat::FindBetweenNormals((CS[Bones[J+1]].GetLocation()-CS[B].GetLocation()).GetSafeNormal(),Targets[J])*CS[B].GetRotation();const FQuat Relative=(CS[Parent].GetRotation().Inverse()*Desired).GetNormalized();const auto* Joint=Rig->Joints.FindByPredicate([&](const FVamRigJoint& X){return X.Bone==Ref.GetBoneName(B);});if(!Joint)return false;const FQuat Delta=VamPoseControl::Clamp(*Joint,(Relative*Reference[B].GetRotation().Inverse()).Rotator()).Quaternion();Anim->GeometricDebugBones.Add(B);Actor->Character->SetDebugBoneOffset(B,FTransform(Delta));Animated[B].SetRotation((Delta*Reference[B].GetRotation()).GetNormalized());}
     }
    }
    Actor->SetActorLocation(Start+MotionDirection*Z);Tick();
    if(I%2==0)
    {
     Capture->CaptureScene();ENQUEUE_RENDER_COMMAND(VamJiggleVideoFence)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();});FlushRenderingCommands();
     FBufferArchive PNG;Test.TestTrue(TEXT("Jiggle video PNG export"),FImageUtils::ExportRenderTarget2DAsPNG(Capture->TextureTarget,PNG));Test.TestTrue(TEXT("Jiggle video frame save"),FFileHelper::SaveArrayToFile(PNG,*(VideoDir/FString::Printf(TEXT("frame-%04d.png"),I/2))));
     auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("frame"),I/2);Sample->SetNumberField(TEXT("time_seconds"),I/60.);Sample->SetNumberField(TEXT("root_z_cm"),MotionDirection.Z*Z);Sample->SetArrayField(TEXT("root_offset_cm"),Numbers(MotionDirection*Z));TArray<TSharedPtr<FJsonValue>> Nodes;
     const auto& Current=Body->GetComponentSpaceTransforms();for(int B=0;B<Current.Num();++B)if(Ref.GetBoneName(B).ToString().Contains(TEXT("_Thigh_"))||Ref.GetBoneName(B).ToString().Contains(TEXT("_Glute_"))){auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("bone"),Ref.GetBoneName(B).ToString());N->SetArrayField(TEXT("matrix"),Matrix(Current[B]));Nodes.Add(MakeShared<FJsonValueObject>(N));}Sample->SetArrayField(TEXT("helper_matrices"),Nodes);Samples.Add(MakeShared<FJsonValueObject>(Sample));
     if(LegPipeline)
     {
      TArray<TSharedPtr<FJsonValue>> Segments;
      for(int S=0;S<Body->LegRest.Num();++S)
      {
       const auto& Segment=Body->LegRest[S];const auto& Solver=Body->LegSolvers[S];auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Segment.Name.ToString());Row->SetNumberField(TEXT("steps"),Solver.LastSteps);Row->SetNumberField(TEXT("samples"),Solver.Samples);Row->SetNumberField(TEXT("limits"),Solver.LimitCorrections);Row->SetBoolField(TEXT("sleeping"),Solver.bSleeping);Row->SetArrayField(TEXT("acceleration"),Numbers(Solver.LinearAcceleration));Row->SetArrayField(TEXT("omega"),Numbers(Solver.Omega));Row->SetArrayField(TEXT("gravity_residual"),Numbers(Solver.GravityResidualLocal));Row->SetArrayField(TEXT("anchor"),Matrix(Current[Segment.AnchorBone]));TArray<TSharedPtr<FJsonValue>> Particles;
       for(int N=0;N<5;++N){const auto& P=Solver.Nodes[N];const auto& Node=Segment.Dynamics.Nodes[N];auto Particle=MakeShared<FJsonObject>();Particle->SetStringField(TEXT("bone"),Ref.GetBoneName(Node.BoneIndex).ToString());Particle->SetArrayField(TEXT("raw_cm"),Numbers(P.Displacement));Particle->SetArrayField(TEXT("final_cm"),Numbers(Current[Segment.AnchorBone].InverseTransformPosition(Current[Node.BoneIndex].GetLocation())-Node.Rest));Particle->SetArrayField(TEXT("matrix"),Matrix(Current[Node.BoneIndex]));Particle->SetArrayField(TEXT("support"),Numbers(P.Support));Particle->SetArrayField(TEXT("damping"),Numbers(P.Damping));Particle->SetArrayField(TEXT("travel"),Numbers(P.Travel));Particle->SetNumberField(TEXT("mass"),P.Mass);Particle->SetNumberField(TEXT("nonlinear_travel"),P.NonlinearTravel);Particle->SetNumberField(TEXT("tension"),Body->LegTension[S][N]);Particles.Add(MakeShared<FJsonValueObject>(Particle));}
       Row->SetArrayField(TEXT("nodes"),Particles);Segments.Add(MakeShared<FJsonValueObject>(Row));
      }
      Sample->SetArrayField(TEXT("leg_pipeline"),Segments);TArray<TSharedPtr<FJsonValue>> BoneFrames;for(const auto& Bone:Current)BoneFrames.Add(MakeShared<FJsonValueArray>(Matrix(Bone)));Sample->SetArrayField(TEXT("bone_matrices"),BoneFrames);
     }
    }
   }
   auto Video=MakeShared<FJsonObject>();Video->SetStringField(TEXT("protocol"),TEXT("120 frames stationary warmup; 0-3s 1Hz translation along motion_direction with 0.5s onset ramp; 3s stop; remainder free decay. dt=1/60, images every 2 frames, encode 30fps. Contact off unless recipe enables it."));Video->SetObjectField(TEXT("request"),C);Video->SetArrayField(TEXT("motion_direction"),Numbers(MotionDirection));Video->SetBoolField(TEXT("diagnostic_pose_preload"),PosePreloadProbe);Video->SetArrayField(TEXT("samples"),Samples);Save(CaseDir/TEXT("video.json"),Video);
  }
 }
 auto Summary=MakeShared<FJsonObject>();Summary->SetStringField(TEXT("schema"),TEXT("vam-pose-captures/1"));Summary->SetStringField(TEXT("recipe"),RecipePath);Summary->SetArrayField(TEXT("states"),Results);Summary->SetStringField(TEXT("comparison_contract"),TEXT("Compare converted actual local/component matrices and source mapping; UI Euler labels are requests only. Dynamics disabled unless explicitly requested jiggle or GPU contact. Failed solver captures are not contact acceptance."));Save(Directory/TEXT("suite.json"),Summary);SeatActor->Destroy();return CaseIndex>0;
}
