#include "VamGluteJiggleBuilder.h"
#include "VamGluteJiggleProfile.h"
FString UVamGluteJiggleBuilder::Build(UVamGluteStructureProfile* G,UVamGluteJiggleProfile* P)
{
    if(!G || !P || !G->IsValidProfile() || G->RefinementVersion!=1) return TEXT("G1 requires valid G0.5 structural calibration");
    P->Algorithm=TEXT("glute-dual-attachment-g1-v2");P->bNormalizedAttachmentDamping=true;
    P->SourceTopologyIdentity=G->SourceTopologyIdentity;P->SkeletonFamily=G->SkeletonFamily;P->DensityKgPerCm3=G->DensityCandidateKgPerCm3;P->Sides.Reset();
    for(const auto& S:G->Sides) P->Sides.Add(VamGluteDynamics::Calibrate(*P,S,VamGluteStructure::Evaluate(*G,S,S.RestThighInAnchor)));
    if(!P->IsValidProfile()) return TEXT("Invalid G1 automatic dynamics calibration");P->MarkPackageDirty();return FString();
}
