#include "UI/LDWorkshop.h"
#include "Algo/Reverse.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/LDCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CapsuleComponent.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDBroadcastStyle.h"
#include "UI/LDBuildRingDecal.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDComponentStorage.h"
#include "UI/LDStatBar.h"
#include "UI/LDTerms.h"
#include "UI/LDUIFonts.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorkshopModel.h"

using namespace LDBuildRing;
using LDRingPaint::FPainter;

namespace
{
// Motion (seconds): the band wipes in clockwise, sector after sector, and everything settles within 200 ms without holding
// back a click or a key. Hover and links are hard cuts.
constexpr double RingOpenTime=.18;
constexpr double RingCloseTime=.12;
constexpr double RingWipeTime=.20;
constexpr double RingWipeStagger=.03;
constexpr double RingRevealTime=.16;
constexpr double RingRevealStagger=.03;
constexpr double RingLandTime=.20;
// The band's ink at its inner rule, at 78% of its outer radius and at the outer rule: one step lighter towards the edge than
// the comp, so the half-disc sits lighter on bright ground.
constexpr float BandInnerInk=.97f,BandMidInk=.94f,BandEdgeInk=.90f;

FLinearColor ElementInk(ELDDemoElement Element)
{
    switch(Element)
    {
    case ELDDemoElement::Fire: return LDBroadcast::Fire;
    case ELDDemoElement::Water: return LDBroadcast::Water;
    case ELDDemoElement::Electric: return LDBroadcast::Shock;
    case ELDDemoElement::Chill: return LDBroadcast::Chill;
    case ELDDemoElement::Rot: return LDBroadcast::Rot;
    default: return LDBroadcast::Mute;
    }
}
FBox2D Square(FVector2D Centre,float Size){return FBox2D(Centre-FVector2D(Size,Size)*.5,Centre+FVector2D(Size,Size)*.5);}
FBox2D Grow(const FBox2D& Box,float By){return FBox2D(Box.Min-FVector2D(By,By),Box.Max+FVector2D(By,By));}
FBox2D Shift(const FBox2D& Box,FVector2D By){return FBox2D(Box.Min+By,Box.Max+By);}
FBox2D BoxAt(FVector2D Min,FVector2D Size){return FBox2D(Min,Min+Size);}
TArray<FVector2D> RectOf(const FBox2D& Box){return {Box.Min,FVector2D(Box.Max.X,Box.Min.Y),Box.Max,FVector2D(Box.Min.X,Box.Max.Y)};}
// A plate whose top-left corner is cut in by Left and bottom-right by Right (the broadcast lean of chips, bars and rows).
TArray<FVector2D> Lean(const FBox2D& Box,float Left,float Right)
{
    return {FVector2D(Box.Min.X+Left,Box.Min.Y),FVector2D(Box.Max.X,Box.Min.Y),FVector2D(Box.Max.X-Right,Box.Max.Y),FVector2D(Box.Min.X,Box.Max.Y)};
}
// The square tile turned 45 degrees (a medal and its socket).
TArray<FVector2D> Diamond(FVector2D Centre,float Half)
{
    return {Centre+FVector2D(0.,-Half),Centre+FVector2D(Half,0.),Centre+FVector2D(0.,Half),Centre+FVector2D(-Half,0.)};
}
// A square rule of Width inside Box.
void Frame(FPainter& P,const FBox2D& Box,float Width,const FLinearColor& Color)
{
    const float W=FMath::Min(Width,float(FMath::Min(Box.GetSize().X,Box.GetSize().Y))*.5f);
    P.Fill(RectOf(FBox2D(Box.Min,FVector2D(Box.Max.X,Box.Min.Y+W))),Color);
    P.Fill(RectOf(FBox2D(FVector2D(Box.Min.X,Box.Max.Y-W),Box.Max)),Color);
    P.Fill(RectOf(FBox2D(FVector2D(Box.Min.X,Box.Min.Y+W),FVector2D(Box.Min.X+W,Box.Max.Y-W))),Color);
    P.Fill(RectOf(FBox2D(FVector2D(Box.Max.X-W,Box.Min.Y+W),FVector2D(Box.Max.X,Box.Max.Y-W))),Color);
}
bool Touches(const FBox2D& A,const FBox2D& B){return A.Min.X<B.Max.X-.5&&B.Min.X<A.Max.X-.5&&A.Min.Y<B.Max.Y-.5&&B.Min.Y<A.Max.Y-.5;}
FBox2D BoundsOf(const TArray<FVector2D>& Points){FBox2D Out(ForceInit);for(const FVector2D& Point:Points){Out+=Point;}return Out;}
float DegreesOf(FVector2D From,FVector2D At){const FVector2D D=At-From;return FMath::RadiansToDegrees(FMath::Atan2(float(D.X),float(-D.Y)));}

UTexture2D* RingArt(EArt Kind)
{
    // Held for the process like the icons: the ring paints them every frame.
    static TStrongObjectPtr<UTexture2D> Loaded[int32(EArt::Count)];
    static bool bWarned[int32(EArt::Count)]={};
    const int32 Index=int32(Kind);
    if(!Loaded[Index].IsValid())
    {
        const FString Package=Art[Index].Package;
        Loaded[Index].Reset(LoadObject<UTexture2D>(nullptr,*(Package+TEXT(".")+FPackageName::GetShortName(Package)),nullptr,LOAD_NoWarn|LOAD_Quiet));
        if(!Loaded[Index].IsValid()&&!bWarned[Index])
        {
            bWarned[Index]=true;
            UE_LOG(LogLDUI,Warning,TEXT("LD_UI_RING_ART_MISSING %s (%s); the ring paints the name instead."),Art[Index].Id,*Package);
        }
    }
    return Loaded[Index].Get();
}
UTexture2D* RingIcon(const FString& IconId){return IconId.IsEmpty()?nullptr:LDUIIcons::Find(IconId);}
// The ring's faces at a design px size times Scale (local units per design px): Source Han Sans Regular, Bold and Heavy, and
// the condensed italic figure face of numbers and keys.
enum class EFace : uint8 { Regular, Bold, Heavy, Figure };
FSlateFontInfo Face(EFace Kind,float DesignPx,float Scale)
{
    // 1 pt = 4/3 px: a design px size reads at 0.75 pt.
    const float Points=FMath::Max(1.f,DesignPx*.75f*Scale);
    switch(Kind)
    {
    case EFace::Regular: return LDUIFonts::Font(LDUIFonts::ERole::Body,Points);
    case EFace::Figure: return LDUIFonts::Font(LDUIFonts::ERole::Figure,Points);
    case EFace::Heavy:
    {
        FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,Points);
        Font.TypefaceFontName=FName(TEXT("Heavy"));
        return Font;
    }
    default: return LDUIFonts::Font(LDUIFonts::ERole::Strong,Points);
    }
}
float WidthOf(const FString& Text,const FSlateFontInfo& Font){return float(LDRingPaint::Measure(Text,Font).X);}
FString Diamonds(int32 Count){FString Out;for(int32 Index=0;Index<Count;++Index){Out+=TEXT("◆");}return Out;}
// One line of Text inside Width, ellipsised.
FString OneLine(const FString& Text,const FSlateFontInfo& Font,float Width)
{
    const TArray<FString> Lines=LDRingPaint::Wrap(Text,Font,Width,1);
    return Lines.IsEmpty()?FString():Lines[0];
}
// A stat chip's size at the radar's corner: its name, its points and (with points to spend) the orange plus.
FVector2D ChipSize(const FStat& Stat,bool bPlus,float F)
{
    return FVector2D(10.f*F+WidthOf(Stat.Name,Face(EFace::Bold,18.f,F))+6.f*F+FMath::Max(16.f*F,WidthOf(FString::FromInt(Stat.Points),Face(EFace::Figure,26.f,F)))
        +(bPlus?6.f*F+22.f*F:0.f)+8.f*F,32.f*F);
}
// The short form name an offer row shows after the card type.
FString FormShort(ELDCardForm Form)
{
    switch(Form)
    {
    case ELDCardForm::Skill: return TEXT("技能");
    case ELDCardForm::Dash: return TEXT("冲刺");
    case ELDCardForm::Medal: return TEXT("勋章");
    default: return TEXT("本体");
    }
}
}

