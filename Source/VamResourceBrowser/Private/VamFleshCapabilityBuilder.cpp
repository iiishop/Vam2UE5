#include "VamFleshCapabilityBuilder.h"
#include "VamFleshCapabilityAsset.h"
#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ChaosFlesh/FleshAsset.h"
#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/FleshCollectionEngineUtility.h"
#include "GeometryCollection/Facades/CollectionTetrahedralBindingsFacade.h"
#include "OptimusDeformer.h"
#include "OptimusNodeGraph.h"
#include "OptimusNode.h"
#include "OptimusNodePin.h"
#include "IOptimusShaderTextProvider.h"
#include "UObject/UnrealType.h"

FString UVamFleshCapabilityBuilder::DescribeSurfaceGraph(UMeshDeformer* Deformer)
{
    const auto* Graph=Cast<UOptimusDeformer>(Deformer);
    if(!Graph) return TEXT("Not an Optimus graph");
    FString Description;
    for(const auto* Update:Graph->GetGraphs()) for(const auto* Node:Update->GetAllNodes())
    {
        Description+=Update->GetName()+TEXT("/")+Node->GetName()+TEXT(" [")+Node->GetClass()->GetName()+TEXT("]\n");
        if(const auto* Property=FindFProperty<FObjectPropertyBase>(Node->GetClass(),TEXT("DataInterfaceData")))
            if(const auto* Interface=Property->GetObjectPropertyValue_InContainer(Node))
                Description+=TEXT("DataInterface: ")+Interface->GetClass()->GetName()+TEXT("\n");
        for(const auto* Pin:Node->GetPinsByDirection(EOptimusNodePinDirection::Input,true))
            for(const auto* Source:Pin->GetConnectedPins())
                Description+=Source->GetPinPath()+TEXT(" -> ")+Pin->GetPinPath()+TEXT("\n");
        if(const auto* Shader=Cast<IOptimusShaderTextProvider>(Node))
            Description+=Shader->GetDeclarations()+TEXT("\n")+Shader->GetShaderText()+TEXT("\n");
    }
    return Description;
}

