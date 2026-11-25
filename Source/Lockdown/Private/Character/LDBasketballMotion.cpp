#include "Character/LDBasketballMotion.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"

bool ULDBasketballMotionSet::Validate(const USkeletalMesh& Body, FString& Error) const
{
    const auto Fail = [&](const FString& Detail) { Error = TEXT("Basketball motion: ") + Detail; return false; };
    if (SourceSha256.Len() != 64 || !BallMesh || !BallMaterial || !FMath::IsFinite(BallRadiusCm) || BallRadiusCm <= 0
        || !FMath::IsFinite(RunSpeedCmPerSec) || RunSpeedCmPerSec <= 0 || RightPalm.ContainsNaN() || LeftPalm.ContainsNaN()
        || !RightPalmNormal.IsNormalized() || !LeftPalmNormal.IsNormalized()) { return Fail(TEXT("invalid source, prop or calibrated dimensions.")); }
    for (const FName Bone : {FName(TEXT("hand_r")),FName(TEXT("hand_l")),FName(TEXT("spine_01")),FName(TEXT("clavicle_r")),FName(TEXT("clavicle_l"))})
    { if (Body.GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE) { return Fail(TEXT("missing body bone ") + Bone.ToString()); } }
    for (const auto* Group : {&Idle,&Run,&Handling,&Passes,&StandingShots,&MovingShots,&Turns,&Crossovers})
    {
        if (Group->IsEmpty()) { return Fail(TEXT("a required motion group is empty.")); }
        for (const auto& Clip : *Group)
        {
            if (!Clip.Animation || Clip.Animation->GetSkeleton() != Body.GetSkeleton() || Clip.Animation->bEnableRootMotion
                || !FMath::IsFinite(Clip.Start) || !FMath::IsFinite(Clip.Release) || !FMath::IsFinite(Clip.End)
                || Clip.Start < 0 || Clip.Start >= Clip.Release || Clip.Release >= Clip.End
                || Clip.End > Clip.Animation->GetPlayLength() + .001f) { return Fail(TEXT("invalid clip, skeleton or release interval.")); }
        }
    }
    Error.Reset(); return true;
}

namespace
{
    // The rate of the clip after its release: the windup's own warp, except that a windup so short that the warp would be a
    // blur (an instant cast) lets the follow-through play at FollowRate so the gesture still reads.
    float FollowScale(const FLDBasketballActionClip& Clip, float ImpactSeconds)
    {
        const float Scale = ImpactSeconds > 0 ? (Clip.Release - Clip.Start) / ImpactSeconds : 0;
        return ImpactSeconds <= 0 || Scale > LDBasketballMotion::MaxWarp ? LDBasketballMotion::FollowRate : Scale;
    }
}

float LDBasketballMotion::SampleTime(const FLDBasketballActionClip& Clip, float Age, float ImpactSeconds)
{
    const float Scale = ImpactSeconds > 0 ? (Clip.Release - Clip.Start) / ImpactSeconds : 0;
    Age = FMath::Max(Age, 0.f);
    const float Time = Age <= ImpactSeconds ? Clip.Start + Age * Scale
        : Clip.Release + (Age - FMath::Max(ImpactSeconds, 0.f)) * FollowScale(Clip, ImpactSeconds);
    return FMath::Clamp(Time, Clip.Start, Clip.End);
}

float LDBasketballMotion::Duration(const FLDBasketballActionClip& Clip, float ImpactSeconds)
{
    return Clip.Release > Clip.Start ? FMath::Max(ImpactSeconds, 0.f) + (Clip.End - Clip.Release) / FollowScale(Clip, ImpactSeconds) : 0;
}
