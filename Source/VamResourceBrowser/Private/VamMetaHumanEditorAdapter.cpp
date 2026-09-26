#include "VamMetaHumanEditorAdapter.h"
#include "VamMetaHumanComponent.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "GameFramework/Actor.h"
#include "SkelMeshDNAUtils.h"
#include "JsonObjectConverter.h"
#include "Containers/Ticker.h"
#include "UObject/UObjectGlobals.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanRigEvaluatedState.h"

FString UVamMetaHumanEditorAdapter::InspectMeshSections(UObject* Object)
{
    const auto* Mesh=Cast<USkeletalMesh>(Object);
    if (!Mesh) return TEXT("");
    const FMeshDescription* Description=Mesh->GetMeshDescription(0);
    if (!Description) return TEXT("");
    const FStaticMeshConstAttributes Attributes(*Description);
    const auto Names=Attributes.GetPolygonGroupMaterialSlotNames();
    const auto Normals=Attributes.GetVertexInstanceNormals();
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Groups;
    for (const FPolygonGroupID Group:Description->PolygonGroups().GetElementIDs())
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("slot"),Names[Group].ToString());
        for (const auto& Material:Mesh->GetMaterials())
            if (Material.MaterialSlotName==Names[Group] || Material.ImportedMaterialSlotName==Names[Group])
                Item->SetStringField(TEXT("material"),GetPathNameSafe(Material.MaterialInterface));
        TArray<TSharedPtr<FJsonValue>> Indices,CornerNormals;
        for (const FTriangleID Triangle:Description->Triangles().GetElementIDs())
            if (Description->GetTrianglePolygonGroup(Triangle)==Group)
            {
                for (const FVertexID Vertex:Description->GetTriangleVertices(Triangle))
                    Indices.Add(MakeShared<FJsonValueNumber>(Vertex.GetValue()));
                for (const FVertexInstanceID Instance:Description->GetTriangleVertexInstances(Triangle))
                {
                    const FVector3f N=Normals[Instance];TArray<TSharedPtr<FJsonValue>> XYZ;
                    XYZ.Add(MakeShared<FJsonValueNumber>(N.X));XYZ.Add(MakeShared<FJsonValueNumber>(N.Y));XYZ.Add(MakeShared<FJsonValueNumber>(N.Z));
                    CornerNormals.Add(MakeShared<FJsonValueArray>(XYZ));
                }
            }
        Item->SetArrayField(TEXT("corner_normals"),CornerNormals);
        Item->SetArrayField(TEXT("triangles"),Indices);Groups.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("sections"),Groups);FString Json;
    FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}

FString UVamMetaHumanEditorAdapter::Probe()
{
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("engine"),FEngineVersion::Current().ToString());
    Result->SetBoolField(TEXT("core_data"),FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled());
    auto Plugins=MakeShared<FJsonObject>();
    for (const TCHAR* Name:{TEXT("MetaHumanCharacter"),TEXT("RigLogic"),TEXT("HairStrands"),TEXT("ChaosClothAsset"),TEXT("ChaosClothAssetEditor"),TEXT("ChaosOutfitAsset")})
    {
        const auto Plugin=IPluginManager::Get().FindPlugin(Name);
        auto Item=MakeShared<FJsonObject>(); Item->SetBoolField(TEXT("installed"),Plugin.IsValid());
        Item->SetBoolField(TEXT("enabled"),Plugin.IsValid() && Plugin->IsEnabled());
        if (Plugin) Item->SetStringField(TEXT("descriptor"),Plugin->GetDescriptorFileName());
        Plugins->SetObjectField(Name,Item);
    }
    Result->SetObjectField(TEXT("plugins"),Plugins);
    auto Rendering=MakeShared<FJsonObject>();
    for (const TCHAR* Name:{TEXT("r.SkinCache.CompileShaders"),TEXT("r.SkinCache.DefaultBehavior"),TEXT("r.HairStrands.Strands"),TEXT("r.VirtualTextures")})
    { const auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name); Rendering->SetStringField(Name,Variable?Variable->GetString():TEXT("unavailable")); }
    Result->SetObjectField(TEXT("rendering"),Rendering);
    Result->SetBoolField(TEXT("cloud_authorized"),false);
    FString Json; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json)); return Json;
}

