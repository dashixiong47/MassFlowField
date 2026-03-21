#include "VAT/FlowFieldVATDataAsset.h"

int32 UFlowFieldVATDataAsset::FindAnimationByName(FName Name) const
{
    for (int32 i = 0; i < Animations.Num(); i++)
    {
        if (Animations[i].AnimationName == Name) return i;
    }
    return -1;
}

const FFlowFieldVATAnimation* UFlowFieldVATDataAsset::GetAnimation(int32 AnimationID) const
{
    if (!Animations.IsValidIndex(AnimationID)) return nullptr;
    return &Animations[AnimationID];
}