namespace
{
double Determinant(const FVector& A,const FVector& B,const FVector& C) { return FVector::DotProduct(A,FVector::CrossProduct(B,C)); }
FVector4f Coordinates(const FVector& P,const FIntVector4& T,const TArray<FVector>& X)
{
    const FVector A=X[T[0]], B=X[T[1]]-A,C=X[T[2]]-A,D=X[T[3]]-A,Q=P-A;
    const double Den=Determinant(B,C,D);
    const double Y=Determinant(Q,C,D)/Den,Z=Determinant(B,Q,D)/Den,W=Determinant(B,C,Q)/Den;
    return FVector4f(1-Y-Z-W,Y,Z,W);
}
}
FString UVamFleshCapabilityBuilder::Build(UVamFleshCapabilityAsset* Target,UVamCharacterDefinition* Definition,FName SourceBone,int32 Cells)
{
    if(!Target || !Definition || Cells<2 || Cells>8) return TEXT("Invalid explicit capability inputs");
    USkeletalMesh* Body=Definition->Body.LoadSynchronous();
    if(!Body || !Body->GetImportedModel() || Body->GetImportedModel()->LODModels.IsEmpty()) return TEXT("Native source LOD0 unavailable");
    const FReferenceSkeleton& Ref=Body->GetRefSkeleton();const int32 Bone=Ref.FindBoneIndex(SourceBone);
    if(Bone<0) return TEXT("Source bone absent");
    TArray<FTransform> CS=Ref.GetRefBonePose();for(int32 I=0;I<CS.Num();++I) if(Ref.GetParentIndex(I)>=0) CS[I]*=CS[Ref.GetParentIndex(I)];
    const auto& LOD=Body->GetImportedModel()->LODModels[0];TArray<FVector> RenderPositions;TArray<float> Mask;
    RenderPositions.SetNum(LOD.NumVertices);Mask.Init(0,LOD.NumVertices);FBox LocalBounds(ForceInit);
    for(const auto& Section:LOD.Sections) for(int32 V=0;V<Section.SoftVertices.Num();++V)
    {
        const auto& Vertex=Section.SoftVertices[V];const int32 Index=Section.BaseVertexIndex+V;
        RenderPositions[Index]=FVector(Vertex.Position);float Weight=0;
        for(int32 J=0;J<MAX_TOTAL_INFLUENCES;++J)
        {
            if(!Vertex.InfluenceWeights[J]) continue;
            int32 B=Section.BoneMap[Vertex.InfluenceBones[J]];
            while(B>=0 && B!=Bone) B=Ref.GetParentIndex(B);
            if(B==Bone) Weight+=float(Vertex.InfluenceWeights[J])/65535.f;
        }
        Mask[Index]=FMath::Clamp((Weight-.05f)/.45f,0.f,1.f);
        if(Mask[Index]>0) LocalBounds+=CS[Bone].InverseTransformPosition(RenderPositions[Index]);
    }
    const auto* RenderData=Body->GetResourceForRendering();
    if(!RenderData || RenderData->LODRenderData.IsEmpty()) return TEXT("Render LOD0 unavailable for binding validation");
    const auto& PositionBuffer=RenderData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
    if(PositionBuffer.GetNumVertices()!=uint32(RenderPositions.Num())) return TEXT("Imported/render vertex counts differ; explicit remap required");
    for(int32 V=0;V<RenderPositions.Num();++V)
        if(!RenderPositions[V].Equals(FVector(PositionBuffer.VertexPosition(V)),1.e-4))
            return TEXT("Imported/render vertex order differs; explicit remap required");
    if(!LocalBounds.IsValid || LocalBounds.GetSize().GetMin()<.5) return TEXT("Bone weights do not define a usable volume region");
    LocalBounds=LocalBounds.ExpandBy(LocalBounds.GetSize()*.02);
    TArray<FVector> Vertices;TArray<FIntVector4> Tets;
    auto Id=[Cells](int32 X,int32 Y,int32 Z){return X+(Cells+1)*(Y+(Cells+1)*Z);};
    for(int32 Z=0;Z<=Cells;++Z) for(int32 Y=0;Y<=Cells;++Y) for(int32 X=0;X<=Cells;++X)
        Vertices.Add(CS[Bone].TransformPosition(LocalBounds.Min+LocalBounds.GetSize()*FVector(double(X)/Cells,double(Y)/Cells,double(Z)/Cells)));
    const int32 Permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    float Volume=0;
    for(int32 Z=0;Z<Cells;++Z) for(int32 Y=0;Y<Cells;++Y) for(int32 X=0;X<Cells;++X) for(const auto& Order:Permutations)
    {
        FIntVector P(X,Y,Z);FIntVector4 Tet;Tet[0]=Id(P.X,P.Y,P.Z);
        for(int32 J=0;J<3;++J){++P[Order[J]];Tet[J+1]=Id(P.X,P.Y,P.Z);}
        double D=Determinant(Vertices[Tet[1]]-Vertices[Tet[0]],Vertices[Tet[2]]-Vertices[Tet[0]],Vertices[Tet[3]]-Vertices[Tet[0]]);
        if(FMath::Abs(D)<1.e-6) return TEXT("Degenerate capability tetrahedron");
        if(D<0) Swap(Tet[1],Tet[2]);Tets.Add(Tet);Volume+=FMath::Abs(D)/6;
    }
    TArray<FIntVector4> Parents;TArray<FVector4f> Weights;TArray<FVector3f> Offsets;
    Parents.Init(Tets[0],RenderPositions.Num());Weights.Init(FVector4f(1,0,0,0),RenderPositions.Num());Offsets.Init(FVector3f::ZeroVector,RenderPositions.Num());int32 Bound=0;
    for(int32 V=0;V<Mask.Num();++V) if(Mask[V]>0)
    {
        bool Found=false;
        for(const auto& Tet:Tets)
        {
            const FVector4f W=Coordinates(RenderPositions[V],Tet,Vertices);
            if(FMath::Min(FMath::Min(W.X,W.Y),FMath::Min(W.Z,W.W))>=-1.e-5)
            {Parents[V]=Tet;Weights[V]=W;Found=true;++Bound;break;}
        }
        if(!Found) return TEXT("Render vertex escaped the closed volume cage");
    }
    if(Bound<12) return TEXT("Insufficient native skin vertices for capability experiment");
    TUniquePtr<FFleshCollection> Collection(FFleshCollection::NewFleshCollection(Vertices,Tets,false));
    Collection->Mass.Fill(0);
    for(const auto& Tet:Tets)
    {
        const float Mass=Target->DensityKgPerCm3*Determinant(Vertices[Tet[1]]-Vertices[Tet[0]],Vertices[Tet[2]]-Vertices[Tet[0]],Vertices[Tet[3]]-Vertices[Tet[0]])/24;
        for(int32 J=0;J<4;++J) Collection->Mass[Tet[J]]+=Mass;
    }
    // Capability-only fixed support: the quarter closest to the parent joint.
    // Production attachments must follow the animated skeleton and shaped rest.
    const FVector Parent=CS[FMath::Max(0,Ref.GetParentIndex(Bone))].GetLocation();
    const FVector Axis=(LocalBounds.GetCenter()-CS[Bone].InverseTransformPosition(Parent)).GetSafeNormal();
    double Min=TNumericLimits<double>::Max(),Max=-Min;
    for(const auto& V:Vertices){const double S=FVector::DotProduct(CS[Bone].InverseTransformPosition(V),Axis);Min=FMath::Min(Min,S);Max=FMath::Max(Max,S);}
    for(int32 I=0;I<Vertices.Num();++I) if(FVector::DotProduct(CS[Bone].InverseTransformPosition(Vertices[I]),Axis)<=Min+(Max-Min)*.25) Collection->Mass[I]=0;
    Collection->AddAttribute<float>(TEXT("Stiffness"),FGeometryCollection::VerticesGroup).Fill(100000.f);
    Collection->AddAttribute<float>(TEXT("Damping"),FGeometryCollection::VerticesGroup).Fill(.1f);
    Collection->AddAttribute<float>(TEXT("Incompressibility"),FGeometryCollection::VerticesGroup).Fill(.45f);
    Collection->AddAttribute<float>(TEXT("Inflation"),FGeometryCollection::VerticesGroup).Fill(1.f);
    GeometryCollection::Facades::FTetrahedralBindings Bindings(*Collection);Bindings.DefineSchema();
    // Match the runtime Flesh deformer, including its fallback for meshes without a primary asset ID.
    Bindings.AddBindingsGroup(0,FName(ChaosFlesh::GetMeshId(Body,false)),0);Bindings.SetBindingsData(Parents,Weights,Offsets,Mask);
    auto* Flesh=NewObject<UFleshAsset>(Target,TEXT("FleshRest"),RF_Public|RF_Transactional);
    Flesh->SetFleshCollection(MoveTemp(Collection));Flesh->TargetDeformationSkeleton=Body;Flesh->SkeletalMesh=Body;
    Target->Flesh=Flesh;Target->Body=Body;Target->SourceBone=SourceBone;Target->BindSignature=Definition->BindSignature;
    Target->RestVertices=MoveTemp(Vertices);Target->Tetrahedra=MoveTemp(Tets);Target->SurfaceParents=MoveTemp(Parents);Target->SurfaceWeights=MoveTemp(Weights);Target->SurfaceMask=MoveTemp(Mask);
    Target->RestVolumeCm3=Volume;Target->BoundRenderVertices=Bound;Target->MarkPackageDirty();return FString();
}
