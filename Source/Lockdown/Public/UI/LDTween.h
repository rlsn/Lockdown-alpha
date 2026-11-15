#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"

// Interface motion: a duration and one of three curves give a 0..1 progress that drives a widget's render scale,
// opacity or translation. Times are local real seconds; a tween never gates input or a submission.
namespace LDTween
{
    enum class ECurve : uint8
    {
        // Entering, hovering, expanding.
        EaseOut,
        // Leaving, collapsing.
        EaseIn,
        // Landing, gaining, choosing: passes the target by at most 8% and settles back.
        Back,
    };

    // Back's coefficient: the peak overshoot of 1+(c+1)u^3+cu^2 is 4c^3/(27(c+1)^2), exactly 8% at c=1.5.
    inline constexpr float BackOvershoot=1.5f;

    inline float Ease(ECurve Curve,float T)
    {
        T=FMath::Clamp(T,0.f,1.f);
        switch(Curve)
        {
        case ECurve::EaseIn: return T*T*T;
        case ECurve::Back: {const float U=T-1.f;return 1.f+(BackOvershoot+1.f)*U*U*U+BackOvershoot*U*U;}
        case ECurve::EaseOut: default: {const float U=1.f-T;return 1.f-U*U*U;}
        }
    }
    // Eased progress of a tween started Age seconds ago; 1 once it is over, 0 before it starts.
    inline float Progress(double Age,double Duration,ECurve Curve)
    {
        if(Age<0.){return 0.f;}
        return Duration<=0.?1.f:Ease(Curve,float(Age/Duration));
    }
    // Rise and fall within one Period: eases out to 1 at the half and eases back in to 0, repeating.
    inline float PingPong(double Age,double Period)
    {
        if(Age<0.||Period<=0.){return 0.f;}
        const float Phase=float(FMath::Fmod(Age,Period)/Period);
        return Phase<.5f?Ease(ECurve::EaseOut,Phase*2.f):1.f-Ease(ECurve::EaseIn,(Phase-.5f)*2.f);
    }

    // One value moving toward a target, restarted whenever the target changes, so hover in and out reverse smoothly.
    struct FTrack
    {
        float From=0.f,To=0.f;double StartedAt=-1.;
        void Retarget(float Target,double Now,double Duration,ECurve Curve)
        {
            if(FMath::IsNearlyEqual(Target,To)){return;}
            From=Value(Now,Duration,Curve);To=Target;StartedAt=Now;
        }
        float Value(double Now,double Duration,ECurve Curve) const
        {
            return StartedAt<0.?To:FMath::Lerp(From,To,Progress(Now-StartedAt,Duration,Curve));
        }
        bool IsMoving(double Now,double Duration) const {return StartedAt>=0.&&Now-StartedAt<Duration;}
    };

    // Applied only on change, so a steady state costs no layout or paint invalidation.
    inline void SetScale(UWidget* W,float Scale)
    {
        if(W&&!W->GetRenderTransform().Scale.Equals(FVector2D(Scale,Scale),1e-4)){W->SetRenderScale(FVector2D(Scale,Scale));}
    }
    inline void SetOpacity(UWidget* W,float Opacity)
    {
        if(W&&!FMath::IsNearlyEqual(W->GetRenderOpacity(),Opacity,1e-4f)){W->SetRenderOpacity(Opacity);}
    }
    inline void SetTranslation(UWidget* W,FVector2D Offset)
    {
        if(W&&!W->GetRenderTransform().Translation.Equals(Offset,.01)){W->SetRenderTranslation(Offset);}
    }
}
