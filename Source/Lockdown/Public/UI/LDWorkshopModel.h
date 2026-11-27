#pragma once
#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "UI/LDHUDModel.h"

// The build workshop's read model (APP_SPEC §7 构筑): catalog facts and own-view summaries the workshop, the rule board and
// the library draw. Pure presentation over the own progression view and the approved catalog; nothing here decides a rule.
namespace LDWorkshop
{
    // A card's catalog facts for its frame: the type sets the shape, the form the library group.
    struct FCardFacts
    {
        bool bKnown=false;
        ELDCardForm Form=ELDCardForm::Body;
        ELDCardType Type=ELDCardType::Trigger;
    };
    LOCKDOWN_API FCardFacts CardFacts(FName Card,const FLDDemoContentDefinitions* Content);
    // A rule board column head: the learned active's (or the dash's) icon, name, momentum cost and cooldown and the elements it
    // applies; 普攻 shows its glyph, 本体 the holder. An empty Q/W/E slot is bEmpty.
    struct FColumnHead
    {
        FString Name;
        FString IconId;
        FString Glyph;
        FString Momentum;
        FString Cooldown;
        TArray<ELDDemoElement> Elements;
        bool bEmpty=false;
    };
    // Branch is the slot's learned branch (None = rank 1): the head shows that variant's momentum, cooldown and elements.
    LOCKDOWN_API FColumnHead ColumnHead(LDHUD::EStorageColumn Column,FName Active,ELDSkillBranch Branch,const FString& HolderName,
        const FLDDemoContentDefinitions* Content);
    // The elements a card's effects apply (a medal's element).
    LOCKDOWN_API TArray<ELDDemoElement> CardElements(FName Card,const FLDDemoContentDefinitions* Content);
    // The reaction name of two elements, empty when they do not react.
    LOCKDOWN_API FString ReactionName(ELDDemoElement First,ELDDemoElement Second);
    // Whether the workshop opens full screen now: a rest's build, payout and return steps, and the own death wait outside a
    // PK. Every other moment (the field, the PK steps) keeps it to the bottom of the screen.
    LOCKDOWN_API bool FullScreen(bool bRest,ELDRestStep Step,bool bDeadWait);
}
