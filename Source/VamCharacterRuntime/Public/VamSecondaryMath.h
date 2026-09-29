#pragma once
#include "CoreMinimal.h"
namespace VamSecondaryMath
{
// Tiny dense solve for the five-node implicit network (15 unknowns), with pivoting.
inline bool Solve(double A[15][15],double B[15],double X[15],int32 Count)
{
    for(int32 K=0;K<Count;++K)
    {
        int32 Pivot=K;for(int32 I=K+1;I<Count;++I) if(FMath::Abs(A[I][K])>FMath::Abs(A[Pivot][K])) Pivot=I;
        if(!FMath::IsFinite(A[Pivot][K]) || FMath::Abs(A[Pivot][K])<1.e-12) return false;
        if(Pivot!=K) { for(int32 J=K;J<Count;++J) Swap(A[K][J],A[Pivot][J]);Swap(B[K],B[Pivot]); }
        for(int32 I=K+1;I<Count;++I)
        {
            const double F=A[I][K]/A[K][K];
            for(int32 J=K;J<Count;++J) A[I][J]-=F*A[K][J];B[I]-=F*B[K];
        }
    }
    for(int32 I=Count-1;I>=0;--I) { double V=B[I];for(int32 J=I+1;J<Count;++J) V-=A[I][J]*X[J];X[I]=V/A[I][I];if(!FMath::IsFinite(X[I])) return false; }
    return true;
}
}