// ---- lifetime ------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> ULDWorkshop::RebuildWidget()
{
    if(!WidgetTree->RootWidget)
    {
        UCanvasPanel* Root=WidgetTree->ConstructWidget<UCanvasPanel>();
        Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        WidgetTree->RootWidget=Root;
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
void ULDWorkshop::NativeDestruct()
{
    if(ALDBuildRadarDecal* Decal=Radar.Get()){Decal->Destroy();}
    Radar=nullptr;
    Super::NativeDestruct();
}
double ULDWorkshop::UiNow() const{return GetWorld()?GetWorld()->GetRealTimeSeconds():0.;}
bool ULDWorkshop::IsOpening() const
{
    const double Now=UiNow();
    return (IsOpen()&&OpenedAt>=0.&&Now-OpenedAt<RingWipeTime+HostCount*RingWipeStagger)||(!IsOpen()&&ClosedAt>=0.&&Now-ClosedAt<RingCloseTime)
        ||(BatchAt>=0.&&Now-BatchAt<RingRevealTime+3*RingRevealStagger);
}
float ULDWorkshop::OpenAmount() const
{
    const double Now=UiNow();
    if(IsOpen())
    {
        const float T=OpenedAt<0.?1.f:FMath::Clamp(float((Now-OpenedAt)/RingOpenTime),0.f,1.f);
        return 1.f-FMath::Square(1.f-T);
    }
    if(ClosedAt<0.){return 0.f;}
    const float T=FMath::Clamp(float((Now-ClosedAt)/RingCloseTime),0.f,1.f);
    return 1.f-T;
}
void ULDWorkshop::Tell(const FString& Text) const{if(!Text.IsEmpty()&&Notify){Notify(Text);}}
void ULDWorkshop::ApplyTextScale(float Scale){TextScale=FMath::Clamp(Scale,.5f,2.f);}
void ULDWorkshop::AllocationApplied(const FLDBuildReceipt&){LDWidgets::PlayCue(this,ELDAudioCue::UIAddPoint);}
void ULDWorkshop::FinalModApplied(const FLDBuildReceipt&,const FLDFinalModSelection&){}
void ULDWorkshop::InterruptFinalFeedback(){}

void ULDWorkshop::Present(const FInput& Input,const FGeometry*)
{
    const EMode Previous=Mode;
    Mode=Input.bShown?Input.Mode:EMode::Closed;
    if(Previous!=EMode::Open&&Mode==EMode::Open){OpenedAt=UiNow();ClosedAt=-1.;LDWidgets::PlayCue(this,ELDAudioCue::UIWorkshopOpen);}
    if(Previous==EMode::Open&&Mode!=EMode::Open)
    {
        ClosedAt=UiNow();Selected=NAME_None;SelectedBlueprint=INDEX_NONE;bShowHand=false;bMatrix=false;
        LDWidgets::PlayCue(this,ELDAudioCue::UIWorkshopClose);
    }
    Latest=Input;
    if(Input.Progression){if(!LDHUD::SameProgression(View0,*Input.Progression)){View0=*Input.Progression;}}
    else if(View0.bValid){View0=FLDProgressionView();}
    Rebuild();
    const bool bVisible=Input.bShown&&Ring.bValid;
    bWantShown=bVisible;
    ApplyVisibility();
    if(!bVisible&&Radar){Radar->Hide();}
}
void ULDWorkshop::ApplyVisibility()
{
    const ESlateVisibility Wanted=!bWantShown?ESlateVisibility::Collapsed:bTakesPointer?ESlateVisibility::Visible:ESlateVisibility::HitTestInvisible;
    if(GetVisibility()!=Wanted){SetVisibility(Wanted);}
}
void ULDWorkshop::Rebuild()
{
    FInputs In;
    In.Progression=&View0;In.Storage=&StorageView;In.Content=Latest.Content;In.Attributes=&Latest.Attributes;
    In.Stats=Latest.Stats;In.Keys=Latest.Keys;In.DashKey=Latest.DashKey;
    In.bRestShop=Latest.Stage==LDProgression::EMatchStage::Rest;
    In.Unspent=StatBar.IsValid()&&View0.bValid?StatBar->EffectiveUnspent():FMath::Max<int64>(0,View0.UnspentPoints);
    if(StatBar.IsValid()&&Latest.Content)
    {
        const int32 Count=Latest.Content->StatsInDesignOrder().Num();
        for(int32 Index=0;Index<Count;++Index){In.Points.Add(StatBar->EffectivePoints(Index));}
    }
    // The model is a pure function of these inputs: an unchanged sample keeps the last one. The library's own model follows
    // the view, the catalog and its unseen marks, so the marks stand in for it.
    const LDHUD::FStorageView* Library=Storage.IsValid()?&Storage->Model():nullptr;
    bool bSame=ModelKey.bBuilt&&ModelKey.Content==In.Content&&ModelKey.bRestShop==In.bRestShop&&ModelKey.Unspent==In.Unspent
        &&ModelKey.bOwnStorage==(Library==nullptr)&&ModelKey.Points==In.Points&&ModelKey.Stats==In.Stats&&ModelKey.Keys==In.Keys
        &&ModelKey.DashKey==In.DashKey&&LDHUD::SameAttributes(ModelKey.Attributes,Latest.Attributes)&&LDHUD::SameProgression(ModelKey.View,View0);
    if(bSame&&Library)
    {
        bSame=Library->Stored.Num()==ModelKey.StoredCards.Num();
        for(int32 Index=0;bSame&&Index<Library->Stored.Num();++Index)
        {bSame=Library->Stored[Index].Card==ModelKey.StoredCards[Index]&&Library->Stored[Index].bFresh==ModelKey.StoredFresh[Index];}
    }
    if(bSame)
    {
        for(auto It=Landed.CreateIterator();It;++It){if(UiNow()-It.Value()>RingLandTime){It.RemoveCurrent();}}
        return;
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDWorkshop_BuildModel);
        StorageView=Library?*Library:LDHUD::CardStorage(View0,Latest.Content);
        Ring=Build(In);
        ++ModelSerial;
        ModelKey.bBuilt=true;ModelKey.View=View0;ModelKey.Content=In.Content;ModelKey.Attributes=Latest.Attributes;ModelKey.Stats=In.Stats;
        ModelKey.Keys=In.Keys;ModelKey.DashKey=In.DashKey;ModelKey.Points=In.Points;ModelKey.Unspent=In.Unspent;ModelKey.bRestShop=In.bRestShop;
        ModelKey.bOwnStorage=Library==nullptr;
        ModelKey.StoredCards.Reset();ModelKey.StoredFresh.Reset();
        if(Library){for(const LDHUD::FStoredView& Each:Library->Stored){ModelKey.StoredCards.Add(Each.Card);ModelKey.StoredFresh.Add(Each.bFresh);}}
    }
    // A selection the view no longer holds is dropped (taken, sold or consumed elsewhere).
    if(!Selected.IsNone()&&!Ring.Hand.ContainsByPredicate([this](const FCard& Card){return Card.Key==Selected;})){Selected=NAME_None;}
    if(Ring.Batch.Kind!=EBatch::Blueprint){SelectedBlueprint=INDEX_NONE;}
    if(!Ring.Batch.Options.IsValidIndex(SelectedBlueprint)){SelectedBlueprint=INDEX_NONE;}
    // The reveal plays whenever another batch comes on top.
    uint32 Key=uint32(Ring.Batch.Kind);
    for(const FOption& Option:Ring.Batch.Options){Key=HashCombine(Key,GetTypeHash(Option.Id));}
    if(Key!=BatchKey){BatchKey=Key;BatchAt=Ring.Batch.Kind==EBatch::None?-1.:UiNow();if(Ring.Batch.Kind!=EBatch::None){LDWidgets::PlayCue(this,ELDAudioCue::UIOfferDeal);}}
    if(Ring.Batch.Kind==EBatch::None){bShowHand=false;}
    // Install feedback: a key newly in a host lands with a flash.
    TSet<FName> Installed;
    for(const FHost& Host:Ring.Hosts)
    {
        for(const FCard& Card:Host.Cards){for(const FName& Each:Card.Keys){Installed.Add(Each);}}
        if(Host.bHasMedal){Installed.Add(Host.Medal.Key);}
    }
    if(Ring.bValid&&!KnownInstalled.IsEmpty()){for(const FName& Each:Installed){if(!KnownInstalled.Contains(Each)){Landed.Add(Each,UiNow());}}}
    if(Ring.bValid){KnownInstalled=MoveTemp(Installed);}
    for(auto It=Landed.CreateIterator();It;++It){if(UiNow()-It.Value()>RingLandTime){It.RemoveCurrent();}}
}

// ---- layout --------------------------------------------------------------------------------------------------------------

bool ULDWorkshop::OnBand(FVector2D Point,float Margin) const
{
    const FLayout& L=Layout;
    const float Radius=float(FVector2D::Distance(Point,L.Centre));
    if(Radius<1.f){return false;}
    const float Degrees=DegreesOf(L.Centre,Point);
    const float Slack=FMath::RadiansToDegrees(Margin/Radius);
    for(int32 Host=0;Host<HostCount;++Host)
    {
        if(FMath::Abs(Degrees-HostAngle[Host])<=SectorHalf+Slack&&Radius>=SectorInner*L.R-Margin&&Radius<=L.Outer[Host]+TickLength*L.R+Margin){return true;}
    }
    return false;
}

void ULDWorkshop::ComputeLayout(const FGeometry& Geometry)
{
    FLayout& L=Layout;
    L.Boxes.Reset();L.Anchors.Reset();L.Segments.Reset();HandFaces.Reset();OfferFaces.Reset();
    L.Card=FBox2D(ForceInit);L.Bar=FBox2D(ForceInit);L.RowTop=0.f;
    L.Size=Geometry.GetLocalSize();
    if(L.Size.X<2.||L.Size.Y<2.){L.Size=FVector2D(1920.,1080.);}
    L.K=FMath::Max(.01f,Geometry.Scale);
    const FScale S=ScaleFor(float(L.Size.Y)*L.K,TextScale);
    L.U=S.Layout/L.K;L.R=S.Ring/L.K;L.F=S.Text/L.K;
    const float U=L.U,R=L.R,F=L.F;
    // The ring's centre: the own feet on screen, held near the approved composition so the band never meets the side columns.
    FVector2D Wanted(L.Size.X*.5,585.*U);
    if(bHasCentre){Wanted=SmoothedCentre;}
    L.Centre=FVector2D(FMath::Clamp(Wanted.X,L.Size.X*.5-60.*U,L.Size.X*.5+60.*U),FMath::Clamp(Wanted.Y,545.*U,615.*U));
    const FVector2D C=L.Centre;
    ActionsAt=FVector2D(L.Size.X-40.f*U,L.Size.Y-48.f*U);
    const auto Local=[&Geometry](const FSlateRect& Rect)
    {
        return Rect.Right>Rect.Left&&Rect.Bottom>Rect.Top?FBox2D(Geometry.AbsoluteToLocal(FVector2D(Rect.Left,Rect.Top)),Geometry.AbsoluteToLocal(FVector2D(Rect.Right,Rect.Bottom))):FBox2D(ForceInit);
    };
    const FBox2D Band=Local(Latest.Band);
    const auto Add=[&L](EHit Kind,int32 Host,int32 Index,TArray<FVector2D> Shape){FShape& Box=L.Boxes.AddDefaulted_GetRef();Box.Hit={Kind,Host,Index};Box.Shape=MoveTemp(Shape);};
    if(!Ring.bValid){return;}
    if(!IsOpen())
    {
        // Closed: the build entry right of the HUD band, the offer on top waiting over it.
        const float X0=Band.bIsValid?float(Band.Max.X)+24.f*U:1490.f*U;
        const FSlateFontInfo Label=Face(EFace::Bold,18.f,F),Key=Face(EFace::Figure,22.f,F);
        const FString OpenKey=Latest.OpenKey.IsEmpty()?FString(TEXT("B")):Latest.OpenKey;
        float W=14.f*F+WidthOf(TEXT("构筑"),Label)+8.f*F+WidthOf(OpenKey,Key)+14.f*F+14.f*F;
        if(Ring.Unspent>0){W+=WidthOf(FString::Printf(TEXT("◆%lld"),Ring.Unspent),Face(EFace::Figure,26.f,F))+12.f*F;}
        if(Ring.Fresh>0){W+=WidthOf(FString::Printf(TEXT("新 %d"),Ring.Fresh),Label)+16.f*F+10.f*F;}
        const float H=40.f*F;
        const FBox2D Entry(FVector2D(X0,L.Size.Y-28.f*U-H),FVector2D(X0+W,L.Size.Y-28.f*U));
        Add(EHit::Entry,INDEX_NONE,INDEX_NONE,RectOf(Entry));
        const float Z=NodeSize*R;
        for(int32 Index=0;Index<Ring.Batch.Options.Num();++Index)
        {
            const FBox2D Tile=BoxAt(FVector2D(X0+Index*(Z+12.f*U),Entry.Min.Y-14.f*U-Z),FVector2D(Z,Z));
            Add(EHit::Offer,INDEX_NONE,Index,RectOf(Tile));
            FFace& Face0=OfferFaces.AddDefaulted_GetRef();Face0.Hit={EHit::Offer,INDEX_NONE,Index};Face0.Box=Tile;
        }
        return;
    }
    // The sectors: the band (drop target), the installed cards and the open place, the host and its element socket.
    const bool bFull=Ring.Capacity>0&&Ring.Installed>=Ring.Capacity;
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num()&&HostIndex<HostCount;++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        const float A=HostAngle[HostIndex];
        const int32 Slots=Host.Cards.Num()+(bFull?0:1);
        L.Outer[HostIndex]=OuterFor(Slots)*R;
        {
            FShape& Box=L.Boxes.AddDefaulted_GetRef();Box.Hit={EHit::Drop,HostIndex,INDEX_NONE};Box.Centre=C;Box.bWedge=true;
            Box.Wedge=FVector4f(SectorInner*R,L.Outer[HostIndex],A-SectorHalf,A+SectorHalf);
        }
        for(int32 Index=0;Index<Slots;++Index)
        {
            const FVector2D Polar=SlotPolar(Index,A);
            const FVector2D At=LDRingPaint::Polar(C,float(Polar.X)*R,float(Polar.Y));
            if(Index<Host.Cards.Num())
            {
                Add(EHit::Installed,HostIndex,Index,RectOf(Square(At,NodeSize*R)));
                L.Anchors.Add(AnchorKey(EHit::Installed,HostIndex,Index),At);
            }
            else{Add(EHit::Drop,HostIndex,Index,RectOf(Square(At,NodeSize*R)));}
        }
        const FVector2D HostAt=LDRingPaint::Polar(C,HostRadius*R,A);
        Add(EHit::Host,HostIndex,INDEX_NONE,RectOf(Square(HostAt,HostSize*R)));
        L.Anchors.Add(AnchorKey(EHit::Host,HostIndex,INDEX_NONE),HostAt);
        if(Host.bSocket){Add(EHit::Gem,HostIndex,INDEX_NONE,Diamond(LDRingPaint::Polar(C,GemRadius*R,A),GemSize*.5f*R));}
    }
    // The stat chips at the radar's corners (the only place to add a point): on the ground decal when it is laid, else
    // around the centre.
    for(int32 Index=0;Index<Ring.Stats.Num();++Index)
    {
        const float Degrees=Index*360.f/FMath::Max(1,Ring.Stats.Num());
        const float Radius=Index==0?ChipRadius-2.f:ChipRadius+6.f;
        FVector2D At=LDRingPaint::Polar(C,Radius*R,Degrees);
        if(Radar&&Radar->IsShown())
        {
            FVector2D Screen;
            if(UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),Radar->WorldAt(Radius,Degrees),Screen,false)){At=Screen;}
        }
        // The upper side chips lean towards the centre, clear of the sockets.
        At.X+=(Index==1?-10.f:Index==Ring.Stats.Num()-1?10.f:0.f)*R;
        const FVector2D Size=ChipSize(Ring.Stats[Index],Ring.Unspent>0,F);
        L.Anchors.Add(AnchorKey(EHit::Stat,INDEX_NONE,Index),At);
        Add(EHit::Stat,INDEX_NONE,Index,Lean(FBox2D(At-Size*.5,At+Size*.5),0.f,0.f));
    }
    // The build bar docks on the HUD band: points, capacity, library, then the commands.
    {
        const float BandTop=Band.bIsValid?float(Band.Min.Y):L.Size.Y-125.f*U;
        const float X0=Band.bIsValid?float(Band.Min.X)+19.f*U:486.f*U;
        const float H=46.f*F;
        const float Bottom=FMath::Min(926.f*U,BandTop-10.f*U);
        const FSlateFontInfo Label=Face(EFace::Bold,18.f,F),Figure=Face(EFace::Figure,32.f,F),Small=Face(EFace::Figure,22.f,F),Word=Face(EFace::Bold,20.f,F);
        const float Pad=18.f*F,Gap=6.f*U;
        float X=X0;
        const auto Segment=[&](float Width){const FBox2D Box(FVector2D(X,Bottom-H),FVector2D(X+Width,Bottom));L.Segments.Add(Box);X+=Width+Gap;return Box;};
        Segment(Pad+WidthOf(TEXT("属性点"),Label)+10.f*F+WidthOf(TEXT("◆"),Small)+WidthOf(FString::Printf(TEXT("%lld"),Ring.Unspent),Figure)+Pad);
        Segment(Pad+WidthOf(TEXT("构筑"),Label)+10.f*F+WidthOf(FString::FromInt(Ring.Installed),Figure)+WidthOf(FString::Printf(TEXT("/%d"),Ring.Capacity),Small)
            +10.f*F+Ring.Capacity*12.f*F+Pad);
        const FBox2D Library=Segment(Pad+WidthOf(TEXT("牌库"),Label)+10.f*F+WidthOf(FString::FromInt(Ring.Hand.Num()),Figure)+Pad);
        Add(EHit::HandPile,INDEX_NONE,INDEX_NONE,RectOf(Library));
        const bool bRespec=Latest.Stage==LDProgression::EMatchStage::Rest;
        const FString Key=Latest.OpenKey.IsEmpty()?FString(TEXT("B")):Latest.OpenKey;
        const float CloseW=WidthOf(TEXT("收起"),Word)+6.f*F+WidthOf(Key,Small)+14.f*F;
        const FBox2D Commands=Segment(Pad+(bRespec?WidthOf(TEXT("洗点"),Word)+18.f*F:0.f)+WidthOf(TEXT("反应表"),Word)+18.f*F+CloseW+Pad);
        float CX=float(Commands.Min.X)+Pad*.5f;
        const auto Command=[&](EHit Kind,float Width){Add(Kind,INDEX_NONE,INDEX_NONE,RectOf(FBox2D(FVector2D(CX,Commands.Min.Y),FVector2D(CX+Width+Pad*.5f+9.f*F,Commands.Max.Y))));CX+=Width+18.f*F;};
        if(bRespec){Command(EHit::Respec,WidthOf(TEXT("洗点"),Word));}
        Command(EHit::Matrix,WidthOf(TEXT("反应表"),Word));
        Command(EHit::Close,CloseW-14.f*F);
        L.Bar=FBox2D(L.Segments[0].Min,L.Segments.Last().Max);
    }
    // The rest actions stand beside the band; when they are too wide for that corner they rise over the bar's line.
    if(Latest.Actions.X>0.f&&Band.bIsValid&&float(ActionsAt.X)-float(Latest.Actions.X)<float(Band.Max.X)+12.f*U)
    {
        ActionsAt.Y=float(L.Bar.Min.Y)-8.f*U;
    }
    LayoutTable();
    LayoutShop();
    LayoutRow();
    // The element chooser of a card that asks for one, and a rest service's confirm line, over the ring's top.
    if(Storage.IsValid()&&Storage->IsChoosing())
    {
        const TArray<ELDDemoElement> Elements=Storage->ChooserElements();
        const float W=64.f*F;
        const float X0=float(C.X)-Elements.Num()*(W+6.f*U)*.5f;
        for(int32 Index=0;Index<Elements.Num();++Index)
        {
            const FVector2D At(X0+Index*(W+6.f*U),48.f*U);
            Add(EHit::Element,INDEX_NONE,Index,RectOf(FBox2D(At,At+FVector2D(W,32.f*F))));
        }
    }
    if(Choices.IsValid()&&Choices->HasChildConfirmation())
    {
        const FVector2D At(float(C.X)+5.f*F,16.f*U+40.f*F);
        Add(EHit::ConfirmYes,INDEX_NONE,INDEX_NONE,RectOf(FBox2D(At-FVector2D(96.f*F,0.f),At+FVector2D(-5.f*F,32.f*F))));
        Add(EHit::ConfirmNo,INDEX_NONE,INDEX_NONE,RectOf(FBox2D(At+FVector2D(5.f*F,0.f),At+FVector2D(96.f*F,32.f*F))));
    }
    LayoutCard();
}

float ULDWorkshop::Refit(FFitMemo& Memo,TConstArrayView<float> From,TFunctionRef<float()> Fit)
{
    if(Memo.From.Num()==From.Num()&&FMemory::Memcmp(Memo.From.GetData(),From.GetData(),From.Num()*sizeof(float))==0){return Memo.Scale;}
    Memo.From=From;
    Memo.Scale=Fit();
    return Memo.Scale;
}
void ULDWorkshop::LayoutTable()
{
    FLayout& L=Layout;
    const float U=L.U,F=L.F;
    // Each stat's attributes (the real per-point mapping), as one aligned grid of columns.
    TArray<TArray<int32>> Rows;Rows.SetNum(Ring.Stats.Num());
    for(int32 Index=0;Index<Ring.Derived.Num();++Index){if(Rows.IsValidIndex(Ring.Derived[Index].Source)){Rows[Ring.Derived[Index].Source].Add(Index);}}
    L.TableColumns=2;
    for(const TArray<int32>& Row:Rows){L.TableColumns=FMath::Max(L.TableColumns,Row.Num());}
    // Natural sizes in design px (measured at the text scale).
    float NameW=36.f,SubW=80.f;
    for(const FStat& Stat:Ring.Stats)
    {
        NameW=FMath::Max(NameW,FMath::Max(WidthOf(Stat.Name,Face(EFace::Bold,18.f,F)),WidthOf(FString::FromInt(Stat.Points),Face(EFace::Figure,38.f,F)))/F);
    }
    for(const FDerived& Row:Ring.Derived)
    {
        SubW=FMath::Max(SubW,FMath::Max(WidthOf(Row.Name,Face(EFace::Regular,18.f,F)),WidthOf(Row.Value,Face(EFace::Figure,30.f,F)))/F+10.f);
    }
    L.TableSubX=FMath::Max(136.f,80.f+NameW+16.f);
    L.TableSubW=SubW;
    const float Width=L.TableSubX+L.TableColumns*SubW+14.f;
    const float Height=36.f+Rows.Num()*84.f-8.f;
    // Fitted beside the ring's band and above the minimap and the offer row; the text never drops under 12 px.
    const float Floor=12.f/(18.f*F*L.K);
    float Limit=L.Bar.bIsValid?float(L.Bar.Min.Y)-12.f*U:float(L.Size.Y);
    if(Latest.Map.Bottom>Latest.Map.Top){Limit=FMath::Min(Limit,float(GetCachedGeometry().AbsoluteToLocal(FVector2D(Latest.Map.Left,Latest.Map.Top)).Y)-12.f*U);}
    const float Top=252.f*U;
    const auto Fit=[&](float Left)
    {
        float Scale=1.f;
        for(int32 Pass=0;Pass<3;++Pass)
        {
            const float S=F*Scale;
            float Room=TNumericLimits<float>::Max();
            for(int32 Row=0;Row<Rows.Num();++Row)
            {
                const float Y0=Top+(36.f+Row*84.f)*S,Y1=Y0+76.f*S;
                for(const float Y:{Y0,(Y0+Y1)*.5f,Y1})
                {
                    // The band's first point from the left on this line.
                    for(float X=Left;X<float(L.Centre.X);X+=2.f*U){if(OnBand(FVector2D(X,Y),0.f)){Room=FMath::Min(Room,X-8.f*U-Left);break;}}
                }
            }
            Scale=FMath::Min3(1.f,Room/(Width*F),(Limit-Top)/(Height*F));
        }
        return Scale;
    };
    // Everything Fit and OnBand read, so an unchanged frame keeps its scale.
    const auto Fitted=[&](FFitMemo& Memo,float At)
    {
        const float From[]={At,Top,F,U,L.R,float(L.Centre.X),float(L.Centre.Y),L.Outer[0],L.Outer[1],L.Outer[2],L.Outer[3],L.Outer[4],Width,Height,Limit,float(Rows.Num())};
        return Refit(Memo,From,[&](){return Fit(At);});
    };
    float Left=44.f*U;
    float Scale=Fitted(TableFit[0],Left);
    if(Scale<Floor){Left=28.f*U;Scale=Fitted(TableFit[1],Left);}
    Scale=FMath::Max(Scale,Floor);
    L.Table=FVector2D(Left,Top);L.TableScale=F*Scale;L.TableWidth=Width*L.TableScale;
    const float S=L.TableScale;
    for(int32 Row=0;Row<Rows.Num();++Row)
    {
        const float Y=Top+(36.f+Row*84.f)*S;
        FShape& Head=L.Boxes.AddDefaulted_GetRef();Head.Hit={EHit::Wing,Row,INDEX_NONE};
        Head.Shape=RectOf(FBox2D(FVector2D(Left,Y),FVector2D(Left+L.TableSubX*S,Y+76.f*S)));
        L.Anchors.Add(AnchorKey(EHit::Wing,Row,INDEX_NONE),FVector2D(Left+L.TableWidth,Y+38.f*S));
        for(int32 Column=0;Column<Rows[Row].Num();++Column)
        {
            FShape& Cell=L.Boxes.AddDefaulted_GetRef();Cell.Hit={EHit::Wing,Row,Rows[Row][Column]};
            const float X=Left+(L.TableSubX+Column*SubW)*S;
            Cell.Shape=RectOf(FBox2D(FVector2D(X,Y),FVector2D(X+SubW*S,Y+76.f*S)));
        }
    }
}

