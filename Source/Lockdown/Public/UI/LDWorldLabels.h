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
};
