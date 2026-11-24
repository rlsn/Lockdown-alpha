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
    // The library's groups, one per card form (技能, 冲刺, 本体, 勋章): each stored definition once (its first copy's key, the
    // storage view's order). A selected type filter (ELDCardType) excludes the other types. Group = ELDCardForm − 1 (form 0
    // is retired), so the four groups fit the FIntVector4 page counts.
    constexpr int32 LibraryGroupCount=4;
    inline int32 LibraryGroupOf(ELDCardForm Form){return int32(Form)-1;}
    inline uint8 LibraryGroupForm(int32 Group){return uint8(Group+1);}
    struct FLibraryGroups
    {
        TArray<FName> Items[LibraryGroupCount];
        FIntVector4 Counts() const {return FIntVector4(Items[0].Num(),Items[1].Num(),Items[2].Num(),Items[3].Num());}
    };
    LOCKDOWN_API FLibraryGroups CollectLibrary(const LDHUD::FStorageView& Storage,const FLDDemoContentDefinitions* Content,int32 TypeFilter);
    // The library's icon size and row length for Count stored cards, so every one fits without scrolling:
    // 68 px five a row up to 15, 56 px six a row up to 30, 46 px seven a row beyond.
    struct FLibraryTier
    {
        float Icon=68.f;
        int32 PerRow=5;
    };
    LOCKDOWN_API FLibraryTier LibraryTier(int32 Count);
    // Reserve room for at least one library card when the pending tray shares the same column.
    struct FPendingPage
    {
        int32 First=0;
        int32 Count=0;
    };
    struct FPendingFit
    {
        TArray<FPendingPage> Pages;
        bool bFits=false;
    };
    // Keep each pending decision whole, allowing independent blueprints to occupy separate rows.
    LOCKDOWN_API FPendingFit FitPending(const TArray<float>& Heights,float Height,float PagerHeight);
    // The elements an active applies (burn, wet and electric effects), in fire, water, electric order.
    LOCKDOWN_API TArray<ELDDemoElement> ActiveElements(FName Active,const FLDDemoContentDefinitions* Content);
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
    // The capacity bar (6–9 cells): one cell per slot, the used ones in board order by type, and the next level that adds a
    // slot (0 at the cap).
    struct FCapacityView
    {
        int32 Capacity=0;
        TArray<ELDCardType> Used;
        // The installed card in each used cell, in the same order (every copy its own cell).
        TArray<FName> Cards;
        int32 UnlockLevel=0;
    };
    LOCKDOWN_API FCapacityView Capacity(const FLDProgressionView& Progression,const LDHUD::FStorageView& Storage,const FLDDemoContentDefinitions* Content);
    // Why a stored skill card has no place: no skill is learned yet; empty otherwise.
    LOCKDOWN_API FString MissingPlace(FName Card,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    struct FOfferLinks
    {
        TArray<FName> Cards;
        ELDGrowthReason Failure=ELDGrowthReason::None;
        bool bLearned=false;
    };
    // The owned skill cards (their first copy's key) that would take the offered Active once it is learned now, and the cards
    // sharing its vocabulary; a choice stored as a blueprint links nothing yet. Failures are separate from a valid empty result.
    LOCKDOWN_API FOfferLinks LinkableCards(FName Active,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
        LDProgression::EMatchStage Stage,ELDLifePhase LifePhase);
    // Whether the workshop opens full screen now: a rest's build, payout and return steps, and the own death wait outside a
    // PK. Every other moment (the field, the PK steps) keeps it to the bottom of the screen.
    LOCKDOWN_API bool FullScreen(bool bRest,ELDRestStep Step,bool bDeadWait);
}