UStaticMesh* UVamMetaHumanEditorAdapter::CreateTarget(const FString& Path,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,FString& Error)
{
    Error.Reset();
    if (!Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Path) || FPackageName::DoesPackageExist(Path) || FindPackage(nullptr,*Path))
    { Error=TEXT("NameCollisionOrInvalidPath: choose another user name; no overwrite"); return nullptr; }
    if (Vertices.IsEmpty() || Triangles.IsEmpty() || Triangles.Num()%3)
    { Error=TEXT("InvalidTargetGeometry"); return nullptr; }
    for (const FVector& V:Vertices) if (V.ContainsNaN()) { Error=TEXT("NonFiniteTarget"); return nullptr; }
    for (int32 I:Triangles) if (!Vertices.IsValidIndex(I)) { Error=TEXT("InvalidTargetIndex"); return nullptr; }
    FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
    auto Positions=Attributes.GetVertexPositions(); auto UVs=Attributes.GetVertexInstanceUVs(); UVs.SetNumChannels(1);
    TArray<FVertexID> IDs; for (const auto& V:Vertices) { auto ID=Description.CreateVertex(); Positions[ID]=FVector3f(V); IDs.Add(ID); }
    auto Group=Description.CreatePolygonGroup();
    // MeshDescription uses UE clockwise winding. Use edge2 x edge1 and
    // accumulate area-weighted normals, matching the native mesh adapter.
    TArray<FVector> SmoothNormals; SmoothNormals.Init(FVector::ZeroVector,Vertices.Num());
    for (int32 I=0;I<Triangles.Num();I+=3)
    {
        const FVector N=FVector::CrossProduct(Vertices[Triangles[I+2]]-Vertices[Triangles[I]],Vertices[Triangles[I+1]]-Vertices[Triangles[I]]);
        for (int32 J=0;J<3;++J) SmoothNormals[Triangles[I+J]]+=N;
    }
    for (int32 I=0;I<Triangles.Num();I+=3)
    {
        TArray<FVertexInstanceID> Corners;
        for (int32 J=0;J<3;++J) { auto Corner=Description.CreateVertexInstance(IDs[Triangles[I+J]]); Attributes.GetVertexInstanceNormals()[Corner]=FVector3f(SmoothNormals[Triangles[I+J]].GetSafeNormal()); UVs.Set(Corner,0,FVector2f::ZeroVector); Corners.Add(Corner); }
        Description.CreatePolygon(Group,Corners);
    }
    auto* Mesh=NewObject<UStaticMesh>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone|RF_Transactional);
    Mesh->GetStaticMaterials().Add(FStaticMaterial());
    UStaticMesh::FBuildMeshDescriptionsParams Params; Params.bBuildSimpleCollision=false;
    if (!Mesh->BuildFromMeshDescriptions({&Description},Params)) { Error=TEXT("TargetMeshBuildFailed"); return nullptr; }
    FAssetRegistryModule::AssetCreated(Mesh); Mesh->MarkPackageDirty(); return Mesh;
}

