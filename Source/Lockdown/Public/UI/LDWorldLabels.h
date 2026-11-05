#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDWorldLabels.generated.h"
struct FLDObservedUnitView;
struct FLDOwnerState;
struct FLDCombatOwnerView;
struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;
namespace LDHUD{struct FActionContext;struct FActionView;}

// Overhead labels of currently visible units (APP_SPEC 8). A contender reads its public roster name and a camp member
// its approved display name. Under the name sits a compact health bar (a contender's level badge, in its identity tint,
// left of it) and above the name the unit's public statuses as text, so no state relies on colour alone: the shield is named
// there as well as marked on the bar.
namespace LDWorldLabels
{
    struct FLabel
    {
        // One public status as shown: its text and its definition, so it can be drawn in that status's marker colour.
        struct FStatusPart
        {
            FString Text;
            uint8 StatusId=0;
        };
        FString Name;          // empty when the public roster does not list the contender: never invented
        FString Status;        // public statuses as on the target card, without its prefix; empty when there are none
        TArray<FStatusPart> StatusParts;  // the parts Status joins with " · ", in the same order
        bool bContender=false;
        uint8 Presentation=0;  // contender identity tint key; 0 for camp members
        uint8 CreatureId=0;    // camp member definition; 0 for contenders
        uint8 Level=0;         // contender level; 0 for camp members, which have none
        bool bHealth=false;    // false without a known maximum or with a malformed current: no bar, never an empty one
        float HealthFraction=0.f;  // current over maximum, clamped to 0..1
        bool bShield=false;    // the unit has the public shield status; its amount is not part of the label
    };
    // SC-07 subset for a label: name, level, HP fraction, shield presence and public statuses; never mana, equipment,
    // skills, cooldowns or gold. A remaining time counts from the frame's own authority sample and rounds up to a
    // tenth, the same text as the target card.
    LOCKDOWN_API FLabel Build(const FLDObservedUnitView& Unit,const FString& RosterName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);

    struct FOwnBars
    {
        bool bVisible=false;
        float Health=0,Shield=0,Mana=0;
        bool bMana=false;
    };
    // Bars over the local owner's own body, from this controller's own combat projection only and behind the HUD band's
    // gate: the current public match, this life, alive, with a body and a known health maximum. The shield continues after the
    // health fill and, when both exceed the bar, the two share it in proportion; mana is omitted without a known maximum.
    // The widget also requires the owner's pawn, which anchors the bars.
    LOCKDOWN_API FOwnBars OwnBars(const FLDOwnerState& Own,uint32 PublicGeneration,const FLDCombatOwnerView& Combat);

    // Readiness pips under the own bars, one each for Q, W, 1 and 2.
    enum class EReadinessPip : uint8 { Hidden, Ready, Cooling, NoMana, Controlled };
    // Classifies the HUD band's action view of one own entry (LDHUD::Action) by its execution limits and cooldown, so a
    // pip never re-derives a rule. An empty slot or one without an active draws nothing, nor does an action state that
    // is not current (unsynced, dead, not playing); then cooling, controlled and short of mana in the band's state
    // order. Any other limit, such as a published server reason, is not ready and reads as cooling.
    LOCKDOWN_API EReadinessPip ReadinessPip(const LDHUD::FActionView& View);
    // Pip of own entry Index (0..3 = Q, W, 1, 2), with the action view built like the HUD band's slot: LDHUD::ActionInput
    // of the projected entry, controlled when an own status blocks that entry kind (cast for Q/W, gear for 1/2), then
    // LDHUD::Action under the band's context. An entry the projection does not list stays unprojected and hidden.
    LOCKDOWN_API EReadinessPip OwnReadinessPip(const FLDCombatOwnerView& Combat,int32 Index,const LDHUD::FActionContext& Context,const FLDDemoContentDefinitions* Content);
}

// Full-viewport layer under the HUD that is never hit-tested or focused; ULDUIRoot shows it exactly while the in-match
// HUD is shown. Every paint reads the owner's current frame of this match and life and labels only units presented
// right now; a dead, stale or foreign frame labels nothing and no unit is remembered between frames. The own body's
// bars, level badge and readiness pips read only this controller's own projections, so they show even with no other unit
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
};
