#include "VamGluteJiggleBuilder.h"
#include "VamGluteJiggleProfile.h"
FString UVamGluteJiggleBuilder::Build(UVamGluteStructureProfile* G,UVamGluteJiggleProfile* P)
{
    if(!G || !P || !G->IsValidProfile() || G->RefinementVersion!=1) return TEXT("G1 requires valid G0.5 structural calibration");
    P->SchemaVersion=2;P->Algorithm=TEXT("glute-dual-attachment-g1.1-reference-gravity-v1");P->bNormalizedAttachmentDamping=true;
    P->AuthoredGravityWorld=FVector(0,0,-980);
    P->GravityPolicy=TEXT("body-attached-imported-1g-v1");
    P->SourceTopologyIdentity=G->SourceTopologyIdentity;P->SkeletonFamily=G->SkeletonFamily;P->DensityKgPerCm3=G->DensityCandidateKgPerCm3;P->Sides.Reset();
    for(const auto& S:G->Sides)
    {
        auto Side=VamGluteDynamics::Calibrate(*P,S,VamGluteStructure::Evaluate(*G,S,S.RestThighInAnchor));
        Side.ReferenceGravityLocal=(S.AnchorLocal*G->RestPelvisComponent).GetRotation().UnrotateVector(P->AuthoredGravityWorld);
        P->Sides.Add(Side);
    }
    if(!P->IsValidProfile()) return TEXT("Invalid G1 automatic dynamics calibration");P->MarkPackageDirty();return FString();
}
