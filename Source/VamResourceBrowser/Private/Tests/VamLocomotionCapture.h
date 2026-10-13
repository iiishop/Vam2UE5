#pragma once
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

// Recording-only geometric retarget of shipped mannequin clips. Production rig
// limits and production jiggle stay active; no gameplay controller is replaced.
static bool VamCaptureLocomotion(FAutomationTestBase& Test,AVamCharacterActor* Actor,UWorld* World,USceneCaptureComponent2D* Capture,const FString& Directory,TFunctionRef<void()> Tick)
{
 auto* Body=CastChecked<UVamLegSkeletalMeshComponent>(Actor->Character->Body);
 auto* Anim=CastChecked<UVamShapeAnimInstance>(Body->GetAnimInstance());auto* Rig=Actor->Character->RigProfile.Get();
 const auto& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();const auto Reference=Actor->Character->GetShapeReferencePose();
 auto CS=[&](const TArray<FTransform>& Local){auto R=Local;for(int I=0;I<R.Num();++I)if(Ref.GetParentIndex(I)>=0)R[I]*=R[Ref.GetParentIndex(I)];return R;};
 const auto Neutral=CS(Reference);auto Target=[&](const TCHAR* Name){return Ref.FindBoneIndex(Rig->BoneForSemantic(Name));};
 const int Pelvis=Target(TEXT("pelvis")),MotionRoot=0,Head=Target(TEXT("head"));
 auto* Walk=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"));
 auto* Run=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd"));
 auto* Idle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
 if(!Test.TestTrue(TEXT("Locomotion clips and target rig available"),Walk&&Run&&Idle&&Pelvis>=0&&Head>=0))return false;
 const auto& SR=Walk->GetSkeleton()->GetReferenceSkeleton();auto Source=[&](const TCHAR* N){return SR.FindBoneIndex(N);};
 auto Sample=[&](UAnimSequence* Clip,double Phase){TArray<FTransform> R;R.SetNum(SR.GetNum());for(int I=0;I<R.Num();++I){Clip->GetBoneTransform(R[I],FSkeletonPoseBoneIndex(I),FAnimExtractContext(FMath::Fmod(Phase,1.)*Clip->GetPlayLength(),true),false);if(SR.GetParentIndex(I)>=0)R[I]*=R[SR.GetParentIndex(I)];}const FTransform Root=R[0];for(auto& B:R)B=B.GetRelativeTransform(Root);return R;};
 auto SB=SR.GetRefBonePose();for(int I=0;I<SB.Num();++I)if(SR.GetParentIndex(I)>=0)SB[I]*=SB[SR.GetParentIndex(I)];
 const int SP=Source(TEXT("pelvis")),SF=Source(TEXT("foot_l")),ST=Source(TEXT("ball_l"));
 if(!Test.TestTrue(TEXT("Source anatomical frame available"),SP>=0&&SF>=0&&ST>=0))return false;
 const FVector SUp=FVector::UpVector,SFront=FVector::VectorPlaneProject(SB[ST].GetLocation()-SB[SF].GetLocation(),SUp).GetSafeNormal();
 FVector Feet=FVector::ZeroVector;for(const auto& S:Body->LegRest)if(!S.bCalf)Feet+=Neutral[S.Foot].GetLocation()*.5;
 const FVector Up=(Neutral[Head].GetLocation()-Feet).GetSafeNormal();FVector Front=Neutral[Body->RestSides[0].AnchorBone].GetUnitAxis(EAxis::X);Front=FVector::VectorPlaneProject(Front,Up).GetSafeNormal();const FVector Right=FVector::CrossProduct(Up,Front).GetSafeNormal();
 const FQuat Map=FRotationMatrix::MakeFromXZ(Front,Up).ToQuat()*FRotationMatrix::MakeFromXZ(SFront,SUp).ToQuat().Inverse();
 const double Scale=(Neutral[Pelvis].GetLocation()-Feet).Size()/(SB[SP].GetLocation()-SB[SF].GetLocation()).Size();
 auto ClipSpeed=[&](UAnimSequence* Clip){FTransform A,B;Clip->GetBoneTransform(A,FSkeletonPoseBoneIndex(0),FAnimExtractContext(0.,false),false);Clip->GetBoneTransform(B,FSkeletonPoseBoneIndex(0),FAnimExtractContext(Clip->GetPlayLength()-.00001,false),false);return FVector::VectorPlaneProject(B.GetLocation()-A.GetLocation(),SUp).Size()/Clip->GetPlayLength();};const double WalkSpeed=ClipSpeed(Walk),RunSpeed=ClipSpeed(Run);if(!Test.TestTrue(TEXT("Measured source root speeds available"),WalkSpeed>10&&RunSpeed>WalkSpeed))return false;const auto IdlePose=Sample(Idle,0);
 struct FLink{int Bone,Child,Src,SrcChild;};TArray<FLink> Links;
 auto Link=[&](const TCHAR* T,const TCHAR* TC,const TCHAR* S,const TCHAR* SC){FLink L{Target(T),Target(TC),Source(S),Source(SC)};if(L.Bone>=0&&L.Child>=0&&L.Src>=0&&L.SrcChild>=0)Links.Add(L);};
 Link(TEXT("spine"),TEXT("chest"),TEXT("spine_01"),TEXT("spine_04"));Link(TEXT("chest"),TEXT("neck"),TEXT("spine_04"),TEXT("neck_01"));
 Link(TEXT("neck"),TEXT("head"),TEXT("neck_01"),TEXT("head"));
 for(int Side=0;Side<2;++Side){const FString P=Side==0?TEXT("left_"):TEXT("right_"),S=Side==0?TEXT("_l"):TEXT("_r");
  Link(*(P+TEXT("hip")),*(P+TEXT("knee")),*(TEXT("thigh")+S),*(TEXT("calf")+S));Link(*(P+TEXT("knee")),*(P+TEXT("foot")),*(TEXT("calf")+S),*(TEXT("foot")+S));Link(*(P+TEXT("foot")),*(P+TEXT("toe")),*(TEXT("foot")+S),*(TEXT("ball")+S));
  Link(*(P+TEXT("shoulder")),*(P+TEXT("elbow")),*(TEXT("upperarm")+S),*(TEXT("lowerarm")+S));Link(*(P+TEXT("elbow")),*(P+TEXT("hand")),*(TEXT("lowerarm")+S),*(TEXT("hand")+S));}
 Links.Sort([](const FLink& A,const FLink& B){return A.Bone<B.Bone;});
 int Limited=0;double MaxClamp=0;
 auto Apply=[&](const TArray<FTransform>& Pose){auto Local=Reference;
  auto Put=[&](int B,const FQuat& Desired,const FVector& Translation){auto Current=CS(Local);const int P=Ref.GetParentIndex(B);const FQuat Relative=P>=0?Current[P].GetRotation().Inverse()*Desired:Desired;const FQuat Requested=(Relative*Reference[B].GetRotation().Inverse()).GetNormalized();FQuat Delta=Requested;
   if(const auto* J=Rig->Joints.FindByPredicate([&](const FVamRigJoint& X){return X.Bone==Ref.GetBoneName(B);}))Delta=VamPoseControl::Clamp(*J,Requested.Rotator()).Quaternion();
   const double Error=FMath::RadiansToDegrees(Delta.AngularDistance(Requested));if(Error>.01){++Limited;MaxClamp=FMath::Max(MaxClamp,Error);}Anim->GeometricDebugBones.Add(B);Actor->Character->SetDebugBoneOffset(B,FTransform(Delta,Translation));Local[B].SetRotation((Delta*Reference[B].GetRotation()).GetNormalized());Local[B].AddToTranslation(Translation);};
  const FQuat Q=Map*(Pose[SP].GetRotation()*IdlePose[SP].GetRotation().Inverse())*Map.Inverse()*Neutral[MotionRoot].GetRotation();
  const FVector Shift=Map.RotateVector(Pose[SP].GetLocation()-IdlePose[SP].GetLocation())*Scale;Put(MotionRoot,Q,Shift);
  for(const auto& L:Links){auto Current=CS(Local);const FVector From=(Current[L.Child].GetLocation()-Current[L.Bone].GetLocation()).GetSafeNormal(),To=Map.RotateVector((Pose[L.SrcChild].GetLocation()-Pose[L.Src].GetLocation()).GetSafeNormal());Put(L.Bone,FQuat::FindBetweenNormals(From,To)*Current[L.Bone].GetRotation(),FVector::ZeroVector);}
 };
 auto Smooth=[](double X){X=FMath::Clamp(X,0.,1.);return X*X*(3-2*X);};
 const FTransform Initial=Actor->GetActorTransform();const FVector Start=Initial.GetLocation();
 // Fixed world track markings make translation visible even with a follow camera.
 TArray<AActor*> Props;auto Box=[&](const FVector& Location,const FVector& Size){auto* A=World->SpawnActor<AActor>();auto* M=NewObject<UStaticMeshComponent>(A);A->SetRootComponent(M);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->RegisterComponent();A->SetActorLocation(Location);A->SetActorScale3D(Size/100);Props.Add(A);};
 const FVector WF=Initial.TransformVectorNoScale(Front),WR=Initial.TransformVectorNoScale(Right),WU=Initial.TransformVectorNoScale(Up);const FVector Ground=Initial.TransformPosition(Feet-Up*8);
 Box(Ground+WF*2000-WU*5,FVector(6000,6000,10));for(int I=-2;I<45;++I){Box(Ground+WF*(I*100)+WR*110+WU*1,FVector(8,8,2));Box(Ground+WF*(I*100)-WR*110+WU*1,FVector(8,8,2));}
 for(TActorIterator<APawn> It(World);It;++It)Capture->HideActorComponents(*It);TArray<UPointLightComponent*> Points;Actor->GetComponents(Points);for(auto* P:Points)P->SetIntensity(0);TArray<UDirectionalLightComponent*> ExistingSuns;Actor->GetComponents(ExistingSuns);for(auto* L:ExistingSuns)L->SetIntensity(0);auto* Fill=NewObject<UDirectionalLightComponent>(Actor);Fill->Intensity=.7;Fill->RegisterComponent();Fill->SetWorldRotation((-WF+WR-WU*.3).Rotation());auto* Sun=NewObject<UDirectionalLightComponent>(Actor);Sun->Intensity=3;Sun->RegisterComponent();Sun->SetWorldRotation((WF-WR-WU).Rotation());
 Capture->TextureTarget->ResizeTarget(1280,720);Capture->FOVAngle=60;Capture->PostProcessSettings.AutoExposureBias=1;Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetTemporalAA(false);
 auto Camera=[&](){const FVector P=Actor->GetActorLocation()-Start;const FVector Look=Initial.TransformPosition(Neutral[Pelvis].GetLocation()-Up*18)+P;const FVector Eye=Look-WF*340+WR*75+WU*65;Capture->SetWorldLocationAndRotation(Eye,FRotationMatrix::MakeFromXZ((Look+WF*35-Eye).GetSafeNormal(),WU).Rotator());};
 Apply(IdlePose);Actor->ResetDeformationAuditState();for(int I=0;I<120;++I)Tick();
 const FString VideoDir=Directory/TEXT("video");IFileManager::Get().MakeDirectory(*VideoDir,true);
 auto Numbers=[](const FVector& V){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};};
 auto Matrix=[](const FTransform& T){TArray<TSharedPtr<FJsonValue>> V;auto M=T.ToMatrixWithScale();for(int I=0;I<4;++I)for(int J=0;J<4;++J)V.Add(MakeShared<FJsonValueNumber>(M.M[I][J]));return V;};
 TArray<TSharedPtr<FJsonValue>> Samples;double Distance=0,Phase=0;
 for(int I=0;I<960;++I){const double T=I/60.,Move=Smooth((T-1)/1.)*(1-Smooth((T-10.5)/.2)),RunAlpha=Smooth((T-5)/1.5),Speed=Move*FMath::Lerp(160.,400.,RunAlpha);
  Distance+=Speed/60.;Phase+=Move*FMath::Lerp(160./(WalkSpeed*Scale*Walk->GetPlayLength()),400./(RunSpeed*Scale*Run->GetPlayLength()),RunAlpha)/60.;auto W=Sample(Walk,Phase),R=Sample(Run,Phase);const double PoseAlpha=Smooth((T-1)/1.)*(1-Smooth((T-10.5)/.55));for(int B=0;B<W.Num();++B){FTransform M;M.Blend(W[B],R[B],RunAlpha);W[B].Blend(IdlePose[B],M,PoseAlpha);}Apply(W);
  Actor->SetActorLocation(Start+WF*Distance);Tick();Camera();
  if(I%2)continue;Capture->CaptureScene();ENQUEUE_RENDER_COMMAND(VamLocomotionFence)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();});FlushRenderingCommands();FBufferArchive PNG;
  Test.TestTrue(TEXT("Locomotion PNG export"),FImageUtils::ExportRenderTarget2DAsPNG(Capture->TextureTarget,PNG));Test.TestTrue(TEXT("Locomotion frame save"),FFileHelper::SaveArrayToFile(PNG,*(VideoDir/FString::Printf(TEXT("frame-%04d.png"),I/2))));
  auto S=MakeShared<FJsonObject>();S->SetNumberField(TEXT("time"),T);S->SetNumberField(TEXT("distance_cm"),Distance);S->SetNumberField(TEXT("speed_cm_s"),Speed);S->SetArrayField(TEXT("actor"),Matrix(Actor->GetActorTransform()));S->SetArrayField(TEXT("camera"),Matrix(Capture->GetComponentTransform()));TArray<TSharedPtr<FJsonValue>> Bones,Segments;
  for(const auto& B:Body->GetComponentSpaceTransforms())Bones.Add(MakeShared<FJsonValueArray>(Matrix(B)));S->SetArrayField(TEXT("bones"),Bones);
  for(int J=0;J<Body->LegRest.Num();++J){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Body->LegRest[J].Name.ToString());O->SetNumberField(TEXT("limits"),Body->LegSolvers[J].LimitCorrections);TArray<TSharedPtr<FJsonValue>> Nodes;for(const auto& N:Body->LegSolvers[J].Nodes)Nodes.Add(MakeShared<FJsonValueArray>(Numbers(N.Displacement)));O->SetArrayField(TEXT("nodes_cm"),Nodes);Segments.Add(MakeShared<FJsonValueObject>(O));}S->SetArrayField(TEXT("leg"),Segments);TArray<TSharedPtr<FJsonValue>> Glutes;for(int G=0;G<2;++G){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("side"),G);O->SetNumberField(TEXT("limits"),Body->GluteSolvers[G].LimitCorrections);TArray<TSharedPtr<FJsonValue>> Nodes;for(const auto& N:Body->GluteSolvers[G].Nodes)Nodes.Add(MakeShared<FJsonValueArray>(Numbers(N.Displacement)));O->SetArrayField(TEXT("nodes_cm"),Nodes);Glutes.Add(MakeShared<FJsonValueObject>(O));}S->SetArrayField(TEXT("glute"),Glutes);Samples.Add(MakeShared<FJsonValueObject>(S));
 }
 auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("protocol"),TEXT("Real PIE BP; mannequin idle/walk/jog geometrically retargeted with production joint limits. Scripted world translation (not CharacterMovement navigation). 0-1 idle,1-2 start,2-5 walk,5-6.5 accelerate,6.5-10.5 jog,10.5-10.7 translation stop,10.5-11.05 pose settle,11.05-16 rest. Fixed dt 1/60; capture30fps. Contact disabled."));Report->SetStringField(TEXT("walk"),Walk->GetPathName());Report->SetStringField(TEXT("run"),Run->GetPathName());Report->SetNumberField(TEXT("leg_length_scale"),Scale);Report->SetNumberField(TEXT("walk_speed_cm_s"),160.);Report->SetNumberField(TEXT("run_speed_cm_s"),400.);Report->SetNumberField(TEXT("walk_play_rate"),160./(WalkSpeed*Scale));Report->SetNumberField(TEXT("run_play_rate"),400./(RunSpeed*Scale));Report->SetNumberField(TEXT("retarget_clamp_count"),Limited);Report->SetNumberField(TEXT("max_clamp_degrees"),MaxClamp);Report->SetBoolField(TEXT("glute_jiggle"),Body->bGluteJiggleEnabled);Report->SetBoolField(TEXT("leg_jiggle"),Body->bLegJiggleEnabled);Report->SetArrayField(TEXT("samples"),Samples);
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("locomotion.json")));
 for(auto* A:Props)A->Destroy();Sun->DestroyComponent();Fill->DestroyComponent();Actor->SetActorTransform(Initial);return true;
}