void ULDWorkshop::LayoutShop()
{
    FLayout& L=Layout;
    L.ShopWidth=0.f;
    if(!Ring.bShop){return;}
    const float U=L.U,F=L.F;
    float NameW=0.f,PriceW=0.f;
    for(const FShopItem& Item:Ring.Shop)
    {
        NameW=FMath::Max(NameW,FMath::Max(WidthOf(Item.Name,Face(EFace::Bold,22.f,F)),WidthOf(Item.Note,Face(EFace::Regular,18.f,F))));
        PriceW=FMath::Max(PriceW,WidthOf(Item.bSold?FString(TEXT("已买")):Diamonds(Item.Price),Face(EFace::Bold,22.f,F)));
    }
    const float Width=FMath::Max(400.f,90.f+NameW/F+16.f+PriceW/F+16.f);
    const float Height=44.f+Ring.Shop.Num()*64.f-6.f;
    const float Floor=12.f/(18.f*F*L.K);
    float Top=252.f*U;
    // Above the rest actions in the bottom-right corner.
    const float Limit=float(ActionsAt.Y)-FMath::Max(66.f*U,float(Latest.Actions.Y))-12.f*U;
    const auto Fit=[&](float Right,float From)
    {
        float Scale=1.f;
        for(int32 Pass=0;Pass<3;++Pass)
        {
            const float S=F*Scale;
            float Room=TNumericLimits<float>::Max();
            for(int32 Row=0;Row<Ring.Shop.Num();++Row)
            {
                const float Y0=From+(44.f+Row*64.f)*S,Y1=Y0+58.f*S;
                for(const float Y:{Y0,(Y0+Y1)*.5f,Y1})
                {
                    for(float X=Right;X>float(L.Centre.X);X-=2.f*U){if(OnBand(FVector2D(X,Y),0.f)){Room=FMath::Min(Room,Right-X-8.f*U);break;}}
                }
            }
            Scale=FMath::Min3(1.f,Room/(Width*F),(Limit-From)/(Height*F));
        }
        return Scale;
    };
    const auto Fitted=[&](FFitMemo& Memo,float At,float From0)
    {
        const float From[]={At,From0,F,U,L.R,float(L.Centre.X),float(L.Centre.Y),L.Outer[0],L.Outer[1],L.Outer[2],L.Outer[3],L.Outer[4],Width,Height,Limit,float(Ring.Shop.Num())};
        return Refit(Memo,From,[&](){return Fit(At,From0);});
    };
    float Right=float(L.Size.X)-80.f*U;
    float Scale=Fitted(ShopFit[0],Right,Top);
    // The comp's column ends 80 px from the edge; only a clearly squeezed shelf takes the outer 40 px.
    if(Scale<.9f){Right=float(L.Size.X)-40.f*U;Scale=Fitted(ShopFit[1],Right,Top);}
    // A long shelf over risen rest actions starts higher, under the top corner.
    if(Scale<Floor){Top=140.f*U;Scale=Fitted(ShopFit[2],Right,Top);}
    Scale=FMath::Max(Scale,Floor);
    const float S=F*Scale;
    L.ShopScale=S;L.ShopWidth=Width*S;
    L.Shop=FVector2D(Right-L.ShopWidth,Top);
    for(int32 Row=0;Row<Ring.Shop.Num();++Row)
    {
        const FBox2D Box(FVector2D(L.Shop.X,Top+(44.f+Row*64.f)*S),FVector2D(Right,Top+(44.f+Row*64.f+58.f)*S));
        FShape& Shape=L.Boxes.AddDefaulted_GetRef();Shape.Hit={EHit::Shop,INDEX_NONE,Row};Shape.Shape=Lean(Box,14.f*S,0.f);
        L.Anchors.Add(AnchorKey(EHit::Shop,INDEX_NONE,Row),FVector2D(Box.Min.X+49.f*S,Box.GetCenter().Y));
    }
    // 刷新 beside the heading.
    const float HeadW=WidthOf(TEXT("休息商店"),Face(EFace::Heavy,22.f,S))+12.f*S;
    const FString Refresh=Ring.RefreshPrice>0?FString::Printf(TEXT("刷新 · %s"),*Diamonds(Ring.RefreshPrice)):FString(TEXT("刷新 · 免费"));
    const float RefreshW=WidthOf(Refresh,Face(EFace::Bold,18.f,S))+28.f*S;
    FShape& Button=L.Boxes.AddDefaulted_GetRef();Button.Hit={EHit::ShopRefresh,INDEX_NONE,INDEX_NONE};
    Button.Shape=Lean(BoxAt(FVector2D(L.Shop.X+HeadW,Top),FVector2D(RefreshW,30.f*S)),9.f*S,9.f*S);
}

void ULDWorkshop::LayoutRow()
{
    FLayout& L=Layout;
    const float U=L.U,R=L.R,F=L.F;
    const float Bottom=float(L.Bar.Min.Y)-10.f*U;
    const float X0=float(L.Bar.Min.X);
    // The row ends left of the shop where the two would meet.
    float Limit=float(L.Size.X)-40.f*U;
    if(L.ShopWidth>0.f){Limit=FMath::Min(Limit,float(L.Shop.X)-12.f*U);}
    // Risen rest actions share the row's line.
    if(Latest.Actions.X>0.f&&ActionsAt.Y<L.Bar.Min.Y){Limit=FMath::Min(Limit,float(ActionsAt.X-Latest.Actions.X)-12.f*U);}
    const auto Add=[&L](EHit Kind,int32 Host,int32 Index,TArray<FVector2D> Shape){FShape& Box=L.Boxes.AddDefaulted_GetRef();Box.Hit={Kind,Host,Index};Box.Shape=MoveTemp(Shape);};
    const FSlateFontInfo Chip=Face(EFace::Bold,18.f,F);
    const float ChipH=30.f*F;
    // A header chip right-aligned at Right: returns its left edge.
    const auto RightChip=[&](EHit Kind,const FString& Text,float Right,float Top){const float W=WidthOf(Text,Chip)+24.f*F;Add(Kind,INDEX_NONE,INDEX_NONE,Lean(FBox2D(FVector2D(Right-W,Top),FVector2D(Right,Top+ChipH)),7.f*F,7.f*F));return Right-W-6.f*U;};
    if(Ring.Batch.Kind!=EBatch::None&&!bShowHand)
    {
        const int32 Count=Ring.Batch.Options.Num();
        const float Gap=12.f*U;
        const float W=FMath::Min(300.f*U,(Limit-X0-(Count-1)*Gap)/FMath::Max(1,Count));
        const float H=FMath::Max(86.f*U,22.f*U+24.f*F*1.25f+18.f*F*1.3f);
        const float Top=Bottom-H;
        for(int32 Index=0;Index<Count;++Index)
        {
            FFace& Face0=OfferFaces.AddDefaulted_GetRef();
            Face0.Hit={EHit::Offer,INDEX_NONE,Index};
            Face0.Box=FBox2D(FVector2D(X0+Index*(W+Gap),Top),FVector2D(X0+Index*(W+Gap)+W,Bottom));
            Add(EHit::Offer,INDEX_NONE,Index,Lean(Face0.Box,12.f*U,12.f*U));
            L.Anchors.Add(AnchorKey(EHit::Offer,INDEX_NONE,Index),FVector2D(Face0.Box.Min.X+(66.f*U+32.f*U),Face0.Box.GetCenter().Y));
        }
        const float HeadTop=Top-6.f*U-ChipH;
        L.RowTop=HeadTop;
        float Right=X0+Count*(W+Gap)-Gap;
        if(Ring.Batch.Queued>0){Right=RightChip(EHit::OfferPile,FString::Printf(TEXT("还有 %d 批"),Ring.Batch.Queued),Right,HeadTop);}
        if(Ring.Batch.Kind==EBatch::Card)
        {
            Right=RightChip(EHit::Skip,TEXT("跳过"),Right,HeadTop);
            RightChip(EHit::Reroll,FString::Printf(TEXT("重抽 ×%d"),Ring.Batch.Rerolls),Right,HeadTop);
        }
        else if(Ring.Batch.Kind==EBatch::Active&&Ring.Batch.SkipPoints>0){RightChip(EHit::Skip,FString::Printf(TEXT("跳过 +%lld"),Ring.Batch.SkipPoints),Right,HeadTop);}
        return;
    }
    if(Ring.Hand.IsEmpty()&&!bShowHand){L.RowTop=Bottom;return;}
    // The library: one tile per stored group with its name under it; a long library drops the names (the card says them).
    const float Z=NodeSize*R;
    const float NameH=24.f*F;
    float Pitch=FMath::Max(Z+12.f*U,84.f*F);
    const int32 Count=Ring.Hand.Num();
    if(Count>0&&Count*Pitch>Limit-X0){Pitch=FMath::Max(Z+4.f*U,(Limit-X0)/Count);}
    const float Top=Bottom-Z-6.f*U-NameH;
    for(int32 Index=0;Index<Count;++Index)
    {
        FFace& Face0=HandFaces.AddDefaulted_GetRef();
        Face0.Hit={EHit::Hand,INDEX_NONE,Index};
        const float Mid=X0+Index*Pitch+Pitch*.5f;
        Face0.Box=Square(FVector2D(Mid,Top+Z*.5f),Z);
        Add(EHit::Hand,INDEX_NONE,Index,RectOf(Face0.Box));
        L.Anchors.Add(AnchorKey(EHit::Hand,INDEX_NONE,Index),Face0.Box.GetCenter());
    }
    // No header (the bar's 牌库 part is lit while the library shows): the row's top clears a raised card and its 新 tab,
    // and the way back to the offer stands after the last tile.
    L.RowTop=Top-18.f*U;
    if(Ring.Batch.Kind!=EBatch::None)
    {
        const float W=WidthOf(FString::Printf(TEXT("待选 %d"),Ring.Batch.Queued+1),Chip)+24.f*F;
        const float X=FMath::Min(X0+Count*Pitch+12.f*U,Limit-W);
        RightChip(EHit::OfferPile,FString::Printf(TEXT("待选 %d"),Ring.Batch.Queued+1),X+W,Top+(Z-ChipH)*.5f);
    }
}

void ULDWorkshop::LayoutCard()
{
    FLayout& L=Layout;
    L.Card=FBox2D(ForceInit);
    if(!IsOpen()){return;}
    const FInfo Info=InfoOf(Focus());
    if(Info.Name.IsEmpty()){return;}
    const float U=L.U,F=L.F;
    const float W=FMath::Min(720.f*U,float(L.Size.X)-32.f*U);
    const float Icon=72.f*U;
    const float TextW=W-(18.f*U+Icon+16.f*U)-30.f*U;
    const int32 Lines=LDRingPaint::Wrap(Info.Body,Face(EFace::Regular,20.f,F),TextW,3).Num();
    const float H=FMath::Max(Icon+28.f*U,12.f*U+34.f*F+26.f*F+(Info.Gain.IsEmpty()?0.f:26.f*F)+Lines*26.f*F+12.f*U);
    // Above the ring's top; where that would cover the hovered part or the HUD's toast, in the top-right corner.
    FBox2D Card=BoxAt(FVector2D(FMath::Clamp(float(L.Centre.X)-W*.5f,16.f*U,float(L.Size.X)-16.f*U-W),8.f*U),FVector2D(W,H));
    const FHit Shown=Focus();
    const FGeometry& G=GetCachedGeometry();
    const FBox2D Toast=Latest.Toast.Right>Latest.Toast.Left?FBox2D(G.AbsoluteToLocal(FVector2D(Latest.Toast.Left,Latest.Toast.Top)),G.AbsoluteToLocal(FVector2D(Latest.Toast.Right,Latest.Toast.Bottom))):FBox2D(ForceInit);
    const FShape* Hovered0=L.Boxes.FindByPredicate([&Shown](const FShape& Box){return Box.Hit==Shown&&!Box.bWedge;});
    if((Hovered0&&Touches(Card,BoundsOf(Hovered0->Shape)))||(Toast.bIsValid&&Touches(Card,Toast)))
    {
        Card=BoxAt(FVector2D(float(L.Size.X)-40.f*U-W,8.f*U),FVector2D(W,H));
    }
    L.Card=Card;
}

ULDWorkshop::FHit ULDWorkshop::HitAt(FVector2D Local) const
{
    for(int32 Index=Layout.Boxes.Num()-1;Index>=0;--Index)
    {
        const FShape& Box=Layout.Boxes[Index];
        if(Box.bWedge)
        {
            const float Radius=float(FVector2D::Distance(Local,Box.Centre));
            const float Degrees=DegreesOf(Box.Centre,Local);
            if(Radius>=Box.Wedge.X&&Radius<=Box.Wedge.Y&&Degrees>=Box.Wedge.Z&&Degrees<=Box.Wedge.W){return Box.Hit;}
            continue;
        }
        if(LDRingPaint::Inside(Box.Shape,Local)){return Box.Hit;}
    }
    return FHit();
}
bool ULDWorkshop::IsHit(FVector2D Absolute) const
{
    if(!IsVisible()||!Ring.bValid){return false;}
    const FGeometry& Geometry=GetCachedGeometry();
    return !HitAt(Geometry.AbsoluteToLocal(Absolute)).IsNone();
}

TArray<FString> ULDWorkshop::LayoutProblems() const
{
    TArray<FString> Out;
    const FLayout& L=Layout;
    const float U=L.U;
    TArray<FBox2D> Rows,Shelf;
    const FBox2D Band=Latest.Band.Right>Latest.Band.Left?FBox2D(GetCachedGeometry().AbsoluteToLocal(FVector2D(Latest.Band.Left,Latest.Band.Top)),
        GetCachedGeometry().AbsoluteToLocal(FVector2D(Latest.Band.Right,Latest.Band.Bottom))):FBox2D(ForceInit);
    const auto Probe=[&](const FBox2D& Bounds,const FString& Part)
    {
        // Corners and edge midpoints of a part outside the ring never touch its band.
        const FVector2D Mid=Bounds.GetCenter();
        for(const FVector2D& Point:{Bounds.Min,Bounds.Max,FVector2D(Bounds.Min.X,Bounds.Max.Y),FVector2D(Bounds.Max.X,Bounds.Min.Y),
            FVector2D(Mid.X,Bounds.Min.Y),FVector2D(Mid.X,Bounds.Max.Y),FVector2D(Bounds.Min.X,Mid.Y),FVector2D(Bounds.Max.X,Mid.Y)})
        {
            if(OnBand(Point,-1.f*U)){Out.Add(TEXT("into the ring ")+Part);return;}
        }
    };
    for(const FShape& Box:L.Boxes)
    {
        if(Box.bWedge){continue;}
        const FBox2D Bounds=BoundsOf(Box.Shape);
        const FString Part=FString::Printf(TEXT("part %d/%d/%d (%.0f,%.0f)-(%.0f,%.0f)"),int32(Box.Hit.Kind),Box.Hit.Host,Box.Hit.Index,Bounds.Min.X,Bounds.Min.Y,Bounds.Max.X,Bounds.Max.Y);
        if(Bounds.Min.X<-1.||Bounds.Min.Y<-1.||Bounds.Max.X>L.Size.X+1.||Bounds.Max.Y>L.Size.Y+1.){Out.Add(TEXT("off screen ")+Part);}
        switch(Box.Hit.Kind)
        {
        case EHit::Wing:
            Probe(Bounds,Part);
            for(const FBox2D& Other:Rows){if(Touches(Bounds,Other)){Out.Add(TEXT("table overlap ")+Part);}}
            if(L.RowTop>0.f&&Bounds.Max.Y>L.RowTop+.5f&&Bounds.Max.X>L.Bar.Min.X){Out.Add(TEXT("table into the offer row ")+Part);}
            Rows.Add(Bounds);
            break;
        case EHit::Shop:
            Probe(Bounds,Part);
            for(const FBox2D& Other:Shelf){if(Touches(Bounds,Other)){Out.Add(TEXT("shop overlap ")+Part);}}
            Shelf.Add(Bounds);
            break;
        case EHit::Offer: case EHit::Hand: case EHit::OfferPile: case EHit::Reroll: case EHit::Skip:
            if(!IsOpen()){break;}
            Probe(Bounds,Part);
            if(Bounds.Max.Y>L.Bar.Min.Y+.5f){Out.Add(TEXT("below the bar line ")+Part);}
            break;
        case EHit::HandPile: case EHit::Respec: case EHit::Matrix: case EHit::Close:
            Probe(Bounds,Part);
            if(Band.bIsValid&&Bounds.Max.Y>Band.Min.Y+.5f){Out.Add(TEXT("into the HUD band ")+Part);}
            break;
        default: break;
        }
    }
    // The rest actions' corner (the HUD's buttons) against the band, the bar and every part.
    if(IsOpen()&&Latest.Actions.X>0.f)
    {
        const FBox2D Actions(ActionsAt-Latest.Actions,ActionsAt);
        Probe(Actions,TEXT("rest actions"));
        if(Band.bIsValid&&Touches(Actions,Band)){Out.Add(TEXT("rest actions on the HUD band"));}
        for(const FBox2D& Segment:L.Segments){if(Touches(Actions,Segment)){Out.Add(TEXT("rest actions on the build bar"));}}
        for(const FShape& Box:L.Boxes){if(!Box.bWedge&&Touches(Actions,BoundsOf(Box.Shape))){Out.Add(FString::Printf(TEXT("rest actions on part %d/%d/%d"),int32(Box.Hit.Kind),Box.Hit.Host,Box.Hit.Index));}}
    }
    if(L.TableScale*L.K*18.f<11.9f){Out.Add(FString::Printf(TEXT("table text %.1f px"),L.TableScale*L.K*18.f));}
    if(L.ShopWidth>0.f&&L.ShopScale*L.K*18.f<11.9f){Out.Add(FString::Printf(TEXT("shop text %.1f px"),L.ShopScale*L.K*18.f));}
    if(L.Card.bIsValid)
    {
        if(L.Card.Min.X<-1.||L.Card.Min.Y<-1.||L.Card.Max.X>L.Size.X+1.||L.Card.Max.Y>L.Size.Y+1.){Out.Add(TEXT("hover card off screen"));}
        for(const FShape& Box:L.Boxes)
        {
            if(Box.bWedge||!(Box.Hit==Focus()||Box.Hit.Kind==EHit::Installed||Box.Hit.Kind==EHit::Host)){continue;}
            if(Touches(L.Card,BoundsOf(Box.Shape))){Out.Add(FString::Printf(TEXT("hover card covers part %d/%d/%d"),int32(Box.Hit.Kind),Box.Hit.Host,Box.Hit.Index));}
        }
    }
    return Out;
}

// ---- tick: the pointer, the radar ----------------------------------------------------------------------------------------

