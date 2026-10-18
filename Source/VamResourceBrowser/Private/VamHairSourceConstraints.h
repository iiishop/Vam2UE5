#pragma once

// Baked source data belongs to the immutable Groom cache, not a preset-specific
// runtime branch. Niagara executes the sparse links on GPU against the last
// published guide buffer; rod/contact projection still owns the final position.
static FString HairSourceConstraintHlsl(const TSharedPtr<FJsonObject>& Data, int32 Iterations, FString& Error)
{
    const auto& R=Data->GetArrayField(TEXT("rigidity"));
    const auto& L=Data->GetArrayField(TEXT("links"));
    if(R.IsEmpty()||R.Num()>128||L.Num()!=R.Num()*32){Error=TEXT("Invalid source constraint table size");return {};}
    FString Code=TEXT("const float SourceSupport[] = {");
    for(const auto& Row:R){if(Row->AsArray().Num()!=32){Error=TEXT("Invalid source node layout");return {};}
        for(const auto& V:Row->AsArray()){const double X=V->AsNumber();if(!FMath::IsFinite(X)||X<0||X>1){Error=TEXT("Invalid source support");return {};}Code+=FString::Printf(TEXT("%.8ff,"),X);}}
    Code.RemoveFromEnd(TEXT(","));Code+=TEXT("};\nconst float4 SourceLinks[] = {");
    for(const auto& Row:L)
    {
        if(Row->AsArray().Num()!=2){Error=TEXT("Invalid source adjacency");return {};}
        Code+=TEXT("float4(");
        for(const auto& V:Row->AsArray())
        {
            const auto& Pair=V->AsArray();if(Pair.Num()!=2){Error=TEXT("Invalid source edge");return {};}
            const int32 I=Pair[0]->AsNumber();const double D=Pair[1]->AsNumber();
            if(I<-1||I>=L.Num()||D<0||!FMath::IsFinite(D)){Error=TEXT("Invalid source edge range");return {};}
            Code+=FString::Printf(TEXT("%d.0,%.8ff,"),I,D);
        }
        Code.RemoveFromEnd(TEXT(","));Code+=TEXT("),");
    }
    Code.RemoveFromEnd(TEXT(","));Code+=TEXT("};\n");
    Code+=FString::Printf(TEXT("int SI=min(ExecIndex(),%d); int SolverIterations=%d;\n"),L.Num()-1,Iterations);
    Code+=FString::Printf(TEXT("float NearbyRate=%.9ff;\n"),Data->GetNumberField(TEXT("nearby_rate_per_second")));
    Code+=TEXT(R"HLSL(
float Support=SourceSupport[SI];
// Interpret painted rigidity as point support, not as a bend modulus.
// VaM executes point support on every internal iteration. Account for that
// repeated action when folding it into the native final projection stage.
float Keep=pow(max(0.0,1.0-Support),max(Dt,0.0)*60.0*max(1,SolverIterations));
Result=Rest+(Current-Rest)*Keep;
float4 Links=SourceLinks[SI];
float3 Correction=0; float Count=0;
for(int J=0;J<2;++J)
{
    int Other=int(J==0?Links.x:Links.z);
    float Length=J==0?Links.y:Links.w;
    if(Other>=0)
    {
        // Niagara stores 32 guide control points, but simulates 31 physical
        // nodes plus a virtual root. Convert explicitly between these layouts.
        int Curve=Other/32; float U=max(0,(Other%32)-1)/30.0;
        float P=U*31; int A=int(floor(P)), B=min(31,A+1);
        float3 PA,PB; Hair.GetPointPosition(Curve*32+A,PA); Hair.GetPointPosition(Curve*32+B,PB);
        float3 Delta=Result-lerp(PA,PB,P-A); float Distance=length(Delta);
        if(Distance>1.e-5){Correction-=Delta*((Distance-Length)/Distance);Count+=1;}
    }
}
// Lagged Jacobi links are deliberately compliant and bounded. They preserve
// authored bundles without introducing strand-strand collision or hard pins.
float Blend=1.0-exp(-NearbyRate*max(Dt,0.0));
Correction*=Blend/max(1.0,Count)*(1.0-Support);
Correction*=min(1.0,.25/max(length(Correction),1.e-6));
Result+=Correction;
)HLSL");
    return Code;
}
