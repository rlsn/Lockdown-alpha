#include "UI/LDWorldLabels.h"
#include "UI/LDHUDModel.h"
#include "UI/LDControlSettings.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
// Anchor height above the presented body centre, clear of the head and its status markers. The proxy draws the
// high-tier camp body larger, so its label sits higher.
double WorldLabelHeadroomCm(const LDWorldLabels::FLabel& Label)
{return !Label.bContender&&Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)?150.:120.;}
// Anchors further off-screen are skipped; the margin keeps a label whose anchor sits just past an edge.
constexpr float WorldLabelCullMargin=160.f;
const FLinearColor WorldLabelContenderInk(.96f,.97f,1.f);
const FLinearColor WorldLabelCampInk(1.f,.8f,.42f);
const FLinearColor WorldLabelStatusInk(.8f,.93f,1.f);
const FLinearColor WorldLabelRim(0.f,0.f,0.f,.85f);
// The UI's default CJK-capable font at the local text scale, outlined so a label reads over any ground or body.
FSlateFontInfo WorldLabelFont(const FName Typeface,int32 Size,float TextScale)
{
    FFontOutlineSettings Outline;Outline.OutlineSize=2;Outline.OutlineColor=WorldLabelRim;
    return FCoreStyle::GetDefaultFontStyle(Typeface,float(FMath::Max(1,FMath::RoundToInt(Size*TextScale))),Outline);
}
}

LDWorldLabels::FLabel LDWorldLabels::Build(const FLDObservedUnitView& Unit,const FString& RosterName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    FLabel Label;
    Label.bContender=Unit.Kind==ELDObservedUnitKind::Contender;
    if(Label.bContender){Label.Name=RosterName;Label.Presentation=Unit.Presentation;}
    else{Label.Name=LDHUD::CreatureName(Content,Unit.CreatureId);Label.CreatureId=Unit.CreatureId;}
    // The same parts as the target card's status line (LDHUD::ObservedCard), so both read alike.
    for(const FLDObservedStatus& Status:Unit.Statuses)
    {
        FString Part=LDHUD::StatusName(Content,Status.StatusId);
        if(Status.Stacks>1){Part+=FString::Printf(TEXT(" ×%d"),Status.Stacks);}
        if(Status.ExpiresAtServerMs>0){Part+=TEXT(" ")+LDHUD::TenthsUp(int64(FMath::RoundToDouble(double(Status.ExpiresAtServerMs)-SampledAtServerSec*1000.)));}
        Label.Status+=(Label.Status.IsEmpty()?TEXT(""):TEXT(" · "))+Part;
    }
    return Label;
}

void ULDWorldLabels::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    if(bDisplayContentTried){return;}
    // Missing content stays visible: names then read as the HUD reports them (未知野怪 #n, stable status ids).
    bDisplayContentTried=true;FString Error;
    if(!LDDemoEntry::LoadRuntimeContent(DisplayContent,Error)||!DisplayContent){DisplayContent.Reset();UE_LOG(LogTemp,Error,TEXT("LD_WORLD_LABELS_CONTENT_UNAVAILABLE %s"),*Error);}
}