void ULDWorkshop::UpdateRadar()
{
    ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    APawn* Pawn=PC?PC->GetPawn():nullptr;
    UWorld* World=GetWorld();
    const bool bWanted=Ring.bValid&&Pawn&&World&&!Latest.bDead&&(IsOpen()||Latest.bShown);
    if(!bWanted){if(Radar){Radar->Hide();}bHasCentre=false;return;}
    if(!Radar)
    {
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Radar=World->SpawnActor<ALDBuildRadarDecal>(ALDBuildRadarDecal::StaticClass(),FTransform::Identity,Params);
        if(!Radar){return;}
    }
    FVector Feet=Pawn->GetActorLocation();
    if(const ALDCharacter* Body=Cast<ALDCharacter>(Pawn)){Feet=Body->PresentedLocation();}
    if(const UCapsuleComponent* Capsule=Pawn->FindComponentByClass<UCapsuleComponent>()){Feet.Z-=Capsule->GetScaledCapsuleHalfHeight();}
    const float Yaw=PC->PlayerCameraManager?float(PC->PlayerCameraManager->GetCameraRotation().Yaw):0.f;
    FVector2D Centre,Side;
    const FVector Right=FRotator(0.f,Yaw+90.f,0.f).Vector();
    if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,Feet,Centre,false)
        ||!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,Feet+Right*100.f,Side,false))
    {Radar->Hide();bHasCentre=false;return;}
    // Widget-space units (the viewport's DPI space) match this widget's local units on a full-screen HUD.
    const float PerCm=float(FVector2D::Distance(Centre,Side))/100.f;
    const float Reach=RadarRadius*Layout.R;
    const float RadiusCm=PerCm>1e-3f?Reach/PerCm:60.f;
    SmoothedCentre=bHasCentre?FMath::Lerp(SmoothedCentre,Centre,.35):Centre;bHasCentre=true;
    FLDBuildRadarState State;
    for(const FStat& Stat:Ring.Stats){State.Points.Add(Stat.Points);}
    State.Keystone=Ring.Keystone;
    State.bClosed=!IsOpen();
    State.Installed=Ring.Installed;State.Capacity=Ring.Capacity;State.MaxCapacity=Ring.MaxCapacity;
    for(int32 Index=0;Index<Ring.Hosts.Num()&&Index<HostCount;++Index)
    {
        if(Ring.Hosts[Index].bHasMedal){State.Medals.Add(TPair<float,FLinearColor>(HostAngle[Index],ElementInk(Ring.Hosts[Index].Medal.Element)));}
    }
    State.Opacity=IsOpen()?FMath::Max(.35f,OpenAmount()):.62f;
    Radar->Show(State,Feet,Yaw,RadiusCm);
}
void ULDWorkshop::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    {TRACE_CPUPROFILER_EVENT_SCOPE(LDWorkshop_Radar);UpdateRadar();}
    const double Now=UiNow();
    if(FrozenOpen>=0.f&&IsOpen()){OpenedAt=Now-FrozenOpen;}
    if(FrozenReveal>=0.f&&BatchAt>=0.){BatchAt=Now-FrozenReveal;}
    if(IsOpen()||IsOpening()||!Ring.bValid)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDWorkshop_LayoutOpen);
        ClosedKey.bValid=false;
        ComputeLayout(Geometry);
    }
    else
    {
        // Closed, the layout is the entry by the band and the waiting offer: it follows the model, the widget's size and
        // scale, the band's place and the key's name, not the frame.
        FClosedKey Key;
        Key.bValid=true;Key.Model=ModelSerial;Key.Size=Geometry.GetLocalSize();Key.Scale=Geometry.Scale;Key.TextScale=TextScale;
        Key.Band=Latest.Band;Key.OpenKey=Latest.OpenKey;
        if(!(Key==ClosedKey))
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(LDWorkshop_LayoutClosed);
            ClosedKey=MoveTemp(Key);
            ComputeLayout(Geometry);
        }
    }
    FHit Next;
    if(FSlateApplication::IsInitialized()&&Ring.bValid)
    {
        const FVector2D Pointer=Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
        if(Pointer.X>=0.&&Pointer.Y>=0.&&Pointer.X<=Layout.Size.X&&Pointer.Y<=Layout.Size.Y){Next=HitAt(Pointer);}
    }
    if(!(Next==Hovered)){Hovered=Next;LayoutCard();}
    bTakesPointer=!Next.IsNone()||!Selected.IsNone()||SelectedBlueprint!=INDEX_NONE;
    ApplyVisibility();
    // A hand card looked at loses its 新 mark.
    const FHit Shown=Focus();
    if(Shown.Kind==EHit::Hand&&Ring.Hand.IsValidIndex(Shown.Index)&&Ring.Hand[Shown.Index].bFresh&&Storage.IsValid())
    {Storage->MarkSeen(Ring.Hand[Shown.Index].Key);}
    if(!IsOpen()&&ClosedAt>=0.&&Now-ClosedAt>RingCloseTime){ClosedAt=-1.;}
}

// ---- input ---------------------------------------------------------------------------------------------------------------

void ULDWorkshop::PinHover(EHit Kind,int32 Host,int32 Index){Pinned=Kind==EHit::None?FHit():FHit{Kind,Host,Index};}
bool ULDWorkshop::SelectHand(int32 Index)
{
    if(!Ring.Hand.IsValidIndex(Index)){Selected=NAME_None;return false;}
    Selected=Ring.Hand[Index].Key;return true;
}
const FCard* ULDWorkshop::SelectedCard() const
{
    return Selected.IsNone()?nullptr:Ring.Hand.FindByPredicate([this](const FCard& Card){return Card.Key==Selected;});
}
bool ULDWorkshop::HandleKey(const FKey& Key)
{
    const int32 Index=Key==EKeys::One||Key==EKeys::NumPadOne?0:Key==EKeys::Two||Key==EKeys::NumPadTwo?1:Key==EKeys::Three||Key==EKeys::NumPadThree?2:INDEX_NONE;
    if(Index==INDEX_NONE||!Ring.bValid||!Latest.bShown||!Ring.Batch.Options.IsValidIndex(Index)){return false;}
    if(IsOpen()&&bShowHand){bShowHand=false;}
    Click(FHit{EHit::Offer,INDEX_NONE,Index},false);
    return true;
}
FReply ULDWorkshop::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    const FHit Hit=HitAt(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
    const bool bRight=Event.GetEffectingButton()==EKeys::RightMouseButton;
    if(Hit.IsNone())
    {
        // A click on the world puts a selected card back; the click itself still reaches the world.
        if(!Selected.IsNone()||SelectedBlueprint!=INDEX_NONE){Selected=NAME_None;SelectedBlueprint=INDEX_NONE;}
        return FReply::Unhandled();
    }
    if(Event.GetEffectingButton()!=EKeys::LeftMouseButton&&!bRight){return FReply::Unhandled();}
    Click(Hit,bRight);
    return FReply::Handled();
}
FReply ULDWorkshop::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    const FHit Hit=HitAt(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
    if(Hit.Kind==EHit::Stat&&StatBar.IsValid()&&Event.GetWheelDelta()!=0.f)
    {
        if(Event.GetWheelDelta()>0.f){StatBar->Allocate(Hit.Index);}else{StatBar->Refund(Hit.Index);}
        return FReply::Handled();
    }
    return Super::NativeOnMouseWheel(Geometry,Event);
}
void ULDWorkshop::ProbeClick(EHit Kind,int32 Host,int32 Index)
{
#if WITH_DEV_AUTOMATION_TESTS
    Click(FHit{Kind,Host,Index},false);
#endif
}
void ULDWorkshop::Click(const FHit& Hit,bool bRight)
{
    ULDComponentStorage* Cards=Storage.Get();
    ULDChoicesPanel* Tray=Choices.Get();
    const FCard* Held=SelectedCard();
    const auto Install=[&](int32 HostIndex,FName OnKey)
    {
        if(!Held||!Cards||!Ring.Hosts.IsValidIndex(HostIndex)){return;}
        const FHost& Host=Ring.Hosts[HostIndex];
        if(Held->Form==ELDCardForm::Medal){Tell(TEXT("勋章镶进菱形槽，不占构筑格"));return;}
        if(Held->Form!=Host.Form||(Host.Form==ELDCardForm::Skill&&Host.bEmpty))
        {
            Tell(Host.bEmpty&&Held->Form==ELDCardForm::Skill?FString(TEXT("这个技能槽还没有技能")):FString::Printf(TEXT("这张牌只能%s"),*FormPlace(Held->Form)));
            return;
        }
        Cards->DropOn(Held->Key,Host.Column,OnKey);
    };
    switch(Hit.Kind)
    {
    case EHit::Stat:
        if(!StatBar.IsValid()){return;}
        if(bRight){StatBar->Refund(Hit.Index);}else{StatBar->Allocate(Hit.Index);}
        return;
    case EHit::Host:
        if(bRight)
        {
            if(Tray&&Hit.Host>=1&&Hit.Host<=3&&!Tray->ForgetAtColumn(Hit.Host)){Tell(TEXT("休息时才能遗忘技能"));}
            return;
        }
        if(SelectedBlueprint!=INDEX_NONE&&Tray&&Hit.Host>=1&&Hit.Host<=3)
        {
            const uint8 Key=uint8(Hit.Host-1);
            if(Ring.Hosts[Hit.Host].bEmpty){Tray->LearnBlueprint(SelectedBlueprint,Key);}else{Tray->AskReplace(SelectedBlueprint,Key);}
            SelectedBlueprint=INDEX_NONE;
            return;
        }
        Install(Hit.Host,NAME_None);
        return;
    case EHit::Drop:
        if(Held){Install(Hit.Host,NAME_None);}
        return;
    case EHit::Installed:
    {
        if(!Ring.Hosts.IsValidIndex(Hit.Host)||!Ring.Hosts[Hit.Host].Cards.IsValidIndex(Hit.Index)||!Cards){return;}
        const FCard& Card=Ring.Hosts[Hit.Host].Cards[Hit.Index];
        if(Held&&!bRight){Install(Hit.Host,Card.Key);return;}
        // Without a card in hand a click takes one copy back to the library.
        Cards->DropInStorage(Card.Keys.Last());
        return;
    }
    case EHit::Gem:
    {
        if(!Ring.Hosts.IsValidIndex(Hit.Host)||!Cards){return;}
        const FHost& Host=Ring.Hosts[Hit.Host];
        if(Held&&Held->Form==ELDCardForm::Medal){Cards->DropOn(Held->Key,Host.Column,Host.bHasMedal?Host.Medal.Key:NAME_None);return;}
        if(Held){Tell(TEXT("这里只能镶元素勋章"));return;}
        if(Host.bHasMedal){Cards->DropInStorage(Host.Medal.Key);}
        return;
    }
    case EHit::Hand:
        if(!Ring.Hand.IsValidIndex(Hit.Index)){return;}
        if(bRight){Selected=NAME_None;return;}
        Selected=Selected==Ring.Hand[Hit.Index].Key?NAME_None:Ring.Hand[Hit.Index].Key;
        if(Cards){Cards->MarkSeen(Ring.Hand[Hit.Index].Key);}
        LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);
        return;
    case EHit::Offer:
        if(!Tray||!Ring.Batch.Options.IsValidIndex(Hit.Index)){return;}
        switch(Ring.Batch.Kind)
        {
        case EBatch::Final: Tray->ChooseFinalMod(Hit.Index);return;
        case EBatch::Active: Tray->ChooseOffer(Hit.Index);return;
        case EBatch::Card: Tray->PickCardOffer(Hit.Index);return;
        case EBatch::Blueprint:
        {
            bool bEmpty=false;
            for(int32 Host=1;Host<=3;++Host){bEmpty|=Ring.Hosts.IsValidIndex(Host)&&Ring.Hosts[Host].bEmpty;}
            if(bEmpty){Tray->LearnBlueprintCard(Hit.Index);return;}
            // Every slot holds a skill: pick the blueprint, then the skill it replaces (a rest or a dorm swap).
            SelectedBlueprint=SelectedBlueprint==Hit.Index?INDEX_NONE:Hit.Index;Selected=NAME_None;
            if(SelectedBlueprint!=INDEX_NONE){Tell(TEXT("点一个技能替换它"));}
            return;
        }
        default: return;
        }
    case EHit::OfferPile: case EHit::HandPile: bShowHand=!bShowHand;PageAt=UiNow();return;
    case EHit::Reroll: if(Tray){Tray->RerollCardOffer();}return;
    case EHit::Skip:
        if(!Tray){return;}
        if(Ring.Batch.Kind==EBatch::Card){Tray->SkipCardOffer();}else{Tray->SkipOffer();}
        return;
    case EHit::Shop:
        if(!Tray||!Ring.Shop.IsValidIndex(Hit.Index)){return;}
        if(Ring.Shop[Hit.Index].Slot==FLDBuildCommand::ShopSkillPointSlot){Tray->BuyShopSkillPoint();}
        else if(Ring.Shop[Hit.Index].Slot==FLDBuildCommand::ShopMedalSlot){Tray->BuyShopMedal();}
        else{Tray->BuyShopSlot(Ring.Shop[Hit.Index].Slot);}
        return;
    case EHit::ShopRefresh: if(Tray){Tray->RefreshShopShelf();}return;
    case EHit::Close: if(RequestClose){RequestClose();}return;
    case EHit::Entry: if(RequestOpen){RequestOpen();}return;
    case EHit::Matrix: bMatrix=!bMatrix;return;
    case EHit::Respec: if(Tray){Tray->Respec();}return;
    case EHit::Element: if(Cards){Cards->ChooseOption(Hit.Index);}return;
    case EHit::ConfirmYes: if(Tray){Tray->ConfirmRestAction();}return;
    case EHit::ConfirmNo: if(Tray){Tray->CancelRestAction();}return;
    default: return;
    }
}

// ---- links ---------------------------------------------------------------------------------------------------------------

uint32 ULDWorkshop::VocabOf(const FHit& Hit) const
{
    const FLDDemoContentDefinitions* Content=Latest.Content;
    switch(Hit.Kind)
    {
    case EHit::Host: return Ring.Hosts.IsValidIndex(Hit.Host)?ActiveVocab(Content,Ring.Hosts[Hit.Host].Active):0;
    case EHit::Installed: return Ring.Hosts.IsValidIndex(Hit.Host)&&Ring.Hosts[Hit.Host].Cards.IsValidIndex(Hit.Index)
        ?CardVocab(Content,Ring.Hosts[Hit.Host].Cards[Hit.Index].Definition):0;
    case EHit::Hand: return Ring.Hand.IsValidIndex(Hit.Index)?CardVocab(Content,Ring.Hand[Hit.Index].Definition):0;
    case EHit::Offer:
        if(!Ring.Batch.Options.IsValidIndex(Hit.Index)){return 0;}
        if(Ring.Batch.Kind==EBatch::Card){return CardVocab(Content,Ring.Batch.Options[Hit.Index].Id);}
        if(Ring.Batch.Kind==EBatch::Active||Ring.Batch.Kind==EBatch::Blueprint){return ActiveVocab(Content,Ring.Batch.Options[Hit.Index].Id);}
        return 0;
    case EHit::Shop:
        if(!Ring.Shop.IsValidIndex(Hit.Index)){return 0;}
        return Ring.Shop[Hit.Index].Kind==ELDShopItemKind::Card?CardVocab(Content,Ring.Shop[Hit.Index].Id)
            :Ring.Shop[Hit.Index].Kind==ELDShopItemKind::Skill?ActiveVocab(Content,Ring.Shop[Hit.Index].Id):0;
    default: return 0;
    }
}
TArray<uint64> ULDWorkshop::LinkedTo(const FHit& Hit) const
{
    TArray<uint64> Out;
    const uint32 Mask=VocabOf(Hit);
    if(Mask==0||!IsOpen()){return Out;}
    const FLDDemoContentDefinitions* Content=Latest.Content;
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num();++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        if(!(Hit.Kind==EHit::Host&&Hit.Host==HostIndex)&&(ActiveVocab(Content,Host.Active)&Mask)!=0){Out.Add(AnchorKey(EHit::Host,HostIndex,INDEX_NONE));}
        for(int32 Index=0;Index<Host.Cards.Num();++Index)
        {
            if(Hit.Kind==EHit::Installed&&Hit.Host==HostIndex&&Hit.Index==Index){continue;}
            if((CardVocab(Content,Host.Cards[Index].Definition)&Mask)!=0){Out.Add(AnchorKey(EHit::Installed,HostIndex,Index));}
        }
    }
    return Out;
}

