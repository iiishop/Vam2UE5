#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamRuntimeConfiguration.h"
#include "VamLegSkeletalMeshComponent.h"
#include "VamBreastContactComponent.h"
#include "VamBodyContactResponseComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Animation/MorphTarget.h"
#include "BoneContainer.h"
#include "Animation/MeshDeformer.h"
#include "Animation/MeshDeformerGeometryReadback.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UObject/UObjectIterator.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

namespace VamAudit
{
using J=TSharedPtr<FJsonObject>;using A=TArray<TSharedPtr<FJsonValue>>;
J Obj(){return MakeShared<FJsonObject>();}
TSharedPtr<FJsonValue> Num(double V){return MakeShared<FJsonValueNumber>(V);}
A Vec(const FVector& V){return {Num(V.X),Num(V.Y),Num(V.Z)};}
A Matrix(const FTransform& T){A R;const auto M=T.ToMatrixWithScale();for(int I=0;I<4;++I)for(int K=0;K<4;++K)R.Add(Num(M.M[I][K]));return R;}
void Save(const FString& File,const J& O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));FFileHelper::SaveStringToFile(S,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
void Points(const FString& File,const TArray<FVector>& P){auto O=Obj();A V;for(const auto& X:P)V.Add(MakeShared<FJsonValueArray>(Vec(X)));O->SetStringField(TEXT("space"),TEXT("body_component_cm"));O->SetArrayField(TEXT("positions"),V);Save(File,O);}
J Properties(UObject* O){auto R=Obj();if(!O){R->SetStringField(TEXT("status"),TEXT("absent"));return R;}R->SetStringField(TEXT("path"),O->GetPathName());R->SetStringField(TEXT("class"),O->GetClass()->GetPathName());
 for(TFieldIterator<FProperty> I(O->GetClass());I;++I){auto* P=*I;if(P->ArrayDim!=1)continue;
  if(CastField<FBoolProperty>(P)||CastField<FNumericProperty>(P)||CastField<FStrProperty>(P)||CastField<FNameProperty>(P)||CastField<FSoftObjectProperty>(P)){
   FString S;P->ExportText_InContainer(0,S,O,O,O,PPF_None);R->SetStringField(P->GetName(),S);}}
 return R;}
TMap<FName,float> Morphs(USkeletalMeshComponent* B){TMap<FName,float> R;for(const auto& P:B->ActiveMorphTargets)if(P.Key&&B->MorphTargetWeights.IsValidIndex(P.Value))R.Add(P.Key->GetFName(),B->MorphTargetWeights[P.Value]);return R;}
J MorphJson(const TMap<FName,float>& M){auto O=Obj();for(const auto& P:M)O->SetNumberField(P.Key.ToString(),P.Value);return O;}
// Evaluates the exact stored LOD0 render weights and morph deltas, without changing
// the live component's skinning mode, material, animation or solver state.
bool Surface(const FString& Dir,USkeletalMesh* Mesh,FSkinWeightVertexBuffer* Override,const TArray<FTransform>& Pose,const TMap<FName,float>& Morph,J& Metadata,const TArray<FMatrix44f>* InverseOverride=nullptr)
{
 Metadata=Obj();Metadata->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh));
 if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty()){Metadata->SetStringField(TEXT("error"),TEXT("render data unavailable"));return false;}
 auto& L=Mesh->GetResourceForRendering()->LODRenderData[0];auto* W=Override?Override:&L.SkinWeightVertexBuffer;
 if(!W->GetDataVertexBuffer()->GetWeightData()||Pose.Num()!=Mesh->GetRefSkeleton().GetNum()) {Metadata->SetStringField(TEXT("error"),TEXT("CPU weight access or pose unavailable"));return false;}
 const auto& Inverse=InverseOverride?*InverseOverride:Mesh->GetRefBasesInvMatrix();
 TArray<FVector> V;const int N=L.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();for(int I=0;I<N;++I)V.Add(FVector(L.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I)));
 for(const auto& M:Morph){auto* T=Mesh->FindMorphTarget(M.Key);if(!T||M.Value==0)continue;const auto& Models=T->GetMorphLODModels();if(Models.IsEmpty())continue;for(const auto& D:Models[0].Vertices)if(V.IsValidIndex(D.SourceIdx))V[D.SourceIdx]+=FVector(D.PositionDelta)*M.Value;}
 TArray<FVector> Out;Out.SetNumZeroed(N);A Weights;Weights.SetNum(N);
 for(const auto& S:L.RenderSections)for(uint32 I=S.BaseVertexIndex;I<S.BaseVertexIndex+S.NumVertices;++I){A Row;
  for(uint32 K=0;K<W->GetMaxBoneInfluences();++K){const uint16 Raw=W->GetBoneWeight(I,K);if(!Raw)continue;const int B=S.BoneMap[W->GetBoneIndex(I,K)];const double Weight=Raw/65535.;
   Row.Add(MakeShared<FJsonValueArray>(A{Num(B),Num(Weight)}));const FVector BindPoint=FVector(Inverse[B].TransformPosition(FVector3f(V[I])));Out[I]+=Pose[B].TransformPosition(BindPoint)*Weight;}
  Weights[I]=MakeShared<FJsonValueArray>(Row);}
 IFileManager::Get().MakeDirectory(*Dir,true);Points(Dir/TEXT("preskin.json"),V);Points(Dir/TEXT("surface.json"),Out);auto Data=Obj();Data->SetArrayField(TEXT("render_vertex_influences"),Weights);
 A Bones;for(int B=0;B<Pose.Num();++B){auto O=Obj();O->SetStringField(TEXT("name"),Mesh->GetRefSkeleton().GetBoneName(B).ToString());O->SetNumberField(TEXT("parent"),Mesh->GetRefSkeleton().GetParentIndex(B));A Inv;const auto& M=Inverse[B];for(int R=0;R<4;++R)for(int C=0;C<4;++C)Inv.Add(Num(M.M[R][C]));O->SetArrayField(TEXT("inverse_bind_matrix_row_major"),Inv);O->SetArrayField(TEXT("component_matrix_row_major"),Matrix(Pose[B]));Bones.Add(MakeShared<FJsonValueObject>(O));}
 if(FParse::Param(FCommandLine::Get(),TEXT("VamSeamAudit")))
 {
  A Vertices;Vertices.SetNum(N);int SectionIndex=0;
  for(const auto& S:L.RenderSections){for(uint32 I=S.BaseVertexIndex;I<S.BaseVertexIndex+S.NumVertices;++I){auto O=Obj();O->SetNumberField(TEXT("render_vertex"),I);O->SetNumberField(TEXT("section"),SectionIndex);O->SetNumberField(TEXT("material"),S.MaterialIndex);O->SetNumberField(TEXT("lod"),0);O->SetArrayField(TEXT("bind_position"),Vec(FVector(L.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I))));O->SetArrayField(TEXT("normal"),Vec(FVector(L.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I))));O->SetArrayField(TEXT("tangent"),Vec(FVector(L.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(I))));auto UV=L.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0);O->SetArrayField(TEXT("uv"),{Num(UV.X),Num(UV.Y)});Vertices[I]=MakeShared<FJsonValueObject>(O);}++SectionIndex;}
  auto Detail=Obj();Detail->SetArrayField(TEXT("vertices"),Vertices);auto All=Obj();for(const auto& M:Mesh->GetMorphTargets()){A Rows;for(const auto& D:M->GetMorphTargetDeltas(0)){auto O=Obj();O->SetNumberField(TEXT("vertex"),D.SourceIdx);O->SetArrayField(TEXT("delta"),Vec(FVector(D.PositionDelta)));O->SetArrayField(TEXT("normal_delta"),Vec(FVector(D.TangentZDelta)));Rows.Add(MakeShared<FJsonValueObject>(O));}All->SetArrayField(M->GetName(),Rows);}Detail->SetObjectField(TEXT("morphs"),All);Save(Dir/TEXT("seam-attributes.json"),Detail);
 }
 Data->SetArrayField(TEXT("bones"),Bones);Data->SetObjectField(TEXT("active_morphs"),MorphJson(Morph));A Tri;auto* Indices=L.MultiSizeIndexContainer.GetIndexBuffer();for(int32 I=0;I<Indices->Num();++I)Tri.Add(Num(Indices->Get(I)));Data->SetArrayField(TEXT("triangle_indices"),Tri);Save(Dir/TEXT("inputs.json"),Data);
 Metadata->SetStringField(TEXT("status"),TEXT("exported_cpu_lbs_reconstruction"));Metadata->SetNumberField(TEXT("vertices"),N);Metadata->SetStringField(TEXT("surface"),Dir/TEXT("surface.json"));return true;
}
void Mapping(const FString& File,UVamCharacterDefinition* D){auto O=Obj();auto* Shape=D?D->Shape.LoadSynchronous():nullptr;auto* G=Shape?Shape->Geometry.LoadSynchronous():nullptr;
 if(!G){O->SetStringField(TEXT("error"),TEXT("geometry binding unavailable"));Save(File,O);return;}O->SetStringField(TEXT("path"),G->GetPathName());O->SetStringField(TEXT("topology_digest"),G->TopologyDigest);A R,S,P;
 for(int V:G->RenderToInput)R.Add(Num(V));for(int V:G->InputToSource)S.Add(Num(V));for(const auto& V:G->RenderRefinementParents)P.Add(MakeShared<FJsonValueArray>(A{Num(V.X),Num(V.Y)}));
 O->SetArrayField(TEXT("render_to_input"),R);O->SetArrayField(TEXT("input_to_source"),S);O->SetArrayField(TEXT("refinement_parents"),P);O->SetStringField(TEXT("refinement_semantics"),TEXT("source -1 is an added vertex; recursively follow its earlier input parents, never nearest-position matching"));Save(File,O);}
}