bool UVamMetaHumanEditorAdapter::Conform(UObject* Object,UStaticMesh* Target,const TMap<int32,FVector>& Keypoints,const FString& CalibrationJson,FString& Error)
{
    Error.Reset(); auto* Character=Cast<UMetaHumanCharacter>(Object); auto* Subsystem=UMetaHumanCharacterEditorSubsystem::Get();
    if (!Character || !Target || !Subsystem) { Error=TEXT("InvalidCharacterOrTarget"); return false; }
    if (!FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled()) { Error=TEXT("CoreDataMissing: install Creator Core Data then resume recipe"); return false; }
    if (!Subsystem->IsObjectAddedForEditing(Character)) { Error=TEXT("CharacterNotOpenForEditing"); return false; }
    FConformTargetParams Params;
    Params.bAutoSolve=true; Params.BodyConformSolveSettings.PipelineName=TEXT("combined");
    if (!CalibrationJson.IsEmpty() && !FJsonObjectConverter::JsonObjectStringToUStruct(CalibrationJson,&Params,0,0))
    { Error=TEXT("InvalidCalibrationRecipe"); return false; }
    if (!Subsystem->GetMeshDataForConforming(Target,Params.ConformTargetMesh.BodyVertices,Params.ConformTargetMesh.BodyVertexIndices))
    { Error=TEXT("TargetMeshDescriptionMissing"); return false; }
    Params.ConformTargetMesh.TargetPartsType=ETargetPartsType::Combined;
    for (const auto& Pair:Keypoints) { if (Pair.Key<0 || Pair.Value.ContainsNaN()) { Error=TEXT("InvalidKeypoint"); return false; } Params.KeyPointTargets.Add(Pair.Key,FVector3f(Pair.Value)); }
    FMetaHumanCharacterTargetMeshKey Key; Key.CombinedMesh=Target;
    bool Completed=false,Cancelled=false,Success=false;
    const auto Handle=Subsystem->OnAsyncMeshConformCompleted(Character).AddLambda([&](bool Ok,bool WasCancelled){Completed=true;Success=Ok;Cancelled=WasCancelled;});
    const bool Solved=Subsystem->ConformToTargetMeshes(Character,Key,Params);
    Subsystem->OnAsyncMeshConformCompleted(Character).Remove(Handle);
    if (!Solved || !Completed || !Success || Cancelled) { Error=TEXT("ConformFailed: Draft retained; calibrate From Custom Mesh landmarks and save recipe"); return false; }
    // Keep reusable correspondences on the editable Character as well as the
    // external recipe. Opening From Custom Mesh must not lose the calibration.
    FMetaHumanCharacterTargetKeyPoints StoredPoints; TMap<FName,EKeyPointType> PointTypes;
    for (const auto& Pair:Params.KeyPointTargets)
    {
        const FName Name(*FString::Printf(TEXT("Recipe_%d"),Pair.Key));
        StoredPoints.CharacterBodyVertexIndexes.Add(Name,Pair.Key);
        StoredPoints.TargetBodyPositions.Add(Name,Pair.Value);PointTypes.Add(Name,EKeyPointType::Custom);
    }
    if (!Params.KeyPointTargets.IsEmpty()) Subsystem->CommitTargetMeshKeypoints(Character,Key,StoredPoints,PointTypes);
    if (!Params.CurveTrackingPoints.IsEmpty())
    {
        FMetaHumanCharacterTargetTrackingResults Tracking;
        Tracking.CameraViewInfo=Params.CameraViewInfo;Tracking.ImageSize=Params.ImageSize;
        for (const auto& Pair:Params.CurveTrackingPoints)
        {FMetaHumanCharacterCurveTrackingPoints Curve;Curve.Points=Pair.Value.TrackingPoints;Tracking.CurveTrackingPoints.Add(Pair.Key,MoveTemp(Curve));}
        Subsystem->CommitTargetMeshTrackingResults(Character,Key,Tracking);
    }
    Subsystem->CommitPosedStateAsAPose(Character,Key);
    Subsystem->CommitFaceState(Character); Subsystem->CommitBodyState(Character); Character->MarkPackageDirty();
    return true;
}
FString UVamMetaHumanEditorAdapter::ExportCalibration(UObject* Object,UStaticMesh* Target)
{
    auto* Character=Cast<UMetaHumanCharacter>(Object); auto* Subsystem=UMetaHumanCharacterEditorSubsystem::Get();
    if (!Character || !Target || !Subsystem || !Subsystem->IsObjectAddedForEditing(Character)) return TEXT("");
    FMetaHumanCharacterTargetMeshKey Key;Key.CombinedMesh=Target;
    FConformTargetParams Params;Params.bAutoSolve=true;Params.BodyConformSolveSettings.PipelineName=TEXT("combined");
    if (const auto* Points=Character->TargetMeshKeyPointsCollection.PerMeshTargetKeyPoints.Find(Key))
    {
        auto Indices=Subsystem->GetPresetBodyKeyPoints(Character);
        Indices.Append(Points->CharacterBodyVertexIndexes);Indices.Append(Points->CharacterHeadVertexIndexes);
        for (const auto& Index:Indices)
        {
            const auto* Position=Points->TargetBodyPositions.Find(Index.Key);
            if (!Position) Position=Points->TargetHeadPositions.Find(Index.Key);
            if (Position) Params.KeyPointTargets.Add(Index.Value,*Position);
        }
    }
    if (const auto* Tracking=Character->TargetMeshTrackingResultsCollection.PerMeshTrackingResults.Find(Key))
    {
        Params.CameraViewInfo=Tracking->CameraViewInfo;Params.ImageSize=Tracking->ImageSize;
        for (const auto& Curve:Tracking->CurveTrackingPoints) {FTrackingPoints Points;Points.TrackingPoints=Curve.Value.Points;Params.CurveTrackingPoints.Add(Curve.Key,MoveTemp(Points));}
    }
    FString Json;FJsonObjectConverter::UStructToJsonObjectString(Params,Json);return Json;
}
FString UVamMetaHumanEditorAdapter::InspectFitGeometry(UObject* Object,UStaticMesh* Target,bool Posed)
{
    auto* Character=Cast<UMetaHumanCharacter>(Object); auto* Subsystem=UMetaHumanCharacterEditorSubsystem::Get();
    if (!Character || !Subsystem || !Subsystem->IsObjectAddedForEditing(Character)) return TEXT("");
    auto State=Subsystem->CopyBodyState(Character);
    if (Posed)
    {
        FMetaHumanCharacterTargetMeshKey Key; Key.CombinedMesh=Target;
        const FSharedBuffer Buffer=Character->GetBodyTargetPoseStateData(Key);
        if (Buffer.IsNull() || !State->Deserialize(Buffer)) return TEXT("");
        State->SetEvaluatePose(true);
        State->SetApplyFloorOffset(false);
    }
    const auto Geometry=State->GetVerticesAndVertexNormals();
    const auto Counts=State->GetNumVerticesPerLOD();
    if (Counts.IsEmpty() || Counts[0]<=0 || Counts[0]>Geometry.Vertices.Num()) return TEXT("");
    auto Report=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Vertices,Triangles;
    for (int32 I=0;I<Counts[0];++I)
    {
        const auto V=Geometry.Vertices[I];
        TArray<TSharedPtr<FJsonValue>> XYZ{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
        Vertices.Add(MakeShared<FJsonValueArray>(XYZ));
    }
    const auto Indices=State->GetTrianglesIndices();
    for (int32 I=0;I+2<Indices.Num();I+=3)
        if (Indices[I]<Counts[0] && Indices[I+1]<Counts[0] && Indices[I+2]<Counts[0])
            for (int32 J=0;J<3;++J) Triangles.Add(MakeShared<FJsonValueNumber>(Indices[I+J]));
    Report->SetArrayField(TEXT("vertices"),Vertices);Report->SetArrayField(TEXT("triangles"),Triangles);
    Report->SetStringField(TEXT("coordinates"),TEXT("DNA centimeters: X lateral, Y up, Z forward"));
    Report->SetBoolField(TEXT("posed"),Posed);Report->SetBoolField(TEXT("visual_acceptance_passed"),false);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return Json;
}
bool UVamMetaHumanEditorAdapter::RefineFit(UObject* Object,UStaticMesh* Target,const FString& ParamsJson,FString& Error)
{
    Error.Reset();auto* Character=Cast<UMetaHumanCharacter>(Object);auto* Subsystem=UMetaHumanCharacterEditorSubsystem::Get();
    if (!Character || !Target || !Subsystem || !Subsystem->IsObjectAddedForEditing(Character)) {Error=TEXT("InvalidRefinementInput");return false;}
    FMetaHumanCharacterTargetMeshKey Key;Key.CombinedMesh=Target;
    const FSharedBuffer Buffer=Character->GetBodyTargetPoseStateData(Key);
    auto Body=Subsystem->CopyBodyState(Character);auto Face=Subsystem->CopyFaceState(Character);
    if (Buffer.IsNull() || !Body->Deserialize(Buffer)) {Error=TEXT("MissingSolvedTargetPose");return false;}
    Body->SetEvaluatePose(true);Body->SetApplyFloorOffset(false);
    FRefinementTargetParams Params;
    if (!FJsonObjectConverter::JsonObjectStringToUStruct(ParamsJson,&Params,0,0)) {Error=TEXT("InvalidRefinementRecipe");return false;}
    Params.ConformTargetMesh.TargetPartsType=ETargetPartsType::Combined;
    if (!Subsystem->GetMeshDataForConforming(Target,Params.ConformTargetMesh.BodyVertices,Params.ConformTargetMesh.BodyVertexIndices)) {Error=TEXT("MissingRefinementGeometry");return false;}
    // The public CoreTech state API is the same operation used by the official
    // mesh-import task runner. Its UI-only async wrapper requires Slate; this
    // local synchronous operation commits only after successful completion.
    if (!Body->RefineVerticesToTarget(Params)) {Error=TEXT("RefinementFailed");return false;}
    FMetaHumanRigEvaluatedState NoDelta,WithDelta;Body->GetVerticesWithAndWithoutDeltas(NoDelta,WithDelta);
    Face->FitWithVertexDeltasFromBody(Body->CopyComponentPose(),NoDelta.Vertices,WithDelta.Vertices,WithDelta.VertexNormals,Body->GetNumVerticesPerLOD());
    Subsystem->CommitBodyState(Character,Body);Subsystem->CommitFaceState(Character,Face);
    Subsystem->CommitPosedStateAsAPose(Character,Key);
    Subsystem->CommitBodyState(Character);Subsystem->CommitFaceState(Character);
    return true;
}
bool UVamMetaHumanEditorAdapter::HasFullRig(UObject* Object)
{ const auto* Character=Cast<UMetaHumanCharacter>(Object); return Character && Character->HasFaceDNA() && Character->HasFaceDNABlendshapes() && Character->HasBodyDNA(); }
bool UVamMetaHumanEditorAdapter::IsCharacterSourceValid(UObject* Object)
{ const auto* Character=Cast<UMetaHumanCharacter>(Object); return Character && Character->IsCharacterValid() && Character->GetFaceStateData().GetSize()>0; }
FString UVamMetaHumanEditorAdapter::InspectAssembly(AActor* Actor)
{
    // The official construction script requests post-process classes asynchronously.
    // Observe the completed official setup instead of replacing its animation ownership.
    FlushAsyncLoading();
    FTSTicker::GetCoreTicker().Tick(0.0f);
    auto Report=MakeShared<FJsonObject>(); bool Valid=IsValid(Actor); bool Face=false,Body=false;
    if (Valid)
    {
        Valid=Actor->FindComponentByClass<UVamMetaHumanComponent>()!=nullptr;
        TArray<USkeletalMeshComponent*> Components; Actor->GetComponents(Components);
        for (auto* Component:Components)
        {
            if (Component->GetFName()!=TEXT("Face") && Component->GetFName()!=TEXT("Body")) continue;
            auto* Mesh=Component->GetSkeletalMeshAsset(); auto Item=MakeShared<FJsonObject>();
            const bool DNA=Mesh && USkelMeshDNAUtils::GetDNAReader(Mesh).IsValid();
            Item->SetBoolField(TEXT("dna"),DNA); Item->SetNumberField(TEXT("lods"),Mesh?Mesh->GetLODNum():0);
            Item->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh));
            Item->SetStringField(TEXT("skeleton"),Mesh?GetPathNameSafe(Mesh->GetSkeleton()):TEXT("None"));
            Item->SetStringField(TEXT("animation"),GetPathNameSafe(Component->GetAnimClass()));
            TArray<TSharedPtr<FJsonValue>> Nodes;
            if (const UClass* AnimClass=Component->GetAnimClass())
                for (TFieldIterator<FStructProperty> It(AnimClass);It;++It)
                    Nodes.Add(MakeShared<FJsonValueString>(It->Struct->GetName()));
            Item->SetArrayField(TEXT("animation_node_structs"),Nodes);
            const auto PostProcess=Component->GetPostProcessAnimBPClassToBeUsed();
            Item->SetStringField(TEXT("post_process"),GetPathNameSafe(PostProcess));
            const bool MeshValid=Mesh && Mesh->GetSkeleton() && Mesh->GetLODNum()>1 && DNA;
            if (Component->GetFName()==TEXT("Face")) Face=MeshValid && PostProcess && Component->GetAnimClass();
            else Body=MeshValid && PostProcess;
            Report->SetObjectField(Component->GetName(),Item);
        }
    }
    Report->SetBoolField(TEXT("valid"),Valid && Face && Body);
    Report->SetBoolField(TEXT("visual_acceptance_passed"),false);
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json)); return Json;
}
bool UVamMetaHumanEditorAdapter::AttachRuntime(UBlueprint* Blueprint,FString& Error)
{
    Error.Reset(); if (!Blueprint || !Blueprint->SimpleConstructionScript) { Error=TEXT("InvalidAssemblyBlueprint");return false; }
    for (const auto* Node:Blueprint->SimpleConstructionScript->GetAllNodes()) if (Node->ComponentClass->IsChildOf(UVamMetaHumanComponent::StaticClass())) return true;
    auto* Node=Blueprint->SimpleConstructionScript->CreateNode(UVamMetaHumanComponent::StaticClass(),TEXT("VamMetaHuman"));
    Blueprint->SimpleConstructionScript->AddNode(Node); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint); Blueprint->MarkPackageDirty();
    if (Blueprint->Status==BS_Error) { Error=TEXT("AssemblyBlueprintCompileFailed"); return false; } return true;
}