ULDWorkshop::FInfo ULDWorkshop::InfoOf(const FHit& Hit) const
{
    FInfo Out;
    const FLDDemoContentDefinitions* Content=Latest.Content;
    const auto CardInfo=[&](FName Definition)
    {
        const LDHUD::FCardFaceView Face0=LDHUD::CardFace(Definition,Content,Latest.Stats);
        const ERarity Rarity=RarityOf(Content,Definition);
        Out.Name=Face0.Name;Out.IconId=Face0.IconId;Out.Body=Face0.SentencePlain;
        Out.Meta=FString::Printf(TEXT("%s · %s · %s"),LDTerms::CardTypeName(uint8(Face0.Type)),*FormPlace(Face0.Form),*RarityName(Rarity));
        if(Face0.Form==ELDCardForm::Medal){Out.Meta=FString::Printf(TEXT("勋章 · %s"),*FormPlace(Face0.Form));}
    };
    const auto ActiveInfo=[&](FName Id,ELDSkillBranch Branch,const FString& Where)
    {
        const LDHUD::FActiveCardView Face0=LDHUD::ActiveCard(Id,Content,Latest.Stats,Branch);
        Out.Name=Face0.Name;Out.IconId=Face0.IconId;Out.Body=Face0.SentencePlain;Out.Meta=Where;
        if(!Face0.Cost.IsEmpty()){Out.Meta+=TEXT(" · ")+Face0.Cost;}
    };
    const auto StatInfo=[&](int32 Index)
    {
        if(!Ring.Stats.IsValidIndex(Index)||!Content){return;}
        const FStat& Stat=Ring.Stats[Index];
        const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
        Out.Name=Stat.Name;Out.IconId=Stat.IconId;
        Out.Meta=Stat.bKeystone?FString::Printf(TEXT("%d 点 · 关键石已激活"),Stat.Points):FString::Printf(TEXT("%d 点 · 再投 %d 点激活关键石"),Stat.Points,FMath::Max(0,Ring.Keystone-Stat.Points));
        if(!Stats.IsValidIndex(Index)){return;}
        // One more point: the orange gain of every attribute it raises (the catalog's per-point line says the same, so it
        // stands in for it).
        TArray<FString> Gains;
        for(const LDHUD::FStatEffectView& Effect:LDHUD::StatEffects(*Stats[Index],Latest.Attributes)){Gains.Add(Effect.Attribute+TEXT(" ")+Effect.PerPoint);}
        Out.Gain=FString(TEXT("每点 "))+FString::Join(Gains,TEXT(" · "));
        if(Gains.IsEmpty()){Out.Body=LDHUD::StatRow(*Stats[Index],Stat.Points,Content).PerPoint;}
    };
    switch(Hit.Kind)
    {
    case EHit::Installed:
        if(Ring.Hosts.IsValidIndex(Hit.Host)&&Ring.Hosts[Hit.Host].Cards.IsValidIndex(Hit.Index)){CardInfo(Ring.Hosts[Hit.Host].Cards[Hit.Index].Definition);}
        break;
    case EHit::Hand:
        if(Ring.Hand.IsValidIndex(Hit.Index)){CardInfo(Ring.Hand[Hit.Index].Definition);}
        break;
    case EHit::Host:
        if(!Ring.Hosts.IsValidIndex(Hit.Host)){break;}
        {
            const FHost& Host=Ring.Hosts[Hit.Host];
            if(!Host.Active.IsNone()){ActiveInfo(Host.Active,Host.Branch,FString::Printf(TEXT("主动技能 · %s"),*Host.Key));}
            else if(Host.Column==LDHUD::EStorageColumn::Body){Out.Name=TEXT("本体");Out.Meta=TEXT("角色本体 · 普攻");Out.Body=TEXT("本体牌装在这里；元素槽让第 2、3 段普攻带上元素。");Out.bBodyIcon=true;}
            else if(Host.Column==LDHUD::EStorageColumn::Dash){Out.Name=Host.Name;Out.IconId=Host.IconId;Out.Meta=FString::Printf(TEXT("冲刺 · %s"),*Host.Key);Out.Body=TEXT("冲刺牌装在这里。");}
            else{Out.Name=TEXT("空技能槽");Out.Meta=Host.Key;Out.Body=TEXT("学会一个主动技能后，技能牌才能装在这里。");}
        }
        break;
    case EHit::Gem:
        if(!Ring.Hosts.IsValidIndex(Hit.Host)){break;}
        if(Ring.Hosts[Hit.Host].bHasMedal)
        {
            CardInfo(Ring.Hosts[Hit.Host].Medal.Definition);
            static const TCHAR* Tiers[]={TEXT(""),TEXT("Ⅰ"),TEXT("Ⅱ"),TEXT("Ⅲ")};
            Out.Meta=FString::Printf(TEXT("%s 阶 · %s 的元素"),Tiers[FMath::Clamp<int32>(Ring.Hosts[Hit.Host].Medal.Tier,0,3)],*Ring.Hosts[Hit.Host].Name);
        }
        else{Out.Name=TEXT("元素槽 · 空");Out.Meta=Ring.Hosts[Hit.Host].Name;Out.Body=TEXT("镶一枚勋章，让这个位置的命中带上元素。不占构筑容量。");}
        break;
    case EHit::Stat: StatInfo(Hit.Index);break;
    case EHit::Wing:
        if(Hit.Index==INDEX_NONE){StatInfo(Hit.Host);break;}
        if(Ring.Derived.IsValidIndex(Hit.Index)&&Ring.Stats.IsValidIndex(Ring.Derived[Hit.Index].Source))
        {
            const FDerived& Row=Ring.Derived[Hit.Index];
            Out.Name=Row.Name;Out.Meta=FString::Printf(TEXT("%s · 来自%s"),*Row.Value,*Ring.Stats[Row.Source].Name);
            const FLDCombatAttributeRowView* Source=Latest.Attributes.FindByPredicate([&Row](const FLDCombatAttributeRowView& R){return R.Attribute==Row.Attribute;});
            if(Source){Out.Body=LDHUD::AttributeRow(*Source).Breakdown;}
        }
        break;
    case EHit::Offer:
        if(!Ring.Batch.Options.IsValidIndex(Hit.Index)){break;}
        {
            const FOption& Option=Ring.Batch.Options[Hit.Index];
            if(Ring.Batch.Kind==EBatch::Card){CardInfo(Option.Id);}
            else if(Ring.Batch.Kind==EBatch::Final){Out.Name=Option.Name;Out.IconId=Option.IconId;Out.Body=Option.Text;Out.Meta=TEXT("终局改装 · 选定后锁定构筑");}
            else{ActiveInfo(Option.Id,ELDSkillBranch::None,Ring.Batch.Kind==EBatch::Blueprint?FString(LDTerms::Blueprint):FString(TEXT("主动技能")));}
        }
        break;
    case EHit::Shop:
        if(!Ring.Shop.IsValidIndex(Hit.Index)){break;}
        {
            const FShopItem& Item=Ring.Shop[Hit.Index];
            if(Item.Kind==ELDShopItemKind::Card){CardInfo(Item.Id);}
            else if(Item.Kind==ELDShopItemKind::Skill){ActiveInfo(Item.Id,ELDSkillBranch::None,TEXT("主动技能 · 商店"));}
            else if(Item.Slot==FLDBuildCommand::ShopMedalSlot){Out.Name=Item.Name;Out.Meta=Item.Note;Out.Body=TEXT("五种元素勋章随机一枚，放进卡库。");}
            else{Out.Name=TEXT("技能点");Out.Meta=Item.Note;Out.Body=TEXT("给已学的技能升一级。");}
            Out.Meta+=FString::Printf(TEXT(" · %s"),*Diamonds(Item.Price));
        }
        break;
    default: break;
    }
    return Out;
}

// ---- paint ---------------------------------------------------------------------------------------------------------------

void ULDWorkshop::PaintTile(FPainter& P,const FBox2D& Box0,const FString& IconId,const FString& Name,ETile State,ERarity Rarity,float RuleAlpha,UTexture2D* Override) const
{
    const float R=Layout.R;
    const float Saved=P.Opacity;
    if(State==ETile::Dim){P.Opacity*=.26f;}
    const FBox2D Box=State==ETile::Hot?Grow(Box0,float(Box0.GetSize().X)*.04f):Box0;
    // Lit and hovered tiles stand off the band on an ink keyline.
    if(State==ETile::Hot||State==ETile::Lit){P.Fill(RectOf(Grow(Box,(State==ETile::Hot?3.f:2.f)*R)),LDBroadcast::Ink);}
    P.Fill(RectOf(Box),LDBroadcast::Panel);
    if(UTexture2D* Icon=Override?Override:RingIcon(IconId)){P.Image(Icon,Box);}
    else if(!Name.IsEmpty()){P.Text(Name.Left(1),Box.GetCenter(),Face(EFace::Heavy,float(Box.GetSize().Y)*.42f,1.f),LDBroadcast::Grey,FVector2D(.5,.5));}
    const bool bStrong=State==ETile::Hot||State==ETile::Lit;
    const FLinearColor Rule=bStrong?LDBroadcast::White:Rarity==ERarity::Legendary?LDBroadcast::Gold:LDBroadcast::White.CopyWithNewOpacity(RuleAlpha);
    Frame(P,Box,(State==ETile::Hot?4.f:State==ETile::Lit?3.f:2.f)*R,Rule);
    // Rarity by construction: a corner notch, rare blue or legendary gold.
    if(Rarity!=ERarity::Common)
    {
        const float N=float(Box.GetSize().X)*.24f;
        P.Fill({FVector2D(Box.Max.X-N,Box.Min.Y),FVector2D(Box.Max.X,Box.Min.Y),FVector2D(Box.Max.X,Box.Min.Y+N)},Rarity==ERarity::Legendary?LDBroadcast::Gold:LDBroadcast::Rare);
    }
    P.Opacity=Saved;
}

void ULDWorkshop::PaintBand(FPainter& P) const
{
    const FLayout& L=Layout;
    const float R=L.R;
    const FVector2D C=L.Centre;
    const FCard* Held=SelectedCard();
    const ELDCardForm HeldForm=Held?Held->Form:ELDCardForm::Count;
    const double Age=OpenedAt<0.?1.:UiNow()-OpenedAt;
    TArray<FVector2D> Ticks;TArray<float> TickInk;
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num()&&HostIndex<HostCount;++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        const float A=HostAngle[HostIndex];
        // The clockwise wipe of the opening, sector after sector.
        const float Sweep=FMath::Clamp(float((Age-HostIndex*RingWipeStagger)/RingWipeTime),0.f,1.f);
        if(Sweep<=0.f){continue;}
        const float From=A-SectorHalf,To=From+2.f*SectorHalf*(1.f-FMath::Square(1.f-Sweep));
        const float Inner=SectorInner*R,Outer=L.Outer[HostIndex],Mid=FMath::Max(Inner+1.f,.78f*Outer);
        const TArray<FVector2D> In=LDRingPaint::Arc(C,Inner,From,To,32),Middle=LDRingPaint::Arc(C,Mid,From,To,32),Out=LDRingPaint::Arc(C,Outer,From,To,32);
        // A physical plate over the floor, in three states. Empty: a dim, dark plate with a dashed rim. Equipped: a full
        // plate that lightens towards the rim and carries its medal's element (a white inner rule without one). Target of
        // the held card: the same plate lit orange. Every plate has a shadow falling outside its rim, a bevel (a light
        // line inside the rim, a dark one inside the inner edge), dark seams on its two radial sides, and a faint grid.
        const bool bTarget=(HeldForm==Host.Form&&!(Host.Form==ELDCardForm::Skill&&Host.bEmpty))||(SelectedBlueprint!=INDEX_NONE&&HostIndex>=1&&HostIndex<=3);
        const float Plate=Host.bEmpty?.90f:1.f;
        const FLinearColor Rim=Host.bEmpty?LDBroadcast::Ink:LDBroadcast::Raised;
        const FLinearColor Accent=bTarget?LDBroadcast::Orange:Host.bHasMedal?ElementInk(Host.Medal.Element):LDBroadcast::White;
        const auto Along=[&](float Radius){return LDRingPaint::Arc(C,Radius,From,To,32);};
        P.Band(Out,Along(Outer+18.f*R),[&](FVector2D At){return LDBroadcast::Ink.CopyWithNewOpacity(.5f*FMath::Square(1.f-FMath::Clamp((float(FVector2D::Distance(At,C))-Outer)/(18.f*R),0.f,1.f)));});
        P.Band(In,Middle,[&](FVector2D At){return LDBroadcast::Ink.CopyWithNewOpacity(Plate*FMath::Lerp(BandInnerInk,BandMidInk,FMath::Clamp((float(FVector2D::Distance(At,C))-Inner)/(Mid-Inner),0.f,1.f)));});
        P.Band(Middle,Out,[&](FVector2D At)
        {
            const float T=FMath::Clamp((float(FVector2D::Distance(At,C))-Mid)/FMath::Max(1.f,Outer-Mid),0.f,1.f);
            return FMath::Lerp(LDBroadcast::Ink,Rim,T).CopyWithNewOpacity(Plate*FMath::Lerp(BandMidInk,BandEdgeInk,T));
        });
        // The grid: a hairline every ten degrees and one along each card row.
        for(float T=10.f;From+T<To-.01f;T+=10.f)
        {P.Stroke({LDRingPaint::Polar(C,Inner+8.f*R,From+T),LDRingPaint::Polar(C,Outer-8.f*R,From+T)},1.f*R,LDBroadcast::White.CopyWithNewOpacity(Host.bEmpty?.05f:.09f));}
        for(float Radius=Inner+(Outer-Inner)/3.f;Radius<Outer-12.f*R;Radius+=(Outer-Inner)/3.f)
        {P.Stroke(Along(Radius),1.f*R,LDBroadcast::White.CopyWithNewOpacity(Host.bEmpty?.05f:.09f));}
        if(!Host.bEmpty||bTarget)
        {
            // The plate's colour: a wash that is strongest at the inner edge, and a solid inner rule.
            P.Band(In,Out,[&](FVector2D At){return Accent.CopyWithNewOpacity((bTarget?.22f:Host.bHasMedal?.20f:.05f)*(1.f-.7f*FMath::Clamp((float(FVector2D::Distance(At,C))-Inner)/FMath::Max(1.f,Outer-Inner),0.f,1.f)));});
            P.Band(In,Along(Inner+6.f*R),[&](FVector2D){return Accent.CopyWithNewOpacity(Host.bHasMedal||bTarget?1.f:.7f);});
        }
        // The bevel and the seams.
        P.Stroke(Along(Outer-4.f*R),2.f*R,LDBroadcast::White.CopyWithNewOpacity(Host.bEmpty?.08f:.2f));
        P.Stroke(Along(Inner+(Host.bEmpty?3.f:9.f)*R),2.f*R,FLinearColor::Black.CopyWithNewOpacity(.45f));
        for(const float Edge:{From,To})
        {
            if(To-From<2.f*SectorHalf-.05f&&Edge==To){continue;}
            P.Stroke({LDRingPaint::Polar(C,Inner,Edge),LDRingPaint::Polar(C,Outer,Edge)},3.f*R,FLinearColor::Black.CopyWithNewOpacity(.6f));
        }
        // The rim: a thick orange rule on a host that can take the held card, a dashed rule on an empty host, else white.
        if(bTarget){P.Stroke(Out,4.f*R,LDBroadcast::Orange);}
        else if(Host.bEmpty){P.Dashed(Out,2.f*R,12.f*R,8.f*R,LDBroadcast::White.CopyWithNewOpacity(.5f),false);}
        else{P.Stroke(Out,2.f*R,LDBroadcast::White.CopyWithNewOpacity(.9f));}
        if(Host.bEmpty&&!bTarget){P.Stroke(In,2.f*R,LDBroadcast::White.CopyWithNewOpacity(.2f));}
        for(float T=0.f;From+T<=To+.01f;T+=2.5f)
        {
            const bool bMajor=FMath::Abs(FMath::Fmod(T,10.f))<.01f;
            Ticks.Add(LDRingPaint::Polar(C,Outer+4.f*R,From+T));Ticks.Add(LDRingPaint::Polar(C,Outer+(bMajor?TickLength:9.f)*R,From+T));
            TickInk.Add(bMajor?.7f:.35f);
        }
    }
    P.Segments(Ticks,2.f*R,[&](int32 Index){return LDBroadcast::White.CopyWithNewOpacity(TickInk.IsValidIndex(Index)?TickInk[Index]:.35f);});
    // The element track through the sockets; a reaction colours the stretch between its two sockets.
    P.Stroke(LDRingPaint::Arc(C,GemRadius*R,HostAngle[0],HostAngle[3]),2.f*R,LDBroadcast::White.CopyWithNewOpacity(.22f));
    int32 Lane=0;
    for(const FReaction& Reaction:Ring.Reactions)
    {
        if(Reaction.A>=HostCount||Reaction.B>=HostCount){continue;}
        const float A0=HostAngle[Reaction.A],A1=HostAngle[Reaction.B];
        const float Radius=(Reaction.B-Reaction.A==1?GemRadius:GemRadius-14.f*(1+Lane++))*R;
        const float Inset=FMath::RadiansToDegrees(GemSize*.5f*R/Radius);
        const TArray<FVector2D> Arc=LDRingPaint::Arc(C,Radius,A0+(Reaction.B-Reaction.A==1?Inset:0.f),A1-(Reaction.B-Reaction.A==1?Inset:0.f),24);
        P.Stroke(Arc,7.f*R,LDBroadcast::Ink.CopyWithNewOpacity(.8f));
        P.Stroke(Arc,4.f*R,ElementInk(Reaction.First),false,ElementInk(Reaction.Second));
        const FVector2D At=LDRingPaint::Polar(C,Radius,(A0+A1)*.5f);
        const FSlateFontInfo Font=Face(EFace::Bold,18.f,L.F);
        const float W=WidthOf(Reaction.Name,Font)+14.f*L.F;
        const FBox2D Chip(At-FVector2D(W*.5f,12.f*L.F),At+FVector2D(W*.5f,12.f*L.F));
        P.Fill(Lean(Chip,5.f*L.F,5.f*L.F),LDBroadcast::Ink.CopyWithNewOpacity(.9f));
        P.Text(Reaction.Name,Chip.GetCenter(),Font,LDBroadcast::White,FVector2D(.5,.5));
    }
}

void ULDWorkshop::PaintLinks(FPainter& P,const TArray<uint64>& Lit) const
{
    // One continuous white stroke through the hovered part and everything it links to, in their order around the ring
    // (under the tiles, over the band). A hovered part off the ring joins the nearest linked one first.
    const FLayout& L=Layout;
    const FHit Shown=Focus();
    if(Lit.IsEmpty()){return;}
    const uint64 SelfKey=AnchorKey(Shown.Kind,Shown.Kind==EHit::Host||Shown.Kind==EHit::Installed?Shown.Host:INDEX_NONE,Shown.Kind==EHit::Host?INDEX_NONE:Shown.Index);
    const FVector2D* Self=L.Anchors.Find(SelfKey);
    const bool bSelfOnRing=Shown.Kind==EHit::Host||Shown.Kind==EHit::Installed;
    struct FStop{FVector2D At;float Radius;float Degrees;};
    TArray<FStop> Stops;
    const auto Stop=[&](FVector2D At){Stops.Add({At,float(FVector2D::Distance(At,L.Centre)),DegreesOf(L.Centre,At)});};
    for(const uint64 Key:Lit){if(const FVector2D* At=L.Anchors.Find(Key)){Stop(*At);}}
    if(Self&&bSelfOnRing){Stop(*Self);}
    if(Stops.IsEmpty()){return;}
    Stops.Sort([](const FStop& A,const FStop& B){return A.Degrees<B.Degrees;});
    TArray<FVector2D> Path;
    if(Self&&!bSelfOnRing)
    {
        // From the hovered part to the end of the stroke nearer to it.
        if(FVector2D::Distance(*Self,Stops.Last().At)<FVector2D::Distance(*Self,Stops[0].At)){Algo::Reverse(Stops);}
        Path.Add(*Self);
    }
    for(int32 Index=0;Index<Stops.Num();++Index)
    {
        if(Index==0){Path.Add(Stops[0].At);continue;}
        const FStop& A=Stops[Index-1];const FStop& B=Stops[Index];
        const int32 Steps=FMath::Clamp(FMath::CeilToInt(FMath::Abs(B.Degrees-A.Degrees)/1.5f),2,90);
        for(int32 Step=1;Step<=Steps;++Step)
        {
            const float T=float(Step)/Steps;
            Path.Add(LDRingPaint::Polar(L.Centre,FMath::Lerp(A.Radius,B.Radius,T),FMath::Lerp(A.Degrees,B.Degrees,T)));
        }
    }
    if(Path.Num()<2){return;}
    P.Stroke(Path,7.f*L.R,LDBroadcast::Ink.CopyWithNewOpacity(.6f));
    P.Stroke(Path,3.f*L.R,LDBroadcast::White);
}

