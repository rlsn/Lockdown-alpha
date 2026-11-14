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
    // A component's catalog facts for its frame: the kind sets the shape, a scarce one gets the iridescent edge, the use tags
    // drive the library filter.
    struct FComponentFacts
    {
        bool bKnown=false;
        ELDDemoComponentKind Kind=ELDDemoComponentKind::Base;
        bool bScarce=false;
        TArray<ELDDemoComponentTag> Tags;
    };
    LOCKDOWN_API FComponentFacts ComponentFacts(FName Component,const FLDDemoContentDefinitions* Content);
    // The library's icon size and row length for Count stored components, so up to 63 fit without scrolling:
    // 68 px five a row up to 15, 56 px six a row up to 30, 46 px seven a row beyond.
    struct FLibraryTier
    {
        float Icon=68.f;
        int32 PerRow=5;
    };
    LOCKDOWN_API FLibraryTier LibraryTier(int32 Count);
    // The elements an active applies (burn, wet and electric effects), in fire, water, electric order.
    LOCKDOWN_API TArray<ELDDemoElement> ActiveElements(FName Active,const FLDDemoContentDefinitions* Content);
    // A rule board column head: the learned active's (or the dash's) icon, name, mana and cooldown and the elements it
    // applies; 本体 shows the holder. An empty Q/W/E slot is bEmpty.
    struct FColumnHead
    {
        FString Name;
        FString IconId;
        FString Glyph;
        FString Mana;
        FString Cooldown;
        TArray<ELDDemoElement> Elements;
        bool bEmpty=false;
    };
    LOCKDOWN_API FColumnHead ColumnHead(LDHUD::EStorageColumn Column,FName Active,const FString& HolderName,const FLDDemoContentDefinitions* Content);
    // Every element the current build applies: the learned actives' and the installed components' (basic elements, B18's
    // bound element and burn, wet or electric effects).
    LOCKDOWN_API TArray<ELDDemoElement> BuildElements(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The three element reactions, lit when the build applies both of their elements.
    struct FReactionRow
    {
        ELDDemoElement First=ELDDemoElement::None;
        ELDDemoElement Second=ELDDemoElement::None;
        FString Name;
        bool bLit=false;
    };
    LOCKDOWN_API TArray<FReactionRow> Reactions(const TArray<ELDDemoElement>& BuildApplies);
    // The reaction name of two elements, empty when they do not react.
    LOCKDOWN_API FString ReactionName(ELDDemoElement First,ELDDemoElement Second);
    // The capacity bar: one cell per workshop slot, the used ones in install order by kind, and the next level that adds a
    // slot (0 at the cap).
    struct FCapacityView
    {
        int32 Capacity=0;
        TArray<ELDDemoComponentKind> Used;
        // The installed component in each used cell, in the same order.
        TArray<FName> Components;
        int32 UnlockLevel=0;
    };
    LOCKDOWN_API FCapacityView Capacity(const FLDProgressionView& Progression,const LDHUD::FStorageView& Storage,const FLDDemoContentDefinitions* Content);
    // Why a stored mod or connector has no place: the actives it needs learned ("需要已学：A、B 或 C"); empty otherwise.
    LOCKDOWN_API FString MissingPlace(FName Component,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The trigger sources of a base component's effects ("普攻命中", "受到伤害", ...), once each.
    LOCKDOWN_API TArray<FString> TriggerChips(FName Component,const FLDDemoContentDefinitions* Content);
    // The owned components that install on (or bind) Active, in the view's order: its 可接 row on an offer card.
    LOCKDOWN_API TArray<FName> LinkableComponents(FName Active,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The keyword names a card sentence uses ("<b>name</>：…" lines), for the offer card's tag row.
    LOCKDOWN_API TArray<FString> KeywordNames(const TArray<FString>& KeywordLines);
    // The six phase-bar dots of a rest: build, draw and betting, PK ready, PK, settlement and payout, return.
    constexpr int32 RestStepDots=6;
    LOCKDOWN_API int32 RestStepDot(ELDRestStep Step);
    // Whether the workshop opens full screen now: a rest's build, payout and return steps, and the own death wait outside a
    // PK. Every other moment (the field, the PK steps) keeps it to the bottom of the screen.
    LOCKDOWN_API bool FullScreen(bool bRest,ELDRestStep Step,bool bDeadWait);
}
