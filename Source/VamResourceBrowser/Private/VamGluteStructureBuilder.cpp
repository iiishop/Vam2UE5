#include "VamGluteStructureBuilder.h"
#include "VamBreastJiggleBuilder.h"
#include "VamGluteCorrectiveBuilder.h"
#include "VamBreastWeightUtils.h"
#include "VamNativeBuilder.h"
#include "VamGluteStructure.h"
#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
double Smooth(double X) { X=FMath::Clamp(X,0.,1.);return X*X*(3-2*X); }
template<class T> T* Copy(T* Source,const FString& Path)
{
    if(!Source || FPackageName::DoesPackageExist(Path) || FindPackage(nullptr,*Path)) return nullptr;
    auto* R=DuplicateObject<T>(Source,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path));
    R->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(R);R->MarkPackageDirty();return R;
}
double Kernel(const FVector& A,const FVector& B,const FVector& Dimensions)
{
    const FVector D=(A-B)/Dimensions;return FMath::Exp(-D.SizeSquared()*16);
}
// Measure support cones, partitioned by normalized semantic kernels. No fixed regional mass shares.
void Measure(FVamGluteSide& S,const FVamNativeMeshInput& Input,const TArray<FVector>& Vertices,const FTransform& Anchor,const TArray<TMap<int32,double>>& Weights,const FVector& Shin,double Density,double Modulus)
{
    S.RegionPoints.Reset();S.EffectiveVolumeCm3=0;S.SupportAreaCm2=0;S.COM=FVector::ZeroVector;
    FVector Mean=FVector::ZeroVector,Variance=FVector::ZeroVector;double Total=0;
    for(int32 V=0;V<Vertices.Num();++V) { const FVector L=Anchor.InverseTransformPosition(Vertices[V]);S.RegionPoints.Add(L);Mean+=L*S.RegionWeights[V];Total+=S.RegionWeights[V]; }
    Mean/=FMath::Max(1.e-9,Total);
    for(int32 V=0;V<Vertices.Num();++V) { const FVector D=S.RegionPoints[V]-Mean;Variance+=D*D*S.RegionWeights[V]; }
    for(int32 A=0;A<3;++A) S.Dimensions[A]=FMath::Max(.1,4*FMath::Sqrt(Variance[A]/FMath::Max(1.e-9,Total)));
    S.Dimensions.X=FMath::Max(S.Dimensions.X,Mean.X);
    const FVector Centers[5]={Mean,Mean+FVector(0,0,S.Dimensions.Z*.22),Mean-FVector(0,0,S.Dimensions.Z*.22),Mean-FVector(0,S.SideSign*S.Dimensions.Y*.22,0),Mean+FVector(0,S.SideSign*S.Dimensions.Y*.22,0)};
    const FName Names[5]={TEXT("Core"),TEXT("Upper"),TEXT("Lower"),TEXT("Medial"),TEXT("Lateral")};
    S.Regions.SetNum(5);
    const FVector Hip=S.RestThighInAnchor.GetLocation();const FVector Femur=(Shin-Hip).GetSafeNormal();const double Length=(Shin-Hip).Size();
    for(int32 N=0;N<5;++N)
    {
        auto& R=S.Regions[N];R.Semantic=Names[N];R.Rest=R.MassCenter=FVector::ZeroVector;R.EffectiveVolumeCm3=0;
        double Sum=0,WP=0,WT=0,WG=0;
        for(int32 V=0;V<Vertices.Num();++V)
        {
            const double W=S.RegionWeights[V]*Kernel(S.RegionPoints[V],Centers[N],S.Dimensions);
            Sum+=W;R.Rest+=S.RegionPoints[V]*W;WP+=Weights[V].FindRef(S.PelvisBone)*W;WT+=Weights[V].FindRef(S.ThighBone)*W;WG+=Weights[V].FindRef(S.SourceGluteBone)*W;
        }
        R.Rest/=FMath::Max(Sum,1.e-9);R.PelvisPoint=FVector(0,R.Rest.Y,R.Rest.Z);
        const double Along=FMath::Clamp(FVector::DotProduct(R.Rest-Hip,Femur),Length*.02,Length*.18);
        // Off-axis posterior/fascial attachment makes femoral axial rotation observable.
        const FVector Point=Hip+Femur*Along+FVector(S.Dimensions.X*.16,(R.Rest.Y-Hip.Y)*.3,0);
        R.ThighPointLocal=S.RestThighInAnchor.InverseTransformPosition(Point);
        const double DP=FMath::Max(.01,(R.Rest-R.PelvisPoint).Size()),DT=FMath::Max(.01,(R.Rest-Point).Size());
        const double P=(WP+WG*DT/(DP+DT))/DP,T=(WT+WG*DP/(DP+DT))/DT;
        R.PelvisAttachment=FMath::Clamp(P/FMath::Max(1.e-12,P+T),.02,.98);R.ThighAttachment=1-R.PelvisAttachment;
        R.LeverArmCm=(Point-R.PelvisPoint).Size();
    }
    for(int32 T=0;T<Input.Triangles.Num();T+=3)
    {
        const int32 A=Input.Triangles[T],B=Input.Triangles[T+1],C=Input.Triangles[T+2];
        const FVector Center=(S.RegionPoints[A]+S.RegionPoints[B]+S.RegionPoints[C])/3.;
        const double Area=FMath::Abs(FVector::CrossProduct(S.RegionPoints[B]-S.RegionPoints[A],S.RegionPoints[C]-S.RegionPoints[A]).X)*.5*(S.RegionWeights[A]+S.RegionWeights[B]+S.RegionWeights[C])/3.;
        const double Volume=Area*FMath::Max(0.,Center.X)/3.;const FVector MC(Center.X*.75,Center.Y,Center.Z);
        S.EffectiveVolumeCm3+=Volume;S.SupportAreaCm2+=Area;S.COM+=MC*Volume;
        double K[5],Sum=0;for(int32 N=0;N<5;++N) { K[N]=Kernel(Center,Centers[N],S.Dimensions);Sum+=K[N]; }
        if(Sum<1.e-30) continue;
        for(int32 N=0;N<5;++N) { const double Part=Volume*K[N]/Sum;S.Regions[N].EffectiveVolumeCm3+=Part;S.Regions[N].MassCenter+=MC*Part; }
    }
    S.COM/=FMath::Max(1.e-9,S.EffectiveVolumeCm3);
    for(auto& R:S.Regions)
    {
        R.MassCenter/=FMath::Max(1.e-9,R.EffectiveVolumeCm3);R.MassFractionCandidate=R.EffectiveVolumeCm3/FMath::Max(1.e-9,S.EffectiveVolumeCm3);
        const FVector D=R.MassCenter-S.COM,Size=S.Dimensions*.5;const double Mass=R.EffectiveVolumeCm3*Density;
        R.InertiaCandidate=Mass*FVector(D.Y*D.Y+D.Z*D.Z+(Size.Y*Size.Y+Size.Z*Size.Z)/12,D.X*D.X+D.Z*D.Z+(Size.X*Size.X+Size.Z*Size.Z)/12,D.X*D.X+D.Y*D.Y+(Size.X*Size.X+Size.Y*Size.Y)/12);
        R.SupportBaseline=Modulus*.01*S.SupportAreaCm2*R.MassFractionCandidate/FMath::Max(.1,R.LeverArmCm);
    }
    const FVector Lower=S.Regions[2].Rest;
    S.FoldReferences={Lower-FVector(0,S.SideSign*S.Dimensions.Y*.2,0),Lower,Lower+FVector(0,S.SideSign*S.Dimensions.Y*.2,0)};
}
}
UVamCharacterDefinition* UVamGluteStructureBuilder::Build(const FString& Root,UVamCharacterDefinition* Source,UVamGluteStructureProfile* P,UVamGluteCorrectiveProfile* Corrective,const FString& FamilyJson,FString& Error)
{
    Error.Reset();auto Fail=[&](const FString& Why)->UVamCharacterDefinition* { Error=Why;return nullptr; };
    if(!Source || !P) return Fail(TEXT("Missing Glute source/profile"));
    TSharedPtr<FJsonObject> Family;const TSharedPtr<FJsonObject>* Map=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FamilyJson),Family) || !Family || !Family->TryGetObjectField(TEXT("glute_structure"),Map)) return Fail(TEXT("Glute Structure unsupported family: missing semantic mapping"));
    FVamNativeMeshInput I;if(!UVamBreastJiggleBuilder::ExtractNative(Source->Body.LoadSynchronous(),I,Error)) return nullptr;
    auto* Shape=Source->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;
    if(!Geometry || Geometry->InputToSource.Num()!=I.Vertices.Num()) return Fail(TEXT("Glute source topology correspondence unavailable"));
    P->SchemaVersion=2;P->RefinementVersion=1;P->Algorithm=TEXT("glute-structure-g05-v1");
    P->SourceTopologyIdentity=Geometry->TopologyDigest;P->SkeletonFamily=Family->GetStringField(TEXT("family"));P->SourceBoneCount=I.Bones.Num();P->Sides.Reset();
    auto Bone=[&](const TCHAR* Key)->int32 { FString Name;if(!(*Map)->TryGetStringField(Key,Name)) return INDEX_NONE;return I.Bones.IndexOfByPredicate([&](const FVamBuildBone& B){return B.Name==FName(*Name);}); };
    const int32 Pelvis=Bone(TEXT("pelvis")),Superior=Bone(TEXT("superior"));
    const int32 Glutes[]={Bone(TEXT("left_glute")),Bone(TEXT("right_glute"))},Thighs[]={Bone(TEXT("left_thigh")),Bone(TEXT("right_thigh"))},Shins[]={Bone(TEXT("left_shin")),Bone(TEXT("right_shin"))};
    if(Pelvis<0 || Superior<0 || Glutes[0]<0 || Glutes[1]<0 || Thighs[0]<0 || Thighs[1]<0 || Shins[0]<0 || Shins[1]<0) return Fail(TEXT("Glute Structure unsupported family: required source semantics absent"));
    TArray<FTransform> CS;for(const auto& B:I.Bones) CS.Add(B.Parent<0?B.LocalBind:B.LocalBind*CS[B.Parent]);
    P->RestPelvisComponent=CS[Pelvis];
    TArray<TMap<int32,double>> W;W.SetNum(I.Vertices.Num());for(const auto& F:I.Influences) W[F.Vertex].Add(F.Bone,F.Weight);
    TArray<TArray<int32>> Adj;Adj.SetNum(I.Vertices.Num());
    for(int32 T=0;T<I.Triangles.Num();T+=3) for(int32 K=0;K<3;++K) { const int32 A=I.Triangles[T+K],B=I.Triangles[T+(K+1)%3];Adj[A].AddUnique(B);Adj[B].AddUnique(A); }
    TMap<int32,int32> First;for(int32 V=0;V<I.Vertices.Num();++V) { const int32 Id=Geometry->InputToSource[V];if(const int32* Other=First.Find(Id)) { Adj[V].AddUnique(*Other);Adj[*Other].AddUnique(V); }else First.Add(Id,V); }
    const FVector Z=(CS[Superior].GetLocation()-CS[Pelvis].GetLocation()).GetSafeNormal();
    FVector Y=(CS[Thighs[0]].GetLocation()-CS[Thighs[1]].GetLocation());Y=(Y-Z*FVector::DotProduct(Y,Z)).GetSafeNormal();
    FVector X=FVector::CrossProduct(Y,Z).GetSafeNormal(),Posterior=FVector::ZeroVector;double Support=0;
    for(int32 V=0;V<I.Vertices.Num();++V) { const double Weight=W[V].FindRef(Glutes[0])+W[V].FindRef(Glutes[1]);Posterior+=(I.Vertices[V]-CS[Pelvis].GetLocation())*Weight;Support+=Weight; }
    const bool HasGluteWeights=Support>=1;
    if(!HasGluteWeights)
    {
        // Some valid VamFemale88 native sources retain glute semantics but no glute skin influences.
        // Their bind locations are region evidence only, never runtime reference frames.
        Posterior=(CS[Glutes[0]].GetLocation()+CS[Glutes[1]].GetLocation())*.5-CS[Pelvis].GetLocation();Support=1;
        UE_LOG(LogTemp,Display,TEXT("G0_REGION_EVIDENCE: source glute weights absent; using source glute bind landmarks + pelvis/proximal-thigh donor topology + localized morph deltas"));
    }
    if(X.IsNearlyZero() || FMath::Abs(FVector::DotProduct(Posterior,X))/Support<.01) return Fail(FString::Printf(TEXT("Ambiguous posterior source evidence: support %.6f posterior %s X %s pelvis %s superior %s left thigh %s right thigh %s"),Support,*Posterior.ToString(),*X.ToString(),*CS[Pelvis].GetLocation().ToString(),*CS[Superior].GetLocation().ToString(),*CS[Thighs[0]].GetLocation().ToString(),*CS[Thighs[1]].GetLocation().ToString()));
    if(FVector::DotProduct(Posterior,X)<0) { X=-X;Y=-Y; }
    const FTransform Anchor(FRotationMatrix::MakeFromXY(X,Y).ToQuat(),CS[Pelvis].GetLocation());
    for(int32 Side=0;Side<2;++Side)
    {
        FVamGluteSide S;S.Side=Side==0?TEXT("L"):TEXT("R");S.PelvisBone=Pelvis;S.ThighBone=Thighs[Side];S.SourceGluteBone=Glutes[Side];
        S.SideSign=FVector::DotProduct(CS[Thighs[Side]].GetLocation()-CS[Thighs[1-Side]].GetLocation(),Y)>0?1:-1;
        S.AnchorLocal=Anchor.GetRelativeTransform(CS[Pelvis]);S.RestThighInAnchor=CS[S.ThighBone].GetRelativeTransform(Anchor);
        TArray<double> Seeds;Seeds.SetNum(I.Vertices.Num());
        const double FemurLength=(CS[Shins[Side]].GetLocation()-CS[S.ThighBone].GetLocation()).Size();
        const FVector Landmark=CS[S.SourceGluteBone].GetLocation();
        const double SeedRadius=FMath::Max(FemurLength*.18,(Landmark-CS[S.ThighBone].GetLocation()).Size()*.6);
        for(int32 V=0;V<I.Vertices.Num();++V)
        {
            const FVector L=Anchor.InverseTransformPosition(I.Vertices[V]);
            Seeds[V]=HasGluteWeights?W[V].FindRef(S.SourceGluteBone):
                (W[V].FindRef(Pelvis)+W[V].FindRef(S.ThighBone))*FMath::Exp(-(I.Vertices[V]-Landmark).SizeSquared()/FMath::Square(SeedRadius))*Smooth(L.X/FMath::Max(.1,SeedRadius*.5))*Smooth(S.SideSign*L.Y/FMath::Max(.1,SeedRadius*.5));
        }
        FVector Mean=FVector::ZeroVector,Variance=FVector::ZeroVector;double Sum=0;
        for(int32 V=0;V<I.Vertices.Num();++V) { const double Weight=Seeds[V];Mean+=Anchor.InverseTransformPosition(I.Vertices[V])*Weight;Sum+=Weight; }
        if(Sum<1) return Fail(TEXT("Insufficient original glute weight evidence"));Mean/=Sum;
        for(int32 V=0;V<I.Vertices.Num();++V) { const FVector D=Anchor.InverseTransformPosition(I.Vertices[V])-Mean;Variance+=D*D*Seeds[V]; }
        FVector Spread;for(int32 A=0;A<3;++A) Spread[A]=FMath::Max(.1,FMath::Sqrt(Variance[A]/Sum));
        TArray<double> MorphEvidence;MorphEvidence.Init(0,I.Vertices.Num());
        for(const auto& M:I.Morphs)
        {
            double All=0,Local=0;for(int32 V=0;V<I.Vertices.Num();++V) { const double E=M.Deltas[V].SizeSquared();All+=E;Local+=E*Seeds[V]; }
            if(All<1.e-10 || Local/All<.25) continue;
            for(int32 V=0;V<I.Vertices.Num();++V) MorphEvidence[V]+=M.Deltas[V].SizeSquared()/All;
        }
        double MaxMorph=0;for(double E:MorphEvidence) MaxMorph=FMath::Max(MaxMorph,E);
        TArray<float> Gate;Gate.SetNum(I.Vertices.Num());S.RegionWeights.SetNum(I.Vertices.Num());
        for(int32 V=0;V<I.Vertices.Num();++V)
        {
            const FVector L=Anchor.InverseTransformPosition(I.Vertices[V]);const double G=Seeds[V],T=W[V].FindRef(S.ThighBone),PW=W[V].FindRef(Pelvis);
            const double Nearby=FMath::Exp(-.5*((L-Mean)/(Spread*2)).SizeSquared());
            const double PosteriorGate=Smooth(L.X/FMath::Max(.1,Mean.X*.6));
            const double SideGate=Smooth(S.SideSign*L.Y/FMath::Max(.1,FMath::Abs(Mean.Y)*.45));
            const double Inferior=Smooth((L.Z-(Mean.Z-Spread.Z*2-FemurLength*.05))/FMath::Max(.1,Spread.Z));
            const double SuperiorGate=Smooth((Mean.Z+Spread.Z*2.5-L.Z)/FMath::Max(.1,Spread.Z));
            Gate[V]=PosteriorGate*SideGate*Inferior*SuperiorGate*FMath::Clamp(G+(PW+T)*Nearby,0.,1.);
            const double ME=MaxMorph>0?FMath::Sqrt(MorphEvidence[V]/MaxMorph):0;
            S.RegionWeights[V]=Gate[V]*FMath::Clamp(G*2+Nearby*(PW*.35+T*.15)+ME*.25,0.,1.);
        }
        for(int32 Pass=0;Pass<8;++Pass)
        {
            auto Next=S.RegionWeights;
            for(int32 V=0;V<I.Vertices.Num();++V) if(!Adj[V].IsEmpty()) { double Average=0;for(int32 N:Adj[V]) Average+=S.RegionWeights[N];Next[V]=FMath::Min(double(Gate[V]),.6*S.RegionWeights[V]+.4*Average/Adj[V].Num()); }
            S.RegionWeights=MoveTemp(Next);
        }
        const FVector Shin=Anchor.InverseTransformPosition(CS[Shins[Side]].GetLocation());
        Measure(S,I,I.Vertices,Anchor,W,Shin,P->DensityCandidateKgPerCm3,P->EffectiveModulusPa);
        S.FemurAxisInAnchor=(Shin-S.RestThighInAnchor.GetLocation()).GetSafeNormal();
        VamGluteStructure::CalibratePoseRefinement(S);
        if(S.EffectiveVolumeCm3<=.01) return Fail(TEXT("Glute effective volume evidence empty"));
        S.AnchorBone=I.Bones.Num();FVamBuildBone A;A.Name=FName(*(S.Side.ToString()+TEXT("_Glute_Anchor")));A.Parent=Pelvis;A.LocalBind=S.AnchorLocal;I.Bones.Add(A);
        for(auto& R:S.Regions) { R.BoneIndex=I.Bones.Num();FVamBuildBone B;B.Name=FName(*(S.Side.ToString()+TEXT("_Glute_")+R.Semantic.ToString()));B.Parent=S.AnchorBone;B.LocalBind=FTransform(VamGluteStructure::FiberBasis(S,R),R.Rest);I.Bones.Add(B); }
        for(const auto& Param:Source->Parameters)
        {
            if(Param.Group==TEXT("Expression")) continue;
            const auto* M=I.Morphs.FindByPredicate([&](const FVamBuildMorph& Morph){return Morph.Name==Param.Target;});
            double Evidence=0;TArray<FVector> Changed=I.Vertices;
            for(int32 V=0;V<Changed.Num();++V) { if(M) { Changed[V]+=M->Deltas[V];Evidence+=M->Deltas[V].SizeSquared()*S.RegionWeights[V]; } }
            if(Evidence<1.e-10 && Param.BoneCenters.IsEmpty()) continue;
            TArray<FTransform> NextCS;
            for(int32 B=0;B<P->SourceBoneCount;++B)
            {
                FTransform Local=I.Bones[B].LocalBind;
                for(const auto& Delta:Param.BoneCenters) if(Delta.BoneIndex==B) Local.AddToTranslation(Delta.LocalTranslation);
                NextCS.Add(I.Bones[B].Parent<0?Local:Local*NextCS[I.Bones[B].Parent]);
            }
            const FTransform NextAnchor=S.AnchorLocal*NextCS[Pelvis];
            auto Next=S;Next.RestThighInAnchor=NextCS[S.ThighBone].GetRelativeTransform(NextAnchor);
            Measure(Next,I,Changed,NextAnchor,W,NextAnchor.InverseTransformPosition(NextCS[Shins[Side]].GetLocation()),P->DensityCandidateKgPerCm3,P->EffectiveModulusPa);
            FVamGluteShapeResponse R;R.Parameter=Param.Target;R.DefaultValue=Param.DefaultValue;R.LogVolume=FMath::Loge(FMath::Max(1.e-8,Next.EffectiveVolumeCm3/S.EffectiveVolumeCm3));R.COM=Next.COM-S.COM;R.Dimensions=Next.Dimensions-S.Dimensions;R.SupportAreaDelta=Next.SupportAreaCm2-S.SupportAreaCm2;
            for(int32 N=0;N<5;++N) { R.PelvisAttachmentDeltas.Add(Next.Regions[N].PelvisAttachment-S.Regions[N].PelvisAttachment);R.MassCenterDeltas.Add(Next.Regions[N].MassCenter-S.Regions[N].MassCenter);R.RestDeltas.Add(Next.Regions[N].Rest-S.Regions[N].Rest);R.PelvisDeltas.Add(Next.Regions[N].PelvisPoint-S.Regions[N].PelvisPoint);R.ThighDeltas.Add(Next.Regions[N].ThighPointLocal-S.Regions[N].ThighPointLocal);R.VolumeSlopes.Add(FMath::Loge(FMath::Max(1.e-8,Next.Regions[N].EffectiveVolumeCm3/S.Regions[N].EffectiveVolumeCm3))); }
            S.ShapeResponses.Add(MoveTemp(R));
        }
        P->Sides.Add(MoveTemp(S));
    }
    for(int32 V=0;V<I.Vertices.Num();++V)
    {
        for(const auto& S:P->Sides)
        {
            TArray<TPair<int32,double>> Helpers;for(const auto& R:S.Regions) Helpers.Emplace(R.BoneIndex,Kernel(S.RegionPoints[V],R.Rest,S.Dimensions));
            TSet<int32> Donors={Pelvis,S.SourceGluteBone,S.ThighBone};
            const double Transfer=.7*S.RegionWeights[V]*Smooth(S.RegionPoints[V].X/FMath::Max(.1,S.Dimensions.X*.5));
            VamBreastWeights::Redistribute(W[V],Donors,Helpers,Transfer);
        }
        double Total=0;for(const auto& F:W[V]) Total+=F.Value;for(auto& F:W[V]) F.Value/=Total;
    }
    I.Influences.Reset();for(int32 V=0;V<W.Num();++V) for(const auto& F:W[V]) { FVamBuildInfluence R;R.Vertex=V;R.Bone=F.Key;R.Weight=F.Value;I.Influences.Add(R); }
    P->RegionProvenance=FString(HasGluteWeights?TEXT("Glute source skin support present. "):TEXT("Glute source skin support absent: source glute bind landmarks localize pelvis/proximal femur donor evidence. "))+TEXT("Original glute/pelvis/proximal femur weights; actual morph delta support; source triangle adjacency and source-ID seam weld; signed posterior pelvis frame; smooth side/posterior/proximal gates; 8 diffusion passes. Surface-to-pelvis-wall cone volume is an effective proxy. Regional attachments inferred from donor evidence and distances, not measured anatomy.");
    P->SkinWeightIdentity=FMD5::HashAnsiString(*(P->Algorithm+P->SourceTopologyIdentity+Source->SourceDigest));
    if(!Corrective || !VamGluteCorrectiveBuilder::Build(I,Geometry->InputToSource,*P,*Corrective,FamilyJson,Error)) return nullptr;
    auto* Body=UVamNativeBuilder::BuildMesh(Root+TEXT("/SK_Body"),I,Error);if(!Body) return nullptr;
    auto* Result=Copy(Source,Root+TEXT("/CD_Character"));auto* NewShape=Copy(Shape,Root+TEXT("/SD_Shape"));auto* NewGeometry=Copy(Geometry,Root+TEXT("/GD_Bindings"));
    if(!Result || !NewShape || !NewGeometry) return Fail(TEXT("Glute immutable destination conflict"));
    Result->Body=Body;Result->Skeleton=Body->GetSkeleton();Result->Shape=NewShape;Result->SkeletonExtensionVersion+=TEXT("+")+P->Algorithm;Result->Parts.Reset();
    NewShape->Geometry=NewGeometry;NewGeometry->RenderToInput=UVamNativeBuilder::GetRenderToInputMap(Body);
    if(NewShape->NeutralLocalBind.Num()!=P->SourceBoneCount) return Fail(TEXT("Glute neutral source bind mismatch"));
    for(int32 B=P->SourceBoneCount;B<I.Bones.Num();++B) NewShape->NeutralLocalBind.Add(I.Bones[B].LocalBind);
    for(int32 Part=0;Part<Source->Parts.Num();++Part)
    {
        FVamNativeMeshInput PartInput;if(!UVamBreastJiggleBuilder::ExtractNative(Source->Parts[Part].LoadSynchronous(),PartInput,Error)) return nullptr;
        PartInput.Bones=I.Bones;auto* Mesh=UVamNativeBuilder::BuildMesh(Root+FString::Printf(TEXT("/SK_Part_%d"),Part),PartInput,Error);
        if(!Mesh || !UVamNativeBuilder::ShareCompatibleSkeleton(Mesh,Body)) return Fail(TEXT("Glute part skeleton mismatch: ")+Error);Result->Parts.Add(Mesh);
    }
    P->MarkPackageDirty();Result->MarkPackageDirty();Error=Validate(Result,P);return Error.IsEmpty()?Result:nullptr;
}
FString UVamGluteStructureBuilder::Validate(UVamCharacterDefinition* D,UVamGluteStructureProfile* P)
{
    if(!D || !P || !P->IsValidProfile()) return TEXT("Invalid G0 profile");
    auto* Mesh=D->Body.LoadSynchronous();if(!Mesh) return TEXT("Missing G0 body");const auto& Ref=Mesh->GetRefSkeleton();
    if(Ref.GetRawBoneNum()!=P->SourceBoneCount+12) return TEXT("G0 append count mismatch");
    for(const auto& S:P->Sides)
    {
        if(Ref.GetParentIndex(S.AnchorBone)!=S.PelvisBone) return TEXT("G0 anchor parent mismatch");
        for(const auto& R:S.Regions) if(Ref.GetParentIndex(R.BoneIndex)!=S.AnchorBone) return TEXT("G0 regional parent mismatch");
    }
    auto CS=Ref.GetRefBonePose();for(int32 B=0;B<CS.Num();++B) if(Ref.GetParentIndex(B)>=0) CS[B]=CS[B]*CS[Ref.GetParentIndex(B)];
    for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections) for(const auto& V:Section.SoftVertices)
    {
        int32 Count=0;uint32 Sum=0;FVector Position=FVector::ZeroVector;
        for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if(V.InfluenceWeights[K]) { ++Count;Sum+=V.InfluenceWeights[K];const int32 B=Section.BoneMap[V.InfluenceBones[K]];Position+=CS[B].TransformPosition(FVector(Mesh->GetRefBasesInvMatrix()[B].TransformPosition(V.Position)))*double(V.InfluenceWeights[K]); }
        if(Count>8 || FMath::Abs(int32(Sum)-65535)>8 || Sum==0) return TEXT("G0 invalid normalized influences");
        if(!(Position/Sum).Equals(FVector(V.Position),.001)) return TEXT("G0 bind reconstruction exceeded .001 cm");
    }
    if(!D->Shape.LoadSynchronous() || D->Shape.Get()->NeutralLocalBind.Num()!=Ref.GetRawBoneNum()) return TEXT("G0 Shape identity mismatch");
    return FString();
}