void ULDWorkshop::PaintSockets(FPainter& P) const
{
    const FLayout& L=Layout;
    const float R=L.R;
    const FHit Shown=Focus();
    const FCard* Held=SelectedCard();
    const bool bMedalHeld=Held&&Held->Form==ELDCardForm::Medal;
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num()&&HostIndex<HostCount;++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        if(!Host.bSocket){continue;}
        const FVector2D At=LDRingPaint::Polar(L.Centre,GemRadius*R,HostAngle[HostIndex]);
        const bool bHot=Shown.Kind==EHit::Gem&&Shown.Host==HostIndex;
        const float Half=GemSize*.5f*R*(bHot?1.08f:1.f);
        if(bHot||bMedalHeld){P.Fill(Diamond(At,Half+(bHot?5.f:4.f)*R),LDBroadcast::White);}
        if(Host.bHasMedal)
        {
            const FLinearColor Element=ElementInk(Host.Medal.Element);
            P.Fill(Diamond(At,Half),Element);
            const TArray<FVector2D> Seat=Diamond(At,Half-4.f*R);
            P.Fill(Seat,LDBroadcast::Panel);
            if(UTexture2D* Icon=RingIcon(Host.Medal.IconId)){P.Image(Icon,Square(At,(Half-4.f*R)*1.42f),FLinearColor::White,&Seat);}
            // The fusion tier: three small diamonds under the socket, lit up to the tier (gold at the third).
            for(int32 Tier=0;Tier<3;++Tier)
            {
                const FVector2D Pip=At+FVector2D((Tier-1)*13.f*R,Half+11.f*R);
                const bool bOn=Tier<Host.Medal.Tier;
                P.Fill(Diamond(Pip,6.5f*R),LDBroadcast::Ink);
                P.Fill(Diamond(Pip,4.5f*R),bOn?(Host.Medal.Tier>=3?LDBroadcast::Gold:LDBroadcast::White):LDBroadcast::White.CopyWithNewOpacity(.25f));
            }
        }
        else
        {
            P.Fill(Diamond(At,Half),LDBroadcast::White.CopyWithNewOpacity(.55f));
            P.Fill(Diamond(At,Half-2.f*R),LDBroadcast::Ink.CopyWithNewOpacity(.6f));
        }
    }
}

void ULDWorkshop::PaintHosts(FPainter& P,const TArray<uint64>& Lit,bool bDim) const
{
    const FLayout& L=Layout;
    const float R=L.R,F=L.F;
    const FVector2D C=L.Centre;
    const FHit Shown=Focus();
    const FCard* Held=SelectedCard();
    const bool bFull=Ring.Capacity>0&&Ring.Installed>=Ring.Capacity;
    const double Now=UiNow();
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num()&&HostIndex<HostCount;++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        const float A=HostAngle[HostIndex];
        const bool bTarget=Held&&Held->Form==Host.Form&&!(Host.Form==ELDCardForm::Skill&&Host.bEmpty);
        // The installed cards in their fixed places, then the open place.
        const int32 Slots=Host.Cards.Num()+(bFull?0:1);
        for(int32 Index=0;Index<Slots;++Index)
        {
            const FVector2D Polar=SlotPolar(Index,A);
            const FVector2D At=LDRingPaint::Polar(C,float(Polar.X)*R,float(Polar.Y));
            if(Index>=Host.Cards.Num())
            {
                const FBox2D Open=Square(At,NodeSize*R);
                const bool bOver=Shown.Kind==EHit::Drop&&Shown.Host==HostIndex;
                Frame(P,Open,(bTarget&&bOver?4.f:bTarget?3.f:2.f)*R,LDBroadcast::White.CopyWithNewOpacity(bTarget?1.f:.22f));
                continue;
            }
            const FCard& Card=Host.Cards[Index];
            const bool bHot=Shown.Kind==EHit::Installed&&Shown.Host==HostIndex&&Shown.Index==Index;
            const bool bLit=Lit.Contains(AnchorKey(EHit::Installed,HostIndex,Index));
            const ETile State=bHot?ETile::Hot:bLit?ETile::Lit:bDim?ETile::Dim:ETile::Plain;
            const FBox2D Box=Square(At,NodeSize*R);
            PaintTile(P,Box,Card.IconId,Card.Name,State,Card.Rarity);
            float Flash=0.f;
            for(const FName& Each:Card.Keys){if(const double* At0=Landed.Find(Each)){Flash=FMath::Max(Flash,1.f-float((Now-*At0)/RingLandTime));}}
            if(Flash>0.f){P.Fill(RectOf(Box),LDBroadcast::White.CopyWithNewOpacity(.6f*Flash));}
            if(Card.Copies>1&&State!=ETile::Dim)
            {
                // ×N on a white tab at the top-right corner.
                const FSlateFontInfo Font=Face(EFace::Figure,18.f,F);
                const FString Text=FString::Printf(TEXT("×%d"),Card.Copies);
                const float W=WidthOf(Text,Font)+12.f*F,H=20.f*F;
                const FBox2D Tab(FVector2D(Box.Max.X+6.f*R-W,Box.Min.Y-8.f*R),FVector2D(Box.Max.X+6.f*R,Box.Min.Y-8.f*R+H));
                P.Fill(Lean(Tab,4.f*F,4.f*F),LDBroadcast::White);
                P.Text(Text,Tab.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
            }
        }
        // The host: the one container at its largest size, with its key on a white tab under it.
        const FVector2D HostAt=LDRingPaint::Polar(C,HostRadius*R,A);
        const bool bHot=Shown.Kind==EHit::Host&&Shown.Host==HostIndex;
        const bool bLit=Lit.Contains(AnchorKey(EHit::Host,HostIndex,INDEX_NONE));
        const ETile State=bHot?ETile::Hot:bLit?ETile::Lit:bDim?ETile::Dim:ETile::Plain;
        const FBox2D Box=Square(HostAt,HostSize*R);
        const bool bBody=Host.Column==LDHUD::EStorageColumn::Body;
        PaintTile(P,Box,Host.bEmpty?FString():Host.IconId,Host.bEmpty?FString(TEXT("空")):Host.Name,State,ERarity::Common,.9f,bBody?RingArt(EArt::Body):nullptr);
        const FString Tag=bBody?FString(TEXT("本体")):Host.Key;
        if(!Tag.IsEmpty())
        {
            const float Saved=P.Opacity;if(State==ETile::Dim){P.Opacity*=.26f;}
            const FSlateFontInfo Font=bBody?Face(EFace::Heavy,18.f,F):Face(EFace::Figure,24.f,F);
            const float W=WidthOf(Tag,Font)+18.f*F,H=24.f*F;
            const FBox2D Tab(FVector2D(HostAt.X-W*.5f,Box.Max.Y+2.f*R),FVector2D(HostAt.X+W*.5f,Box.Max.Y+2.f*R+H));
            P.Fill(Lean(Tab,5.f*F,5.f*F),LDBroadcast::White);
            P.Text(Tag,Tab.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
            P.Opacity=Saved;
        }
    }
}

void ULDWorkshop::PaintRadarChips(FPainter& P) const
{
    const FLayout& L=Layout;
    const float F=L.F;
    const FHit Shown=Focus();
    const bool bPlus=Ring.Unspent>0;
    for(int32 Index=0;Index<Ring.Stats.Num();++Index)
    {
        const FVector2D* At=L.Anchors.Find(AnchorKey(EHit::Stat,INDEX_NONE,Index));
        if(!At){continue;}
        const FStat& Stat=Ring.Stats[Index];
        const FVector2D Size=ChipSize(Stat,bPlus,F);
        const FBox2D Chip(*At-Size*.5,*At+Size*.5);
        const bool bHot=(Shown.Kind==EHit::Stat&&Shown.Index==Index)||(Shown.Kind==EHit::Wing&&Shown.Host==Index&&Shown.Index==INDEX_NONE);
        // A maxed keystone is the one gold chip.
        P.Fill(RectOf(Chip),Stat.bKeystone?LDBroadcast::Gold:LDBroadcast::Ink.CopyWithNewOpacity(.86f));
        if(bHot){Frame(P,Grow(Chip,2.f*F),2.f*F,LDBroadcast::White);}
        float X=float(Chip.Min.X)+10.f*F;const float Y=float(Chip.GetCenter().Y);
        X+=WidthOf(Stat.Name,Face(EFace::Bold,18.f,F));
        P.Text(Stat.Name,FVector2D(Chip.Min.X+10.f*F,Y),Face(EFace::Bold,18.f,F),Stat.bKeystone?LDBroadcast::Ink:LDBroadcast::Grey,FVector2D(0.,.5));
        X+=6.f*F;
        const FString Points=FString::FromInt(Stat.Points);
        const float NumW=FMath::Max(16.f*F,WidthOf(Points,Face(EFace::Figure,26.f,F)));
        P.Text(Points,FVector2D(X+NumW,Y),Face(EFace::Figure,26.f,F),Stat.bKeystone?LDBroadcast::Ink:LDBroadcast::White,FVector2D(1.,.5));
        X+=NumW+6.f*F;
        if(bPlus)
        {
            const FBox2D Plus(FVector2D(X,Y-11.f*F),FVector2D(X+22.f*F,Y+11.f*F));
            P.Fill(RectOf(Plus),LDBroadcast::Orange);
            P.Text(TEXT("+"),Plus.GetCenter(),Face(EFace::Heavy,22.f,F),LDBroadcast::Ink,FVector2D(.5,.5));
        }
    }
}

void ULDWorkshop::PaintTable(FPainter& P) const
{
    const FLayout& L=Layout;
    if(Ring.Stats.IsEmpty()){return;}
    const float S=L.TableScale;
    const FVector2D At=L.Table;
    const FHit Shown=Focus();
    // 五维 → 二级属性
    {
        const FVector2D Size=P.Text(TEXT("五维"),At,Face(EFace::Heavy,22.f,S),LDBroadcast::White);
        P.Text(TEXT("→ 二级属性"),FVector2D(At.X+Size.X+12.f*S,At.Y+Size.Y),Face(EFace::Regular,18.f,S),LDBroadcast::Grey,FVector2D(0.,1.));
    }
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Wing||Box.Hit.Index!=INDEX_NONE||!Ring.Stats.IsValidIndex(Box.Hit.Host)){continue;}
        const int32 Row=Box.Hit.Host;
        const FStat& Stat=Ring.Stats[Row];
        const FBox2D Plate(Box.Shape[0],FVector2D(Box.Shape[0].X+L.TableWidth,Box.Shape[2].Y));
        // A hovered row, or the row of the hovered radar chip, lights up.
        const bool bHot=(Shown.Kind==EHit::Wing&&Shown.Host==Row)||(Shown.Kind==EHit::Stat&&Shown.Index==Row);
        const TArray<FVector2D> Shape=Lean(Plate,0.f,14.f*S);
        P.FillShaded(Shape,[&](FVector2D Point)
        {
            const float T=float((Point.X-Plate.Min.X)/FMath::Max(1.,Plate.GetSize().X));
            return bHot?LDRingPaint::Mix(LDBroadcast::White.CopyWithNewOpacity(.18f),LDBroadcast::Ink.CopyWithNewOpacity(.78f),T):LDRingPaint::Mix(LDBroadcast::Ink.CopyWithNewOpacity(.9f),LDBroadcast::Ink.CopyWithNewOpacity(.78f),T);
        });
        P.Fill(RectOf(FBox2D(Plate.Min,FVector2D(Plate.Min.X+3.f*S,Plate.Max.Y))),LDBroadcast::White);
        PaintTile(P,BoxAt(Plate.Min+FVector2D(14.f,11.f)*S,FVector2D(54.f,54.f)*S),Stat.IconId,Stat.Name,ETile::Plain);
        P.Text(Stat.Name,Plate.Min+FVector2D(80.f,9.f)*S,Face(EFace::Bold,18.f,S),LDBroadcast::Grey);
        P.Text(FString::FromInt(Stat.Points),Plate.Min+FVector2D(80.f,27.f)*S,Face(EFace::Figure,38.f,S),Stat.bKeystone?LDBroadcast::Gold:LDBroadcast::White);
    }
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Wing||!Ring.Derived.IsValidIndex(Box.Hit.Index)){continue;}
        const FDerived& Row=Ring.Derived[Box.Hit.Index];
        const FVector2D Min=Box.Shape[0];
        if(Shown.Kind==EHit::Wing&&Shown.Index==Box.Hit.Index){P.Fill(RectOf(FBox2D(FVector2D(Min.X,Box.Shape[2].Y-3.f*S),FVector2D(Box.Shape[2].X-8.f*S,Box.Shape[2].Y))),LDBroadcast::White);}
        P.Text(OneLine(Row.Name,Face(EFace::Regular,18.f,S),L.TableSubW*S-6.f*S),Min+FVector2D(0.f,10.f)*S,Face(EFace::Regular,18.f,S),LDBroadcast::Grey);
        P.Text(Row.Value,Min+FVector2D(0.f,31.f)*S,Face(EFace::Figure,30.f,S),LDBroadcast::White);
    }
}

void ULDWorkshop::PaintShop(FPainter& P,const TArray<uint64>& Lit) const
{
    const FLayout& L=Layout;
    if(!Ring.bShop||L.ShopWidth<=0.f){return;}
    const float S=L.ShopScale;
    const FHit Shown=Focus();
    P.Text(TEXT("休息商店"),FVector2D(L.Shop.X,L.Shop.Y+15.f*S),Face(EFace::Heavy,22.f,S),LDBroadcast::White,FVector2D(0.,.5));
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind==EHit::ShopRefresh)
        {
            const FBox2D Button=BoundsOf(Box.Shape);
            const bool bHot=Shown.Kind==EHit::ShopRefresh;
            P.Fill(Box.Shape,LDBroadcast::White);
            P.Fill(Lean(Grow(Button,-(bHot?3.f:2.f)*S),7.f*S,7.f*S),LDBroadcast::Ink.CopyWithNewOpacity(.95f));
            P.Text(Ring.RefreshPrice>0?FString::Printf(TEXT("刷新 · %s"),*Diamonds(Ring.RefreshPrice)):FString(TEXT("刷新 · 免费")),Button.GetCenter(),Face(EFace::Bold,18.f,S),LDBroadcast::White,FVector2D(.5,.5));
            continue;
        }
        if(Box.Hit.Kind!=EHit::Shop||!Ring.Shop.IsValidIndex(Box.Hit.Index)){continue;}
        const FShopItem& Item=Ring.Shop[Box.Hit.Index];
        const FBox2D Plate=BoundsOf(Box.Shape);
        const bool bHot=Shown.Kind==EHit::Shop&&Shown.Index==Box.Hit.Index;
        const float Saved=P.Opacity;P.Opacity*=Item.bSold?.35f:1.f;
        P.FillShaded(Box.Shape,[&](FVector2D Point)
        {
            const float T=float((Point.X-Plate.Min.X)/FMath::Max(1.,Plate.GetSize().X));
            return LDRingPaint::Mix(LDBroadcast::Ink.CopyWithNewOpacity(.78f),LDBroadcast::Ink.CopyWithNewOpacity(.9f),T);
        });
        if(bHot){P.Fill(RectOf(FBox2D(FVector2D(Plate.Max.X-4.f*S,Plate.Min.Y),Plate.Max)),LDBroadcast::White);}
        const FBox2D Tile=BoxAt(Plate.Min+FVector2D(22.f,2.f)*S,FVector2D(54.f,54.f)*S);
        if(Item.Slot==FLDBuildCommand::ShopSkillPointSlot)
        {
            PaintTile(P,Tile,FString(),FString(),bHot?ETile::Hot:ETile::Plain);
            P.Text(TEXT("技能"),Tile.GetCenter()-FVector2D(0.,9.f*S),Face(EFace::Heavy,18.f,S),LDBroadcast::White,FVector2D(.5,.5));
            P.Text(TEXT("点"),Tile.GetCenter()+FVector2D(0.,10.f*S),Face(EFace::Heavy,18.f,S),LDBroadcast::White,FVector2D(.5,.5));
        }
        else if(Item.Slot==FLDBuildCommand::ShopMedalSlot)
        {
            PaintTile(P,Tile,FString(),FString(),bHot?ETile::Hot:ETile::Plain);
            P.Text(TEXT("元素"),Tile.GetCenter()-FVector2D(0.,9.f*S),Face(EFace::Heavy,18.f,S),LDBroadcast::White,FVector2D(.5,.5));
            P.Text(TEXT("?"),Tile.GetCenter()+FVector2D(0.,10.f*S),Face(EFace::Heavy,18.f,S),LDBroadcast::White,FVector2D(.5,.5));
        }
        else{PaintTile(P,Tile,Item.IconId,Item.Name,bHot?ETile::Hot:Lit.Contains(AnchorKey(EHit::Shop,INDEX_NONE,Box.Hit.Index))?ETile::Lit:ETile::Plain,Item.Rarity);}
        const bool bNote=!Item.Note.IsEmpty()&&!Item.bSold;
        const float NameY=bNote?Plate.Min.Y+4.f*S:float(Plate.GetCenter().Y)-14.f*S;
        P.Text(Item.Name,FVector2D(Plate.Min.X+90.f*S,NameY),Face(EFace::Bold,22.f,S),LDBroadcast::White);
        if(bNote){P.Text(Item.Note,FVector2D(Plate.Min.X+90.f*S,Plate.Min.Y+32.f*S),Face(EFace::Regular,18.f,S),LDBroadcast::Grey);}
        P.Text(Item.bSold?FString(TEXT("已买")):Diamonds(Item.Price),FVector2D(Plate.Max.X-16.f*S,Plate.GetCenter().Y),Face(EFace::Bold,22.f,S),LDBroadcast::White,FVector2D(1.,.5));
        P.Opacity=Saved;
    }
}