void AVamCharacterActor::ResetDeformationAuditState()
{
 if(BreastContact)BreastContact->ResetContact();
 if(BodyContactResponse)BodyContactResponse->ResetResponse();
 if(auto* B=Character?Cast<UVamLegSkeletalMeshComponent>(Character->Body):nullptr){B->ResetBreastJiggle();B->ResetGluteJiggle();B->ResetLegJiggle();B->ClearMorphTargets();Character->PreviewParameters(Character->GetShapeState().Values);B->TickAnimation(0,false);B->RefreshBoneTransforms();}
 DeformationAuditStatus=TEXT("Contact and Jiggle histories reset; shape and pose retained");
}

void AVamCharacterActor::ExportDeformationAudit()
{
 using namespace VamAudit;
 auto* B=Character?Cast<UVamLegSkeletalMeshComponent>(Character->Body):nullptr;
 if(!B||!B->GetSkeletalMeshAsset()){DeformationAuditStatus=TEXT("Load this character first");return;}
 if(DeformationAuditStatus.StartsWith(TEXT("Capturing:")))return;
 TArray<TPair<TWeakObjectPtr<UActorComponent>,bool>> SavedTicks;TInlineComponentArray<UActorComponent*> Components(this);for(auto* C:Components){SavedTicks.Emplace(C,C->IsComponentTickEnabled());C->SetComponentTickEnabled(false);}
 const bool ActorTick=IsActorTickEnabled();SetActorTickEnabled(false);
 auto Expired=MakeShared<TAtomic<bool>,ESPMode::ThreadSafe>(false);
 auto Done=MakeShared<bool>(); // game-thread completion only
 auto Finish=[Weak=TWeakObjectPtr<AVamCharacterActor>(this),SavedTicks,ActorTick,Done](){if(*Done)return;*Done=true;for(const auto& P:SavedTicks)if(P.Key.IsValid())P.Key->SetComponentTickEnabled(P.Value);if(Weak.IsValid()){Weak->SetActorTickEnabled(ActorTick);Weak->DeformationAuditStatus.RemoveFromStart(TEXT("Capturing:"));}};
 const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("VamDeformationAudit")/(FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
 IFileManager::Get().MakeDirectory(*Dir,true);
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Finish,Done,Expired,Dir](float){if(!*Done){Expired->Store(true);auto O=Obj();O->SetStringField(TEXT("status"),TEXT("readback timeout; frozen pose released, no valid GPU snapshot"));Save(Dir/TEXT("gpu-surface-status.json"),O);Finish();}return false;}),15.f);
 auto Root=Obj();Root->SetStringField(TEXT("schema"),TEXT("vam-deformation-audit/1"));Root->SetStringField(TEXT("actor"),GetPathName());Root->SetStringField(TEXT("actor_class"),GetClass()->GetPathName());Root->SetStringField(TEXT("world"),GetWorld()->GetPathName());Root->SetNumberField(TEXT("world_type"),int32(GetWorld()->WorldType));Root->SetStringField(TEXT("frame"),LexToString(GFrameCounter));Root->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Root->SetStringField(TEXT("load_generation"),LexToString(Character->GetLoadGeneration()));Root->SetArrayField(TEXT("body_to_world"),Matrix(B->GetComponentTransform()));
 auto* Config=Character->GetLoadedRuntimeConfiguration();Root->SetObjectField(TEXT("configuration"),Properties(Config));Root->SetObjectField(TEXT("body"),Properties(B));Root->SetObjectField(TEXT("contact"),Properties(BreastContact));
 auto ComponentStates=Obj();for(auto* C:Components)ComponentStates->SetObjectField(C->GetName(),Properties(C));Root->SetObjectField(TEXT("components"),ComponentStates);
 auto Corrective=Obj();Corrective->SetBoolField(TEXT("enabled"),B->bCorrectiveEnabled);A TargetSides;
 for(int Side=0;Side<B->CorrectiveTargetWeights.Num();++Side){auto O=Obj();O->SetNumberField(TEXT("side"),Side);A Weights;for(double W:B->CorrectiveTargetWeights[Side])Weights.Add(Num(W));O->SetArrayField(TEXT("target_weights"),Weights);if(B->HipPoseState.Sides.IsValidIndex(Side)){const auto& H=B->HipPoseState.Sides[Side];O->SetArrayField(TEXT("swing_twist_degrees"),Vec(FVector(H.FlexionExtension,H.AbductionAdduction,H.ExternalInternalRotation)*180./PI));}TargetSides.Add(MakeShared<FJsonValueObject>(O));}
 Corrective->SetArrayField(TEXT("sides"),TargetSides);Corrective->SetObjectField(TEXT("requested_morph_weights"),MorphJson(B->CorrectiveWeights));Root->SetObjectField(TEXT("corrective_runtime"),Corrective);
 auto Profiles=Obj();Profiles->SetObjectField(TEXT("breast"),Properties(B->BreastProfile));Profiles->SetObjectField(TEXT("glute"),Properties(B->GluteProfile));Profiles->SetObjectField(TEXT("corrective"),Properties(B->CorrectiveProfile));Profiles->SetObjectField(TEXT("glute_jiggle"),Properties(B->GluteJiggleProfile));Profiles->SetObjectField(TEXT("leg"),Properties(B->LegProfile));Root->SetObjectField(TEXT("profiles"),Profiles);
 auto* ContactProfile=Config?Config->BreastContact.LoadSynchronous():nullptr;Profiles->SetObjectField(TEXT("contact"),Properties(ContactProfile));Root->SetObjectField(TEXT("shape"),MorphJson(Character->GetShapeState().Values));auto AllMorphs=Morphs(B);Root->SetObjectField(TEXT("active_morphs"),MorphJson(AllMorphs));A Deformers;for(const auto& D:B->GetAuditDeformers().Deformers)Deformers.Add(MakeShared<FJsonValueString>(GetPathNameSafe(D.Get())));Root->SetArrayField(TEXT("selected_deformers"),Deformers);
 A Cameras;for(TObjectIterator<UCameraComponent> I;I;++I)if(I->GetWorld()==GetWorld()){auto C=Obj();C->SetStringField(TEXT("path"),I->GetPathName());C->SetArrayField(TEXT("world_matrix"),Matrix(I->GetComponentTransform()));C->SetNumberField(TEXT("fov"),I->FieldOfView);Cameras.Add(MakeShared<FJsonValueObject>(C));}if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* C=PC->PlayerCameraManager.Get()){auto P=Obj();P->SetArrayField(TEXT("world_matrix"),Matrix(FTransform(C->GetCameraRotation(),C->GetCameraLocation())));P->SetNumberField(TEXT("fov"),C->GetFOVAngle());Root->SetObjectField(TEXT("player_camera"),P);}for(TObjectIterator<USceneCaptureComponent2D> I;I;++I)if(I->GetWorld()==GetWorld()){auto C=Obj();C->SetStringField(TEXT("path"),I->GetPathName());C->SetArrayField(TEXT("world_matrix"),Matrix(I->GetComponentTransform()));C->SetNumberField(TEXT("fov"),I->FOVAngle);Cameras.Add(MakeShared<FJsonValueObject>(C));}Root->SetArrayField(TEXT("scene_cameras"),Cameras);
 #if WITH_EDITOR
 if(GEditor){A Views;for(auto* V:GEditor->GetAllViewportClients())if(V&&V->GetWorld()==GetWorld()){auto C=Obj();C->SetArrayField(TEXT("world_matrix"),Matrix(FTransform(V->GetViewRotation(),V->GetViewLocation())));C->SetNumberField(TEXT("fov"),V->ViewFOV);Views.Add(MakeShared<FJsonValueObject>(C));}Root->SetArrayField(TEXT("editor_viewports"),Views);}
