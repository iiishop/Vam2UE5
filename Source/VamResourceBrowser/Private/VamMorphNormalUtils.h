#pragma once
#include "CoreMinimal.h"

namespace VamMorphNormals
{
// MeshDescription preserves authored normals; its triangle winding is not an
// outward-normal convention. Orient BOTH areas against the unchanged authored
// basis before subtracting, otherwise clockwise meshes get the inverse delta.
inline TArray<FVector> Deltas(const TArray<FVector>& Positions,const TArray<FVector>& Normals,
    const TArray<int32>& Triangles,const TArray<FVector>& Displacements)
{
    TArray<FVector> Before,After,Result;
    Before.Init(FVector::ZeroVector,Positions.Num());After=Before;Result=Before;
    for(int32 T=0;T<Triangles.Num();T+=3)
    {
        const int32 A=Triangles[T],B=Triangles[T+1],C=Triangles[T+2];
        FVector N0=FVector::CrossProduct(Positions[B]-Positions[A],Positions[C]-Positions[A]);
        FVector N1=FVector::CrossProduct(Positions[B]+Displacements[B]-Positions[A]-Displacements[A],
            Positions[C]+Displacements[C]-Positions[A]-Displacements[A]);
        if(FVector::DotProduct(N0,Normals[A]+Normals[B]+Normals[C])<0){N0=-N0;N1=-N1;}
        for(int32 V:{A,B,C}){Before[V]+=N0;After[V]+=N1;}
    }
    for(int32 V=0;V<Positions.Num();++V)
        if(!Before[V].IsNearlyZero() && !After[V].IsNearlyZero())Result[V]=After[V].GetSafeNormal()-Before[V].GetSafeNormal();
    return Result;
}
}