void ULDWorkshop::PaintRow(FPainter& P,const TArray<uint64>& Lit) const
{
    const FLayout& L=Layout;
    const float U=L.U,R=L.R,F=L.F;
    const double Now=UiNow();
    const FHit Shown=Focus();
    const FSlateFontInfo ChipFont=Face(EFace::Bold,18.f,F);
    // The header: a white title chip on the left, ink command chips on the right.
    const auto Title=[&](const FString& Text,FVector2D At)
    {
        const float W=WidthOf(Text,Face(EFace::Heavy,18.f,F))+24.f*F;
        const FBox2D Chip=BoxAt(At,FVector2D(W,30.f*F));
        P.Fill(Lean(Chip,7.f*F,7.f*F),LDBroadcast::White);
        P.Text(Text,Chip.GetCenter(),Face(EFace::Heavy,18.f,F),LDBroadcast::Ink,FVector2D(.5,.5));
    };
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::OfferPile&&Box.Hit.Kind!=EHit::Reroll&&Box.Hit.Kind!=EHit::Skip){continue;}
        const FBox2D Chip=BoundsOf(Box.Shape);
        FString Text;
        if(Box.Hit.Kind==EHit::Reroll){Text=FString::Printf(TEXT("重抽 ×%d"),Ring.Batch.Rerolls);}
        else if(Box.Hit.Kind==EHit::Skip){Text=Ring.Batch.SkipPoints>0?FString::Printf(TEXT("跳过 +%lld"),Ring.Batch.SkipPoints):FString(TEXT("跳过"));}
        else{Text=bShowHand?FString::Printf(TEXT("待选 %d"),Ring.Batch.Queued+1):FString::Printf(TEXT("还有 %d 批"),Ring.Batch.Queued);}
        const bool bOff=Box.Hit.Kind==EHit::Reroll&&Ring.Batch.Rerolls<=0;
        P.Fill(Box.Shape,LDBroadcast::Ink.CopyWithNewOpacity(.9f));
        if(Shown==Box.Hit){Frame(P,Chip,2.f*F,LDBroadcast::White);}
        P.Text(Text,Chip.GetCenter(),ChipFont,bOff?LDBroadcast::Mute:LDBroadcast::White,FVector2D(.5,.5));
    }
    if(!OfferFaces.IsEmpty())
    {
        Title(Ring.Batch.Title,FVector2D(OfferFaces[0].Box.Min.X,L.RowTop));
        for(const FFace& Face0:OfferFaces)
        {
            const FOption& Option=Ring.Batch.Options[Face0.Hit.Index];
            const float Age=BatchAt<0.?1.f:FMath::Clamp(float((Now-BatchAt-Face0.Hit.Index*RingRevealStagger)/RingRevealTime),0.f,1.f);
            const float Ease=1.f-FMath::Square(1.f-Age);
            const FBox2D Box=Shift(Face0.Box,FVector2D(0.,(1.f-Ease)*20.f*U));
            const float Saved=P.Opacity;P.Opacity*=Ease*(PageAt<0.?1.f:FMath::Clamp(float((Now-PageAt)/RingRevealTime),0.f,1.f));
            const bool bHot=Shown==Face0.Hit;
            const bool bChosen=Ring.Batch.Kind==EBatch::Blueprint&&SelectedBlueprint==Face0.Hit.Index;
            const TArray<FVector2D> Shape=Lean(Box,12.f*U,12.f*U);
            P.Fill(Shape,LDBroadcast::Ink.CopyWithNewOpacity(.92f));
            // Hover and the chosen blueprint: the thick white rule along the plate.
            if(bHot||bChosen)
            {
                P.Fill(RectOf(FBox2D(FVector2D(Box.Min.X+12.f*U,Box.Min.Y),FVector2D(Box.Max.X,Box.Min.Y+4.f*U))),LDBroadcast::White);
                P.Fill(RectOf(FBox2D(FVector2D(Box.Min.X,Box.Max.Y-4.f*U),FVector2D(Box.Max.X-12.f*U,Box.Max.Y))),LDBroadcast::White);
            }
            // The pick key in a ruled box, the icon tile, the name and the type.
            const FBox2D Key=BoxAt(Box.Min+FVector2D(18.f*U,Box.GetSize().Y*.5f-19.f*U),FVector2D(36.f*U,38.f*U));
            Frame(P,Key,2.f*U,LDBroadcast::White);
            P.Text(FString::FromInt(Face0.Hit.Index+1),Key.GetCenter(),Face(EFace::Figure,30.f,F),LDBroadcast::White,FVector2D(.5,.5));
            const float Z=64.f*U;
            const FBox2D Tile=BoxAt(FVector2D(Box.Min.X+66.f*U,Box.GetCenter().Y-Z*.5f),FVector2D(Z,Z));
            PaintTile(P,Tile,Option.IconId,Option.Name,ETile::Plain,Option.Rarity);
            const float TextX=float(Tile.Max.X)+14.f*U;
            const float TextW=float(Box.Max.X)-TextX-16.f*U;
            P.Text(OneLine(Option.Name,Face(EFace::Heavy,24.f,F),TextW),FVector2D(TextX,Box.GetCenter().Y-2.f*U),Face(EFace::Heavy,24.f,F),LDBroadcast::White,FVector2D(0.,1.));
            FString Type;
            if(Ring.Batch.Kind==EBatch::Card)
            {
                const LDHUD::FCardFaceView Face1=LDHUD::CardFace(Option.Id,Latest.Content,Latest.Stats);
                Type=FString::Printf(TEXT("%s · %s"),LDTerms::CardTypeName(uint8(Face1.Type)),*FormShort(Face1.Form));
            }
            else if(Ring.Batch.Kind==EBatch::Final){Type=TEXT("终局改装");}
            else{Type=Ring.Batch.Kind==EBatch::Blueprint?FString(LDTerms::Blueprint):FString(TEXT("主动技能"));}
            P.Text(OneLine(Type,Face(EFace::Regular,18.f,F),TextW),FVector2D(TextX,Box.GetCenter().Y+4.f*U),Face(EFace::Regular,18.f,F),LDBroadcast::Grey);
            P.Opacity=Saved;
        }
        return;
    }
    if(HandFaces.IsEmpty()&&!bShowHand){return;}
    // The library: tiles with their names, the selected card raised on the thick rule; medals as diamonds.
    const float Page=PageAt<0.?1.f:FMath::Clamp(float((Now-PageAt)/RingRevealTime),0.f,1.f);
    const float PageSaved=P.Opacity;P.Opacity*=1.f-FMath::Square(1.f-Page);
    const float Pitch=HandFaces.Num()>1?float(HandFaces[1].Box.Min.X-HandFaces[0].Box.Min.X):84.f*F;
    const bool bNames=Pitch>=64.f*F;
    for(const FFace& Face0:HandFaces)
    {
        const FCard& Card=Ring.Hand[Face0.Hit.Index];
        const bool bSelected=Card.Key==Selected;
        const bool bHot=Shown==Face0.Hit;
        const FBox2D Box=Shift(Face0.Box,FVector2D(0.,bSelected?-8.f*U:0.f));
        if(Card.Form==ELDCardForm::Medal)
        {
            const FVector2D At=Box.GetCenter();
            const float Half=float(Box.GetSize().X)*.5f*(bHot?1.08f:1.f);
            if(bSelected||bHot){P.Fill(Diamond(At,Half+4.f*R),LDBroadcast::White);}
            P.Fill(Diamond(At,Half),ElementInk(Card.Element));
            const TArray<FVector2D> Seat=Diamond(At,Half-4.f*R);
            P.Fill(Seat,LDBroadcast::Panel);
            if(UTexture2D* Icon=RingIcon(Card.IconId)){P.Image(Icon,Square(At,(Half-4.f*R)*1.42f),FLinearColor::White,&Seat);}
        }
        else{PaintTile(P,Box,Card.IconId,Card.Name,bSelected||bHot?ETile::Hot:Lit.Contains(AnchorKey(EHit::Hand,INDEX_NONE,Face0.Hit.Index))?ETile::Lit:ETile::Plain,Card.Rarity);}
        if(bNames)
        {
            const FSlateFontInfo Font=Face(EFace::Bold,18.f,F);
            P.Text(OneLine(Card.Name,Font,Pitch-6.f*U),FVector2D(Box.GetCenter().X,Face0.Box.Max.Y+4.f*U),Font,bSelected?LDBroadcast::White:LDBroadcast::Grey,FVector2D(.5,0.));
        }
        if(Card.Copies>1)
        {
            const FSlateFontInfo Font=Face(EFace::Figure,18.f,F);
            const FString Text=FString::Printf(TEXT("×%d"),Card.Copies);
            const float W=WidthOf(Text,Font)+12.f*F;
            const FBox2D Tab(FVector2D(Box.Max.X+6.f*R-W,Box.Max.Y-14.f*R),FVector2D(Box.Max.X+6.f*R,Box.Max.Y-14.f*R+20.f*F));
            P.Fill(Lean(Tab,4.f*F,4.f*F),LDBroadcast::White);P.Text(Text,Tab.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
        }
        if(Card.bFresh)
        {
            // 新: an orange tab on the top-right corner (unseen is "for me, now").
            const FSlateFontInfo Font=Face(EFace::Heavy,18.f,F);
            const float W=WidthOf(TEXT("新"),Font)+12.f*F;
            const FBox2D Tab(FVector2D(Box.Max.X+6.f*R-W,Box.Min.Y-10.f*R),FVector2D(Box.Max.X+6.f*R,Box.Min.Y-10.f*R+22.f*F));
            P.Fill(Lean(Tab,4.f*F,4.f*F),LDBroadcast::Orange);P.Text(TEXT("新"),Tab.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
        }
    }
    P.Opacity=PageSaved;
}

void ULDWorkshop::PaintBar(FPainter& P) const
{
    const FLayout& L=Layout;
    if(L.Segments.Num()<4){return;}
    const float F=L.F;
    const FHit Shown=Focus();
    const FSlateFontInfo Label=Face(EFace::Bold,18.f,F),Figure=Face(EFace::Figure,32.f,F),Small=Face(EFace::Figure,22.f,F),Word=Face(EFace::Bold,20.f,F);
    const float Pad=18.f*F;
    for(int32 Index=0;Index<L.Segments.Num();++Index)
    {
        const FBox2D& Box=L.Segments[Index];
        P.Fill(Lean(Box,10.f*F,10.f*F),LDBroadcast::Ink.CopyWithNewOpacity(.92f));
        if(Index==2&&(Shown.Kind==EHit::HandPile||bShowHand)){P.Fill(RectOf(FBox2D(FVector2D(Box.Min.X+10.f*F,Box.Max.Y-3.f*F),FVector2D(Box.Max.X-10.f*F,Box.Max.Y))),LDBroadcast::White);}
    }
    const float Y=float(L.Bar.GetCenter().Y);
    // 属性点 ◆N: unspent points are "mine, now".
    {
        float X=float(L.Segments[0].Min.X)+Pad;
        X+=float(P.Text(TEXT("属性点"),FVector2D(X,Y),Label,LDBroadcast::Grey,FVector2D(0.,.5)).X)+10.f*F;
        X+=float(P.Text(TEXT("◆"),FVector2D(X,Y),Small,Ring.Unspent>0?LDBroadcast::Orange:LDBroadcast::Mute,FVector2D(0.,.5)).X);
        P.Text(FString::Printf(TEXT("%lld"),Ring.Unspent),FVector2D(X,Y),Figure,Ring.Unspent>0?LDBroadcast::Orange:LDBroadcast::White,FVector2D(0.,.5));
    }
    // 构筑 x/cap and the capacity ticks.
    {
        float X=float(L.Segments[1].Min.X)+Pad;
        X+=float(P.Text(TEXT("构筑"),FVector2D(X,Y),Label,LDBroadcast::Grey,FVector2D(0.,.5)).X)+10.f*F;
        X+=float(P.Text(FString::FromInt(Ring.Installed),FVector2D(X,Y),Figure,LDBroadcast::White,FVector2D(0.,.5)).X);
        X+=float(P.Text(FString::Printf(TEXT("/%d"),Ring.Capacity),FVector2D(X,Y+3.f*F),Small,LDBroadcast::Grey,FVector2D(0.,.5)).X)+10.f*F;
        for(int32 Tick=0;Tick<Ring.Capacity;++Tick)
        {
            const FBox2D Cell(FVector2D(X,Y-10.f*F),FVector2D(X+9.f*F,Y+10.f*F));
            const TArray<FVector2D> Shape=Lean(Cell,2.f*F,2.f*F);
            if(Tick<Ring.Installed){P.Fill(Shape,LDBroadcast::White);}
            else{P.Stroke(Shape,2.f*F,LDBroadcast::Mute,true);}
            X+=12.f*F;
        }
    }
    // 牌库 N
    {
        float X=float(L.Segments[2].Min.X)+Pad;
        X+=float(P.Text(TEXT("牌库"),FVector2D(X,Y),Label,LDBroadcast::Grey,FVector2D(0.,.5)).X)+10.f*F;
        P.Text(FString::FromInt(Ring.Hand.Num()),FVector2D(X,Y),Figure,LDBroadcast::White,FVector2D(0.,.5));
    }
    // The commands: 洗点 (a rest), 反应表, 收起 with its key.
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Respec&&Box.Hit.Kind!=EHit::Matrix&&Box.Hit.Kind!=EHit::Close){continue;}
        const FBox2D Area=BoundsOf(Box.Shape);
        const float X=float(Area.Min.X)+Pad*.5f;
        const bool bHot=Shown==Box.Hit||(Box.Hit.Kind==EHit::Matrix&&bMatrix);
        float W=0.f;
        if(Box.Hit.Kind==EHit::Close)
        {
            const FString Key=Latest.OpenKey.IsEmpty()?FString(TEXT("B")):Latest.OpenKey;
            W=float(P.Text(TEXT("收起"),FVector2D(X,Y),Word,LDBroadcast::White,FVector2D(0.,.5)).X)+6.f*F;
            const float KW=WidthOf(Key,Small)+14.f*F;
            const FBox2D Kbd(FVector2D(X+W,Y-13.f*F),FVector2D(X+W+KW,Y+13.f*F));
            P.Fill(RectOf(Kbd),LDBroadcast::White);
            P.Text(Key,Kbd.GetCenter(),Small,LDBroadcast::Ink,FVector2D(.5,.5));
            W+=KW;
        }
        else{W=float(P.Text(Box.Hit.Kind==EHit::Respec?TEXT("洗点"):TEXT("反应表"),FVector2D(X,Y),Word,LDBroadcast::White,FVector2D(0.,.5)).X);}
        if(bHot){P.Fill(RectOf(FBox2D(FVector2D(X,Area.Max.Y-5.f*F),FVector2D(X+W,Area.Max.Y-2.f*F))),LDBroadcast::White);}
    }
}