#endif
 auto* Definition=Character->GetLoadedDefinition();Root->SetObjectField(TEXT("definition"),Properties(Definition));Mapping(Dir/TEXT("mapping.json"),Definition);
 const auto* Inverse=B->GetRefPoseOverride()?&B->GetRefPoseOverride()->RefBasesInvMatrix:nullptr;
 auto Final=B->GetComponentSpaceTransforms();const auto& Ref=B->GetSkeletalMeshAsset()->GetRefSkeleton();auto Local=Character->GetShapeReferencePose();
 const bool Offline=!AuditPoseOverrideJson.IsEmpty();
 if(Offline)
 {
  J Request;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AuditPoseOverrideJson),Request)||!Request->HasTypedField<EJson::Object>(TEXT("bones"))){Finish();DeformationAuditStatus=TEXT("Invalid isolated audit pose");return;}
  const auto Bones=Request->GetObjectField(TEXT("bones"));
  for(const auto& Pair:Bones->Values)
  {
   int Index=Ref.FindBoneIndex(FName(*Pair.Key));const auto& Values=Pair.Value->AsArray();
   if(!Final.IsValidIndex(Index)||Values.Num()!=16){Finish();DeformationAuditStatus=TEXT("Audit pose bone/matrix mismatch: ")+FString(*Pair.Key);return;}
   FMatrix M;for(int R=0;R<4;++R)for(int C=0;C<4;++C)M.M[R][C]=Values[R*4+C]->AsNumber();
   FTransform T(M);if(T.ContainsNaN()||!T.GetScale3D().Equals(FVector::OneVector,.001)){Finish();DeformationAuditStatus=TEXT("Nonrigid audit pose rejected: ")+FString(*Pair.Key);return;}Final[Index]=T;
  }
  Root->SetObjectField(TEXT("isolated_pose_request"),Request);
  Root->SetStringField(TEXT("pose_scope"),TEXT("offline geometry audit on actual loaded assets; production joint limits unchanged; no dynamics or rendered readback claim"));
  if(B->GluteProfile && B->CorrectiveProfile && B->GluteRest.Num()==2)
  {
   const auto Hip=VamGluteStructure::CaptureHipPose(*B->GluteProfile,B->GluteRest,Final[B->GluteRest[0].PelvisBone],Final[B->GluteRest[0].ThighBone],Final[B->GluteRest[1].ThighBone],B->GluteShapeRevision);
   A Sides;TArray<TArray<double>> Weights;
   for(int Side=0;Side<2;++Side){Weights.Add(VamGluteCorrective::Weights(*B->CorrectiveProfile,Hip.Sides[Side]));auto O=Obj();A W;for(double V:Weights.Last())W.Add(Num(V));O->SetArrayField(TEXT("target_weights"),W);O->SetNumberField(TEXT("side"),Side);Sides.Add(MakeShared<FJsonValueObject>(O));}
   for(const auto& Basis:B->CorrectiveProfile->Bases){const auto Scale=VamGluteCorrective::ShapeScale(B->CorrectiveProfile->BuildDimensions[Basis.Side],B->GluteRest[Basis.Side].Dimensions);AllMorphs.Add(Basis.Morph,B->bCorrectiveEnabled?Weights[Basis.Side][Basis.Target]*Scale[Basis.Axis]:0);}
   for(const auto& W:VamGluteCorrective::KneeWeights(*B->CorrectiveProfile,Final,&Local))AllMorphs.Add(W.Key,B->bCorrectiveEnabled?W.Value:0);
   Corrective->SetArrayField(TEXT("sides"),Sides);Corrective->SetStringField(TEXT("evaluation"),TEXT("native G062 and knee operator evaluated at supplied matrices; not live actor pose"));Root->SetObjectField(TEXT("active_morphs"),MorphJson(AllMorphs));
  }
 }
 auto Base=Final;
 // Restore appended helper bones from the shape reference hierarchy only. Primary
 // bone component transforms are the exact captured runtime pose.
 auto ResetBone=[&](int Bone){if(Base.IsValidIndex(Bone)&&Local.IsValidIndex(Bone)){int Parent=Ref.GetParentIndex(Bone);Base[Bone]=Parent>=0?Local[Bone]*Base[Parent]:Local[Bone];}};
 for(const auto& S:B->RestSides){ResetBone(S.AnchorBone);for(const auto& N:S.Nodes)ResetBone(N.BoneIndex);}
 for(const auto& S:B->GluteRest){ResetBone(S.AnchorBone);for(const auto& N:S.Regions)ResetBone(N.BoneIndex);}
 for(const auto& S:B->LegRest){ResetBone(S.AnchorBone);for(const auto& N:S.Dynamics.Nodes)ResetBone(N.BoneIndex);}
 auto NonCorrective=AllMorphs;if(B->CorrectiveProfile)for(const auto& X:B->CorrectiveProfile->Bases)NonCorrective.Remove(X.Morph);
 if(B->CorrectiveProfile)for(const auto& K:B->CorrectiveProfile->Knees)for(FName Name:K.Morphs)NonCorrective.Remove(Name);
 auto Stages=Obj();J Meta;Surface(Dir/TEXT("01-final-weights"),B->GetSkeletalMeshAsset(),B->GetSkinWeightBuffer(0),Base,NonCorrective,Meta,Inverse);Meta->SetStringField(TEXT("meaning"),TEXT("final weights, shape-reference helpers, current primary pose; NOT source LBS"));Stages->SetObjectField(TEXT("01-final-weights"),Meta);
 auto Static=Base;
 for(const auto& S:B->LegRest){const auto Anchor=S.AnchorLocal*Static[S.bCalf?S.Shin:S.Thigh];Static[S.AnchorBone]=Anchor;for(const auto& N:S.Dynamics.Nodes)Static[N.BoneIndex]=FTransform(N.Rest)*Anchor;}
 if(B->GluteProfile)for(const auto& S:B->GluteRest){const auto Anchor=S.AnchorLocal*Static[S.PelvisBone];auto State=VamGluteStructure::Evaluate(*B->GluteProfile,S,B->bGluteEnabled?Static[S.ThighBone].GetRelativeTransform(Anchor):S.RestThighInAnchor);Static[S.AnchorBone]=Anchor;for(int I=0;I<S.Regions.Num();++I)Static[S.Regions[I].BoneIndex]=State.Regions[I].Transform*Anchor;}
 Surface(Dir/TEXT("02-static-helpers"),B->GetSkeletalMeshAsset(),B->GetSkinWeightBuffer(0),Static,NonCorrective,Meta,Inverse);Stages->SetObjectField(TEXT("02-static-helpers"),Meta);
 Surface(Dir/TEXT("03-corrective"),B->GetSkeletalMeshAsset(),B->GetSkinWeightBuffer(0),Static,AllMorphs,Meta,Inverse);Stages->SetObjectField(TEXT("03-corrective"),Meta);
 if(!Offline) Surface(Dir/TEXT("04-jiggle"),B->GetSkeletalMeshAsset(),B->GetSkinWeightBuffer(0),Final,AllMorphs,Meta,Inverse);if(!Offline) Stages->SetObjectField(TEXT("04-jiggle"),Meta);
 UVamCharacterDefinition* Source=Config?Config->DiagnosticSourceDefinition.LoadSynchronous():nullptr;
