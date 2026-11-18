#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDHUDModel.h"

class FLDDemoContentSnapshot;

namespace LDWorkshopPreview
{
    enum class EStatus : uint8 { Ready, Synchronizing, ChoiceRequired, Refused, Unavailable };
    enum class EImpact : uint8 { Neutral, Beneficial, Cost };

    struct FLine
    {
        FString Label;
        FString Before;
        FString After;
        EImpact Impact=EImpact::Neutral;
    };

    struct FInput
    {
        const FLDDemoContentSnapshot* Content=nullptr;
        const FLDProgressionView* Progression=nullptr;
        const FLDCombatOwnerView* Combat=nullptr;
        uint32 Match=0;
        LDProgression::EMatchStage Stage=LDProgression::EMatchStage::Closed;
        bool bClock=false;
        int64 NowMs=0;
        bool bFinalField=false;
        FName Component;
        LDHUD::FCardDrop Drop;
        ELDDemoElement Element=ELDDemoElement::None;
        FName Base;
    };

    struct FResult
    {
        EStatus Status=EStatus::Synchronizing;
        ELDGrowthReason Reason=ELDGrowthReason::None;
        FString Error;
        bool bPartial=false;
        TArray<FLine> Lines;
    };

    // A snapshot comparison only: both candidate builds are copies and the authority still judges the actual command.
    LOCKDOWN_API FResult Compare(const FInput& Input);
}
