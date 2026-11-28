#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDWorldLabels.generated.h"
struct FLDObservedUnitView;
struct FLDOwnerState;
struct FLDCombatOwnerView;
struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;

// Overhead labels of currently visible units (APP_SPEC 8). A contender reads its public roster name and a camp member
// its approved display name, tinted by its tier (white, blue, violet, gold; the Boss red) with a bar frame of the same
// colour. Under the name sits a compact health bar (a contender's level badge, in its identity tint,
// left of it) and above the name the unit's public statuses as icon squares (their picture, or the name's first character,
// with a green or red rim), so no state relies on colour alone. A hard control draws its remaining time as a white bar
// under the health bar.
namespace LDWorldLabels
{
    struct FLabel
    {
        // One public status as shown: its text and its definition, so it can be drawn in that status's marker colour.
        struct FStatusPart
        {
            FString Text;
            uint8 StatusId=0;
            FString IconId;      // the approved picture; empty paints the glyph
            FString Glyph;       // the name's first character
            bool bPositive=true; // the rim: green helps its bearer, red hurts
            uint8 Stacks=1;
        };
        FString Name;          // empty when the public roster does not list the contender: never invented
        FString Status;        // public statuses as on the target card, without its prefix; empty when there are none
        TArray<FStatusPart> StatusParts;  // the parts Status joins with " · ", in the same order
        bool bContender=false;
        bool bPeaceful=false;  // a contender the viewer cannot attack now: its bar is not the hostile red
        uint8 Presentation=0;  // contender identity tint key; 0 for camp members
        uint8 CreatureId=0;    // camp member definition; 0 for contenders
        int8 CampTier=-1;      // camp member tier (ELDDemoCampTier); -1 for contenders, the dealer and unknown definitions
        uint8 Level=0;         // contender level; 0 for camp members, which have none
        bool bHealth=false;    // false without a known maximum or with a malformed current: no bar, never an empty one
        float HealthFraction=0.f;  // current over maximum, clamped to 0..1
        float MaxHealth=0.f;       // the maximum in whole HP, for the bar's 100/1000 HP ticks (how tanky the unit is)
        FGuid Handle;              // this visibility interval of the unit, for the bar's damage trail
        bool bShield=false;    // the unit has the public shield status
        float ShieldFraction=0.f;  // the live shield over the maximum HP (quantized, 0..1): the bar's shield segment
        int64 ControlRemainingMs=0;  // the longest public hard control left on the unit; 0 = none
        FGuid BreakHitId;            // the recent hit when it broke the unit's shield (HitFlags ShieldBroken); else none
    };
    // SC-07 subset for a label: name, level, HP fraction, shield presence and public statuses; never momentum, skills or
    // cooldowns. A remaining time counts from the frame's own authority sample and rounds up to a
    // tenth, the same text as the target card.
    LOCKDOWN_API FLabel Build(const FLDObservedUnitView& Unit,const FString& RosterName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);

    struct FOwnBars
    {
        bool bVisible=false;
        float Health=0,Shield=0,Momentum=0;
        bool bMomentum=false;
    };
    // Bars over the local owner's own body, from this controller's own combat projection only and behind the HUD band's
    // gate: the current public match, this life, alive, with a body and a known health maximum. The shield continues after the
    // health fill and, when both exceed the bar, the two share it in proportion; momentum is omitted without a known maximum.
    // The widget also requires the owner's pawn, which anchors the bars.
    LOCKDOWN_API FOwnBars OwnBars(const FLDOwnerState& Own,uint32 PublicGeneration,const FLDCombatOwnerView& Combat);

    // The own status icons' squares over the own bars, LoL-style: PerRow squares of Side a row with Gap between, each row
    // centred on CentreX; the first row's bottom edge rests on Bottom and further rows stack upward. In status order.
    LOCKDOWN_API TArray<FSlateRect> OwnStatusIconRects(int32 Count,float CentreX,float Bottom,float Side,float Gap,int32 PerRow);

    // #328 §1.2: LoL-style segmented health tick marks (100 HP minor half-height switching to 250 HP at >= 3500 HP span,
    // 1000 HP full-height 2px major dividers).
    struct FBarTick
    {
        float X=0.f;
        bool bMajor=false;
    };
    LOCKDOWN_API void BuildHealthTicks(int64 MaxHPP2,int64 CurrentHPP2,int64 ShieldP2,float BarWidth,TArray<FBarTick>& OutTicks);

    // #328 §1.3-§1.4: per-unit local presentation state for 120ms white damage trail hold + 2.4x MaxHP/s smooth decay,
    // 180ms heal flash, >=18% MaxHP / crit bar shake, and 240ms 6-shard shield shatter.
    struct FBarTrailState
    {
        bool bPrimed=false;
        int64 PreviousHPP2=0;
        double TrailHPP2=0.;
        int64 PreviousShieldP2=0;
        int64 HealFromHPP2=0,HealToHPP2=0;
        double LastUpdateSec=0.;
        double LastDamagedAtSec=-10.;
        double LastHealedAtSec=-10.;
        double LastShakeAtSec=-10.;
        double LastShieldBreakAtSec=-10.;
        float ShieldBreakFraction=0.f;
        FGuid LastHitId;
    };
    LOCKDOWN_API void AdvanceBarTrail(FBarTrailState& State,int64 CurrentHPP2,int64 MaxHPP2,int64 CurrentShieldP2,
        uint8 HitFlags,const FGuid& HitId,double NowSec);

    struct FOcclusionSample
    {
        double LastCheckedSec=-10.;
        bool bOccluded=false;
    };
    constexpr double OcclusionTraceIntervalSec=0.07;
    LOCKDOWN_API bool ShouldRefreshOcclusion(double LastCheckedSec,double NowSec,double IntervalSec=OcclusionTraceIntervalSec);
    LOCKDOWN_API bool ComputeSilhouetteBounds(const FVector2f& HeadPx,const FVector2f& FootPx,float HalfWidthPx,FSlateRect& OutRect);
    LOCKDOWN_API bool IsChestOccludedByStaticWorld(const UWorld& World,const FVector& CameraLocation,
        const FVector& PresentedFeet,const AActor* IgnoredBody=nullptr);
}

// Full-viewport layer under the HUD that is never hit-tested or focused; ULDUIRoot shows it exactly while the in-match
// HUD is shown. Every paint reads the owner's current frame of this match and life and labels only units presented
// right now; a dead, stale or foreign frame labels nothing and no unit is remembered between frames. The own body's
// bars and level badge read only this controller's own projections, so they show even with no other unit
// in sight.
UCLASS()
class LOCKDOWN_API ULDWorldLabels : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    // Approved public definitions used only for display names; loaded once per widget like the HUD's.
    TSharedPtr<const FLDDemoContentSnapshot> DisplayContent;
    bool bDisplayContentTried=false;
    mutable LDWorldLabels::FBarTrailState OwnTrail;
    mutable uint32 OwnTrailMatch=0,OwnTrailLife=0;
    mutable TMap<FGuid,LDWorldLabels::FBarTrailState> UnitTrails;
    mutable LDWorldLabels::FOcclusionSample OwnOcclusion;
    mutable TMap<FGuid,LDWorldLabels::FOcclusionSample> UnitOcclusions;
    // The pictures of the status icons in sight, kept alive while painted.
    UPROPERTY(Transient) mutable TMap<FString,TObjectPtr<UTexture2D>> StatusTextures;
    mutable TMap<FString,FSlateBrush> StatusBrushes;
};