void ULDWorkshop::PaintMatrix(FPainter& P) const
{
    // The reaction table: every pair of the five elements, the build's own pairs ruled white. It stands over the 反应表 command.
    if(!bMatrix){return;}
    const FLayout& L=Layout;
    const float F=L.F,U=L.U;
    const FShape* Button=L.Boxes.FindByPredicate([](const FShape& Box){return Box.Hit.Kind==EHit::Matrix;});
    if(!Button){return;}
    const ELDDemoElement Elements[5]={ELDDemoElement::Fire,ELDDemoElement::Water,ELDDemoElement::Electric,ELDDemoElement::Chill,ELDDemoElement::Rot};
    TSet<FString> Mine;for(const FReaction& Reaction:Ring.Reactions){Mine.Add(Reaction.Name);}
    const float CellW=52.f*F,CellH=30.f*F,Gap=3.f*U;
    const FVector2D SheetSize(6*CellW+5*Gap+20.f*U,6*CellH+5*Gap+24.f*U);
    const FVector2D Origin(FMath::Min(float(BoundsOf(Button->Shape).Min.X),float(L.Size.X)-16.f*U-float(SheetSize.X)),float(L.Bar.Min.Y)-8.f*U-float(SheetSize.Y));
    const FBox2D Sheet=BoxAt(Origin,SheetSize);
    P.Fill(RectOf(Sheet),LDBroadcast::Ink.CopyWithNewOpacity(.96f));
    P.Fill(RectOf(FBox2D(Sheet.Min,FVector2D(Sheet.Min.X+3.f*U,Sheet.Max.Y))),LDBroadcast::White);
    const FSlateFontInfo Font=Face(EFace::Bold,18.f,F);
    const FVector2D First=Origin+FVector2D(12.f*U,12.f*U);
    for(int32 Row=0;Row<6;++Row)
    {
        for(int32 Column=0;Column<6;++Column)
        {
            if(Row==0&&Column==0){continue;}
            const FBox2D Cell=BoxAt(First+FVector2D(Column*(CellW+Gap),Row*(CellH+Gap)),FVector2D(CellW,CellH));
            if(Row==0||Column==0)
            {
                const ELDDemoElement E=Elements[(Row==0?Column:Row)-1];
                P.Fill(RectOf(Cell),ElementInk(E));
                P.Text(LDHUD::ElementName(E),Cell.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
                continue;
            }
            P.Fill(RectOf(Cell),LDBroadcast::Panel);
            if(Row==Column){P.Text(TEXT("—"),Cell.GetCenter(),Font,LDBroadcast::Mute,FVector2D(.5,.5));continue;}
            const FString Name=LDWorkshop::ReactionName(Elements[Row-1],Elements[Column-1]);
            const bool bOwn=Mine.Contains(Name);
            if(bOwn){Frame(P,Cell,2.f*U,LDBroadcast::White);}
            P.Text(Name,Cell.GetCenter(),Font,bOwn?LDBroadcast::White:LDBroadcast::Grey,FVector2D(.5,.5));
        }
    }
}

void ULDWorkshop::PaintCard(FPainter& P,int32 LinkCount) const
{
    const FLayout& L=Layout;
    if(!L.Card.bIsValid){return;}
    const FInfo Info=InfoOf(Focus());
    if(Info.Name.IsEmpty()){return;}
    const float U=L.U,F=L.F;
    const FBox2D Card=L.Card;
    P.Fill(Lean(Card,0.f,24.f*U),LDBroadcast::Ink.CopyWithNewOpacity(.94f));
    P.Fill(RectOf(FBox2D(Card.Min,FVector2D(Card.Min.X+4.f*U,Card.Max.Y))),LDBroadcast::White);
    const float Icon=72.f*U;
    const FBox2D Tile=BoxAt(Card.Min+FVector2D(18.f*U,14.f*U),FVector2D(Icon,Icon));
    PaintTile(P,Tile,Info.IconId,Info.Name,ETile::Plain,ERarity::Common,1.f,Info.bBodyIcon?RingArt(EArt::Body):nullptr);
    const float TextX=float(Tile.Max.X)+16.f*U;
    const float TextW=float(Card.Max.X)-TextX-30.f*U;
    float Y=float(Card.Min.Y)+10.f*U;
    P.Text(OneLine(Info.Name,Face(EFace::Heavy,26.f,F),TextW),FVector2D(TextX,Y),Face(EFace::Heavy,26.f,F),LDBroadcast::White);Y+=34.f*F;
    {
        float X=TextX;
        X+=float(P.Text(Info.Meta,FVector2D(X,Y),Face(EFace::Bold,18.f,F),LDBroadcast::Grey).X);
        if(LinkCount>0){P.Text(FString::Printf(TEXT(" · 联动 %d"),LinkCount),FVector2D(X,Y),Face(EFace::Heavy,18.f,F),LDBroadcast::White);}
        Y+=26.f*F;
    }
    if(!Info.Gain.IsEmpty()){P.Text(OneLine(Info.Gain,Face(EFace::Bold,18.f,F),TextW),FVector2D(TextX,Y),Face(EFace::Bold,18.f,F),LDBroadcast::Orange);Y+=26.f*F;}
    for(const FString& Line:LDRingPaint::Wrap(Info.Body,Face(EFace::Regular,20.f,F),TextW,3))
    {
        P.Text(Line,FVector2D(TextX,Y),Face(EFace::Regular,20.f,F),LDBroadcast::White.CopyWithNewOpacity(.9f));Y+=26.f*F;
    }
}

void ULDWorkshop::PaintSkillRules(FPainter& P,const TArray<uint64>& Lit) const
{
    // A linked (or hovered) skill's face on the HUD band takes the link's white rule.
    const FGeometry& G=GetCachedGeometry();
    const FHit Shown=Focus();
    for(int32 Host=1;Host<HostCount;++Host)
    {
        const bool bOn=Lit.Contains(AnchorKey(EHit::Host,Host,INDEX_NONE))||(Shown.Kind==EHit::Host&&Shown.Host==Host);
        if(!bOn||!Latest.Skills.IsValidIndex(Host-1)){continue;}
        const FSlateRect& Rect=Latest.Skills[Host-1];
        if(Rect.Right<=Rect.Left||Rect.Bottom<=Rect.Top){continue;}
        Frame(P,FBox2D(G.AbsoluteToLocal(FVector2D(Rect.Left,Rect.Top)),G.AbsoluteToLocal(FVector2D(Rect.Right,Rect.Bottom))),3.f*Layout.U,LDBroadcast::White);
    }
}

void ULDWorkshop::PaintClosed(FPainter& P) const
{
    const FLayout& L=Layout;
    const float U=L.U,F=L.F;
    const FHit Shown=Focus();
    if(!OfferFaces.IsEmpty())
    {
        const FBox2D First=OfferFaces[0].Box;
        {
            const FSlateFontInfo Font=Face(EFace::Heavy,18.f,F);
            const float W=WidthOf(Ring.Batch.Title,Font)+24.f*F;
            const FBox2D Chip(FVector2D(First.Min.X,First.Min.Y-10.f*U-28.f*F),FVector2D(First.Min.X+W,First.Min.Y-10.f*U));
            P.Fill(Lean(Chip,7.f*F,7.f*F),LDBroadcast::White);
            P.Text(Ring.Batch.Title,Chip.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
        }
        for(const FFace& Face0:OfferFaces)
        {
            const FOption& Option=Ring.Batch.Options[Face0.Hit.Index];
            const float Age=BatchAt<0.?1.f:FMath::Clamp(float((UiNow()-BatchAt-Face0.Hit.Index*RingRevealStagger)/RingRevealTime),0.f,1.f);
            const float Saved=P.Opacity;P.Opacity*=Age;
            const FBox2D Box=Shift(Face0.Box,FVector2D(0.,(1.f-Age)*16.f*U));
            PaintTile(P,Box,Option.IconId,Option.Name,Shown==Face0.Hit?ETile::Hot:ETile::Plain,Option.Rarity,.6f);
            // The pick key on a white tab over the tile's corner.
            const FSlateFontInfo Font=Face(EFace::Figure,20.f,F);
            const FBox2D Tab(FVector2D(Box.Min.X-6.f*U,Box.Min.Y-8.f*U),FVector2D(Box.Min.X-6.f*U+22.f*F,Box.Min.Y-8.f*U+22.f*F));
            P.Fill(RectOf(Tab),LDBroadcast::White);
            P.Text(FString::FromInt(Face0.Hit.Index+1),Tab.GetCenter(),Font,LDBroadcast::Ink,FVector2D(.5,.5));
            P.Opacity=Saved;
        }
        if(Ring.Batch.Queued>0)
        {
            const FBox2D Last=OfferFaces.Last().Box;
            const FBox2D Pile(FVector2D(Last.Max.X+12.f*U,Last.Min.Y),FVector2D(Last.Max.X+12.f*U+48.f*F,Last.Max.Y));
            P.Fill(RectOf(Pile),LDBroadcast::Ink.CopyWithNewOpacity(.92f));
            Frame(P,Pile,2.f*U,LDBroadcast::White.CopyWithNewOpacity(.34f));
            P.Text(FString::Printf(TEXT("+%d"),Ring.Batch.Queued),Pile.GetCenter(),Face(EFace::Figure,26.f,F),LDBroadcast::White,FVector2D(.5,.5));
        }
    }
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Entry){continue;}
        // The entry: 构筑 with its key, the unspent points (orange) and the unseen cards.
        const FBox2D Button=BoundsOf(Box.Shape);
        const bool bHot=Shown==Box.Hit;
        P.Fill(Lean(Button,10.f*F,10.f*F),LDBroadcast::Ink.CopyWithNewOpacity(.92f));
        if(bHot){P.Fill(RectOf(FBox2D(FVector2D(Button.Min.X+10.f*F,Button.Max.Y-4.f*F),FVector2D(Button.Max.X-10.f*F,Button.Max.Y))),LDBroadcast::White);}
        const FSlateFontInfo Label=Face(EFace::Bold,18.f,F),Key=Face(EFace::Figure,22.f,F);
        const FString OpenKey=Latest.OpenKey.IsEmpty()?FString(TEXT("B")):Latest.OpenKey;
        float X=float(Button.Min.X)+14.f*F;const float Y=float(Button.GetCenter().Y);
        X+=float(P.Text(TEXT("构筑"),FVector2D(X,Y),Label,LDBroadcast::White,FVector2D(0.,.5)).X)+8.f*F;
        const float KW=WidthOf(OpenKey,Key)+14.f*F;
        const FBox2D Kbd(FVector2D(X,Y-12.f*F),FVector2D(X+KW,Y+12.f*F));
        P.Fill(RectOf(Kbd),LDBroadcast::White);P.Text(OpenKey,Kbd.GetCenter(),Key,LDBroadcast::Ink,FVector2D(.5,.5));
        X+=KW+12.f*F;
        if(Ring.Unspent>0){X+=float(P.Text(FString::Printf(TEXT("◆%lld"),Ring.Unspent),FVector2D(X,Y),Face(EFace::Figure,26.f,F),LDBroadcast::Orange,FVector2D(0.,.5)).X)+12.f*F;}
        if(Ring.Fresh>0)
        {
            const FString Fresh=FString::Printf(TEXT("新 %d"),Ring.Fresh);
            const float W=WidthOf(Fresh,Label)+16.f*F;
            const FBox2D Tab(FVector2D(X,Y-12.f*F),FVector2D(X+W,Y+12.f*F));
            P.Fill(Lean(Tab,5.f*F,5.f*F),LDBroadcast::Orange);P.Text(Fresh,Tab.GetCenter(),Label,LDBroadcast::Ink,FVector2D(.5,.5));
        }
    }
}

int32 ULDWorkshop::PaintGround(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const
{
    if(!Ring.bValid||!Latest.bShown){return Layer;}
    const float Open=OpenAmount();
    if(Open<=0.f){return Layer;}
    // A radial falloff around the character (clear within 120 px, 44% at the far edge) and soft scrims along the left
    // edge, the bottom and (with the shop) the right edge. Never a rectangle, and never over a HUD surface (this layer is
    // under them).
    FPainter P(Elements,Geometry,Layer);
    P.Opacity=Open;
    const FLayout& L=Layout;
    const float U=L.U;
    const FVector2D Size=Geometry.GetLocalSize();
    const FVector2D C=L.Centre;
    const bool bRight=Ring.bShop;
    const auto Shade=[&](FVector2D At)
    {
        const float D=float(FVector2D::Distance(At,C))/U;
        const float Radial=D<120.f?0.f:D<240.f?FMath::Lerp(0.f,.16f,(D-120.f)/120.f):D<480.f?FMath::Lerp(.16f,.34f,(D-240.f)/240.f):FMath::Lerp(.34f,.44f,FMath::Min(1.f,(D-480.f)/520.f));
        const auto Edge=[](float X){return X<420.f?FMath::Lerp(.62f,.36f,X/420.f):X<600.f?FMath::Lerp(.36f,0.f,(X-420.f)/180.f):0.f;};
        const float Left=Edge(float(At.X)/U);
        const float Right=bRight?Edge(float(Size.X-At.X)/U):0.f;
        const float Bottom=FMath::Clamp(.66f*(1.f-float(Size.Y-At.Y)/(320.f*U)),0.f,.66f);
        const float Alpha=1.f-(1.f-Radial)*(1.f-Left)*(1.f-Right)*(1.f-Bottom);
        return LDBroadcast::Ink.CopyWithNewOpacity(Alpha);
    };
    const int32 Columns=32,Rows=18;
    // The shade of each grid corner follows the size, the centre, the unit and the shop side only: kept between frames.
    const double From[]={Size.X,Size.Y,C.X,C.Y,double(U),bRight?1.:0.};
    if(GroundAlpha.Num()!=(Columns+1)*(Rows+1)||GroundFrom.Num()!=UE_ARRAY_COUNT(From)||FMemory::Memcmp(GroundFrom.GetData(),From,sizeof(From))!=0)
    {
        GroundFrom=MakeArrayView(From);
        GroundAlpha.SetNumUninitialized((Columns+1)*(Rows+1));
        for(int32 Row=0;Row<=Rows;++Row)
        {
            for(int32 Column=0;Column<=Columns;++Column)
            {GroundAlpha[Row*(Columns+1)+Column]=Shade(FVector2D(float(Size.X)*Column/Columns,float(Size.Y)*Row/Rows)).A;}
        }
    }
    TArray<FVector2D> Top,Bottom;
    Top.Reserve(Columns+1);Bottom.Reserve(Columns+1);
    for(int32 Row=0;Row<Rows;++Row)
    {
        Top.Reset();Bottom.Reset();
        for(int32 Column=0;Column<=Columns;++Column)
        {
            const float X=float(Size.X)*Column/Columns;
            Top.Add(FVector2D(X,float(Size.Y)*Row/Rows));Bottom.Add(FVector2D(X,float(Size.Y)*(Row+1)/Rows));
        }
        P.Band(Top,Bottom,[&](FVector2D At)
        {
            const int32 Column=FMath::Clamp(FMath::RoundToInt32(float(At.X/Size.X)*Columns),0,Columns);
            const int32 Line=FMath::Clamp(FMath::RoundToInt32(float(At.Y/Size.Y)*Rows),0,Rows);
            return LDBroadcast::Ink.CopyWithNewOpacity(GroundAlpha[Line*(Columns+1)+Column]);
        });
    }
    return P.Layer;
}

int32 ULDWorkshop::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    Layer=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!Ring.bValid||!Latest.bShown){return Layer;}
    FPainter P(Elements,Geometry,Layer);
    const float Open=OpenAmount();
    if(Open>0.f)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDWorkshop_PaintOpen);
        P.Opacity=Open;
        const FHit Shown=Focus();
        TArray<uint64> Lit=LinkedTo(Shown);
        // A held card lights what it links to as well.
        if(Shown.IsNone()&&!Selected.IsNone())
        {
            const int32 Index=Ring.Hand.IndexOfByPredicate([this](const FCard& Card){return Card.Key==Selected;});
            if(Index!=INDEX_NONE){Lit=LinkedTo(FHit{EHit::Hand,INDEX_NONE,Index});}
        }
        const bool bDim=!Lit.IsEmpty();
        PaintBand(P);
        if(!Shown.IsNone()){PaintLinks(P,Lit);}
        PaintSockets(P);
        PaintHosts(P,Lit,bDim);
        PaintRadarChips(P);
        PaintTable(P);
        PaintShop(P,Lit);
        PaintRow(P,Lit);
        PaintBar(P);
        PaintMatrix(P);
        PaintSkillRules(P,Lit);
        // The element chooser and the rest confirm line, over the ring's top.
        const float F=Layout.F;
        if(Storage.IsValid()&&Storage->IsChoosing())
        {
            const TArray<ELDDemoElement> Choices2=Storage->ChooserElements();
            for(const FShape& Box:Layout.Boxes)
            {
                if(Box.Hit.Kind!=EHit::Element||!Choices2.IsValidIndex(Box.Hit.Index)){continue;}
                const FBox2D Button=BoundsOf(Box.Shape);
                P.Fill(Lean(Button,7.f*F,7.f*F),ElementInk(Choices2[Box.Hit.Index]));
                if(Focus()==Box.Hit){Frame(P,Button,3.f*F,LDBroadcast::White);}
                P.Text(LDHUD::ElementName(Choices2[Box.Hit.Index]),Button.GetCenter(),Face(EFace::Heavy,20.f,F),LDBroadcast::Ink,FVector2D(.5,.5));
            }
            P.Text(TEXT("选一种元素"),FVector2D(Layout.Centre.X,48.f*Layout.U-6.f*Layout.U),Face(EFace::Heavy,20.f,F),LDBroadcast::White,FVector2D(.5,1.));
        }
        if(Choices.IsValid()&&Choices->HasChildConfirmation())
        {
            const FString Line=Choices->ConfirmLine();
            const FSlateFontInfo Font=Face(EFace::Heavy,20.f,F);
            const float W=WidthOf(Line,Font)+36.f*F;
            const FBox2D Plate(FVector2D(Layout.Centre.X-W*.5f,16.f*Layout.U),FVector2D(Layout.Centre.X+W*.5f,16.f*Layout.U+34.f*F));
            P.Fill(Lean(Plate,8.f*F,8.f*F),LDBroadcast::LiveRed);
            P.Text(Line,Plate.GetCenter(),Font,LDBroadcast::White,FVector2D(.5,.5));
            for(const FShape& Box:Layout.Boxes)
            {
                if(Box.Hit.Kind!=EHit::ConfirmYes&&Box.Hit.Kind!=EHit::ConfirmNo){continue;}
                const FBox2D Button=BoundsOf(Box.Shape);
                const bool bYes=Box.Hit.Kind==EHit::ConfirmYes;
                P.Fill(Lean(Button,7.f*F,7.f*F),bYes?LDBroadcast::LiveRed:LDBroadcast::Ink.CopyWithNewOpacity(.92f));
                if(Focus()==Box.Hit){Frame(P,Button,2.f*F,LDBroadcast::White);}
                P.Text(bYes?TEXT("确认"):TEXT("取消"),Button.GetCenter(),Face(EFace::Bold,18.f,F),LDBroadcast::White,FVector2D(.5,.5));
            }
        }
        PaintCard(P,Lit.Num());
    }
    if(!IsOpen())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDWorkshop_PaintClosed);
        P.Opacity=1.f-Open;
        PaintClosed(P);
    }
    return P.Layer;
}

// ---- the ground layer ----------------------------------------------------------------------------------------------------

TSharedRef<SWidget> ULDBuildRingGround::RebuildWidget()
{
    if(!WidgetTree->RootWidget)
    {
        UCanvasPanel* Root=WidgetTree->ConstructWidget<UCanvasPanel>();
        Root->SetVisibility(ESlateVisibility::HitTestInvisible);
        WidgetTree->RootWidget=Root;
        SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    return Super::RebuildWidget();
}
int32 ULDBuildRingGround::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    Layer=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    const ULDWorkshop* Owner=Ring.Get();
    return Owner?Owner->PaintGround(Geometry,Elements,Layer):Layer;
}
