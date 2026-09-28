#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "VamGluteCorrectiveProfile.h"
#include "VamRuntimeConfiguration.h"
#include "VamCharacterDefinition.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "VamGluteStructure.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteCorrectiveTest,"Vam.Glute.CorrectiveGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteCorrectiveTest::RunTest(const FString& Parameters)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("VamGluteTestConfig="),Path)) { AddError(TEXT("Pass -VamGluteTestConfig"));return false; }
    auto* Config=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Config persisted"),Config)) return false;
    auto* P=Config->GluteCorrective.LoadSynchronous();if(!TestNotNull(TEXT("Corrective profile persisted"),P)) return false;
    TestTrue(TEXT("Corrective profile valid"),P->IsValidProfile());
    auto* G=Config->GluteStructure.LoadSynchronous();auto* D=Config->Definition.LoadSynchronous();
    TestTrue(TEXT("Family and topology match structural profile"),P->SourceTopologyIdentity==G->SourceTopologyIdentity && P->SkeletonFamily==G->SkeletonFamily);
    FVamHipSidePose Hip;const auto Neutral=VamGluteCorrective::Weights(*P,Hip);TestEqual(TEXT("Neutral target exactly one"),Neutral[0],1.);
    for(int32 I=1;I<Neutral.Num();++I) TestEqual(TEXT("Neutral corrective exactly zero"),Neutral[I],0.);
    for(int32 T=0;T<P->Targets.Num();++T)
    {
        const FVector R=P->Targets[T].Degrees*(PI/180.);Hip.FlexionExtension=R.X;Hip.AbductionAdduction=R.Y;Hip.ExternalInternalRotation=R.Z;
        const auto W=VamGluteCorrective::Weights(*P,Hip);for(int32 J=0;J<W.Num();++J) TestEqual(TEXT("Cardinal target exact weight"),W[J],J==T?1.:0.);
    }
    TArray<double> Previous;
    for(int32 Step=0;Step<=1200;++Step)
    {
        const double T=Step*.005;Hip.FlexionExtension=.6+.9*FMath::Sin(T);Hip.AbductionAdduction=.5*FMath::Sin(2*T);Hip.ExternalInternalRotation=.5*FMath::Cos(T);
        const auto W=VamGluteCorrective::Weights(*P,Hip);double Sum=0;
        for(int32 I=0;I<W.Num();++I) { Sum+=W[I];TestTrue(TEXT("Finite nonnegative weights"),FMath::IsFinite(W[I]) && W[I]>=0 && W[I]<=1);if(Step) TestTrue(TEXT("Combined pose interpolation continuous"),FMath::Abs(W[I]-Previous[I])<.08); }
        TestTrue(TEXT("Partition of unity"),FMath::Abs(Sum-1)<1.e-12);
        for(int32 FPS:{30,60,120}) { TArray<double> Repeat;for(int32 Frame=0;Frame<FPS;++Frame) Repeat=VamGluteCorrective::Weights(*P,Hip);TestTrue(TEXT("Exact frame count independence"),Repeat==W); }
        FVamHipSidePose Mirror=Hip;Mirror.Side=TEXT("R");TestTrue(TEXT("Mirrored semantic input same pose weights"),VamGluteCorrective::Weights(*P,Mirror)==W);Previous=W;
    }
    TestTrue(TEXT("Anisotropic Shape adapts amplitude"),VamGluteCorrective::ShapeScale(FVector(10,20,30),FVector(20,30,15)).Equals(FVector(2,1.5,.5),1.e-12));
    auto MirrorSide=G->Sides[0];MirrorSide.SideSign*=-1;
    for(auto& R:MirrorSide.Regions) R.Rest.Y*=-1;
    MirrorSide.FoldSemanticMap.MedialInfraglutealAnchor.Y*=-1;MirrorSide.FoldSemanticMap.LateralFade.Y*=-1;
    FVamGluteFoldState Fold;Fold.MiddleTransitionFactor=.4;Fold.LateralFadeFactor=.2;
    for(const auto& Target:P->Targets) for(int32 Sample=0;Sample<30;++Sample)
    {
        FVector Point=G->Sides[0].Regions[2].Rest+G->Sides[0].Dimensions*FVector(.1,Sample/30.-.5,Sample/50.-.3),Mirrored=Point;Mirrored.Y*=-1;
        FVector Delta=VamGluteCorrective::CurvatureDelta(G->Sides[0],Point,Target.Degrees,Fold);Delta.Y*=-1;
        TestTrue(TEXT("Canonical geometry mirrors in anatomical frame"),Delta.Equals(VamGluteCorrective::CurvatureDelta(MirrorSide,Mirrored,Target.Degrees,Fold),1.e-10));
    }
    FVamNativeMeshInput Mesh;FString Error;if(!TestTrue(TEXT("Extract final native geometry"),UVamBreastJiggleBuilder::ExtractNative(D->Body.LoadSynchronous(),Mesh,Error))) return false;
    double Maximum=0;int32 DeltaVertices=0;
    for(const auto& B:P->Bases)
    {
        const auto* M=Mesh.Morphs.FindByPredicate([&](const FVamBuildMorph& V){return V.Name==B.Morph;});if(!TestNotNull(TEXT("Native corrective Morph persisted"),M)) continue;
        bool Finite=true,Bounded=true,Edges=true;double Max=0;
        for(const FVector& V:M->Deltas) { Finite&=!V.ContainsNaN();Max=FMath::Max(Max,V.Size());if(V.Size()>1.e-5) ++DeltaVertices; }
        Bounded=Max<P->BuildDimensions[B.Side].GetMax()*.4;Maximum=FMath::Max(Maximum,Max);
        for(int32 T=0;T<Mesh.Triangles.Num();T+=3) for(int32 K=0;K<3;++K)
        {
            const int32 A=Mesh.Triangles[T+K],C=Mesh.Triangles[T+(K+1)%3];
            Edges&=(M->Deltas[A]-M->Deltas[C]).Size()<FMath::Max(.15,(Mesh.Vertices[A]-Mesh.Vertices[C]).Size()*3);
        }
        TestTrue(TEXT("Corrective finite"),Finite);TestTrue(TEXT("Corrective displacement bounded"),Bounded);TestTrue(TEXT("No local corrective edge spikes"),Edges);
        TestTrue(TEXT("No Shape parameter ownership collision"),!D->Parameters.ContainsByPredicate([&](const FVamMorphParameter& Param){return Param.Target==B.Morph;}));
    }
    // Compare the final skinned surface with and without the COMPLETE blended
    // corrective. CPU skinning here is engineering validation only, never Runtime.
    TArray<FTransform> Bind;for(const auto& B:Mesh.Bones) Bind.Add(B.Parent<0?B.LocalBind:B.LocalBind*Bind[B.Parent]);
    TArray<TArray<FVamBuildInfluence>> Influence;Influence.SetNum(Mesh.Vertices.Num());for(const auto& W:Mesh.Influences) Influence[W.Vertex].Add(W);
    for(int32 Side=0;Side<2;++Side) for(int32 Step=0;Step<18;++Step)
    {
        const auto& S=G->Sides[Side];const FTransform Anchor=S.AnchorLocal*Bind[S.PelvisBone];
        FVector Degrees=Step<P->Targets.Num()?P->Targets[Step].Degrees:FVector((Step-10)*12,20*FMath::Sin(double(Step)),20*FMath::Cos(double(Step)));
        FVector Swing(S.SideSign*Degrees.Y,Degrees.X,0);Swing*=PI/180.;const FVector Axis=S.FemurAxisInAnchor;Swing.Z=-(Swing.X*Axis.X+Swing.Y*Axis.Y)/Axis.Z;
        const double L=Swing.Size();auto Thigh=S.RestThighInAnchor;Thigh.SetRotation((L>1.e-12?FQuat(Swing/L,L):FQuat::Identity)*FQuat(Axis,S.SideSign*FMath::DegreesToRadians(Degrees.Z))*Thigh.GetRotation());
        const auto State=VamGluteStructure::Evaluate(*G,S,Thigh);const auto Weights=VamGluteCorrective::Weights(*P,State.HipPose);
        auto Pose=Bind;Pose[S.ThighBone]=Thigh*Anchor;
        for(int32 B=S.ThighBone+1;B<Mesh.Bones.Num();++B) if(Mesh.Bones[B].Parent>=0) Pose[B]=Mesh.Bones[B].LocalBind*Pose[Mesh.Bones[B].Parent];
        for(int32 N=0;N<5;++N) Pose[S.Regions[N].BoneIndex]=State.Regions[N].Transform*Anchor;
        TArray<FVector> Delta;Delta.SetNumZeroed(Mesh.Vertices.Num());
        for(const auto& Basis:P->Bases) if(Basis.Side==Side && Weights[Basis.Target]>0)
        {
            const auto* M=Mesh.Morphs.FindByPredicate([&](const FVamBuildMorph& X){return X.Name==Basis.Morph;});
            if(M) for(int32 V=0;V<Delta.Num();++V) Delta[V]+=M->Deltas[V]*Weights[Basis.Target];
        }
        TArray<FVector> Base,Final;Base.SetNumZeroed(Delta.Num());Final.SetNumZeroed(Delta.Num());bool Finite=true,Bounded=true;
        for(int32 V=0;V<Delta.Num();++V) for(const auto& W:Influence[V])
        {
            const FMatrix Matrix=Bind[W.Bone].ToInverseMatrixWithScale()*Pose[W.Bone].ToMatrixWithScale();
            Base[V]+=FVector(Matrix.TransformPosition(Mesh.Vertices[V]))*W.Weight;Final[V]+=FVector(Matrix.TransformPosition(Mesh.Vertices[V]+Delta[V]))*W.Weight;
        }
        for(int32 V=0;V<Delta.Num();++V) { Finite&=!Final[V].ContainsNaN();Bounded&=(Final[V]-Base[V]).Size()<S.Dimensions.GetMin()*.35; }
        bool Triangles=true;double WorstRatio=0;int32 WorstTriangle=-1;double WorstBase=0,WorstFinal=0;
        for(int32 T=0;T<Mesh.Triangles.Num();T+=3)
        {
            const int32 A=Mesh.Triangles[T],B=Mesh.Triangles[T+1],C=Mesh.Triangles[T+2];
            const double Area0=FVector::CrossProduct(Base[B]-Base[A],Base[C]-Base[A]).Size();
            const double Area1=FVector::CrossProduct(Final[B]-Final[A],Final[C]-Final[A]).Size();
            const double Ratio=Area1/FMath::Max(.002,Area0);if(Ratio>WorstRatio) { WorstRatio=Ratio;WorstTriangle=T/3;WorstBase=Area0;WorstFinal=Area1; }
            Triangles&=FMath::IsFinite(Area1) && Area1<FMath::Max(.01,Area0*5);
        }
        TestTrue(TEXT("Posed complete corrective finite"),Finite);TestTrue(TEXT("Posed complete corrective bounded"),Bounded);TestTrue(FString::Printf(TEXT("No corrective triangle explosion side %d step %d triangle %d base %.6f final %.6f ratio %.6f"),Side,Step,WorstTriangle,WorstBase,WorstFinal,WorstRatio),Triangles);
        if(Step==0) for(const FVector& V:Delta) if(!V.IsNearlyZero(1.e-10)) { AddError(TEXT("Neutral complete geometry changed"));break; }
    }
    TestTrue(TEXT("Generated actual nonzero corrective geometry"),DeltaVertices>100 && Maximum>.01);
    AddInfo(FString::Printf(TEXT("G06_GEOMETRY targets=%d morphs=%d max_bind_delta_cm=%.6f source_vertex_reuse=%d provenance=%s"),P->Targets.Num(),P->Bases.Num(),Maximum,P->ReusedSourceVertexCount,*P->Provenance));
    return true;
}
#endif
