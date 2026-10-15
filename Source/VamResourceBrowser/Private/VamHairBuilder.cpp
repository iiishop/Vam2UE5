#include "VamHairBuilder.h"
#include "VamHairProfile.h"
#include "VamHairAsset.h"
#include "GroomAsset.h"
#include "HairDescription.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionHairAttributes.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

namespace VamHairBuild
{
static bool Save(UObject* O)
{
    O->MarkPackageDirty();FAssetRegistryModule::AssetCreated(O);
    FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;A.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(O->GetOutermost(),O,*FPackageName::LongPackageNameToFilename(O->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),A);
}
static FVector Vector(const TSharedPtr<FJsonValue>& V){const auto& A=V->AsArray();return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());}
static FLinearColor Color(const TSharedPtr<FJsonObject>& Settings,const TCHAR* Key)
{
    const TSharedPtr<FJsonObject>* C=nullptr;if(!Settings->TryGetObjectField(Key,C))return FLinearColor(.06,.025,.012);
    auto Num=[](const TSharedPtr<FJsonObject>& O,const TCHAR* K){double V=0;if(O->TryGetNumberField(K,V))return float(V);FString S;return O->TryGetStringField(K,S)?FCString::Atof(*S):0.f;};
    FLinearColor R(Num(*C,TEXT("h"))*360,Num(*C,TEXT("s")),Num(*C,TEXT("v")),1);return R.HSVToLinearRGB();
}
struct FCurve{TArray<FVector> P;TArray<float> RigidityProfile;float Rigidity=0;int32 SourceID=INDEX_NONE;};
static double Metric(const FCurve& A,const FCurve& B){double D=0;for(int32 K=0;K<4;++K)D+=FVector::DistSquared(A.P[K*(A.P.Num()-1)/3],B.P[K*(B.P.Num()-1)/3]);return D;}
static TArray<int32> Guides(const TArray<FCurve>& C,int32 Budget)
{
    TArray<int32> Result;TArray<double> D;D.Init(DBL_MAX,C.Num());Result.Add(0);
    while(Result.Num()<FMath::Min(Budget,C.Num())){const int32 Last=Result.Last();int32 Next=0;for(int32 I=0;I<C.Num();++I){D[I]=FMath::Min(D[I],Metric(C[I],C[Last]));if(D[I]>D[Next])Next=I;}if(D[Next]<1.e-8)break;Result.Add(Next);}return Result;
}
}

#include "VamHairStyleSolver.h"

