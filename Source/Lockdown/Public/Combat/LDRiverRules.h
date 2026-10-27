#pragma once
#include "CoreMinimal.h"
#include "Combat/LDTacticalRules.h"

// B82's two-state slice. Grid origin matches the authored public region grid.
namespace LDRiver
{
    struct FContactInput
    {
        bool Floor=false, Water=false, Frozen=false, Growth=false;
        bool Alive=true, Protected=false, Immune=false, Wading=false, IceGrip=false;
    };
    // Shared contact reducer. Returns true when external Burning attribution must clear.
    LOCKDOWN_API bool RefreshContact(LDTactical::FState& State,bool& LastWet,int64& SampledAt,
        const FContactInput& Input,int64 At,bool Boundary);
    constexpr int32 CellSize=200, CastCap=16, OwnerCap=64, MatchCap=256;
    inline FIntPoint Cell(FVector P){return FIntPoint(FMath::FloorToInt((P.X+6000)/200),FMath::FloorToInt((P.Y+4000)/200));}
    inline FVector Centre(FIntPoint C){return FVector(-5900+C.X*200,-3900+C.Y*200,0);}
    inline bool InRiver(FVector P)
    {return P.Z>=-10&&P.Z<=120&&((P.X>=-1800&&P.X<-1400&&P.Y>=-1200&&P.Y<-200)||(P.X>=-600&&P.X<-200&&P.Y>=-600&&P.Y<-200));}
    LOCKDOWN_API TArray<FIntPoint> Footprint(FVector Start,FVector Aim,double Length,double Width,bool Disk);
    // Called only after packet block/reflection and original-life/category checks.
    // The contender endpoint lives outside dispellable statuses.
    inline bool AttemptFrost(LDTactical::FState& State,int64& ReadyAt,int64 At)
    {
        if(At<ReadyAt||!State.Has(LDTactical::EStatus::Wet,At)){return false;}
        State.Statuses.RemoveAll([](const auto& S){return S.Kind==LDTactical::EStatus::Wet;});
        ReadyAt=At+6000;return true;
    }
}
