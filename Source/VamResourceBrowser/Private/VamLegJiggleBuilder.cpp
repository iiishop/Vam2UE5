#include "VamLegJiggleBuilder.h"
#include <queue>
#include <vector>
#include <functional>
#include "VamLegJiggleProfile.h"
#include "VamGluteStructureProfile.h"
#include "VamBreastJiggleBuilder.h"
#include "VamBreastWeightUtils.h"
#include "VamNativeBuilder.h"
#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
double Smooth(double X){X=FMath::Clamp(X,0.,1.);return X*X*(3-2*X);}
template<class T> T* CopyAsset(T* Source,const FString& Path)
{
    if(!Source || FPackageName::DoesPackageExist(Path) || FindPackage(nullptr,*Path)) return nullptr;
    auto* R=DuplicateObject<T>(Source,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path));R->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(R);R->MarkPackageDirty();return R;
}
FVector Vector(const TSharedPtr<FJsonObject>& Row,const TCHAR* Name)
{
    const auto& A=Row->GetArrayField(Name);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
}
// Preserve hip and leg participation separately while sharing the eight-slot budget.
void BlendLegWeights(TMap<int32,double>& W,int32 Primary,const TSet<int32>& HipBones,TArray<TPair<int32,double>> Leg,double Fraction)
{
    const double Moved=W.FindRef(Primary)*FMath::Clamp(Fraction,0.,.85);if(Moved<1.e-8) return;
    TArray<TPair<int32,double>> Hip;double HipMass=0;
    for(const auto& Pair:W) if(HipBones.Contains(Pair.Key) && Pair.Value>0){Hip.Add(Pair);HipMass+=Pair.Value;}
    const int32 Slots=8-(W.Num()-Hip.Num());if(Slots<(Hip.IsEmpty()?1:2)) return;
    auto Sort=[](auto& Group){Group.Sort([](const auto& A,const auto& B){return A.Value==B.Value?A.Key<B.Key:A.Value>B.Value;});};
    Sort(Hip);Sort(Leg);int32 H=Hip.IsEmpty()?0:1,L=1;
    while(H+L<Slots && (H<Hip.Num() || L<Leg.Num()))
    {
        const double HV=H<Hip.Num()?Hip[H].Value:-1,LV=L<Leg.Num()?Leg[L].Value*Moved:-1;
        if(HV>LV) ++H;else ++L;
    }
    for(const auto& Pair:Hip) W.Remove(Pair.Key);
    W.FindChecked(Primary)-=Moved;
    auto Write=[&](const auto& Group,int32 Count,double Mass){double Sum=0;for(int32 K=0;K<Count;++K) Sum+=Group[K].Value;if(Sum>0) for(int32 K=0;K<Count;++K) W.Add(Group[K].Key,Mass*Group[K].Value/Sum);};
    Write(Hip,H,HipMass);Write(Leg,L,Moved);
}
double Kernel(const FVector& Point,const FVector& Center,double Radius,double Length)
{
    return FMath::Exp(-((Point-Center)/FVector(Radius,Radius,Length*.35)).SizeSquared()*2);
}
}
UVamCharacterDefinition* UVamLegJiggleBuilder::Build(const FString& Root,UVamCharacterDefinition* Source,UVamLegJiggleProfile* P,UVamGluteStructureProfile* Glute,const FString& FamilyJson,FString& Error)
{
    Error.Reset();auto Fail=[&](const FString& Why)->UVamCharacterDefinition*{Error=Why;return nullptr;};
    TSharedPtr<FJsonObject> Family;const TSharedPtr<FJsonObject>* Mapping=nullptr;
    if(!Source || !P || !Glute || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FamilyJson),Family) || !Family->TryGetObjectField(TEXT("leg_jiggle"),Mapping)) return Fail(TEXT("Leg Jiggle unsupported family: missing semantic mapping"));
    const auto Map=*Mapping;FVamNativeMeshInput Input;if(!UVamBreastJiggleBuilder::ExtractNative(Source->Body.LoadSynchronous(),Input,Error)) return nullptr;
    auto* Shape=Source->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;
    if(!Geometry || Geometry->InputToSource.Num()!=Input.Vertices.Num()) return Fail(TEXT("Leg topology correspondence missing"));
    auto Bone=[&](const TCHAR* Key)->int32{FString Name;if(!Map->TryGetStringField(Key,Name)) return INDEX_NONE;return Input.Bones.IndexOfByPredicate([&](const FVamBuildBone& B){return B.Name==FName(*Name);});};
    const int32 Pelvis=Bone(TEXT("pelvis")),Superior=Bone(TEXT("superior"));
    const int32 Thigh[]={Bone(TEXT("left_thigh")),Bone(TEXT("right_thigh"))},Shin[]={Bone(TEXT("left_shin")),Bone(TEXT("right_shin"))},Foot[]={Bone(TEXT("left_foot")),Bone(TEXT("right_foot"))},Toe[]={Bone(TEXT("left_toe")),Bone(TEXT("right_toe"))};
    for(int32 B:{Pelvis,Superior,Thigh[0],Thigh[1],Shin[0],Shin[1],Foot[0],Foot[1],Toe[0],Toe[1]}) if(B<0) return Fail(TEXT("Leg Jiggle unsupported source skeleton"));
    TArray<FTransform> CS;P->SourceBoneCount=Input.Bones.Num();P->SourceBoneNames.Reset();P->SourceLocalBind.Reset();
    for(const auto& B:Input.Bones){CS.Add(B.Parent<0?B.LocalBind:B.LocalBind*CS[B.Parent]);P->SourceBoneNames.Add(B.Name);P->SourceLocalBind.Add(B.LocalBind);}
    P->SourceTopologyIdentity=Geometry->TopologyDigest;P->SkeletonFamily=Family->GetStringField(TEXT("family"));P->Segments.Reset();
    P->Integration=NewObject<UVamGluteJiggleProfile>(P,TEXT("Integration"));P->Integration->SchemaVersion=2;P->Integration->bNormalizedAttachmentDamping=true;
    const FVector Up=(CS[Superior].GetLocation()-CS[Pelvis].GetLocation()).GetSafeNormal();
    const FVector Across=(CS[Thigh[0]].GetLocation()-CS[Thigh[1]].GetLocation()).GetSafeNormal();
    FVector Front=FVector::CrossProduct(Up,Across).GetSafeNormal();
    const FVector FootDirection=(CS[Toe[0]].GetLocation()-CS[Foot[0]].GetLocation()+CS[Toe[1]].GetLocation()-CS[Foot[1]].GetLocation()).GetSafeNormal();
    if(FMath::Abs(FVector::DotProduct(Front,FootDirection))<.1) return Fail(TEXT("Ambiguous source anterior foot landmarks"));
    if(FVector::DotProduct(Front,FootDirection)<0) Front=-Front;
    const FVector Lateral=FVector::CrossProduct(Up,Front).GetSafeNormal();
    TArray<TMap<int32,double>> Weights;Weights.SetNum(Input.Vertices.Num());for(const auto& W:Input.Influences) Weights[W.Vertex].Add(W.Bone,W.Weight);
    TArray<double> Area;Area.Init(0,Input.Vertices.Num());TArray<TArray<int32>> Adj;Adj.SetNum(Area.Num());
    for(int32 T=0;T<Input.Triangles.Num();T+=3)
    {
        const int32 A=Input.Triangles[T],B=Input.Triangles[T+1],C=Input.Triangles[T+2];const double Part=FVector::CrossProduct(Input.Vertices[B]-Input.Vertices[A],Input.Vertices[C]-Input.Vertices[A]).Size()/6;
        for(int32 V:{A,B,C}) Area[V]+=Part;
        for(int32 K=0;K<3;++K){const int32 U=Input.Triangles[T+K],V=Input.Triangles[T+(K+1)%3];Adj[U].AddUnique(V);Adj[V].AddUnique(U);}
    }
    TMap<int32,int32> Seams;for(int32 V=0;V<Area.Num();++V){const int32 Id=Geometry->InputToSource[V];if(const int32* Other=Seams.Find(Id)){Adj[V].AddUnique(*Other);Adj[*Other].AddUnique(V);}else Seams.Add(Id,V);}
    // UV/material splits share a source vertex. Solve each logical vertex once;
    // graph adjacency alone does not enforce equality across split copies.
    TMap<int32,TArray<int32>> WeldGroups;
    for(int32 V=0;V<Area.Num();++V) if(Geometry->InputToSource[V]>=0) WeldGroups.FindOrAdd(Geometry->InputToSource[V]).Add(V);
    auto WeldWeights=[&]()
    {
        double MaxDifference=0;int32 Copies=0;
        for(const auto& Group:WeldGroups)
        {
            int32 Representative=Group.Value[0];
            for(int32 V:Group.Value) if(Area[V]>Area[Representative]) Representative=V;
            for(int32 V:Group.Value) if(V!=Representative && Input.Vertices[V].Equals(Input.Vertices[Representative],1.e-4))
            {
                for(const auto& W:Weights[Representative]) MaxDifference=FMath::Max(MaxDifference,FMath::Abs(W.Value-Weights[V].FindRef(W.Key)));
                Weights[V]=Weights[Representative];++Copies;
            }
        }
        UE_LOG(LogTemp,Display,TEXT("LEG_SEAM_WELD copies=%d max incoming weight difference=%.9f"),Copies,MaxDifference);
    };
    WeldWeights();
    TSet<int32> HipBones;for(const auto& Side:Glute->Sides) for(const auto& Region:Side.Regions) HipBones.Add(Region.BoneIndex);
    const double Transfer=Map->GetNumberField(TEXT("skin_transfer"));if(Transfer<=0 || Transfer>.85) return Fail(TEXT("Invalid leg donor participation"));
    for(int32 Side=0;Side<2;++Side) for(int32 Part=0;Part<2;++Part)
    {
        FVamLegSegment S;S.Side=Side;S.bCalf=Part==1;S.Name=FName(*(FString(Side?TEXT("R_"):TEXT("L_"))+(Part?TEXT("Calf"):TEXT("Thigh"))));
        S.Pelvis=Pelvis;S.Thigh=Thigh[Side];S.Shin=Shin[Side];S.Foot=Foot[Side];S.LateralAxis=Lateral;S.BodyUp=Up;
        const int32 Primary=Part?S.Shin:S.Thigh,Distal=Part?S.Foot:S.Shin;
        const FVector Prox=CS[Primary].GetLocation(),End=CS[Distal].GetLocation(),Axis=(Prox-End).GetSafeNormal();S.Length=(Prox-End).Size();
        if(S.Length<1) return Fail(TEXT("Invalid leg segment length"));
        const FVector X=(Front-Axis*FVector::DotProduct(Front,Axis)).GetSafeNormal();const FTransform Anchor(FRotationMatrix::MakeFromXZ(X,Axis).ToQuat(),(Prox+End)*.5);
        S.AnchorLocal=Anchor.GetRelativeTransform(CS[Primary]);S.JointRest={CS[S.Thigh].GetRelativeTransform(CS[Pelvis]),CS[S.Shin].GetRelativeTransform(CS[S.Thigh]),CS[S.Foot].GetRelativeTransform(CS[S.Shin])};
        S.ParentRestRotations={CS[Pelvis].GetRotation(),CS[S.Thigh].GetRotation(),CS[S.Shin].GetRotation()};S.Dynamics.Side=S.Name;S.Dynamics.ReferenceGravityLocal=Anchor.GetRotation().UnrotateVector(FVector(0,0,-980));
        S.RegionWeights.SetNum(Area.Num());S.RegionPoints.SetNum(Area.Num());double Sum=0,Radial=0;
        for(int32 V=0;V<Area.Num();++V)
        {
            const FVector Local=Anchor.InverseTransformPosition(Input.Vertices[V]);S.RegionPoints[V]=Local;const double U=.5-Local.Z/S.Length;
            // Remaining primary-bone ownership already excludes the mass assigned to hip helpers.
            // Do not cut off the leg at tiny glute evidence values: allow a shared transition.
            const double Exclusion=1;
            const double Weight=Area[V]>0?Weights[V].FindRef(Primary)*Smooth(U/.28)*Smooth((1-U)/.30)*Exclusion:0;
            S.RegionWeights[V]=Weight;Sum+=Area[V]*Weight;Radial+=Area[V]*Weight*(Local.X*Local.X+Local.Y*Local.Y);
        }
        if(Sum<1) return Fail(TEXT("Insufficient source leg weight support: ")+S.Name.ToString());S.Radius=FMath::Sqrt(Radial/Sum);
        // Seam-aware diffusion constrained by original ownership prevents leaking into joints/other limbs.
        const auto Gate=S.RegionWeights;for(int32 Iteration=0;Iteration<4;++Iteration){auto Next=S.RegionWeights;for(int32 V=0;V<Area.Num();++V){double Mean=0;for(int32 N:Adj[V]) Mean+=S.RegionWeights[N];if(Adj[V].Num()) Next[V]=FMath::Min(double(Gate[V]),.5*S.RegionWeights[V]+.5*Mean/Adj[V].Num());}S.RegionWeights=MoveTemp(Next);}
        for(const auto& Group:WeldGroups)
        {
            double Total=0,Mean=0;
            for(int32 V:Group.Value){Total+=Area[V];Mean+=Area[V]*S.RegionWeights[V];}
            if(Total>0) for(int32 V:Group.Value) S.RegionWeights[V]=Mean/Total;
        }
        const auto& Regions=Map->GetArrayField(Part?TEXT("calf_regions"):TEXT("thigh_regions"));if(Regions.Num()!=5) return Fail(TEXT("Leg family requires five regional groups"));
        TArray<FVector> Centers;const double SideSign=FVector::DotProduct(Across,Anchor.GetUnitAxis(EAxis::Y))*(Side?-1:1)>0?1:-1;
        for(const auto& Value:Regions){const auto Row=Value->AsObject();FVector Center=Vector(Row,TEXT("center"))*FVector(S.Radius,S.Radius*SideSign,S.Length);Centers.Add(Center);}
        TArray<TArray<double>> Share;Share.SetNum(Area.Num());
        for(int32 V=0;V<Area.Num();++V){double Total=0;for(const FVector& Center:Centers){const double W=Kernel(S.RegionPoints[V],Center,S.Radius,S.Length);Share[V].Add(W);Total+=W;}for(double& W:Share[V]) W/=FMath::Max(Total,1.e-30);}
        S.AnchorBone=Input.Bones.Num();FVamBuildBone AnchorBone;AnchorBone.Name=FName(*(S.Name.ToString()+TEXT("_Anchor")));AnchorBone.Parent=Primary;AnchorBone.LocalBind=S.AnchorLocal;Input.Bones.Add(AnchorBone);
        for(int32 N=0;N<5;++N)
        {
            const auto Row=Regions[N]->AsObject();FVamGluteDynamicNode Node;Node.Semantic=FName(*Row->GetStringField(TEXT("name")));Node.BoneIndex=Input.Bones.Num();double WeightSum=0,Volume=0;FVector Center=FVector::ZeroVector;
            for(int32 V=0;V<Area.Num();++V){const double W=Area[V]*S.RegionWeights[V]*Share[V][N];WeightSum+=W;Center+=S.RegionPoints[V]*W;Volume+=W*FVector(S.RegionPoints[V].X,S.RegionPoints[V].Y,0).Size()*.5;}
            if(WeightSum<1.e-8 || Volume<.01) return Fail(TEXT("Empty leg semantic region"));Node.Rest=Node.COM=Center/WeightSum;Node.MassKg=Volume*P->DensityKgPerCm3;S.EffectiveVolumeCm3+=Volume;
            const FVector Hz=Vector(Row,TEXT("frequency_hz"));Node.Support=Node.MassKg*Hz*Hz*(4*PI*PI);Node.DampingRatio=FVector(Row->GetNumberField(TEXT("damping_ratio")));
            Node.PositiveTravel=Node.NegativeTravel=FVector(S.Radius*.22,S.Radius*.22,S.Radius*.1);Node.PelvisAttachment=.9;Node.ThighAttachment=.1;
            Node.PelvisPoint=FVector(0,0,Node.Rest.Z);Node.ThighPointLocal=CS[Distal].InverseTransformPosition(Anchor.TransformPosition(Node.Rest));
            S.Dynamics.Nodes.Add(Node);S.Dynamics.MassKg+=Node.MassKg;S.Dynamics.COM+=Node.COM*Node.MassKg;
            S.LengthCoefficients.Add(Vector(Row,TEXT("length_response"))*FMath::Clamp(S.Radius/S.Length,.04,.4));
            const auto& L=Row->GetArrayField(TEXT("side_response"));S.SideCoefficients.Add(FVector2D(L[0]->AsNumber(),L[1]->AsNumber())*FMath::Clamp(S.Radius/S.Length,.04,.4));
            FVamBuildBone BoneNode;BoneNode.Name=FName(*(S.Name.ToString()+TEXT("_")+Node.Semantic.ToString()));BoneNode.Parent=S.AnchorBone;BoneNode.LocalBind=FTransform(Node.Rest);Input.Bones.Add(BoneNode);
        }
        S.Dynamics.COM/=S.Dynamics.MassKg;S.Dynamics.Dimensions=FVector(S.Radius*2,S.Radius*2,S.Length);
        const int32 Graph[][2]={{0,1},{0,2},{0,3},{0,4},{1,2},{2,3},{3,4},{1,4}};for(const auto& E:Graph){FVamGluteDynamicEdge Edge;Edge.A=E[0];Edge.B=E[1];Edge.Stiffness=(S.Dynamics.Nodes[E[0]].Support+S.Dynamics.Nodes[E[1]].Support)*.025;S.Dynamics.Couplings.Add(Edge);}
        for(const auto& Param:Source->Parameters)
        {
            if(Param.Group==TEXT("Expression")) continue;const auto* Morph=Input.Morphs.FindByPredicate([&](const FVamBuildMorph& M){return M.Name==Param.Target;});if(!Morph && Param.BoneCenters.IsEmpty()) continue;
            auto NextCS=CS;for(int32 B=0;B<P->SourceBoneCount;++B){auto Local=Input.Bones[B].LocalBind;for(const auto& Delta:Param.BoneCenters) if(Delta.BoneIndex==B) Local.AddToTranslation(Delta.LocalTranslation);NextCS[B]=Input.Bones[B].Parent<0?Local:Local*NextCS[Input.Bones[B].Parent];}
            const FTransform NextAnchor=S.AnchorLocal*NextCS[Primary];FVamLegShapeResponse Response;Response.Parameter=Param.Target;Response.DefaultValue=Param.DefaultValue;double Volume=0,Evidence=0;
            for(int32 N=0;N<5;++N){FVector Mean=FVector::ZeroVector;double Total=0;for(int32 V=0;V<Area.Num();++V){const double W=Area[V]*S.RegionWeights[V]*Share[V][N];const FVector Point=NextAnchor.InverseTransformPosition(Input.Vertices[V]+(Morph?Morph->Deltas[V]:FVector::ZeroVector));Mean+=Point*W;Total+=W;Volume+=W*FVector(Point.X,Point.Y,0).Size()*.5;}const FVector Delta=Mean/FMath::Max(1.e-12,Total)-S.Dynamics.Nodes[N].Rest;Response.RestDeltas.Add(Delta);Evidence+=Delta.SizeSquared();}
            Response.LogVolume=FMath::Loge(FMath::Max(1.e-6,Volume/S.EffectiveVolumeCm3));if(Evidence>1.e-12 || FMath::Abs(Response.LogVolume)>1.e-8) S.ShapeResponses.Add(Response);
        }
        for(int32 V=0;V<Area.Num();++V){TArray<TPair<int32,double>> Helpers;for(int32 N=0;N<5;++N) Helpers.Emplace(S.Dynamics.Nodes[N].BoneIndex,Share[V][N]);BlendLegWeights(Weights[V],Primary,HipBones,Helpers,Transfer*Smooth(S.RegionWeights[V]/.65));}
        P->Segments.Add(MoveTemp(S));
    }
    WeldWeights();
    // A surface-distance extension provides transition width OUTSIDE the old mask.
    // Diffusing only inside that mask cannot remove its narrow fixed boundary.
    const TSharedPtr<FJsonObject>* TransitionPolicy=nullptr;
    if(!Map->TryGetObjectField(TEXT("hip_surface_transition"),TransitionPolicy)) return Fail(TEXT("Unsupported family: missing hip surface transition policy"));
    const auto Transition=*TransitionPolicy;
    const double WidthFraction=Transition->GetNumberField(TEXT("width_fraction"));
    const double CoreFraction=Transition->GetNumberField(TEXT("core_fraction"));
    if(!FMath::IsFinite(WidthFraction) || WidthFraction<.1 || WidthFraction>.6 || !FMath::IsFinite(CoreFraction) || CoreFraction<.4 || CoreFraction>.9) return Fail(TEXT("Invalid hip surface transition policy"));
    auto Quintic=[](double T){T=FMath::Clamp(T,0.,1.);return T*T*T*(10+T*(-15+6*T));};
    for(const auto& Side:Glute->Sides)
    {
        TSet<int32> Bones;for(const auto& R:Side.Regions) Bones.Add(R.BoneIndex);
        TArray<double> Original,Capacity,Distance,Gate;Original.Init(0,Weights.Num());Capacity.Init(0,Weights.Num());Distance.Init(1.e30,Weights.Num());Gate.Init(0,Weights.Num());
        double Peak=0,PeakRegion=0;for(float W:Side.RegionWeights) PeakRegion=FMath::Max(PeakRegion,double(W));
        const FTransform Anchor=Side.AnchorLocal*Glute->RestPelvisComponent;
        const double Width=FMath::Max(.1,Side.Dimensions.Z*WidthFraction);
        using Entry=std::pair<double,int32>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> Queue;
        for(int32 V=0;V<Weights.Num();++V)
        {
            for(const auto& W:Weights[V]) if(Bones.Contains(W.Key)) Original[V]+=W.Value;
            Capacity[V]=Original[V]+Weights[V].FindRef(Pelvis)+Weights[V].FindRef(Side.ThighBone);
            const FVector Local=Anchor.InverseTransformPosition(Input.Vertices[V]);
            Gate[V]=Quintic(Local.X/FMath::Max(.1,Side.Dimensions.X*.35))*Quintic(Side.SideSign*Local.Y/FMath::Max(.1,Side.Dimensions.Y*.22));
            Peak=FMath::Max(Peak,Original[V]);
            if(Area[V]>0 && Side.RegionWeights[V]>=PeakRegion*CoreFraction && Original[V]>1.e-5) {Distance[V]=0;Queue.emplace(0,V);}
        }
        if(Queue.empty()) return Fail(TEXT("Missing hip transition core"));
        while(!Queue.empty())
        {
            const auto Current=Queue.top();Queue.pop();const int32 V=Current.second;
            if(Current.first>Distance[V] || Current.first>=Width) continue;
            for(int32 N:Adj[V])
            {
                if(Capacity[N]<1.e-6 || Gate[N]<=0) continue;
                const double Next=Current.first+(Input.Vertices[N]-Input.Vertices[V]).Size();
                if(Next<Distance[N] && Next<Width) {Distance[N]=Next;Queue.emplace(Next,N);}
            }
        }
        int32 Expanded=0,Changed=0;double CoreBefore=0,CoreAfter=0;
        TArray<double> Final;Final.Init(0,Weights.Num());
        for(int32 V=0;V<Weights.Num();++V)
        {
            auto& W=Weights[V];const double Donor=Capacity[V]-Original[V];
            if(Donor<=1.e-8 || (Distance[V]>=Width && Original[V]<=1.e-8)) {Final[V]=Original[V];continue;}
            const double Field=Peak*(1-Quintic(Distance[V]/Width))*Gate[V];
            const double Desired=FMath::Min(Field,Capacity[V]*.85);
            TArray<TPair<int32,double>> Helpers;
            for(const auto& Pair:W) if(Bones.Contains(Pair.Key) && Pair.Value>0) Helpers.Add(Pair);
            for(int32 HelperIndex:Bones) W.Remove(HelperIndex);
            for(auto& Pair:W) if(Pair.Key==Pelvis || Pair.Key==Side.ThighBone) Pair.Value+=Original[V]*Pair.Value/Donor;
            const FVector Point=Anchor.InverseTransformPosition(Input.Vertices[V]);
            if(Helpers.IsEmpty()) for(const auto& R:Side.Regions) Helpers.Emplace(R.BoneIndex,FMath::Exp(-((Point-R.Rest)/Side.Dimensions).SizeSquared()*16));
            VamBreastWeights::Redistribute(W,{Pelvis,Side.ThighBone},Helpers,Desired/FMath::Max(1.e-8,Capacity[V]));
            for(const auto& Pair:W) if(Bones.Contains(Pair.Key)) Final[V]+=Pair.Value;
            if(Original[V]<1.e-5 && Final[V]>1.e-4) ++Expanded;
            if(FMath::Abs(Final[V]-Original[V])>1.e-5) ++Changed;
            if(Distance[V]==0){CoreBefore+=Area[V]*Original[V];CoreAfter+=Area[V]*Final[V];}
        }
        // Fit semantic support centers without changing the total motion field.
        // Positive multiplicative reweighting retains sparsity and the eight-slot budget.
        for(int32 Pass=0;Pass<96;++Pass)
        {
            FVector Centers[5];double Totals[5]={};for(auto& C:Centers) C=FVector::ZeroVector;
            for(int32 V=0;V<Weights.Num();++V) for(int32 N=0;N<5;++N)
            {
                const double W=Weights[V].FindRef(Side.Regions[N].BoneIndex)*Area[V];
                Centers[N]+=Side.RegionPoints[V]*W;Totals[N]+=W;
            }
            for(int32 N=0;N<5;++N) Centers[N]/=FMath::Max(1.e-12,Totals[N]);
            for(int32 V=0;V<Weights.Num();++V) if(Final[V]>1.e-8)
            {
                double Sum=0;
                for(int32 N=0;N<5;++N) if(double* W=Weights[V].Find(Side.Regions[N].BoneIndex))
                {
                    const FVector ErrorDirection=(Side.Regions[N].Rest-Centers[N])/Side.Dimensions;
                    const FVector Relative=(Side.RegionPoints[V]-Centers[N])/Side.Dimensions;
                    *W*=FMath::Exp(FMath::Clamp(8*FVector::DotProduct(Relative,ErrorDirection),-.5,.5));Sum+=*W;
                }
                if(Sum>1.e-12) for(int32 N=0;N<5;++N) if(double* W=Weights[V].Find(Side.Regions[N].BoneIndex)) *W*=Final[V]/Sum;
            }
        }
        double Before=0,After=0;
        for(int32 V=0;V<Weights.Num();++V) for(int32 N:Adj[V]) if(N>V)
        {
            const double Length=(Input.Vertices[N]-Input.Vertices[V]).Size();if(Length<1.e-5) continue;
            Before=FMath::Max(Before,FMath::Abs(Original[N]-Original[V])/Length);After=FMath::Max(After,FMath::Abs(Final[N]-Final[V])/Length);
        }
        UE_LOG(LogTemp,Display,TEXT("HIP_GEODESIC side=%s width=%.3f expanded=%d changed=%d core retention=%.4f max edge gradient %.4f -> %.4f"),*Side.Side.ToString(),Width,Expanded,Changed,CoreAfter/FMath::Max(1.e-8,CoreBefore),Before,After);
    }
    WeldWeights();
    int32 SharedVertices=0;
    for(const auto& VertexWeights:Weights)
    {
        double HipShare=0,LegShare=0;
        for(const auto& W:VertexWeights){if(HipBones.Contains(W.Key)) HipShare+=W.Value;if(W.Key>=P->SourceBoneCount) LegShare+=W.Value;}
        if(HipShare>1.e-4 && LegShare>1.e-4) ++SharedVertices;
    }
    UE_LOG(LogTemp,Display,TEXT("LEG_HIP_SHARED_TRANSITION vertices=%d"),SharedVertices);
    Input.Influences.Reset();for(int32 V=0;V<Weights.Num();++V){double Total=0;for(const auto& W:Weights[V]) Total+=W.Value;for(const auto& W:Weights[V]){FVamBuildInfluence F;F.Vertex=V;F.Bone=W.Key;F.Weight=W.Value/Total;Input.Influences.Add(F);}}
    P->Provenance=TEXT("Source segment skin ownership + anatomical joint endpoints + shared hip/leg influence-budget transition + triangle-area quadrature + seam-aware adjacency diffusion. Smooth longitudinal joint fade. Regional normalized kernels; measured surface-radius volume proxy, not medical volume. Morph/bone-center responses precomputed. Passive length responses are family engineering approximations, not measured activation.");
    auto* Body=UVamNativeBuilder::BuildMesh(Root+TEXT("/SK_Body"),Input,Error);if(!Body) return nullptr;
    auto* Result=CopyAsset(Source,Root+TEXT("/CD_Character"));auto* NewShape=CopyAsset(Shape,Root+TEXT("/SD_Shape"));auto* NewGeometry=CopyAsset(Geometry,Root+TEXT("/GD_Bindings"));if(!Result || !NewShape || !NewGeometry) return Fail(TEXT("Leg immutable destination conflict"));
    Result->Body=Body;Result->Skeleton=Body->GetSkeleton();Result->Shape=NewShape;Result->SkeletonExtensionVersion+=TEXT("+")+P->Algorithm;Result->Parts.Reset();NewShape->Geometry=NewGeometry;NewGeometry->RenderToInput=UVamNativeBuilder::GetRenderToInputMap(Body);
    for(int32 B=P->SourceBoneCount;B<Input.Bones.Num();++B) NewShape->NeutralLocalBind.Add(Input.Bones[B].LocalBind);
    for(int32 Part=0;Part<Source->Parts.Num();++Part){FVamNativeMeshInput PartInput;if(!UVamBreastJiggleBuilder::ExtractNative(Source->Parts[Part].LoadSynchronous(),PartInput,Error)) return nullptr;PartInput.Bones=Input.Bones;auto* Mesh=UVamNativeBuilder::BuildMesh(Root+FString::Printf(TEXT("/SK_Part_%d"),Part),PartInput,Error);if(!Mesh || !UVamNativeBuilder::ShareCompatibleSkeleton(Mesh,Body)) return Fail(TEXT("Leg part skeleton mismatch"));Result->Parts.Add(Mesh);}
    P->MarkPackageDirty();Result->MarkPackageDirty();Error=Validate(Result,P);return Error.IsEmpty()?Result:nullptr;
}
FString UVamLegJiggleBuilder::Validate(UVamCharacterDefinition* D,UVamLegJiggleProfile* P)
{
    if(!D || !P || !P->IsValidProfile()) return TEXT("Invalid Leg Jiggle profile");auto* Mesh=D->Body.LoadSynchronous();if(!Mesh) return TEXT("Missing leg mesh");const auto& Ref=Mesh->GetRefSkeleton();
    if(Ref.GetRawBoneNum()!=P->SourceBoneCount+24) return TEXT("Leg append count mismatch");
    for(int32 B=0;B<P->SourceBoneCount;++B) if(Ref.GetBoneName(B)!=P->SourceBoneNames[B] || !Ref.GetRefBonePose()[B].Equals(P->SourceLocalBind[B],1.e-6)) return TEXT("Leg build changed existing skeleton");
    for(const auto& S:P->Segments){if(Ref.GetParentIndex(S.AnchorBone)!=(S.bCalf?S.Shin:S.Thigh)) return TEXT("Leg anchor hierarchy mismatch");for(const auto& N:S.Dynamics.Nodes) if(Ref.GetParentIndex(N.BoneIndex)!=S.AnchorBone) return TEXT("Leg helper hierarchy mismatch");}
    auto CS=Ref.GetRefBonePose();for(int32 B=0;B<CS.Num();++B) if(Ref.GetParentIndex(B)>=0) CS[B]=CS[B]*CS[Ref.GetParentIndex(B)];
    for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections) for(const auto& V:Section.SoftVertices){int32 Count=0;uint32 Sum=0;FVector Position=FVector::ZeroVector;for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if(V.InfluenceWeights[K]){++Count;Sum+=V.InfluenceWeights[K];const int32 B=Section.BoneMap[V.InfluenceBones[K]];Position+=CS[B].TransformPosition(FVector(Mesh->GetRefBasesInvMatrix()[B].TransformPosition(V.Position)))*double(V.InfluenceWeights[K]);}if(Count>8 || !Sum || FMath::Abs(int32(Sum)-65535)>8 || !(Position/Sum).Equals(FVector(V.Position),.001)) return TEXT("Leg weights/bind reconstruction invalid");}
    if(D->Shape.LoadSynchronous()->NeutralLocalBind.Num()!=Ref.GetRawBoneNum()) return TEXT("Leg Shape identity mismatch");return FString();
}