UVamHairProfile* UVamHairBuilder::BuildHair(const FString& InputFile,const FString& AssetRoot,USkeletalMesh* Body,const FString& GenerationIdentity,FString& Error,UPhysicsAsset* Physics)
{
    using namespace VamHairBuild;
    FString Text;TSharedPtr<FJsonObject> Input;
    if(GenerationIdentity.Len()!=64||!Body||!FFileHelper::LoadFileToString(Text,*InputFile)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Input)||Input->GetStringField(TEXT("schema"))!=TEXT("vam-groom-source/1")){Error=TEXT("Invalid hair input/body");return nullptr;}
    if(FPackageName::DoesPackageExist(AssetRoot/TEXT("HP_Hair"))){Error=TEXT("Refusing to overwrite hair identity");return nullptr;}
    const auto& Ref=Body->GetRefSkeleton();const int32 Head=Ref.FindBoneIndex(TEXT("head"));if(Head<0){Error=TEXT("Body requires semantic head bone");return nullptr;}
    auto Pose=Ref.GetRefBonePose();for(int32 I=0;I<Pose.Num();++I)if(Ref.GetParentIndex(I)>=0)Pose[I]*=Pose[Ref.GetParentIndex(I)];const FTransform HeadFrame=Pose[Head];
    const auto& Groups=Input->GetArrayField(TEXT("groups"));if(Groups.IsEmpty()||Groups.Num()>32){Error=TEXT("Invalid groom group count");return nullptr;}
    TArray<TArray<FCurve>> Curves;TArray<FString> Kinds;TArray<UMaterialInterface*> Mats;
    for(int32 G=0;G<Groups.Num();++G)
    {
        const auto O=Groups[G]->AsObject();Kinds.Add(O->GetStringField(TEXT("kind")));TArray<FCurve>& C=Curves.AddDefaulted_GetRef();
        for(const auto& V:O->GetArrayField(TEXT("curves"))){FCurve& X=C.AddDefaulted_GetRef();X.SourceID=V->AsObject()->GetIntegerField(TEXT("source_curve"));X.Rigidity=V->AsObject()->GetNumberField(TEXT("rigidity"));for(const auto& P:V->AsObject()->GetArrayField(TEXT("points")))X.P.Add(HeadFrame.InverseTransformPosition(Vector(P)));const TArray<TSharedPtr<FJsonValue>>* RP=nullptr;if(V->AsObject()->TryGetArrayField(TEXT("rigidity_profile"),RP))for(const auto& P:*RP)X.RigidityProfile.Add(P->AsNumber());if(X.P.Num()<2){Error=TEXT("Invalid strand");return nullptr;}}
        if(C.IsEmpty()){Error=TEXT("Empty hair group");return nullptr;}
        const FString Path=AssetRoot/FString::Printf(TEXT("M_Hair_%d"),G);auto* M=NewObject<UMaterial>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);
        M->SetShadingModel(MSM_Hair);M->bUsedWithHairStrands=true;M->TwoSided=true;
        const auto Settings=O->GetObjectField(TEXT("settings"));
        auto* RGB=NewObject<UMaterialExpressionConstant3Vector>(M);RGB->Constant=Color(Settings,TEXT("rootColor"));
        auto* Tip=NewObject<UMaterialExpressionConstant3Vector>(M);Tip->Constant=Color(Settings,TEXT("tipColor"));
        auto* Attr=NewObject<UMaterialExpressionHairAttributes>(M);
        auto* Inverse=NewObject<UMaterialExpressionOneMinus>(M);Inverse->Input.Connect(0,Attr);
        auto* Ramp=NewObject<UMaterialExpressionPower>(M);Ramp->Base.Connect(0,Inverse);FString Rolloff;const float Roll=Settings->TryGetStringField(TEXT("colorRolloff"),Rolloff)?FCString::Atof(*Rolloff):1.f;Ramp->ConstExponent=FMath::Max(0.f,FMath::Pow(2.f,Roll)-1.f);
        auto* Blend=NewObject<UMaterialExpressionOneMinus>(M);Blend->Input.Connect(0,Ramp);
        auto* Mix=NewObject<UMaterialExpressionLinearInterpolate>(M);Mix->A.Connect(0,RGB);Mix->B.Connect(0,Tip);Mix->Alpha.Connect(0,Blend);
        for(UMaterialExpression* E:TArray<UMaterialExpression*>{RGB,Tip,Attr,Inverse,Ramp,Blend,Mix})M->GetExpressionCollection().AddExpression(E);
        M->GetEditorOnlyData()->BaseColor.Expression=Mix;M->GetEditorOnlyData()->Roughness.UseConstant=true;M->GetEditorOnlyData()->Roughness.Constant=.4f;
        M->PostEditChange();M->ForceRecompileForRendering();Mats.Add(M);if(!Save(M)){Error=TEXT("Material save failed");return nullptr;}
    }
    auto* Profile=NewObject<UVamHairProfile>(CreatePackage(*(AssetRoot/TEXT("HP_Hair"))),TEXT("HP_Hair"),RF_Public|RF_Standalone);
    Profile->ReferenceBody=Body;
    TArray<TSharedPtr<FJsonValue>> ColliderReport;
    if(!Physics)Physics=Body->GetPhysicsAsset();
    if(Input->HasField(TEXT("physics_policy"))&&!Physics){Error=TEXT("Physics hair requires the actual runtime body collision asset");return nullptr;}
    if(Input->HasField(TEXT("physics_policy"))&&Physics)
    {
        const FString CollisionPath=AssetRoot/TEXT("PA_Hair");
        auto* PA=DuplicateObject<UPhysicsAsset>(Physics,CreatePackage(*CollisionPath),TEXT("PA_Hair"));PA->SetFlags(RF_Public|RF_Standalone);
        int32 OtherPrimitives=0;
        for(const USkeletalBodySetup* S:PA->SkeletalBodySetups)if(S&&S->BoneName!=FName(TEXT("head")))OtherPrimitives+=S->AggGeom.SphereElems.Num()+S->AggGeom.SphylElems.Num()+S->AggGeom.BoxElems.Num();
        const int32 HeadBudget=FMath::Min(64,99-OtherPrimitives);
        for(USkeletalBodySetup* Setup:PA->SkeletalBodySetups)
        {
            const int32 Bone=Ref.FindBoneIndex(Setup->BoneName);if(Bone<0)continue;
            const TSharedPtr<FJsonObject>* HeadCollision=nullptr;
            if(Setup->BoneName==FName(TEXT("head"))&&Input->TryGetObjectField(TEXT("head_collision"),HeadCollision))
            {
                if((*HeadCollision)->GetStringField(TEXT("reference_body"))!=Body->GetPathName()){Error=TEXT("Head collision surface belongs to another body mesh");return nullptr;}
                if(HeadBudget<1){Error=TEXT("Body collision exceeds native Niagara primitive budget");return nullptr;}
                Setup->AggGeom.EmptyElements();
                // Input balls are greedy coverage ordered. Retain their prefix within
                // the native DI's fixed primitive budget; never rely on GPU truncation.
                for(const auto& V:(*HeadCollision)->GetArrayField(TEXT("spheres"))){if(Setup->AggGeom.SphereElems.Num()>=HeadBudget)break;auto O=V->AsObject();FKSphereElem S;S.Center=Vector(O->TryGetField(TEXT("center")));S.Radius=O->GetNumberField(TEXT("radius_cm"));if(!FMath::IsFinite(S.Radius)||S.Radius<=0||S.Center.ContainsNaN()){Error=TEXT("Invalid head surface proxy");return nullptr;}Setup->AggGeom.SphereElems.Add(S);}
                if(Setup->AggGeom.SphereElems.IsEmpty()){Error=TEXT("Head surface proxy is empty");return nullptr;}
                auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("bone"),TEXT("head"));Receipt->SetStringField(TEXT("method"),(*HeadCollision)->GetStringField(TEXT("version")));Receipt->SetNumberField(TEXT("sphere_count"),Setup->AggGeom.SphereElems.Num());Receipt->SetStringField(TEXT("surface_sha256"),(*HeadCollision)->GetStringField(TEXT("surface_sha256")));ColliderReport.Add(MakeShared<FJsonValueObject>(Receipt));Setup->InvalidatePhysicsData();continue;
            }
            TArray<FVector> Points;for(const auto& Group:Curves)for(const auto& Curve:Group)for(const auto& P:Curve.P)Points.Add(Pose[Bone].InverseTransformPosition(HeadFrame.TransformPosition(P)));
            auto Fit=[&](const FVector& A,const FVector& B,float Radius){float Safe=Radius;for(const auto& P:Points)Safe=FMath::Min(Safe,float(FMath::PointDistToSegment(P,A,B))-.17f);return FMath::Max(.01f,Safe);};
            auto Record=[&](float Before,float After){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("bone"),Setup->BoneName.ToString());R->SetNumberField(TEXT("before_radius_cm"),Before);R->SetNumberField(TEXT("after_radius_cm"),After);ColliderReport.Add(MakeShared<FJsonValueObject>(R));};
            for(auto& S:Setup->AggGeom.SphereElems){const float Before=S.Radius;S.Radius=Fit(S.Center,S.Center,S.Radius);Record(Before,S.Radius);}
            for(auto& S:Setup->AggGeom.SphylElems){const FVector Axis=S.Rotation.RotateVector(FVector::UpVector)*S.Length*.5;const float Before=S.Radius;S.Radius=Fit(S.Center-Axis,S.Center+Axis,S.Radius);Record(Before,S.Radius);}
            Setup->InvalidatePhysicsData();
        }
        if(!Save(PA)){Error=TEXT("Hair collision asset save failed");return nullptr;}Profile->HairCollision=PA;
    }
    if(Input->HasTypedField<EJson::Object>(TEXT("catalog")))
    {
        auto* Style=BuildHairstyle(InputFile,TEXT("/Game/VamHairLibrary"),Error);if(!Style)return nullptr;
        Profile->Hairstyle=Style;Profile->HairstyleSignature=Style->GetContentSignature();
    }
    Profile->BuildIdentity=GenerationIdentity+TEXT("-hair");Profile->SourceReceiptJson=Text;for(auto* M:Mats)Profile->Materials.Add(M);
    TArray<TSharedPtr<FJsonValue>> QualityReports;
    for(int32 Quality=0;Quality<2;++Quality)
    {
        const bool Fine=Quality==1;const FString Name=Fine?TEXT("Groom_Fine"):TEXT("Groom_Coarse"),Path=AssetRoot/Name;
        auto* Groom=NewObject<UGroomAsset>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);Groom->SetNumGroup(Groups.Num());
        FHairDescription Desc;Desc.StrandAttributes().RegisterAttribute<int32>(HairAttribute::Strand::GroupID);Desc.StrandAttributes().RegisterAttribute<int32>(HairAttribute::Strand::Guide);Desc.StrandAttributes().RegisterAttribute<int32>(HairAttribute::Strand::ID);Desc.VertexAttributes().RegisterAttribute<float>(HairAttribute::Vertex::Width);
        auto Positions=Desc.VertexAttributes().GetAttributesRef<FVector3f>(HairAttribute::Vertex::Position);auto Widths=Desc.VertexAttributes().GetAttributesRef<float>(HairAttribute::Vertex::Width);auto Counts=Desc.StrandAttributes().GetAttributesRef<int32>(HairAttribute::Strand::VertexCount);auto IDs=Desc.StrandAttributes().GetAttributesRef<int32>(HairAttribute::Strand::ID);auto GIDs=Desc.StrandAttributes().GetAttributesRef<int32>(HairAttribute::Strand::GroupID);auto Guide=Desc.StrandAttributes().GetAttributesRef<int32>(HairAttribute::Strand::Guide);
        int32 NRender=0,NGuides=0;TArray<TSharedPtr<FJsonValue>> GroupReports;
        auto Add=[&](const TArray<FVector>& P,int32 G,bool IsGuide,float Width){auto S=Desc.AddStrand();Counts[S]=P.Num();GIDs[S]=G;Guide[S]=IsGuide?1:0;IDs[S]=S.GetValue();for(int32 J=0;J<P.Num();++J){auto V=Desc.AddVertex();Positions[V]=FVector3f(P[J]);Widths[V]=Width*FMath::Max(.001f,1.f-10.f*FMath::Max(0.f,float(J)/(P.Num()-1)-.9f));}IsGuide?++NGuides:++NRender;};
        for(int32 G=0;G<Groups.Num();++G)
        {
            const auto& C=Curves[G];const bool Anchor=Kinds[G]==TEXT("anchor"),Coiled=Kinds[G]==TEXT("coiled"),Firm=Kinds[G]==TEXT("firm");
            const auto* Policy=Groups[G]->AsObject()->Values.Find(TEXT("physics_policy"));
            const FString Class=Policy?(*Policy)->AsObject()->GetStringField(TEXT("kind")):TEXT("legacy");
            const bool Compact=Class==TEXT("compact"),Supported=Class==TEXT("supported");
            auto Selected=Guides(C,Fine?128:(Class==TEXT("loose")?64:32));for(int32 I:Selected)Add(C[I].P,G,true,.008);
            // Preserve the source scalp triangle domain. Spatially close guide roots can
            // belong to opposing parts of an updo; nearest-curve mixing bridges those parts.
            const int32 RenderStart=NRender;TMap<int32,int32> SourceToCurve;
            double RenderWidth=.012;Groups[G]->AsObject()->TryGetNumberField(TEXT("render_width_cm"),RenderWidth);
            const TArray<TSharedPtr<FJsonValue>>* Evaluated=nullptr;
            if(Groups[G]->AsObject()->TryGetArrayField(TEXT("render_curves"),Evaluated))
            {
                if(!Input->HasTypedField<EJson::Object>(TEXT("native_render_evaluation"))){Error=TEXT("Missing native render evaluation provenance");return nullptr;}
                for(const auto& Curve:*Evaluated){TArray<FVector> Points;for(const auto& Point:Curve->AsArray())Points.Add(HeadFrame.InverseTransformPosition(Vector(Point)));if(Points.Num()<2){Error=TEXT("Invalid native render strand");return nullptr;}Add(Points,G,false,RenderWidth);}
            }
            else
            {
            for(int32 I=0;I<C.Num();++I){SourceToCurve.Add(C[I].SourceID,I);Add(C[I].P,G,false,.012f);}
            const TArray<TSharedPtr<FJsonValue>>* Triangles=nullptr;TSet<int32> Covered;
            FRandomStream Rand(6734+G);
            if(Groups[G]->AsObject()->TryGetArrayField(TEXT("source_triangles"),Triangles))
            for(const auto& Triangle:*Triangles)
            {
                const auto& T=Triangle->AsArray();if(T.Num()!=3)continue;
                int32 V[3];bool Valid=true;for(int K=0;K<3;++K){const int32* I=SourceToCurve.Find(int(T[K]->AsNumber()));if(!I){Valid=false;break;}V[K]=*I;}
                if(!Valid)continue;for(int K=0;K<3;++K)Covered.Add(V[K]);
                for(int Copy=0;Copy<6;++Copy)
                {
                    const float U=FMath::Sqrt(Rand.FRand()),B=Rand.FRand();const float W[3]={1-U,U*(1-B),U*B};
                    TArray<FVector> P;P.SetNum(32);
                    for(int J=0;J<32;++J){P[J]=FVector::ZeroVector;for(int K=0;K<3;++K){const auto& X=C[V[K]].P;const float F=J*(X.Num()-1)/31.f;const int A=FMath::FloorToInt(F);P[J]+=W[K]*FMath::Lerp(X[A],X[FMath::Min(A+1,X.Num()-1)],F-A);}}
                    Add(P,G,false,.012f);
                }
            }
            // Isolated source curves keep their own route. Smooth transverse offsets
            // avoid inventing bridges or injecting independent per-vertex noise.
            for(int I=0;I<C.Num();++I)if(!Covered.Contains(I))for(int Copy=0;Copy<11;++Copy)
            {
                auto P=C[I].P;const FVector Offset=Rand.VRand()*Rand.FRandRange(.005f,.035f);
                for(int J=0;J<P.Num();++J)P[J]+=Offset*FMath::Sin(PI*J/(P.Num()-1));Add(P,G,false,.012f);
            }
            }
            auto& IP=Groom->GetHairGroupsInterpolation()[G].InterpolationSettings;IP.GuideType=EGroomGuideType::Imported;IP.InterpolationQuality=EHairInterpolationQuality::High;IP.InterpolationDistance=EHairInterpolationWeight::Parametric;IP.bRandomizeGuide=false;IP.bUseUniqueGuide=false;
            auto& L=Groom->GetHairGroupsLOD()[G].LODs;L.SetNum(1);L[0].BindingType=EGroomBindingType::Rigid;L[0].Simulation=Anchor?EGroomOverrideType::Disable:EGroomOverrideType::Enable;
            auto& Physics=Groom->GetHairGroupsPhysics()[G];Physics.SolverSettings.EnableSimulation=!Anchor;Physics.SolverSettings.NiagaraSolver=EGroomNiagaraSolvers::CosseratRods;if(Policy){auto* StyleSolver=BuildHairStyleSolver(AssetRoot,Class,Fine?32:16,Error);if(!StyleSolver)return nullptr;Physics.SolverSettings.NiagaraSolver=EGroomNiagaraSolvers::CustomSolver;Physics.SolverSettings.CustomSystem=StyleSolver;}Physics.SolverSettings.SubSteps=Fine?5:3;Physics.SolverSettings.IterationCount=Fine?8:4;
            auto Length=[](const FCurve& X){double V=0;for(int I=1;I<X.P.Num();++I)V+=FVector::Distance(X.P[I-1],X.P[I]);return V;};
            double SourceLength=0,GuideLength=0;for(const auto& X:C)SourceLength+=Length(X);for(int I:Selected)GuideLength+=Length(C[I]);
            // Match total represented length as well as guide count: long guides
            // must not accidentally make the coarse tier heavier than Fine.
            const float RenderCopies=float(NRender-RenderStart)/C.Num();
            const float RepresentedFibers=RenderCopies*SourceLength/FMath::Max(GuideLength,1.e-6);
            Physics.StrandsParameters.StrandsSize=Fine?EGroomStrandsSize::Size32:EGroomStrandsSize::Size16;Physics.StrandsParameters.StrandsDensity=1.3;Physics.StrandsParameters.StrandsThickness=.008f*FMath::Sqrt(RepresentedFibers);Physics.StrandsParameters.StrandsSmoothing=0;
            Physics.ExternalForces.GravityVector=FVector(0,0,-981);Physics.ExternalForces.AirDrag=Compact?.3f:.15f;
            auto& Bend=Physics.MaterialConstraints.BendConstraint;Bend.SolveBend=true;Bend.ProjectBend=true;Bend.BendDamping=.2;Bend.BendStiffness=(Compact?2000.f:(Supported?100.f:.2f))/RepresentedFibers;
            // Effective bundle area preserves total mass when guide count changes. EI scales
            // with fiber count, not its square: compensate r^4 with the reciprocal count.
            auto* Scale=Bend.BendScale.GetRichCurve();Scale->Reset();for(int32 K=0;K<8;++K){float Rig=0;for(const auto& X:C)Rig+=X.RigidityProfile.IsEmpty()?X.Rigidity:X.RigidityProfile[K*(X.RigidityProfile.Num()-1)/7];Rig/=C.Num();Scale->AddKey(K/7.f,FMath::Lerp(Compact?.8f:(Supported?.6f:.25f),1.f,FMath::Clamp(Rig,0.f,1.f)));}
            auto& Stretch=Physics.MaterialConstraints.StretchConstraint;Stretch.SolveStretch=true;Stretch.ProjectStretch=true;Stretch.StretchStiffness=10;Stretch.StretchDamping=.1;
            auto& Collision=Physics.MaterialConstraints.CollisionConstraint;Collision.SolveCollision=true;Collision.ProjectCollision=true;Collision.CollisionRadius=(Compact||Supported)?.12f:(Fine?.8f:1.1f);Collision.StrandsViscosity=0.f;Collision.GridDimension=Fine?FIntVector(24):FIntVector(12);
            auto& Rendering=Groom->GetHairGroupsRendering()[G];Rendering.GeometrySettings.HairWidth=RenderWidth;Rendering.GeometrySettings.HairWidth_Override=true;Rendering.ShadowSettings.bUseHairRaytracingGeometry=false;Rendering.AdvancedSettings.bUseStableRasterization=true;Rendering.MaterialSlotName=FName(*FString::Printf(TEXT("Hair_%d"),G));
            FHairGroupsMaterial Slot;Slot.SlotName=Rendering.MaterialSlotName;Slot.Material=Mats[G];Groom->GetHairGroupsMaterials().Add(Slot);
            auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Groups[G]->AsObject()->GetStringField(TEXT("name")));R->SetStringField(TEXT("kind"),Kinds[G]);R->SetStringField(TEXT("constraint_class"),Class);R->SetNumberField(TEXT("guides"),Selected.Num());R->SetNumberField(TEXT("source_curves"),C.Num());R->SetBoolField(TEXT("simulation"),!Anchor);R->SetNumberField(TEXT("points_per_guide"),Fine?32:16);GroupReports.Add(MakeShared<FJsonValueObject>(R));
        }
        Groom->CommitHairDescription(MoveTemp(Desc),EHairDescriptionType::Source);if(!Groom->CacheDerivedDatas()){Error=TEXT("Native Groom build failed");return nullptr;}Groom->InitResources();if(!Save(Groom)){Error=TEXT("Groom save failed");return nullptr;}
        if(Fine)Profile->FineGroom=Groom;else Profile->CoarseGroom=Groom;
        auto Q=MakeShared<FJsonObject>();Q->SetStringField(TEXT("quality"),Name);Q->SetNumberField(TEXT("render_curves"),NRender);Q->SetNumberField(TEXT("guides"),NGuides);Q->SetArrayField(TEXT("groups"),GroupReports);QualityReports.Add(MakeShared<FJsonValueObject>(Q));
    }
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("quality"),QualityReports);Report->SetArrayField(TEXT("collision_fit"),ColliderReport);FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Profile->BuildReportJson));
    if(!Save(Profile)){Error=TEXT("Profile save failed");return nullptr;}return Profile;
}
