#pragma once
#include "CoreMinimal.h"
#include "Combat/LDRiverRules.h"
class ALDTacticalField;

namespace LDEnvironment
{
    enum class EInput:uint8 { Water,Frost,Fire,Lightning,Wind,Nature,Shatter,Smoke,Oil,Thaw,Steam,Purify };
    enum class EState:uint8 { Wet,Frozen,Burning,Electrified,Obscured,Overgrown,Oil,Count };
    enum ESubstrate:uint8 { Stone=0,Water=1,Soil=2,Flammable=4,Metal=8 };
    struct FRecord
    {
        FGuid Owner,Life,Cast,BudgetOwner;
        int64 Created=0,Until=0;
        int32 Magnitude=0;
        uint32 Revision=0;
        bool WetSubstrate=false;
    };
    struct FCell
    {
        FRecord Wet,Frozen,Burning,Electrified,Obscured,Overgrown,Oil;
        TWeakObjectPtr<ALDTacticalField> Cue;
        FRecord& Get(EState State);
        const FRecord& Get(EState State) const;
        uint8 Mask(int64 At) const;
        void Expire(int64 At);
        bool Owned(FGuid Owner,int64 At) const;
    };
    struct FCommand
    {
        EInput Input=EInput::Water;
        FRecord State;
        TArray<FIntPoint> Cells;
        TMap<FIntPoint,FCell> Outcomes;
        TSet<FIntPoint> Shattered;
        int32 FrozenBonus=0,BurningBonus=0,ElectricBonus=0,SteamBonus=0;
        FGuid Reservation;
    };
    LOCKDOWN_API int32 Cap(EState State);
    LOCKDOWN_API uint8 Substrate(FIntPoint Cell,bool RiverMap);
    // Exactly one nonrecursive transition from the input snapshot per selected cell.
    LOCKDOWN_API bool Transition(FCell& Cell,const FCommand& Command,uint8 Substrate,int64 At);
    LOCKDOWN_API bool Occludes(FVector From,FVector To,const TSet<FIntPoint>& Obscured);
    LOCKDOWN_API TArray<FIntPoint> Mask(FVector Start,FVector Aim,double Length,double Width,bool Disk,int32 Limit=16,bool Cone=false);
}
