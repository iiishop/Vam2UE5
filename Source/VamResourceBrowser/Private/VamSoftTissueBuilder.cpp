#include "VamSoftTissueBuilder.h"
#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
double Det(const FVector& A,const FVector& B,const FVector& C) {return FVector::DotProduct(A,FVector::CrossProduct(B,C));}
FVector4f Bary(const FVector& P,const FIntVector4& T,const TArray<FVector>& X)
{
    const FVector A=X[T[0]],B=X[T[1]]-A,C=X[T[2]]-A,D=X[T[3]]-A,Q=P-A;const double Den=Det(B,C,D);
    const double Y=Det(Q,C,D)/Den,Z=Det(B,Q,D)/Den,W=Det(B,C,Q)/Den;return FVector4f(1-Y-Z-W,Y,Z,W);
}
}
FString UVamSoftTissueBuilder::Build(UVamSoftTissueProfile* P,UVamCharacterDefinition* Definition,const TArray<FVamSoftTissueRegion>& Regions)
{
    auto* Mesh=Definition?Definition->Body.LoadSynchronous():nullptr;
    auto* Shape=Definition?Definition->Shape.LoadSynchronous():nullptr;
    if(!P || !Mesh || !Shape || !Mesh->GetImportedModel() || Mesh->GetImportedModel()->LODModels.IsEmpty() || Regions.IsEmpty()) return TEXT("Missing explicit native profile inputs");
    const auto& LOD=Mesh->GetImportedModel()->LODModels[0];const auto& Ref=Mesh->GetRefSkeleton();
    const auto* Render=Mesh->GetResourceForRendering();
    if(!Render || Render->LODRenderData.IsEmpty() || Render->LODRenderData[0].GetNumVertices()!=uint32(LOD.NumVertices)) return TEXT("Render/import topology needs explicit remapping");
    P->Positions.SetNum(LOD.NumVertices);P->Normals.SetNum(LOD.NumVertices);P->Skin.SetNum(LOD.NumVertices);
    P->UV0.SetNum(LOD.NumVertices);P->UV1.SetNum(LOD.NumVertices);P->UV2.SetNum(LOD.NumVertices);P->UV3.SetNum(LOD.NumVertices);P->Colors.SetNum(LOD.NumVertices);
    P->Sections.Reset();P->Morphs.Reset();P->ExpressionMorphs.Reset();
    for(const auto& Section:LOD.Sections)
    {
        if(Section.NumVertices!=Section.SoftVertices.Num()) return TEXT("Native soft vertex section mismatch");
        FVamTissueSection Output;Output.MaterialSlot=Section.MaterialIndex;Output.FirstVertex=Section.BaseVertexIndex;Output.NumVertices=Section.NumVertices;
        for(uint32 T=0;T<Section.NumTriangles*3;++T)
        {
            const int32 Index=LOD.IndexBuffer[Section.BaseIndex+T]-Section.BaseVertexIndex;
            if(Index<0 || Index>=Output.NumVertices) return TEXT("Cross-section triangle requires explicit remapping");
            Output.Triangles.Add(Index);
        }
        P->Sections.Add(MoveTemp(Output));
        for(int32 V=0;V<Section.SoftVertices.Num();++V)
        {
            const auto& Vertex=Section.SoftVertices[V];const int32 I=Section.BaseVertexIndex+V;
            P->Positions[I]=FVector(Vertex.Position);P->Normals[I]=FVector(Vertex.TangentZ);
            if(!P->Positions[I].Equals(FVector(Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I)),1.e-4)) return TEXT("Render/import vertex order differs");
            P->UV0[I]=FVector2D(Vertex.UVs[0]);P->UV1[I]=FVector2D(Vertex.UVs[1]);P->UV2[I]=FVector2D(Vertex.UVs[2]);P->UV3[I]=FVector2D(Vertex.UVs[3]);P->Colors[I]=FLinearColor(Vertex.Color);
            auto& Skin=P->Skin[I];Skin.Bones.Reset();Skin.Weights.Reset();float Total=0;
            for(int32 J=0;J<MAX_TOTAL_INFLUENCES;++J) if(Vertex.InfluenceWeights[J])
            {Skin.Bones.Add(Section.BoneMap[Vertex.InfluenceBones[J]]);Skin.Weights.Add(Vertex.InfluenceWeights[J]);Total+=Vertex.InfluenceWeights[J];}
            if(Total<=0) return TEXT("Unbound native vertex");for(auto& W:Skin.Weights) W/=Total;
        }
    }
    P->NormalParents.SetNum(LOD.NumVertices);TMap<FString,int32> NormalGroups;
    for(int32 V=0;V<int32(LOD.NumVertices);++V)
    {
        const auto X=P->Positions[V],N=P->Normals[V];
        const FString Key=FString::Printf(TEXT("%lld,%lld,%lld/%lld,%lld,%lld"),
            FMath::RoundToInt64(X.X*10000),FMath::RoundToInt64(X.Y*10000),FMath::RoundToInt64(X.Z*10000),
            FMath::RoundToInt64(N.X*1000),FMath::RoundToInt64(N.Y*1000),FMath::RoundToInt64(N.Z*1000));
        if(const int32* Found=NormalGroups.Find(Key)) P->NormalParents[V]=*Found;
        else {NormalGroups.Add(Key,V);P->NormalParents[V]=V;}
    }
    for(const auto& Parameter:Definition->Parameters)
    {
        FVamCollisionMorph M;M.Parameter=Parameter.Target;M.Baseline=Definition->ShapeConvention==TEXT("neutral_plus_parameters")?0:Parameter.DefaultValue;
        if(Parameter.Group==TEXT("Expression")) P->ExpressionMorphs.Add(M.Parameter);
        if(const auto* Target=Mesh->FindMorphTarget(M.Parameter)) for(const auto& D:Target->GetMorphTargetDeltas(0))
        {FVamCollisionPointDelta Delta;Delta.Point=D.SourceIdx;Delta.Delta=FVector(D.PositionDelta);if(!P->Positions.IsValidIndex(Delta.Point)) return TEXT("Morph mapping mismatch");M.Deltas.Add(Delta);}
        P->Morphs.Add(MoveTemp(M));
    }
    TArray<FTransform> CS=Ref.GetRefBonePose();for(int32 I=0;I<CS.Num();++I) if(Ref.GetParentIndex(I)>=0) CS[I]*=CS[Ref.GetParentIndex(I)];
    P->Regions=Regions;P->RestParticles.Reset();P->ParticleSkin.Reset();P->Supports.Reset();P->ShapeSourceVertices.Reset();P->ParticleRegions.Reset();P->Tetrahedra.Reset();P->BoundaryTriangles.Reset();
    P->SurfaceMask.Init(0,LOD.NumVertices);P->SurfaceRegions.Init(INDEX_NONE,LOD.NumVertices);P->SurfaceParents.SetNum(LOD.NumVertices);P->SurfaceWeights.SetNum(LOD.NumVertices);
    TSet<FName> Names;
    for(int32 R=0;R<Regions.Num();++R)
    {
        const auto& Region=Regions[R];const int32 Bone=Ref.FindBoneIndex(Region.Bone),C=Region.Cells;
        if(Bone<0 || Region.Name.IsNone() || Names.Contains(Region.Name) || C<2 || C>8 || Region.FullSkinWeight<=Region.MinimumSkinWeight ||
           Region.SupportFraction<=0 || Region.SupportFraction>=1 || Region.DensityKgPerCm3<=0 || Region.Stiffness<=0 || Region.Damping<0 || Region.Incompressibility<0 || Region.Incompressibility>=.5)
            return TEXT("Invalid or duplicate anatomy region configuration");
        Names.Add(Region.Name);FBox Bounds(ForceInit);TArray<float> Masks;Masks.Init(0,LOD.NumVertices);TArray<int32> Candidates;
        for(int32 V=0;V<P->Skin.Num();++V)
        {
            const int32 J=P->Skin[V].Bones.Find(Bone);const float Weight=J>=0?P->Skin[V].Weights[J]:0;
            Masks[V]=FMath::Clamp((Weight-Region.MinimumSkinWeight)/(Region.FullSkinWeight-Region.MinimumSkinWeight),0.f,1.f);
            if(Masks[V]>0) {Candidates.Add(V);Bounds+=CS[Bone].InverseTransformPosition(P->Positions[V]);}
        }
        if(Candidates.Num()<12 || !Bounds.IsValid || Bounds.GetSize().GetMin()<.5) return TEXT("Insufficient geometry for region ")+Region.Name.ToString();
        Bounds=Bounds.ExpandBy(Bounds.GetSize()*.02);const int32 Start=P->RestParticles.Num(),TetStart=P->Tetrahedra.Num();
        auto Id=[C,Start](int32 X,int32 Y,int32 Z){return Start+X+(C+1)*(Y+(C+1)*Z);};
        const FVector Parent=CS[FMath::Max(0,Ref.GetParentIndex(Bone))].GetLocation();
        const FVector Axis=(Bounds.GetCenter()-CS[Bone].InverseTransformPosition(Parent)).GetSafeNormal();
        double Min=TNumericLimits<double>::Max(),Max=-Min;
        for(int32 Z=0;Z<=C;++Z) for(int32 Y=0;Y<=C;++Y) for(int32 X=0;X<=C;++X)
        {
            const FVector Local=Bounds.Min+Bounds.GetSize()*FVector(double(X)/C,double(Y)/C,double(Z)/C);const FVector Position=CS[Bone].TransformPosition(Local);
            P->RestParticles.Add(Position);P->ParticleRegions.Add(R);const double S=FVector::DotProduct(Local,Axis);Min=FMath::Min(Min,S);Max=FMath::Max(Max,S);
            int32 Nearest=INDEX_NONE;double Distance=TNumericLimits<double>::Max();
            for(int32 V:Candidates) {const double D=FVector::DistSquared(Position,P->Positions[V]);if(D<Distance){Distance=D;Nearest=V;}}
            P->ShapeSourceVertices.Add(Nearest);P->ParticleSkin.Add(P->Skin[Nearest]);P->Supports.Add(false);
        }
        for(int32 I=Start;I<P->RestParticles.Num();++I) P->Supports[I]=FVector::DotProduct(CS[Bone].InverseTransformPosition(P->RestParticles[I]),Axis)<=Min+(Max-Min)*Region.SupportFraction;
        const int32 Orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for(int32 Z=0;Z<C;++Z) for(int32 Y=0;Y<C;++Y) for(int32 X=0;X<C;++X) for(const auto& Order:Orders)
        {
            FIntVector Corner(X,Y,Z);FIntVector4 T;T[0]=Id(X,Y,Z);
            for(int32 J=0;J<3;++J){++Corner[Order[J]];T[J+1]=Id(Corner.X,Corner.Y,Corner.Z);}
            const double D=Det(P->RestParticles[T[1]]-P->RestParticles[T[0]],P->RestParticles[T[2]]-P->RestParticles[T[0]],P->RestParticles[T[3]]-P->RestParticles[T[0]]);
            if(D<0) Swap(T[1],T[2]);P->Tetrahedra.Add(T);
        }
        for(int32 V:Candidates) if(Masks[V]>P->SurfaceMask[V])
        {
            bool Found=false;for(int32 I=TetStart;I<P->Tetrahedra.Num();++I)
            {
                const auto& T=P->Tetrahedra[I];const auto W=Bary(P->Positions[V],T,P->RestParticles);
                if(FMath::Min(FMath::Min(W.X,W.Y),FMath::Min(W.Z,W.W))>=-1.e-5)
                {P->SurfaceParents[V]=T;P->SurfaceWeights[V]=W;P->SurfaceMask[V]=Masks[V];P->SurfaceRegions[V]=R;Found=true;break;}
            }
            if(!Found) return TEXT("Skin escaped closed cage");
        }
    }
    TMap<FIntVector,FIntVector> Boundary;TMap<FIntVector,int32> Counts;
    for(const auto& T:P->Tetrahedra)
    {
        const FIntVector Faces[4]={{T[0],T[2],T[1]},{T[0],T[1],T[3]},{T[0],T[3],T[2]},{T[1],T[2],T[3]}};
        for(const auto& Face:Faces) {FIntVector Key=Face;if(Key.X>Key.Y) Swap(Key.X,Key.Y);if(Key.Y>Key.Z) Swap(Key.Y,Key.Z);if(Key.X>Key.Y) Swap(Key.X,Key.Y);++Counts.FindOrAdd(Key);Boundary.Add(Key,Face);}
    }
    for(const auto& Item:Counts) if(Item.Value==1) P->BoundaryTriangles.Add(Boundary[Item.Key]);
    P->Body=Mesh;P->BindSignature=Definition->BindSignature;P->MorphSetLockDigest=Shape->MorphSetLockDigest;P->MarkPackageDirty();return FString();
}
