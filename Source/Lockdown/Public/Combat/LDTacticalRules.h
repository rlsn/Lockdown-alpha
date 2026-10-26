#pragma once
#include "CoreMinimal.h"
#include "Development/LDCharacterBuild.h"

namespace LDTactical
{
    enum class EStatus : uint8 { Stun, Silence, Mute, Root, Disarm, Hex, Break, Blind, Slow, Mark, Wet, Burning };
    enum class EAction : uint8 { Attack, Skill, Item, Move, Dash, Service };
    enum class EDamage : uint8 { Physical, Magical, Pure };
    struct FStatus { EStatus Kind; int64 Until; FGuid Source; int32 Magnitude=0; };
    struct FState
    {
        double MP=0, Barrier=0;
        int32 SurfaceSlow=0;
        bool Directional=false;
        FVector BarrierFacing=FVector::ForwardVector;
        int64 ReflectUntil=0;
        int64 BarrierUntil=0, BlockUntil=0, LastRegen=-1;
        int64 ConcealedUntil=0,GroundedUntil=0,GuardUntil=0,MajorReadyAt=0,AnchorUntil=0;
        int64 HPRegenRemainder=0,MPRegenRemainder=0;
        TArray<FStatus> Statuses;
        bool Has(EStatus Kind,int64 At) const;
        uint16 Mask(int64 At) const;
    };
    LOCKDOWN_API int64 Milliseconds(double Seconds);
    LOCKDOWN_API double MaxMP(const FLDCharacterBuild& B);
    LOCKDOWN_API bool Allows(const FState& S,EAction Action,int64 At,FString* Why=nullptr);
    LOCKDOWN_API void Apply(FState& S,EStatus Kind,int64 Until,FGuid Source,int32 Magnitude=0);
    LOCKDOWN_API void Dispel(FState& S,bool Strong,bool Positive=false);
    LOCKDOWN_API void Barrier(FState& S,double Amount,double MaxHP,int64 Until,int64 At);
    LOCKDOWN_API double Damage(double& HP,FState& S,double Amount,EDamage Kind,double Armor,double MR,int64 At,bool UseBarrier=true);
    LOCKDOWN_API bool ActivateGuard(FState& S,int64 At);
    LOCKDOWN_API bool GuardControl(FState& S,EStatus Kind,int64 At);
    LOCKDOWN_API bool PassiveActive(const FState& S,bool Breakable,int64 At);
    LOCKDOWN_API float BodySight(const FState& S,int64 At);
    LOCKDOWN_API bool Block(FState& S,int64 At);
    LOCKDOWN_API void Regenerate(double& HP,FState& S,double MaxHP,double MaxMP,int64 At,int32 MPRegen=200);
    LOCKDOWN_API FString StatusText(const FState& S,int64 At);
}
