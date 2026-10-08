#include "VamBreastContactBuilder.h"
#include "VamBreastContactProfile.h"
#include "OptimusDeformer.h"
#include "OptimusNodeGraph.h"
#include "OptimusNode.h"
#include "OptimusNodePin.h"
#include "OptimusComputeDataInterface.h"
#include "IOptimusShaderTextProvider.h"
#include "IOptimusNodeAdderPinProvider.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

FString UVamBreastContactBuilder::BuildDeformer(UVamBreastContactProfile* Profile,const FString& AssetPath)
{
    if(!Profile || FPackageName::DoesPackageExist(AssetPath) || FindPackage(nullptr,*AssetPath)) return TEXT("Contact deformer destination must be new");
    auto* Template=LoadObject<UOptimusDeformer>(nullptr,TEXT("/ChaosFlesh/Deformers/DG_FleshDeformer.DG_FleshDeformer"));
    if(!Template) return TEXT("UE Chaos Flesh GPU template is unavailable");
    auto* Deformer=DuplicateObject<UOptimusDeformer>(Template,CreatePackage(*AssetPath),*FPackageName::GetLongPackageAssetName(AssetPath));
    int32 Patched=0, NormalKernels=0;
    for(auto* Graph:Deformer->GetGraphs())
    {
        const auto Nodes=Graph->GetAllNodes();
        for(auto* Node:Nodes) if(auto* Shader=Cast<IOptimusShaderTextProvider>(Node))
        {
            const FString Text=Shader->GetShaderText().Replace(TEXT("Index > ReadNumThreads().x"),TEXT("Index >= ReadNumThreads().x"));
            Shader->SetShaderText(Text);
            if(Text.Contains(TEXT("float Weight = 1;//Corners[CornerId].Angle;")))
            {
                // Corner-angle weighting removes dependence on triangle tessellation density.
                // Keep duplicate-vertex normal accumulation and UV-separated tangents intact.
                FString Smooth=Text.Replace(TEXT("float Weight = 1;//Corners[CornerId].Angle;"),
                    TEXT("float Weight = Corners[CornerId].Angle;"));
                Smooth=Smooth.Replace(TEXT("Corners[CornerId].Angle = AngleBetweenVectorsFast(EdgeA, EdgeB);"),
                    TEXT("Corners[CornerId].Angle = atan2(length(cross(EdgeA, EdgeB)), dot(EdgeA, EdgeB));"));
                Smooth=Smooth.Replace(TEXT("float3 TangentZ = normalize(TriangleNormal);"),
                    TEXT("float3 TangentZ = TriangleNormal * rsqrt(max(dot(TriangleNormal, TriangleNormal), 1e-20));"));
                Smooth=Smooth.Replace(TEXT("/ CP;"), TEXT("/ (abs(CP) < 0.000001f ? 1.0f : CP);"));
                // Degenerate faces must contribute zero, not a NaN or arbitrary normal.
                Smooth=Smooth.Replace(TEXT("float Weight = Corners[CornerId].Angle;"),
                    TEXT("float Weight = dot(TriangleData.TangentZ, TriangleData.TangentZ) > 0.5f ? Corners[CornerId].Angle : 0.0f;"));
                Smooth=Smooth.Replace(TEXT("Out.TangentX = normalize(UnnormalizedTangentX);"),
                    TEXT("Out.TangentX = UnnormalizedTangentX * rsqrt(max(dot(UnnormalizedTangentX, UnnormalizedTangentX), 1e-20));"));
                Shader->SetShaderText(Smooth);
                ++NormalKernels;
            }
            if(Text.Contains(TEXT("float BlendFactor = saturate(dot(VertexColor, BlendMask));")))
                Shader->SetShaderText(Text.Replace(TEXT("float BlendFactor = saturate(dot(VertexColor, BlendMask));"),TEXT("float BlendFactor = 1.0;")));
            if(!Text.Contains(TEXT("ReadEmbeddedPos(Index)"))) continue;
            auto* MorphClass=LoadClass<UOptimusComputeDataInterface>(nullptr,TEXT("/Script/OptimusCore.OptimusMorphTargetDataInterface"));
            if(!MorphClass) return TEXT("Native Morph data interface unavailable");
            auto* Morph=Graph->AddDataInterfaceNode(MorphClass,FVector2D(-600,400));
            auto* Adder=Cast<IOptimusNodeAdderPinProvider>(Node);
            if(!Morph || !Adder) return TEXT("Contact graph cannot add native Morph input");
            UOptimusNodePin* ComponentOutput=nullptr;
            for(auto* Candidate:Nodes) if(Candidate->GetClass()->GetName()==TEXT("OptimusNode_ComponentSource"))
                for(auto* Pin:Candidate->GetPins()) if(Pin->GetDirection()==EOptimusNodePinDirection::Output) ComponentOutput=Pin;
            if(!ComponentOutput) return TEXT("Contact graph component binding absent");
            bool Connected=false;
            for(auto* Pin:Morph->GetPins()) if(Pin->GetDirection()==EOptimusNodePinDirection::Input)
                Connected|=Graph->AddLink(ComponentOutput,Pin);
            if(!Connected) return TEXT("Contact Morph component binding rejected");
            for(const FName Name:{FName(TEXT("DeltaPosition")),FName(TEXT("DeltaNormal"))})
            {
                auto* Source=Morph->FindPin(Name.ToString());if(!Source) return TEXT("Contact Morph pin absent");
                const auto Actions=Adder->GetAvailableAdderPinActions(Source,EOptimusNodePinDirection::Input);
                if(Actions.IsEmpty()) return TEXT("Contact kernel Morph input rejected");
                auto Pins=Adder->TryAddPinFromPin(Actions[0],Source,Name);
                if(Pins.IsEmpty() || !Graph->AddLink(Source,Pins.Last())) return TEXT("Contact Morph kernel connection failed");
            }
            // The producer publishes reference cage + contact-minus-animated-rest residual.
            // Therefore subtract the static reference position BEFORE adding to the fully
            // morphed/skinned position. This is not an absolute-position blend.
            Shader->SetShaderText(TEXT(
                "if (Index >= ReadNumThreads().x) return;\n"
                "float3x4 B = ReadWeightedBoneMatrix(Index);\n"
                "float3 P0 = ReadPosition(Index);\n"
                "float3 P = mul(B,float4(P0+ReadDeltaPosition(Index),1));\n"
                "float Mask = ReadMask(Index);\n"
                "if (Mask > 0) P += Mask*(ReadEmbeddedPos(Index)-P0);\n"
                "float4 Tx=ReadTangentX(Index), Tz=ReadTangentZ(Index);\n"
                "WriteOutPosition(Index,P);\n"
                "WriteOutTangentX(Index,float4(normalize(mul((float3x3)B,Tx.xyz)),Tx.w));\n"
                "WriteOutTangentZ(Index,float4(normalize(mul((float3x3)B,Tz.xyz+ReadDeltaNormal(Index))),Tz.w));\n"));
            if(Profile->bResidualOnlySurface)
                Shader->SetShaderText(Shader->GetShaderText().Replace(TEXT("ReadEmbeddedPos(Index)-P0"),TEXT("ReadEmbeddedPos(Index)")));
            ++Patched;
        }
        if(Profile->bResidualOnlySurface)
        {
            UOptimusNode* SkinNode=nullptr;UOptimusNode* TriangleNode=nullptr;UOptimusNode* ResolveNode=nullptr;
            for(auto* N:Nodes)if(auto* S=Cast<IOptimusShaderTextProvider>(N)){
                const FString Code=S->GetShaderText();
                if(Code.Contains(TEXT("ReadEmbeddedPos(Index)")))SkinNode=N;
                if(Code.Contains(TEXT("AtomicAddAccumulation")))TriangleNode=N;
                if(Code.Contains(TEXT("WriteOutAccumulation")))ResolveNode=N;
            }
            if(!SkinNode && !TriangleNode && !ResolveNode)continue;
            if(!SkinNode || !TriangleNode || !ResolveNode)return TEXT("Normal transport graph kernels absent");
            auto FindLeaf=[](UOptimusNode* N,FName Name)->UOptimusNodePin*{
                for(auto* Root:N->GetPins())for(auto* Pin:Root->GetSubPinsRecursively(true))if(Pin->GetPinNamePath().Last()==Name)return Pin;
                return nullptr;};
            auto* Adder=Cast<IOptimusNodeAdderPinProvider>(TriangleNode);if(!Adder)return TEXT("Normal transport input unavailable");
            for(const TCHAR* Name:{TEXT("Position"),TEXT("WeightedBoneMatrix"),TEXT("DeltaPosition"),TEXT("OutTangentX")}){
                auto* Pin=FindLeaf(SkinNode,FName(Name));if(!Pin)return TEXT("Normal transport source pin missing");
                auto* Source=Pin;if(Pin->GetDirection()==EOptimusNodePinDirection::Input){const auto Links=Pin->GetConnectedPins();if(Links.Num()!=1)return TEXT("Normal transport source ambiguity");Source=Links[0];}
                const auto Actions=Adder->GetAvailableAdderPinActions(Source,EOptimusNodePinDirection::Input);if(Actions.IsEmpty())return TEXT("Normal transport input rejected");
                const FName NewName(*(FString(TEXT("Base"))+Name));auto Pins=Adder->TryAddPinFromPin(Actions[0],Source,NewName);
                if(Pins.IsEmpty() || !Graph->AddLink(Source,Pins.Last()))return TEXT("Normal transport connection failed");
            }
            // Transport the authored smooth basis by the contact deformation gradient.
            // At zero contact this is the identity, independent of triangulation.
            Cast<IOptimusShaderTextProvider>(TriangleNode)->SetShaderText(TEXT(R"HLSL(
float3 BasePosition(uint V)
{
    return mul(ReadBaseWeightedBoneMatrix(V),float4(ReadBasePosition(V)+ReadBaseDeltaPosition(V),1));
}
KERNEL
{
    if(Index>=ReadNumThreads().x)return;
    uint V[3];float3 P[3],B[3];
    for(uint J=0;J<3;++J){V[J]=ReadIndexBuffer(Index*3+J);P[J]=ReadPosition(V[J]);B[J]=BasePosition(V[J]);}
    float3 E1=B[1]-B[0],E2=B[2]-B[0],D1=P[1]-P[0],D2=P[2]-P[0];
    float3 NB=cross(E1,E2),ND=cross(D1,D2);
    float LB=length(NB),LD=length(ND);bool Valid=LB>1e-8 && LD>1e-8;
    NB*=rcp(max(LB,1e-8));ND*=rcp(max(LD,1e-8));
    for(uint J=0;J<3;++J)
    {
        float4 SourceN=ReadTangentZ(V[J]);float3 N=SourceN.xyz,Tx=ReadBaseOutTangentX(V[J]).xyz;
        if(Valid){
            // F^-T n: the normal extension maps the base unit normal to the deformed unit normal.
            N=normalize(cross(D2,ND)*dot(E1,N)+cross(ND,D1)*dot(E2,N)+cross(D1,D2)*dot(NB,N));
            Tx=(D1*dot(cross(E2,NB),Tx)+D2*dot(cross(NB,E1),Tx)+ND*dot(cross(E1,E2),Tx))/LB;
            Tx=normalize(Tx-N*dot(Tx,N));
        }
        float3 EA=B[(J+1)%3]-B[J],EB=B[(J+2)%3]-B[J];
        float W=Valid?atan2(length(cross(EA,EB)),dot(EA,EB)):1;
        int3 IN=int3(N*(W*32767)),IT=int3(Tx*(W*32767));uint K=V[J]*8;
        AtomicAddAccumulation(K,IN.x);AtomicAddAccumulation(K+1,IN.y);AtomicAddAccumulation(K+2,IN.z);
        AtomicAddAccumulation(K+3,IT.x);AtomicAddAccumulation(K+4,IT.y);AtomicAddAccumulation(K+5,IT.z);
        AtomicAddAccumulation(K+6,int(SourceN.w*W*32767));
        uint2 Duplicates=ReadDuplicateVerticesStartAndLength(V[J]);
        for(uint I=0;I<Duplicates.y;++I){uint Q=ReadDuplicateVertex(Duplicates.x+I)*8;
            AtomicAddAccumulation(Q,IN.x);AtomicAddAccumulation(Q+1,IN.y);AtomicAddAccumulation(Q+2,IN.z);}
    }
}
)HLSL"));
            Cast<IOptimusShaderTextProvider>(ResolveNode)->SetShaderText(TEXT(R"HLSL(
KERNEL
{
    if(Index>=ReadNumThreads().x)return;
    uint K=Index*8;float4 OriginalN=ReadOriginalTangentZ(Index),OriginalT=ReadOriginalTangentX(Index);
    float3 N=float3(ReadAccumulation(K),ReadAccumulation(K+1),ReadAccumulation(K+2));
    float3 T=float3(ReadAccumulation(K+3),ReadAccumulation(K+4),ReadAccumulation(K+5));
    N=dot(N,N)>1e-12?normalize(N):OriginalN.xyz;
    T-=N*dot(T,N);if(dot(T,T)<1e-12)T=OriginalT.xyz-N*dot(OriginalT.xyz,N);
    T=normalize(T);int Orientation=ReadAccumulation(K+6);
    WriteOutPosition(Index,ReadPosition(Index));WriteOutTangentX(Index,float4(T,OriginalT.w));
    WriteOutTangentZ(Index,float4(N,Orientation==0?OriginalN.w:(Orientation<0?-1:1)));
    for(uint I=0;I<7;++I)WriteOutAccumulation(K+I,0);
}
)HLSL"));
        }
    }
    if(Patched!=1 || NormalKernels!=1 || !Deformer->Compile()) return TEXT("Contact GPU graph compilation failed");
    Deformer->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Deformer);Deformer->MarkPackageDirty();
    Profile->SurfaceDeformer=Deformer;Profile->MarkPackageDirty();return FString();
}

FString UVamBreastContactBuilder::InspectDeformer(UVamBreastContactProfile* Profile)
{
    const auto* D=Profile?Cast<UOptimusDeformer>(Profile->SurfaceDeformer):nullptr;if(!D)return TEXT("No deformer");FString Out;
    for(auto* G:D->GetGraphs())for(auto* N:G->GetAllNodes())if(auto* S=Cast<IOptimusShaderTextProvider>(N))
    {
        Out+=TEXT("\nNODE ")+N->GetName()+TEXT("\n")+S->GetShaderText()+TEXT("\n");
        for(auto* Root:N->GetPins())for(auto* Pin:Root->GetSubPinsRecursively(true)){
            Out+=TEXT("PIN ")+Pin->GetPinPath()+TEXT("\n");
            for(auto* Peer:Pin->GetConnectedPins())Out+=TEXT("  LINK ")+Peer->GetPinPath()+TEXT("\n");}
    }
    return Out;
}