int32 ULDWorldLabels::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());const UWorld* World=GetWorld();
    const ALDGameState* Public=World?World->GetGameState<ALDGameState>():nullptr;
    FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
    // Nothing is drawn before the display content was tried, so a first frame never flashes fallback ids.
    if(!bDisplayContentTried||!PC||!Public||!Renderer){return Base;}
    const FLDOwnerState& Own=PC->OwnerState;
    // Only the current authorized frame of this match and life, read on every paint: a dead, stale or foreign frame
    // labels nothing, and no unit outlives the frame that lists it (no LastSeen).
    const ALDWorldViewRelay* Relay=Own.LifePhase==ELDLifePhase::Alive?PC->LocalView():nullptr;
    const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const FLDWorldView* Frame=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life?Read.Frame:nullptr;
    int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);
    if(!Frame||Frame->Units.IsEmpty()||ViewW<=0||ViewH<=0){return Base;}
    // Viewport pixels to local units: the layer fills the viewport, so this also removes the DPI scale.
    const FVector2f Size=G.GetLocalSize();const float ToLocalX=Size.X/float(ViewW),ToLocalY=Size.Y/float(ViewH);
    const float TextScale=PC->Controls?PC->Controls->TextScale:1.f;
    const FSlateFontInfo ContenderFont=WorldLabelFont(TEXT("Bold"),15,TextScale),CampFont=WorldLabelFont(TEXT("Regular"),15,TextScale),StatusFont=WorldLabelFont(TEXT("Regular"),13,TextScale);
    const TSharedRef<FSlateFontMeasure> FontMeasure=Renderer->GetFontMeasureService();
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FLDDemoContentDefinitions* Content=DisplayContent?&DisplayContent->Get():nullptr;
    auto Box=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint)
    {FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
    // One centred line whose bottom edge rests on Bottom; Bottom then moves up to the line's top edge.
    auto Line=[&](const FString& Text,const FSlateFontInfo& Font,const FLinearColor& Ink,float CentreX,float& Bottom)
    {
        const FVector2f Extent=FontMeasure->Measure(Text,Font);const FVector2f At(CentreX-Extent.X*.5f,Bottom-Extent.Y);
        FSlateDrawElement::MakeText(E,Base+3,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),Text,Font,ESlateDrawEffect::None,Ink);
        Bottom=At.Y;
    };
    // Labels are measured first and placed lowest-on-screen first; a label that would overlap one already placed moves up
    // above it, so units standing together (a fight, a shared anchor) keep readable names instead of a garbled stack.
    struct FPending
    {
        LDWorldLabels::FLabel Label;
        float CentreX=0.f, Bottom=0.f, Width=0.f, Height=0.f;
    };
    TArray<FPending> Pending;
    for(const FLDObservedUnitView& Unit:Frame->Units)
    {
        // Only a unit whose proxy is presented right now has an anchor: the interpolated pose the player sees.
        FVector Centre;
        if(!Relay->ReadPresentedLocation(Unit.Handle,Unit.ObservationEpoch,Centre)){continue;}
        const bool bContender=Unit.Kind==ELDObservedUnitKind::Contender;
        FPending Entry;
        Entry.Label=LDWorldLabels::Build(Unit,bContender?LDHUD::RosterName(Public->DemoLobby,Unit.ContenderId):FString(),Content,Frame->SampledAtServerSec);
        FVector2D Screen;
        if((Entry.Label.Name.IsEmpty()&&Entry.Label.Status.IsEmpty())||!PC->ProjectWorldLocationToScreen(Centre+FVector(0,0,WorldLabelHeadroomCm(Entry.Label)),Screen)){continue;}
        Entry.CentreX=float(Screen.X)*ToLocalX;Entry.Bottom=float(Screen.Y)*ToLocalY;
        if(Entry.CentreX<-WorldLabelCullMargin||Entry.CentreX>Size.X+WorldLabelCullMargin||Entry.Bottom<0.f||Entry.Bottom>Size.Y+WorldLabelCullMargin){continue;}
        if(!Entry.Label.Name.IsEmpty())
        {
            const FVector2f Name=FontMeasure->Measure(Entry.Label.Name,Entry.Label.bContender?ContenderFont:CampFont);
            Entry.Width=Name.X+2.f;Entry.Height+=Name.Y+(Entry.Label.bContender?6.f:0.f);
        }
        if(!Entry.Label.Status.IsEmpty())
        {
            const FVector2f Status=FontMeasure->Measure(Entry.Label.Status,StatusFont);
            Entry.Width=FMath::Max(Entry.Width,Status.X);Entry.Height+=Status.Y;
        }
        Pending.Add(MoveTemp(Entry));
    }
    Pending.Sort([](const FPending& A,const FPending& B){return A.Bottom>B.Bottom;});
    TArray<FSlateRect> Placed;
    for(FPending& Entry:Pending)
    {
        float Bottom=Entry.Bottom;
        const float Left=Entry.CentreX-Entry.Width*.5f,Right=Entry.CentreX+Entry.Width*.5f;
        for(bool bMoved=true;bMoved;)
        {
            bMoved=false;
            for(const FSlateRect& Other:Placed)
            {
                if(Left<Other.Right&&Right>Other.Left&&Bottom-Entry.Height<Other.Bottom&&Bottom>Other.Top){Bottom=Other.Top-2.f;bMoved=true;}
            }
        }
        Placed.Add(FSlateRect(Left,Bottom-Entry.Height,Right,Bottom));
        const LDWorldLabels::FLabel& Label=Entry.Label;
        const float CentreX=Entry.CentreX;
        if(!Label.Name.IsEmpty())
        {
            if(Label.bContender)
            {
                // Identity tint as a bar under the name: the name itself identifies, the colour is only an extra cue.
                const float Width=FontMeasure->Measure(Label.Name,ContenderFont).X;
                Box(1,FVector2f(CentreX-Width*.5f-1.f,Bottom-5.f),FVector2f(Width+2.f,5.f),WorldLabelRim);
                Box(2,FVector2f(CentreX-Width*.5f,Bottom-4.f),FVector2f(Width,3.f),LDHUD::PaletteColor(Label.Presentation));
                Bottom-=6.f;
            }
            Line(Label.Name,Label.bContender?ContenderFont:CampFont,Label.bContender?WorldLabelContenderInk:WorldLabelCampInk,CentreX,Bottom);
        }
        if(!Label.Status.IsEmpty()){Line(Label.Status,StatusFont,WorldLabelStatusInk,CentreX,Bottom);}
    }
    return Base+3;
}
