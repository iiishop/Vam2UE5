#include "VamBreastContactBuilder.h"
#include "VamBreastContactProfile.h"
#include "VamBreastJiggleProfile.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "CompGeom/ConvexHull3.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

namespace
{
double Det(const FVector& A,const FVector& B,const FVector& C) { return FVector::DotProduct(A,FVector::CrossProduct(B,C)); }
FVector4f Bary(const FVector& P,const FIntVector4& T,const TArray<FVamBreastContactParticle>& X)
{
    const FVector A=X[T[0]].Rest,B=X[T[1]].Rest-A,C=X[T[2]].Rest-A,D=X[T[3]].Rest-A,Q=P-A;
    const double Den=Det(B,C,D),Y=Det(Q,C,D)/Den,Z=Det(B,Q,D)/Den,W=Det(B,C,Q)/Den;
    return FVector4f(1-Y-Z-W,Y,Z,W);
}
double Smooth(double T) { T=FMath::Clamp(T,0.,1.);return T*T*T*(T*(T*6-15)+10); }
}

FString UVamBreastContactBuilder::Build(UVamCharacterDefinition* Definition,UVamBreastJiggleProfile* Breast,UVamBreastContactProfile* P,const FString& FamilyJson)
{
    if(!Definition || !Breast || !P || !Breast->IsValidProfile() || Breast->Sides.Num()!=2) return TEXT("Contact needs a calibrated bilateral Breast profile");
    USkeletalMesh* Mesh=Definition->Body.LoadSynchronous();FVamNativeMeshInput Input;FString Error;
    if(!UVamBreastJiggleBuilder::ExtractNative(Mesh,Input,Error)) return Error;
    // Source vertex domain is retained across the append-only Breast/Glute/Leg builds.
    for(const auto& Side:Breast->Sides) if(Side.RegionWeights.Num()!=Input.Vertices.Num()) return TEXT("Contact/source topology mismatch");
    TSharedPtr<FJsonObject> Family;const TSharedPtr<FJsonObject>* Mapping=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FamilyJson),Family) || !Family || !Family->TryGetObjectField(TEXT("breast_jiggle"),Mapping))
        return TEXT("Contact family semantics missing");
    int32 NippleBones[2];
    for(int32 S=0;S<2;++S){FString Name;if(!(*Mapping)->TryGetStringField(S==0?TEXT("left_front_reference"):TEXT("right_front_reference"),Name))return TEXT("Contact nipple semantic unsupported");
        NippleBones[S]=Input.Bones.IndexOfByPredicate([&](const FVamBuildBone& B){return B.Name==FName(*Name);});if(NippleBones[S]<0)return TEXT("Contact nipple reference bone missing");}
    P->SchemaVersion=4;P->BuildAlgorithmVersion=TEXT("breast-contact-c4-bulk-surface-skin-v1");P->LocalCompressionResistance=.35;P->SurfaceBending=.05;
    P->Particles.Reset();P->Tetrahedra.Reset();P->BoundaryTriangles.Reset();P->Morphs.Reset();P->EffectiveVolumeCm3.Init(0,2);
    P->Body=Mesh;P->BindSignature=Definition->BindSignature;P->SkeletonFamily=Breast->SkeletonFamily;
    P->SourceTopologyIdentity=Breast->SourceTopologyIdentity;
    P->RegionProvenance=Breast->RegionProvenance+TEXT("; contact: evidence-selected convex envelope with anatomical chest-wall closure; convex proxy, not internal anatomy");
    TArray<FTransform> Reference;
    for(const auto& Bone:Input.Bones) Reference.Add(Bone.LocalBind);
    for(int32 B=0;B<Reference.Num();++B) if(Input.Bones[B].Parent>=0) Reference[B]*=Reference[Input.Bones[B].Parent];
    TArray<TMap<int32,float>> Skin;Skin.SetNum(Input.Vertices.Num());
    for(const auto& W:Input.Influences) Skin[W.Vertex].FindOrAdd(W.Bone)+=W.Weight;
    double NipplePeak[2]={0,0};for(int32 S=0;S<2;++S)for(int32 V=0;V<Skin.Num();++V)if(Breast->Sides[S].RegionWeights[V]>.005)NipplePeak[S]=FMath::Max(NipplePeak[S],double(Skin[V].FindRef(NippleBones[S])));
    TArray<double> NippleEvidence[2];for(int32 S=0;S<2;++S){NippleEvidence[S].Init(0,Skin.Num());if(NipplePeak[S]>1.e-5)
        for(int32 V=0;V<Skin.Num();++V)NippleEvidence[S][V]=Smooth(Skin[V].FindRef(NippleBones[S])/NipplePeak[S]);}
    // Some source families retain nipple reference bones but skin them entirely to
    // pectorals. Actual family-selected Morph delta support then supplies the region.
    const TArray<TSharedPtr<FJsonValue>>* Tokens=nullptr;(*Mapping)->TryGetArrayField(TEXT("nipple_morph_tokens"),Tokens);
    if(Tokens)for(const auto& Morph:Input.Morphs){const auto* Param=Definition->Parameters.FindByPredicate([&](const FVamMorphParameter& V){return V.Target==Morph.Name;});
        if(!Param || Param->Group==TEXT("Expression"))continue;bool Match=false;for(const auto& Token:*Tokens)Match|=Param->DisplayName.Contains(Token->AsString(),ESearchCase::IgnoreCase);if(!Match)continue;
        for(int32 S=0;S<2;++S){double Peak=0;for(int32 V=0;V<Skin.Num();++V)if(Breast->Sides[S].RegionWeights[V]>.005)Peak=FMath::Max(Peak,Morph.Deltas[V].Size());
            if(Peak<1.e-6)continue;for(int32 V=0;V<Skin.Num();++V)if(Breast->Sides[S].RegionWeights[V]>.005)
                NippleEvidence[S][V]=FMath::Max(NippleEvidence[S][V],Smooth(Morph.Deltas[V].Size()/Peak));}}
    for(int32 S=0;S<2;++S)if(!NippleEvidence[S].ContainsByPredicate([](double W){return W>.5;}))return TEXT("Contact nipple skin/Morph evidence missing");
    TMap<int32,TArray<int32>> CenterContributors;
    TArray<int32> ParticleSource;TArray<FTransform> ParticleFrames;TArray<bool> ProjectRoot;
    TArray<FIntVector4> InputParents;InputParents.Init(FIntVector4(0,0,0,0),Input.Vertices.Num());
    TArray<FVector4f> InputWeights;InputWeights.Init(FVector4f(1,0,0,0),Input.Vertices.Num());
    TArray<float> InputMask;InputMask.Init(0,Input.Vertices.Num());
    for(int32 S=0;S<2;++S)
    {
        const auto& Side=Breast->Sides[S];if(!Reference.IsValidIndex(Side.ChestBone)) return TEXT("Contact chest semantic absent");
        const FTransform Frame=Side.AnchorLocal*Reference[Side.ChestBone];
        TArray<FVector> Points;TArray<int32> Sources;TArray<bool> Roots;TArray<int32> Candidates;
        for(int32 V=0;V<Input.Vertices.Num();++V) if(Side.RegionWeights[V]>.005)
        {
            Candidates.Add(V);Points.Add(Input.Vertices[V]);Sources.Add(V);Roots.Add(false);
            FVector Root=Frame.InverseTransformPosition(Input.Vertices[V]);Root.X=FMath::Min(0.,Root.X);
            Points.Add(Frame.TransformPosition(Root));Sources.Add(V);Roots.Add(true);
        }
        if(Candidates.Num()<16) return TEXT("Insufficient contact region evidence");
        UE::Geometry::TConvexHull3<double> Hull;
        if(!Hull.Solve(Points.Num(),[&](int32 I){return Points[I];}) || !Hull.IsSolutionAvailable()) return TEXT("Contact envelope is degenerate");
        TSet<int32> Used;for(const auto& T:Hull.GetTriangles()) for(int32 J=0;J<3;++J) Used.Add(T[J]);
        if(Used.Num()>2048) return TEXT("Contact envelope exceeds particle budget; adaptive remeshing required");
        TArray<int32> Ordered=Used.Array();Ordered.Sort();TMap<int32,int32> Remap;
        FVector Center=FVector::ZeroVector;for(int32 I:Ordered) Center+=Points[I];Center/=Ordered.Num();
        auto AddParticle=[&](FVector Position,int32 Source,bool Root)
        {
            FVamBreastContactParticle Particle;Particle.Rest=Position;Particle.Side=S;Particle.bKinematic=Root;
            Particle.NippleSupport=Root?0:NippleEvidence[S][Source];
            const double Depth=Frame.InverseTransformPosition(Position).X;
            Particle.RootSupport=1-Smooth(Depth/FMath::Max(.1,Side.EffectiveDepthCm));
            if(Root) { Particle.Bones.Add(Side.ChestBone);Particle.Weights.Add(1); }
            else
            {
                TArray<int32> Bones;Skin[Source].GetKeys(Bones);Bones.Sort();double Total=0;
                for(int32 B:Bones) { Particle.Bones.Add(B);Particle.Weights.Add(Skin[Source][B]);Total+=Skin[Source][B]; }
                for(float& W:Particle.Weights) W/=Total;
            }
            const int32 Id=P->Particles.Add(MoveTemp(Particle));ParticleSource.Add(Source);ParticleFrames.Add(Frame);ProjectRoot.Add(Root);return Id;
        };
        for(int32 I:Ordered) Remap.Add(I,AddParticle(Points[I],Sources[I],Roots[I]));
        int32 Nearest=Candidates[0];for(int32 V:Candidates) if(FVector::DistSquared(Input.Vertices[V],Center)<FVector::DistSquared(Input.Vertices[Nearest],Center)) Nearest=V;
        const int32 C=AddParticle(Center,Nearest,false),FirstTet=P->Tetrahedra.Num();
        TMap<int32,double> CenterSkin;
        for(int32 I:Ordered) { const int32 V=Remap[I];CenterContributors.FindOrAdd(C).Add(V);
            for(int32 J=0;J<P->Particles[V].Bones.Num();++J) CenterSkin.FindOrAdd(P->Particles[V].Bones[J])+=P->Particles[V].Weights[J]/Ordered.Num(); }
        auto& CP=P->Particles[C];CP.NippleSupport=0;CP.Bones.Reset();CP.Weights.Reset();
        TArray<int32> Keys;CenterSkin.GetKeys(Keys);Keys.Sort();for(int32 B:Keys){CP.Bones.Add(B);CP.Weights.Add(CenterSkin[B]);}
        for(const auto& Face:Hull.GetTriangles())
        {
            FIntVector4 T(C,Remap[Face[0]],Remap[Face[1]],Remap[Face[2]]);
            double Volume=Det(P->Particles[T[1]].Rest-Center,P->Particles[T[2]].Rest-Center,P->Particles[T[3]].Rest-Center)/6;
            if(Volume<0) { Swap(T[2],T[3]);Volume=-Volume; }
            if(Volume<=1.e-8) return TEXT("Contact envelope contains a sliver/degenerate tetrahedron");
            P->Tetrahedra.Add(T);P->BoundaryTriangles.Add(FIntVector(T[1],T[2],T[3]));P->EffectiveVolumeCm3[S]+=Volume;
        }
        for(int32 V:Candidates)
        {
            const float Mask=Smooth((Side.RegionWeights[V]-.005)/.35);if(Mask<=InputMask[V]) continue;
            bool Bound=false;
            for(int32 I=FirstTet;I<P->Tetrahedra.Num();++I)
            {
                const auto W=Bary(Input.Vertices[V],P->Tetrahedra[I],P->Particles);
                if(FMath::Min(FMath::Min(W.X,W.Y),FMath::Min(W.Z,W.W))>=-1.e-5)
                { InputParents[V]=P->Tetrahedra[I];InputWeights[V]=W;InputMask[V]=Mask;Bound=true;break; }
            }
            if(!Bound) return TEXT("Evidence vertex escaped contact envelope");
        }
    }
    const auto RenderMap=UVamNativeBuilder::GetRenderToInputMap(Mesh);
    if(RenderMap.IsEmpty()) return TEXT("Contact render mapping missing");
    P->SurfaceParents.Reset();P->SurfaceWeights.Reset();P->SurfaceMask.Reset();P->SurfaceOffsets.Init(FVector3f::ZeroVector,RenderMap.Num());
    for(int32 V:RenderMap)
    {
        if(!Input.Vertices.IsValidIndex(V)) return TEXT("Contact render mapping invalid");
        P->SurfaceParents.Add(InputParents[V]);P->SurfaceWeights.Add(InputWeights[V]);P->SurfaceMask.Add(InputMask[V]);
    }
    P->SurfaceNippleSupport.Reset();P->MeasurementTriangles.Reset();TArray<int32> InputToRender;InputToRender.Init(INDEX_NONE,Input.Vertices.Num());
    for(int32 I=0;I<RenderMap.Num();++I){const int32 V=RenderMap[I];P->SurfaceNippleSupport.Add(FMath::Max(NippleEvidence[0][V],NippleEvidence[1][V]));if(InputToRender[V]<0)InputToRender[V]=I;}
    for(int32 I=0;I+2<Input.Triangles.Num();I+=3){FIntVector T(InputToRender[Input.Triangles[I]],InputToRender[Input.Triangles[I+1]],InputToRender[Input.Triangles[I+2]]);
        if(T.X>=0 && T.Y>=0 && T.Z>=0 && FMath::Min3(P->SurfaceMask[T.X],P->SurfaceMask[T.Y],P->SurfaceMask[T.Z])>.5)P->MeasurementTriangles.Add(T);}
    for(const auto& Morph:Input.Morphs)
    {
        const auto* Param=Definition->Parameters.FindByPredicate([&](const FVamMorphParameter& V){return V.Target==Morph.Name;});
        if(!Param || Param->Group==TEXT("Expression")) continue;
        FVamBreastContactMorph M;M.Parameter=Morph.Name;M.Baseline=Definition->ShapeConvention==TEXT("neutral_plus_parameters")?0:Param->DefaultValue;
        double Energy=0;
        for(int32 I=0;I<P->Particles.Num();++I)
        {
            FVector D=Morph.Deltas[ParticleSource[I]];
            if(ProjectRoot[I]) { D=ParticleFrames[I].InverseTransformVectorNoScale(D);D.X=0;D=ParticleFrames[I].TransformVectorNoScale(D); }
            M.ParticleDeltas.Add(D);Energy+=D.SizeSquared();
        }
        for(const auto& Entry:CenterContributors) { FVector D=FVector::ZeroVector;for(int32 V:Entry.Value) D+=M.ParticleDeltas[V];M.ParticleDeltas[Entry.Key]=D/Entry.Value.Num(); }
        if(Energy>1.e-10) P->Morphs.Add(MoveTemp(M));
    }
    // Conforming longest surface-edge bisection. Every adjacent tetrahedron is split
    // together; no hanging nodes, no change to the imported envelope or neutral skin.
    for(int32 SideIndex=0;SideIndex<2;++SideIndex)
    {
        const double Spacing=Breast->Sides[SideIndex].EffectiveRadiusCm*P->SurfaceSpacingRadiusFraction;
        for(int32 Pass=0;Pass<P->MaxAdditionalSurfaceParticlesPerSide;++Pass)
        {
            int32 A=INDEX_NONE,B=INDEX_NONE;double Longest=Spacing*Spacing;
            for(const auto& F:P->BoundaryTriangles) if(P->Particles[F[0]].Side==SideIndex)
                for(int32 J=0;J<3;++J) {const int32 U=F[J],V=F[(J+1)%3];
                    if(P->Particles[U].bKinematic && P->Particles[V].bKinematic) continue;
                    const double L=FVector::DistSquared(P->Particles[U].Rest,P->Particles[V].Rest);
                    if(L>Longest){Longest=L;A=U;B=V;}}
            if(A==INDEX_NONE)break;
            const auto PA=P->Particles[A],PB=P->Particles[B];FVamBreastContactParticle Mid;
            Mid.Rest=(PA.Rest+PB.Rest)*.5;Mid.Side=SideIndex;Mid.RootSupport=(PA.RootSupport+PB.RootSupport)*.5;
            Mid.NippleSupport=(PA.NippleSupport+PB.NippleSupport)*.5;
            Mid.bKinematic=PA.bKinematic && PB.bKinematic;TMap<int32,double> SkinWeights;
            for(const auto* Part:{&PA,&PB})for(int32 J=0;J<Part->Bones.Num();++J)SkinWeights.FindOrAdd(Part->Bones[J])+=Part->Weights[J]*.5;
            TArray<int32> Keys;SkinWeights.GetKeys(Keys);Keys.Sort();for(int32 K:Keys){Mid.Bones.Add(K);Mid.Weights.Add(SkinWeights[K]);}
            const int32 M=P->Particles.Add(MoveTemp(Mid));
            for(auto& Morph:P->Morphs)Morph.ParticleDeltas.Add((Morph.ParticleDeltas[A]+Morph.ParticleDeltas[B])*.5);
            const int32 NT=P->Tetrahedra.Num();for(int32 I=0;I<NT;++I){auto T=P->Tetrahedra[I];int32 IA=INDEX_NONE,IB=INDEX_NONE;
                for(int32 J=0;J<4;++J){if(T[J]==A)IA=J;if(T[J]==B)IB=J;}if(IA<0 || IB<0)continue;
                auto Other=T;T[IA]=M;Other[IB]=M;P->Tetrahedra[I]=T;P->Tetrahedra.Add(Other);}
            const int32 NF=P->BoundaryTriangles.Num();for(int32 I=0;I<NF;++I){auto F=P->BoundaryTriangles[I];int32 IA=INDEX_NONE,IB=INDEX_NONE;
                for(int32 J=0;J<3;++J){if(F[J]==A)IA=J;if(F[J]==B)IB=J;}if(IA<0 || IB<0)continue;
                auto Other=F;F[IA]=M;Other[IB]=M;P->BoundaryTriangles[I]=F;P->BoundaryTriangles.Add(Other);}
        }
    }
    // A conforming interior layer preserves the exact source envelope. Each
    // triangular frustum is divided with globally ordered face diagonals, so
    // neighboring cells agree. Unlike a one-center star, surface compression
    // now loads a distributed interior scaffold before reaching the core.
    const auto OuterFaces=P->BoundaryTriangles;P->Tetrahedra.Reset();
    for(int32 S=0;S<2;++S)
    {
        int32 Center=INDEX_NONE;for(const auto& Entry:CenterContributors)if(P->Particles[Entry.Key].Side==S)Center=Entry.Key;
        if(Center<0)return TEXT("Contact center missing");
        TSet<int32> Boundary;for(const auto& F:OuterFaces)if(P->Particles[F[0]].Side==S)for(int32 J=0;J<3;++J)Boundary.Add(F[J]);
        TArray<int32> Ordered=Boundary.Array();Ordered.Sort();TMap<int32,int32> Inner;
        for(int32 I:Ordered)
        {
            const auto A=P->Particles[I],C=P->Particles[Center];FVamBreastContactParticle N;
            N.Side=S;N.Rest=(A.Rest+C.Rest)*.5;N.RootSupport=(A.RootSupport+C.RootSupport)*.5;
            TMap<int32,double> SkinWeights;for(const auto* V:{&A,&C})for(int32 J=0;J<V->Bones.Num();++J)SkinWeights.FindOrAdd(V->Bones[J])+=V->Weights[J]*.5;
            TArray<int32> Keys;SkinWeights.GetKeys(Keys);Keys.Sort();for(int32 K:Keys){N.Bones.Add(K);N.Weights.Add(SkinWeights[K]);}
            Inner.Add(I,P->Particles.Add(MoveTemp(N)));for(auto& M:P->Morphs)M.ParticleDeltas.Add((M.ParticleDeltas[I]+M.ParticleDeltas[Center])*.5);
        }
        for(auto F:OuterFaces)if(P->Particles[F[0]].Side==S)
        {
            if(F.X>F.Y)Swap(F.X,F.Y);if(F.Y>F.Z)Swap(F.Y,F.Z);if(F.X>F.Y)Swap(F.X,F.Y);
            const int32 A=F.X,B=F.Y,C=F.Z,a=Inner[A],b=Inner[B],c=Inner[C];
            FIntVector4 Cells[]={FIntVector4(A,B,C,c),FIntVector4(A,B,b,c),FIntVector4(A,a,b,c),FIntVector4(Center,a,b,c)};
            for(auto T:Cells){const FVector O=P->Particles[T[0]].Rest;
                const double D=Det(P->Particles[T[1]].Rest-O,P->Particles[T[2]].Rest-O,P->Particles[T[3]].Rest-O);
                if(FMath::Abs(D)<=6.e-8)return TEXT("Layered contact cell is degenerate");if(D<0)Swap(T[2],T[3]);P->Tetrahedra.Add(T);}
        }
    }
    // Rebind in the refined cage. All positions are unchanged so no extrapolation is needed.
    for(int32 V=0;V<Input.Vertices.Num();++V) if(InputMask[V]>0)
    {
        const int32 SideIndex=P->Particles[InputParents[V][0]].Side;bool Found=false;
        for(const auto& T:P->Tetrahedra) if(P->Particles[T[0]].Side==SideIndex)
        {
            const auto W=Bary(Input.Vertices[V],T,P->Particles);
            if(FMath::Min(FMath::Min(W.X,W.Y),FMath::Min(W.Z,W.W))>=-1.e-5)
            {InputParents[V]=T;InputWeights[V]=W;Found=true;break;}
        }
        if(!Found)return TEXT("Refined contact embedding failed");
    }
    for(int32 I=0;I<RenderMap.Num();++I){P->SurfaceParents[I]=InputParents[RenderMap[I]];P->SurfaceWeights[I]=InputWeights[RenderMap[I]];}
    TArray<FVector> RestPositions;for(const auto& Particle:P->Particles)RestPositions.Add(Particle.Rest);
    TArray<FVamBreastContactVolumeState> RefinedVolume;
    if(!P->MeasureVolume(RestPositions,RestPositions,RefinedVolume))return TEXT("Refined cage volume invalid");
    for(int32 SideIndex=0;SideIndex<2;++SideIndex)if(FMath::Abs(RefinedVolume[SideIndex].RestVolumeCm3/P->EffectiveVolumeCm3[SideIndex]-1)>1.e-6)
        return FString::Printf(TEXT("Volume triangulation changed neutral volume side %d: %.9f -> %.9f"),SideIndex,P->EffectiveVolumeCm3[SideIndex],RefinedVolume[SideIndex].RestVolumeCm3);
    for(int32 I=0;I<RenderMap.Num();++I)if(P->SurfaceMask[I]>0){FVector Bound=FVector::ZeroVector;
        for(int32 J=0;J<4;++J)Bound+=P->Particles[P->SurfaceParents[I][J]].Rest*P->SurfaceWeights[I][J];
        if(FVector::Distance(Bound,Input.Vertices[RenderMap[I]])>.001)return TEXT("Refined zero-offset skin binding mismatch");}
    P->RegionProvenance+=TEXT("; C2 conforming layered interior volume; root-only foundation; conforming surface-edge refinement; interpolated skin/Morph; local compression barrier and surface strain limits; nipple material: family front-reference bone skin and family-selected real Morph delta support, side-normalized, interpolated; free-moving local shape constraints");
    Error=RemeshFTetWild(P);if(!Error.IsEmpty())return Error;
    Error=P->ValidateData();if(Error.IsEmpty()) P->MarkPackageDirty();return Error;
}
