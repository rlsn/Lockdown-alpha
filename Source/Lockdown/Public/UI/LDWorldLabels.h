#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDWorldLabels.generated.h"
struct FLDObservedUnitView;
struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;

// Overhead labels of currently visible units (APP_SPEC 8). A contender reads its public roster name and a camp member
// its approved display name; each label adds the unit's public statuses as text, so no state relies on colour alone.
namespace LDWorldLabels
{
    struct FLabel
    {
        FString Name;          // empty when the public roster does not list the contender: never invented
        FString Status;        // public statuses as on the target card, without its prefix; empty when there are none
        bool bContender=false;
        uint8 Presentation=0;  // contender identity tint key; 0 for camp members
        uint8 CreatureId=0;    // camp member definition; 0 for contenders
    };
    // SC-07 subset for a label: name and public statuses only, never level, resources or equipment. A remaining time
    // counts from the frame's own authority sample and rounds up to a tenth, the same text as the target card.
    LOCKDOWN_API FLabel Build(const FLDObservedUnitView& Unit,const FString& RosterName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);
}

// Full-viewport layer under the HUD that is never hit-tested or focused; ULDUIRoot shows it exactly while the in-match
// HUD is shown. Every paint reads the owner's current frame of this match and life and labels only units presented
// right now; a dead, stale or foreign frame draws nothing and no unit is remembered between frames.
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