#if WITH_EDITORONLY_DATA
 if(!Source&&Config){auto* C=Config;TSet<FString> Seen;while(C&&!Source&&!Seen.Contains(C->GetPathName())){Seen.Add(C->GetPathName());TSharedPtr<FJsonObject> Receipt;FString Path;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(C->ReceiptJson),Receipt))break;if(Receipt->TryGetStringField(TEXT("source_definition"),Path)){Source=LoadObject<UVamCharacterDefinition>(nullptr,*Path);break;}if(!Receipt->TryGetStringField(TEXT("source_configuration"),Path))break;C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);}}
#endif
 auto SourceMeta=Obj();SourceMeta->SetStringField(TEXT("definition"),GetPathNameSafe(Source));
 if(Source){auto* Mesh=Source->Body.LoadSynchronous();if(Mesh){TArray<FTransform> Pose;TArray<FMatrix44f> SourceInverse;bool Valid=true;for(int I=0;I<Mesh->GetRefSkeleton().GetNum();++I){int Index=Ref.FindBoneIndex(Mesh->GetRefSkeleton().GetBoneName(I));if(!Final.IsValidIndex(Index)){Valid=false;break;}Pose.Add(Final[Index]);if(Inverse)SourceInverse.Add((*Inverse)[Index]);}if(Valid){Surface(Dir/TEXT("00-source-lbs"),Mesh,nullptr,Pose,NonCorrective,SourceMeta,Inverse?&SourceInverse:nullptr);Mapping(Dir/TEXT("source-mapping.json"),Source);}}}
 SourceMeta->SetStringField(TEXT("definition"),GetPathNameSafe(Source));if(!SourceMeta->HasField(TEXT("status")))SourceMeta->SetStringField(TEXT("error"),TEXT("source asset/provenance unavailable; no substitute baseline generated"));Stages->SetObjectField(TEXT("00-source-lbs"),SourceMeta);
 Root->SetObjectField(TEXT("stages"),Stages);Root->SetStringField(TEXT("stage_semantics"),TEXT("fixed-pose decompositions, not six independent time simulations. Stage 04 preserves captured dynamic offsets. Disabled features remain disabled. Inputs permit offline replay without residual solver history."));
 if(Offline){Root->SetStringField(TEXT("gpu_surface_status"),TEXT("not evaluated in isolated pose audit"));Root->SetStringField(TEXT("stage_semantics"),TEXT("source LBS / final weights / static helpers / native G062 at supplied component matrices; no dynamic stages"));Save(Dir/TEXT("manifest.json"),Root);Finish();DeformationAuditStatus=Dir;return;}
 VamGPUFlushBatch();TArray<FVector> Target,Current;uint64 GPUFrame=0;bool Cage=BreastContact&&BreastContact->ReadAuditCage(Target,Current,GPUFrame);Root->SetBoolField(TEXT("gpu_handle_valid"),BreastContact&&BreastContact->HasDiagnosticGPUHandle());Root->SetBoolField(TEXT("cage_readback_valid"),Cage);Root->SetStringField(TEXT("gpu_input_game_frame"),LexToString(GPUFrame));Root->SetBoolField(TEXT("cage_matches_capture_frame"),Cage&&GPUFrame==GFrameCounter);
 if(ContactProfile){TArray<FVector> Canonical;A Particles,Tets,Parents,Weights,Mask;
 for(const auto& P:ContactProfile->Particles){Canonical.Add(P.Rest);auto X=Obj();X->SetNumberField(TEXT("region"),P.Side);X->SetBoolField(TEXT("kinematic"),P.bKinematic);A BW;for(int I=0;I<P.Bones.Num();++I)BW.Add(MakeShared<FJsonValueArray>(A{Num(P.Bones[I]),Num(P.Weights[I])}));X->SetArrayField(TEXT("influences"),BW);Particles.Add(MakeShared<FJsonValueObject>(X));}
 for(const auto& T:ContactProfile->Tetrahedra)Tets.Add(MakeShared<FJsonValueArray>(A{Num(T.X),Num(T.Y),Num(T.Z),Num(T.W)}));
 for(const auto& T:ContactProfile->SurfaceParents)Parents.Add(MakeShared<FJsonValueArray>(A{Num(T.X),Num(T.Y),Num(T.Z),Num(T.W)}));
 for(const auto& T:ContactProfile->SurfaceWeights)Weights.Add(MakeShared<FJsonValueArray>(A{Num(T.X),Num(T.Y),Num(T.Z),Num(T.W)}));for(float V:ContactProfile->SurfaceMask)Mask.Add(Num(V));auto Top=Obj();Top->SetArrayField(TEXT("particles"),Particles);Top->SetArrayField(TEXT("tetrahedra"),Tets);Top->SetArrayField(TEXT("surface_parents"),Parents);Top->SetArrayField(TEXT("surface_weights"),Weights);Top->SetArrayField(TEXT("surface_mask"),Mask);Save(Dir/TEXT("cage-topology.json"),Top);Points(Dir/TEXT("cage-canonical.json"),Canonical);}
 if(BreastContact)Points(Dir/TEXT("cage-shaped-reference.json"),BreastContact->GetAuditShapedRest());if(Cage){Points(Dir/TEXT("cage-animated-target.json"),Target);Points(Dir/TEXT("cage-current.json"),Current);}
 Root->SetStringField(TEXT("gpu_surface_status"),TEXT("pending native Mesh Deformer readback; result has its own status file"));Save(Dir/TEXT("manifest.json"),Root);
 #if WITH_EDITORONLY_DATA
 auto Request=MakeUnique<FMeshDeformerGeometryReadbackRequest>();Request->VertexDataArraysCallback_AnyThread=[Dir,Finish,Expired](const FMeshDeformerGeometryReadbackVertexDataArrays& Data){if(Expired->Load())return;if(Data.LODIndex<0||Data.Positions.IsEmpty()){auto Failed=Obj();Failed->SetStringField(TEXT("status"),TEXT("native readback returned no geometry"));Save(Dir/TEXT("gpu-surface-status.json"),Failed);AsyncTask(ENamedThreads::GameThread,Finish);return;}TArray<FVector> V;for(const auto& P:Data.Positions)V.Add(FVector(P));Points(Dir/TEXT("05-gpu-surface.json"),V);TArray<FVector> Normals,Tangents;for(const auto& N:Data.Normals)Normals.Add(FVector(N));for(const auto& T:Data.Tangents)Tangents.Add(FVector(T));Points(Dir/TEXT("05-gpu-normals.json"),Normals);Points(Dir/TEXT("05-gpu-tangents.json"),Tangents);auto O=Obj();O->SetNumberField(TEXT("lod"),Data.LODIndex);O->SetStringField(TEXT("status"),TEXT("native_render_readback"));O->SetStringField(TEXT("timing"),TEXT("actor component ticks frozen from capture until native readback completion"));Save(Dir/TEXT("gpu-surface-status.json"),O);AsyncTask(ENamedThreads::GameThread,Finish);};
 if(!B->RequestReadbackRenderGeometry(MoveTemp(Request))){auto O=Obj();O->SetStringField(TEXT("status"),TEXT("unavailable: native readback requires an active Mesh Deformer"));Save(Dir/TEXT("gpu-surface-status.json"),O);Finish();}
#else
 auto Unavailable=Obj();Unavailable->SetStringField(TEXT("status"),TEXT("native final geometry readback is editor-only in this UE version"));Save(Dir/TEXT("gpu-surface-status.json"),Unavailable);Finish();
#endif
 DeformationAuditStatus=(*Done?TEXT(""):TEXT("Capturing:"))+Dir;UE_LOG(LogTemp,Display,TEXT("VAM_DEFORMATION_AUDIT %s"),*Dir);
}
