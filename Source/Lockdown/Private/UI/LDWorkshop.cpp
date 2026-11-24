#include "UI/LDWorkshop.h"
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
#include "Misc/ScopeExit.h"

using namespace LDBuildRing;
using LDRingPaint::FPainter;
using LDRingPaint::Hex;

namespace
{
// Motion (seconds): everything settles within 200 ms and never holds back a click or a key.
constexpr double RingOpenTime=.18;
constexpr double RingCloseTime=.12;
constexpr double RingRevealTime=.16;
constexpr double RingRevealStagger=.03;
constexpr double RingLandTime=.20;
constexpr float RingHoverRate=1.f/.08f;
// The ring's street-paint inks (the approved mock): the plate black, the pale gold of a bare host, the sticker red.
const FLinearColor RingInk=Hex(0x070A11);
const FLinearColor RingDeep=Hex(0x04060B);
const FLinearColor RingFace=Hex(0x0C111D);
const FLinearColor RingPale=Hex(0xFFE9A3);
const FLinearColor RingFreshRed=Hex(0xFF2E55);
const FLinearColor RingText=Hex(0xF4F6FA);
const FLinearColor RingSoft=Hex(0xC9D0DD);
const FLinearColor RingUp=Hex(0x6DFF9B);
// The five stats' inks in design order (体格, 脸皮, 力道, 脑力, 身手), shared by the radar's spokes and the wing's bars.
const uint32 RingStatHex[5]={0xFF5A78,0x7BD65A,0xFF9A3D,0xD47BFF,0x3FD0FF};

FLinearColor StatInk(int32 Index){return Index>=0?Hex(RingStatHex[Index%5]):FLinearColor(1.f,1.f,1.f,.35f);}
FLinearColor ElementInk(ELDDemoElement Element)
{
    switch(Element)
    {
    case ELDDemoElement::Fire: return LDBroadcast::Fire;
    case ELDDemoElement::Water: return LDBroadcast::Water;
    case ELDDemoElement::Electric: return LDBroadcast::Shock;
    case ELDDemoElement::Chill: return LDBroadcast::Chill;
    case ELDDemoElement::Rot: return LDBroadcast::Rot;
    default: return RingPale;
    }
}
// A frame's three-stop metal gradient by rarity (145 degrees, light top-left to dark bottom-right) and its band ink.
struct FFrameInk
{
    FLinearColor Light,Mid,Dark,Band;
};
FFrameInk FrameOf(ERarity Rarity)
{
    switch(Rarity)
    {
    case ERarity::Legendary: return {Hex(0xFFF6CF),Hex(0xFFB11F),Hex(0x8A4A05),LDBroadcast::Legendary};
    case ERarity::Rare: return {Hex(0xD4F0FF),Hex(0x2F8EEA),Hex(0x11347C),LDBroadcast::Rare};
    default: return {Hex(0xF7F9FC),Hex(0x9AA2B2),Hex(0x474E5F),LDBroadcast::Common};
    }
}
// The position along a CSS-style linear gradient of Degrees (180 runs top to bottom) over Box.
float GradientT(const FBox2D& Box,FVector2D At,float Degrees)
{
    const float R=FMath::DegreesToRadians(Degrees);
    const FVector2D Dir(FMath::Sin(R),-FMath::Cos(R));
    const FVector2D Size=Box.GetSize();
    const float Length=float(FMath::Abs(Size.X*Dir.X)+FMath::Abs(Size.Y*Dir.Y));
    return FMath::Clamp(.5f+float(FVector2D::DotProduct(At-Box.GetCenter(),Dir))/FMath::Max(1e-3f,Length),0.f,1.f);
}
FLinearColor Metal(const FFrameInk& Ink,const FBox2D& Box,FVector2D At)
{
    const float T=GradientT(Box,At,145.f);
    return T<.42f?LDRingPaint::Mix(Ink.Light,Ink.Mid,T/.42f):LDRingPaint::Mix(Ink.Mid,Ink.Dark,(T-.42f)/.58f);
}
FBox2D Square(FVector2D Centre,float Size){return FBox2D(Centre-FVector2D(Size,Size)*.5,Centre+FVector2D(Size,Size)*.5);}
FBox2D Grow(const FBox2D& Box,float By){return FBox2D(Box.Min-FVector2D(By,By),Box.Max+FVector2D(By,By));}
FBox2D Shift(const FBox2D& Box,FVector2D By){return FBox2D(Box.Min+By,Box.Max+By);}
TArray<FVector2D> Offset(const TArray<FVector2D>& Points,FVector2D By){TArray<FVector2D> Out=Points;for(FVector2D& P:Out){P+=By;}return Out;}
// A sticker star (the 新 badge's silhouette).
TArray<FVector2D> Star(FVector2D Centre,float Radius,int32 Points,float Inner,float Degrees)
{
    TArray<FVector2D> Out;
    for(int32 Index=0;Index<Points*2;++Index)
    {
        Out.Add(LDRingPaint::Polar(Centre,Index%2?Radius*Inner:Radius,Degrees+Index*180.f/Points));
    }
    return Out;
}
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
            UE_LOG(LogLDUI,Warning,TEXT("LD_UI_RING_ART_MISSING %s (%s); the ring paints its fallback shape."),Art[Index].Id,*Package);
        }
    }
    return Loaded[Index].Get();
}
UTexture2D* RingIcon(const FString& IconId){return IconId.IsEmpty()?nullptr:LDUIIcons::Find(IconId);}
FSlateFontInfo RingFont(LDUIFonts::ERole Role,float DesignPx,float F)
{
    // 1 pt = 4/3 px: a design px size reads at 0.75 pt.
    return LDUIFonts::Font(Role,FMath::Max(1.f,DesignPx*.75f*F));
}
FSlateFontInfo RingFigure(float DesignPx,float F)
{
    FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Title,FMath::Max(1.f,DesignPx*.75f*F));
    Font.SkewAmount=FMath::Tan(FMath::DegreesToRadians(LDBroadcast::SkewDegrees));
    return Font;
}
FString Diamonds(int32 Count){FString Out;for(int32 Index=0;Index<Count;++Index){Out+=TEXT("◆");}return Out;}
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
    return (IsOpen()&&OpenedAt>=0.&&Now-OpenedAt<RingOpenTime)||(!IsOpen()&&ClosedAt>=0.&&Now-ClosedAt<RingCloseTime)
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
    View0=Input.Progression?*Input.Progression:FLDProgressionView();
    Rebuild();
    const bool bVisible=Input.bShown&&Ring.bValid;
    const ESlateVisibility Wanted=bVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed;
    if(GetVisibility()!=Wanted){SetVisibility(Wanted);}
    if(!bVisible&&Radar){Radar->Hide();}
}
void ULDWorkshop::Rebuild()
{
    StorageView=Storage.IsValid()?Storage->Model():LDHUD::CardStorage(View0,Latest.Content);
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
    Ring=Build(In);
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

void ULDWorkshop::ComputeLayout(const FGeometry& Geometry)
{
    FLayout& L=Layout;
    L.Boxes.Reset();L.Anchors.Reset();HandFaces.Reset();OfferFaces.Reset();
    L.Size=Geometry.GetLocalSize();
    if(L.Size.X<2.||L.Size.Y<2.){L.Size=FVector2D(1920.,1080.);}
    L.K=FMath::Max(.01f,Geometry.Scale);
    const FScale S=ScaleFor(float(L.Size.Y)*L.K,TextScale);
    L.U=S.Layout/L.K;L.R=S.Ring/L.K;L.F=S.Text/L.K;
    const float U=L.U,R=L.R,F=L.F;
    const auto T=[F](float Px){return Px*F;};
    // The ring's centre: the own feet on screen, held near the approved composition so the rows never touch the HUD.
    FVector2D Wanted(L.Size.X*.5,580.*U);
    if(bHasCentre){Wanted=SmoothedCentre;}
    L.Centre=FVector2D(FMath::Clamp(Wanted.X,L.Size.X*.5-60.*U,L.Size.X*.5+60.*U),FMath::Clamp(Wanted.Y,540.*U,620.*U));
    const FVector2D C=L.Centre;
    const auto Add=[&L](EHit Kind,int32 Host,int32 Index,TArray<FVector2D> Shape){FShape& Box=L.Boxes.AddDefaulted_GetRef();Box.Hit={Kind,Host,Index};Box.Shape=MoveTemp(Shape);return Box.Hit;};
    const auto Rect=[](const FBox2D& Box){return TArray<FVector2D>{Box.Min,FVector2D(Box.Max.X,Box.Min.Y),Box.Max,FVector2D(Box.Min.X,Box.Max.Y)};};
    if(!Ring.bValid){return;}
    const bool bOpen=IsOpen();
    if(bOpen)
    {
        const bool bFull=Ring.Capacity>0&&Ring.Installed>=Ring.Capacity;
        for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num();++HostIndex)
        {
            const FHost& Host=Ring.Hosts[HostIndex];
            const float A=HostAngle[HostIndex];
            const int32 Slots=Host.Cards.Num()+(bFull?0:1);
            const float Outer=RowRadius[RowsFor(Slots)-1]+36.f;
            {
                FShape& Box=L.Boxes.AddDefaulted_GetRef();Box.Hit={EHit::Drop,HostIndex,INDEX_NONE};Box.Centre=C;Box.bWedge=true;
                Box.Wedge=FVector4f(SectorInner*R,Outer*R,A-SectorHalf,A+SectorHalf);
            }
            for(int32 Index=0;Index<Slots;++Index)
            {
                const FVector2D Polar=SlotPolar(Index,A);
                const FVector2D At=LDRingPaint::Polar(C,float(Polar.X)*R,float(Polar.Y));
                if(Index<Host.Cards.Num())
                {
                    Add(EHit::Installed,HostIndex,Index,LDRingPaint::Rounded(Square(At,NodeSize*R),10.f*R,2));
                    L.Anchors.Add(AnchorKey(EHit::Installed,HostIndex,Index),At);
                }
                else{Add(EHit::Drop,HostIndex,Index,LDRingPaint::Rounded(Square(At,(NodeSize-4.f)*R),10.f*R,2));}
            }
            const FVector2D HostAt=LDRingPaint::Polar(C,HostRadius*R,A);
            Add(EHit::Host,HostIndex,INDEX_NONE,LDRingPaint::HostCut(Square(HostAt,HostSize*R),HostSize*R*.2f));
            L.Anchors.Add(AnchorKey(EHit::Host,HostIndex,INDEX_NONE),HostAt);
            if(Host.bSocket){Add(EHit::Gem,HostIndex,INDEX_NONE,LDRingPaint::Octagon(Square(LDRingPaint::Polar(C,GemRadius*R,A),GemSize*R)));}
        }
        // The stat chips at the radar's corners: on the ground decal when it is laid, else around the centre.
        for(int32 Index=0;Index<Ring.Stats.Num();++Index)
        {
            const float Degrees=Index*360.f/FMath::Max(1,Ring.Stats.Num());
            FVector2D At=LDRingPaint::Polar(C,107.f*R,Degrees);
            if(Radar&&Radar->IsShown())
            {
                FVector2D Screen;
                if(UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),Radar->WorldAt(112.f,Degrees),Screen,false)){At=Screen;}
            }
            const FVector2D Size(T(88.f)+T(24.f),T(24.f));
            L.Anchors.Add(AnchorKey(EHit::Stat,INDEX_NONE,Index),At);
            Add(EHit::Stat,INDEX_NONE,Index,LDRingPaint::Slant(FBox2D(At-Size*.5,At+Size*.5),8.f*F));
        }
        // The inspector's room in the top-left corner; the wing starts below it, whatever the text size.
        const float InspectorTop=96.f*U;
        L.InspectorBottom=InspectorTop+28.f*U+T(20.f)*1.25f+T(14.f)*1.4f+8.f*U+4.f*T(16.f)*1.35f;
        // The top-right buttons: 收起 and the reaction table.
        {
            const FVector2D Close(L.Size.X-40.f*U-T(76.f),96.f*U);
            Add(EHit::Close,INDEX_NONE,INDEX_NONE,Rect(FBox2D(Close,Close+FVector2D(T(76.f),T(30.f)))));
            const FVector2D Table(Close.X-T(96.f)-T(4.f),96.f*U);
            Add(EHit::Matrix,INDEX_NONE,INDEX_NONE,Rect(FBox2D(Table,Table+FVector2D(T(92.f),T(30.f)))));
            ActionsAt=FVector2D(L.Size.X-40.f*U,96.f*U+T(30.f)+18.f*U);
        }
    }
    // The ring's lowest point (the lower sectors' outer corners and the radar chips): the plates and the hand stay under it.
    float RingBottom=float(C.Y);
    if(bOpen)
    {
        const bool bFullNow=Ring.Capacity>0&&Ring.Installed>=Ring.Capacity;
        for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num();++HostIndex)
        {
            const float Outer=(RowRadius[RowsFor(Ring.Hosts[HostIndex].Cards.Num()+(bFullNow?0:1))-1]+36.f+11.f)*R;
            for(const float Edge:{HostAngle[HostIndex]-SectorHalf,HostAngle[HostIndex]+SectorHalf})
            {RingBottom=FMath::Max(RingBottom,float(LDRingPaint::Polar(C,Outer,Edge).Y));}
        }
        for(const FShape& Box:L.Boxes){if(Box.Hit.Kind==EHit::Stat){for(const FVector2D& Point:Box.Shape){RingBottom=FMath::Max(RingBottom,float(Point.Y));}}}
    }
    L.RingBottom=RingBottom;
    // Above the cards: the level and reaction plates (and the batch title) take this much.
    const float PlateBand=12.f*U+T(30.f);
    // The bottom: the offer on top (or the hand), above the HUD band and right of the minimap.
    const float BandTop=Latest.Band.Bottom>Latest.Band.Top?float(Geometry.AbsoluteToLocal(FVector2D(Latest.Band.Left,Latest.Band.Top)).Y):L.Size.Y;
    const float Y1=FMath::Min(890.f*U,BandTop-8.f*U);
    const float Left=310.f*U,Right=1490.f*U;
    L.HandBottom=Y1;
    if(bOpen)
    {
        const bool bOffers=Ring.Batch.Kind!=EBatch::None&&!bShowHand;
        if(bOffers)
        {
            const int32 Count=Ring.Batch.Options.Num();
            const float Gap=16.f*U;
            const float W=FMath::Min(372.f*U,(Right-Left-150.f*U-(Count-1)*Gap)/FMath::Max(1,Count));
            L.OfferLines=3;
            const auto Height=[&](int32 Lines){return FMath::Max(108.f*U,24.f*U+T(18.f)*1.2f+Lines*T(16.f)*1.3f);};
            while(L.OfferLines>1&&Y1-Height(L.OfferLines)-PlateBand<RingBottom+4.f*U){--L.OfferLines;}
            const float H=Height(L.OfferLines);
            const float X0=float(C.X)-(Count*W+(Count-1)*Gap)*.5f;
            const float Top=Y1-H;
            L.HandTop=Top;
            for(int32 Index=0;Index<Count;++Index)
            {
                FFace& Face=OfferFaces.AddDefaulted_GetRef();
                Face.Hit={EHit::Offer,INDEX_NONE,Index};
                Face.Box=FBox2D(FVector2D(X0+Index*(W+Gap),Top),FVector2D(X0+Index*(W+Gap)+W,Top+H));
                Add(EHit::Offer,INDEX_NONE,Index,Rect(Face.Box));
                L.Anchors.Add(AnchorKey(EHit::Offer,INDEX_NONE,Index),FVector2D(Face.Box.GetCenter().X,Top));
            }
            const FBox2D HandPile(FVector2D(X0-108.f*U,Top+14.f*U),FVector2D(X0-30.f*U,Y1));
            Add(EHit::HandPile,INDEX_NONE,INDEX_NONE,Rect(HandPile));
            if(Ring.Batch.Queued>0)
            {
                const float PX=X0+Count*(W+Gap)+4.f*U;
                Add(EHit::OfferPile,INDEX_NONE,INDEX_NONE,Rect(FBox2D(FVector2D(PX,Top+14.f*U),FVector2D(PX+78.f*U,Y1))));
            }
            // Reroll and skip stand right of the batch title.
            const float TitleW=float(LDRingPaint::Measure(Ring.Batch.Title,RingFont(LDUIFonts::ERole::Title,14.f,F)).X)+24.f*F;
            float X=float(C.X)+TitleW*.5f+10.f*U;
            const float Y=Top-14.f*U-T(24.f);
            if(Ring.Batch.Kind==EBatch::Card)
            {
                Add(EHit::Reroll,INDEX_NONE,INDEX_NONE,Rect(FBox2D(FVector2D(X,Y),FVector2D(X+T(84.f),Y+T(24.f)))));X+=T(84.f)+6.f*U;
                Add(EHit::Skip,INDEX_NONE,INDEX_NONE,Rect(FBox2D(FVector2D(X,Y),FVector2D(X+T(56.f),Y+T(24.f)))));
            }
            else if(Ring.Batch.Kind==EBatch::Active&&Ring.Batch.SkipPoints>0)
            {
                Add(EHit::Skip,INDEX_NONE,INDEX_NONE,Rect(FBox2D(FVector2D(X,Y),FVector2D(X+T(96.f),Y+T(24.f)))));
            }
        }
        else
        {
            // The hand: one fanned group per form, cards overlapping like a poker hand when they run out of room.
            float CW=FMath::Max(108.f*U,4.25f*T(14.f)+12.f*U);
            {
                const float Room=Y1-24.f*U-PlateBand-RingBottom-4.f*U;
                const float Fit=Room-4.f*U-2.36f*T(14.f);
                CW=FMath::Clamp(Fit,40.f/L.K+8.f*U,CW);
            }
            L.CardWidth=CW;
            const float CH=8.f*U+(CW-8.f*U)+2.36f*T(14.f)+4.f*U;
            const float MW=FMath::Max(82.f*U,46.f/L.K);
            const float GroupGap=30.f*U;
            TArray<TArray<int32>> Groups;Groups.SetNum(4);
            for(int32 Index=0;Index<Ring.Hand.Num();++Index){Groups[HandGroup(Ring.Hand[Index].Form)].Add(Index);}
            Groups.RemoveAll([](const TArray<int32>& Group){return Group.IsEmpty();});
            float Fixed=0.f;int32 Over=0;
            for(const TArray<int32>& Group:Groups)
            {
                Fixed+=Ring.Hand[Group[0]].Form==ELDCardForm::Medal?MW:CW;Over+=Group.Num()-1;
            }
            Fixed+=FMath::Max(0,Groups.Num()-1)*GroupGap;
            const float Pitch=Over>0?FMath::Min(CW+10.f*U,(Right-Left-Fixed)/Over):0.f;
            const float Total=Fixed+Over*Pitch;
            float X=float(C.X)-Total*.5f;
            // The fan's outer cards drop and turn: the row stands high enough that none crosses the band line.
            const float Top=Y1-CH-24.f*U;
            L.HandTop=Top;
            for(const TArray<int32>& Group:Groups)
            {
                const bool bMedal=Ring.Hand[Group[0]].Form==ELDCardForm::Medal;
                const float W=bMedal?MW:CW;
                const float P=bMedal?FMath::Min(Pitch,MW+10.f*U):Pitch;
                for(int32 Within=0;Within<Group.Num();++Within)
                {
                    const int32 Index=Group[Within];
                    const float Mid=X+Within*P+W*.5f;
                    const float Along=(Mid-float(C.X))/(Right-Left)*2.f;
                    FFace& Face=HandFaces.AddDefaulted_GetRef();
                    Face.Hit={EHit::Hand,INDEX_NONE,Index};
                    Face.Degrees=bMedal?0.f:Along*5.f;
                    const float Dy=Along*Along*18.f*U;
                    Face.Box=bMedal?FBox2D(FVector2D(X+Within*P,Top+Dy+22.f*U),FVector2D(X+Within*P+W,Top+Dy+22.f*U+W))
                        :FBox2D(FVector2D(X+Within*P,Top+Dy),FVector2D(X+Within*P+W,Top+Dy+CH));
                    L.Anchors.Add(AnchorKey(EHit::Hand,INDEX_NONE,Index),FVector2D(Mid,Top+Dy+10.f*U));
                }
                X+=(Group.Num()-1)*P+W+GroupGap;
            }
            for(const FFace& Face:HandFaces)
            {
                // A card's hit shape is its rotated frame (about 50% / 130% like the mock's transform origin).
                const FVector2D Pivot(Face.Box.GetCenter().X,Face.Box.Min.Y+Face.Box.GetSize().Y*1.3);
                const FTransform2D Turn=FTransform2D(-Pivot).Concatenate(FTransform2D(FQuat2D(FMath::DegreesToRadians(Face.Degrees)))).Concatenate(FTransform2D(Pivot));
                TArray<FVector2D> Shape=Rect(Face.Box);for(FVector2D& Point:Shape){Point=Turn.TransformPoint(Point);}
                Add(EHit::Hand,INDEX_NONE,Face.Hit.Index,MoveTemp(Shape));
            }
            if(Ring.Batch.Kind!=EBatch::None)
            {
                Add(EHit::OfferPile,INDEX_NONE,INDEX_NONE,Rect(FBox2D(FVector2D(float(C.X)-Total*.5f-108.f*U,Top+24.f*U),FVector2D(float(C.X)-Total*.5f-30.f*U,Y1))));
            }
            if(Ring.Hand.IsEmpty()){L.HandTop=Y1-CH-24.f*U;}
        }
        // The left wing: the derived attributes hang on the outer arc between the inspector's corner and the level plate.
        // When their rows do not fit that height (a small screen with large text) they pair up as glyph chips.
        {
            const int32 Count=Ring.Derived.Num();
            const float RowH=T(28.f);
            const float Top=L.InspectorBottom+8.f*U+RowH*.5f;
            const float Bottom=L.HandTop-12.f*U-T(30.f)-10.f*U-RowH*.5f;
            const float Reach=(WingRadius-4.f)*R;
            const auto ArcX=[&](float Y){const float Dy=float(Y-C.Y);return float(C.X)-FMath::Sqrt(FMath::Max(0.f,Reach*Reach-Dy*Dy));};
            L.bWingCompact=Count>1&&(Count-1)*(RowH+3.f*U)>Bottom-Top;
            L.WingWidth=L.bWingCompact?T(104.f):FMath::Max(190.f*F,T(150.f));
            const int32 Lines=L.bWingCompact?(Count+1)/2:Count;
            const float Step=Lines>1?FMath::Min((Bottom-Top)/(Lines-1),RowH*1.65f):0.f;
            for(int32 Index=0;Index<Count;++Index)
            {
                const int32 Line=L.bWingCompact?Index/2:Index;
                const float Y=Top+Line*Step;
                const float Edge=FMath::Max(ArcX(Y),(L.bWingCompact?2.f*L.WingWidth+6.f*U:L.WingWidth)+16.f*U);
                const float X=L.bWingCompact&&Index%2==0?Edge-L.WingWidth-6.f*U:Edge;
                const FVector2D At(X,Y);
                Add(EHit::Wing,INDEX_NONE,Index,Rect(FBox2D(FVector2D(At.X-L.WingWidth,At.Y-RowH*.5f),FVector2D(At.X,At.Y+RowH*.5f))));
                L.Anchors.Add(AnchorKey(EHit::Wing,INDEX_NONE,Index),At);
            }
        }
        // The right wing: the rest shop. Its label sits under the top HUD (the banner, and in a rest its actions in the
        // corner) and under the ring's own buttons; its rows hang on the outer arc down to the plates.
        if(Ring.bShop)
        {
            const float Z=FMath::Max(54.f*U,36.f/L.K);
            const float RowH=FMath::Max(Z,T(44.f));
            float Ceiling=96.f*U+T(30.f)+8.f*U;
            if(Latest.Banner.Bottom>Latest.Banner.Top)
            {
                const FVector2D BannerMin=Geometry.AbsoluteToLocal(FVector2D(Latest.Banner.Left,Latest.Banner.Top));
                const FVector2D BannerMax=Geometry.AbsoluteToLocal(FVector2D(Latest.Banner.Right,Latest.Banner.Bottom));
                // Only a banner standing over the right wing pushes it down.
                if(BannerMax.X>C.X+200.f*R){Ceiling=FMath::Max(Ceiling,float(BannerMax.Y)+8.f*U);}
                (void)BannerMin;
            }
            const float Reach=WingRadius*R;
            const auto ArcX=[&](float Y){const float Dy=float(Y-C.Y);return float(C.X)+FMath::Sqrt(FMath::Max(0.f,Reach*Reach-Dy*Dy));};
            const int32 Count=Ring.Shop.Num();
            const float Top=Ceiling+T(24.f)+10.f*U+RowH*.5f;
            const float Bottom=L.HandTop-PlateBand-RowH*.5f;
            const float Step=Count>1?FMath::Min((Bottom-Top)/(Count-1),RowH+16.f*U):0.f;
            const float Width=Z+T(150.f);
            for(int32 Index=0;Index<Count;++Index)
            {
                const float Y=Top+Index*Step;
                const float X=FMath::Min(ArcX(Y),float(L.Size.X)-16.f*U-Width+Z*.5f);
                const FVector2D At(X,Y);
                Add(EHit::Shop,INDEX_NONE,Index,Rect(FBox2D(FVector2D(At.X-Z*.5,At.Y-Z*.5),FVector2D(At.X-Z*.5+Width,At.Y+Z*.5))));
                L.Anchors.Add(AnchorKey(EHit::Shop,INDEX_NONE,Index),At);
            }
            const float LabelX=FMath::Min(ArcX(Top)-Z*.5f,float(L.Size.X)-16.f*U-T(110.f)-T(100.f));
            L.ShopLabel=FVector2D(LabelX,Ceiling);
            const FVector2D At(LabelX+T(110.f),Ceiling);
            Add(EHit::ShopRefresh,INDEX_NONE,INDEX_NONE,Rect(FBox2D(At,At+FVector2D(T(96.f),T(24.f)))));
        }
        // The rest's respec chip beside the level plate.
        if(Latest.Stage==LDProgression::EMatchStage::Rest)
        {
            const float PlateTop=L.HandTop-12.f*U-T(30.f);
            const FVector2D At(800.f*U-LevelPlateWidth()-8.f*U-T(72.f),PlateTop);
            Add(EHit::Respec,INDEX_NONE,INDEX_NONE,Rect(FBox2D(At,At+FVector2D(T(72.f),T(30.f)))));
        }
        // The element chooser of a card that asks for one, and a rest service's confirm line.
        if(Storage.IsValid()&&Storage->IsChoosing())
        {
            const TArray<ELDDemoElement> Elements=Storage->ChooserElements();
            const float W=T(64.f);
            const float X0=float(C.X)-Elements.Num()*(W+6.f*U)*.5f;
            for(int32 Index=0;Index<Elements.Num();++Index)
            {
                const FVector2D At(X0+Index*(W+6.f*U),250.f*U);
                Add(EHit::Element,INDEX_NONE,Index,Rect(FBox2D(At,At+FVector2D(W,T(30.f)))));
            }
        }
        if(Choices.IsValid()&&Choices->HasChildConfirmation())
        {
            const FVector2D At(float(C.X)+T(10.f),250.f*U+T(36.f));
            Add(EHit::ConfirmYes,INDEX_NONE,INDEX_NONE,Rect(FBox2D(At-FVector2D(T(96.f),0.f),At+FVector2D(-T(10.f),T(30.f)))));
            Add(EHit::ConfirmNo,INDEX_NONE,INDEX_NONE,Rect(FBox2D(At,At+FVector2D(T(86.f),T(30.f)))));
        }
    }
    else
    {
        // Closed: the offer on top waits right of the HUD band, the entry under it.
        const float X0=Latest.Band.Right>Latest.Band.Left?float(Geometry.AbsoluteToLocal(FVector2D(Latest.Band.Right,0.)).X)+24.f*U:1336.f*U;
        const float Z=FMath::Max(60.f*U,40.f/L.K);
        const float Y=948.f*U;
        for(int32 Index=0;Index<Ring.Batch.Options.Num();++Index)
        {
            const FVector2D At(X0+Index*(Z+14.f*U),Y-Z*.5f);
            Add(EHit::Offer,INDEX_NONE,Index,Rect(FBox2D(At,At+FVector2D(Z,Z))));
            FFace& Face=OfferFaces.AddDefaulted_GetRef();Face.Hit={EHit::Offer,INDEX_NONE,Index};Face.Box=FBox2D(At,At+FVector2D(Z,Z));
        }
        const FVector2D Entry(X0,1022.f*U);
        Add(EHit::Entry,INDEX_NONE,INDEX_NONE,Rect(FBox2D(Entry,Entry+FVector2D(T(200.f),T(30.f)))));
    }
}

ULDWorkshop::FHit ULDWorkshop::HitAt(FVector2D Local) const
{
    for(int32 Index=Layout.Boxes.Num()-1;Index>=0;--Index)
    {
        const FShape& Box=Layout.Boxes[Index];
        if(Box.bWedge)
        {
            const FVector2D D=Local-Box.Centre;
            const float Radius=float(D.Size());
            const float Degrees=FMath::RadiansToDegrees(FMath::Atan2(float(D.X),float(-D.Y)));
            if(Radius>=Box.Wedge.X&&Radius<=Box.Wedge.Y&&Degrees>=Box.Wedge.Z&&Degrees<=Box.Wedge.W){return Box.Hit;}
            continue;
        }
        if(Box.Radius>0.f){if(FVector2D::Distance(Local,Box.Centre)<=Box.Radius){return Box.Hit;}continue;}
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
    TArray<FBox2D> WingRows;
    for(const FShape& Box:L.Boxes)
    {
        if(Box.bWedge){continue;}
        FBox2D Bounds(ForceInit);
        for(const FVector2D& Point:Box.Shape){Bounds+=Point;}
        const FString Part=FString::Printf(TEXT("part %d/%d/%d (%.0f,%.0f)-(%.0f,%.0f)"),int32(Box.Hit.Kind),Box.Hit.Host,Box.Hit.Index,Bounds.Min.X,Bounds.Min.Y,Bounds.Max.X,Bounds.Max.Y);
        if(Bounds.Min.X<-1.||Bounds.Min.Y<-1.||Bounds.Max.X>L.Size.X+1.||Bounds.Max.Y>L.Size.Y+1.){Out.Add(TEXT("off screen ")+Part);}
        if(Box.Hit.Kind==EHit::Wing)
        {
            if(Bounds.Min.Y<L.InspectorBottom-.5f){Out.Add(TEXT("under the inspector ")+Part);}
            if(Bounds.Max.Y>L.HandTop+.5f){Out.Add(TEXT("into the hand ")+Part);}
            for(const FBox2D& Other:WingRows)
            {
                if(!(Bounds.Max.X<=Other.Min.X+.5||Other.Max.X<=Bounds.Min.X+.5||Bounds.Max.Y<=Other.Min.Y+.5||Other.Max.Y<=Bounds.Min.Y+.5)){Out.Add(TEXT("wing overlap ")+Part);}
            }
            WingRows.Add(Bounds);
        }
        if(Box.Hit.Kind==EHit::Hand&&Bounds.Max.Y>L.HandBottom+1.){Out.Add(TEXT("below the band line ")+Part);}
    }
    return Out;
}

// ---- tick: the pointer, the radar, the springs ---------------------------------------------------------------------------

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
    const float Ring92=RadarRadius*Layout.R;
    const float RadiusCm=PerCm>1e-3f?Ring92/PerCm:60.f;
    SmoothedCentre=bHasCentre?FMath::Lerp(SmoothedCentre,Centre,.35):Centre;bHasCentre=true;
    FLDBuildRadarState State;
    for(const FStat& Stat:Ring.Stats){State.Points.Add(Stat.Points);State.Inks.Add(StatInk(State.Inks.Num()));}
    State.Keystone=Ring.Keystone;
    State.bClosed=!IsOpen();
    State.Installed=Ring.Installed;State.Capacity=Ring.Capacity;State.MaxCapacity=Ring.MaxCapacity;
    for(int32 Index=0;Index<Ring.Hosts.Num();++Index)
    {
        if(Ring.Hosts[Index].bHasMedal){State.Medals.Add(TPair<float,FLinearColor>(HostAngle[Index],ElementInk(Ring.Hosts[Index].Medal.Element)));}
    }
    State.Opacity=IsOpen()?FMath::Max(.35f,OpenAmount()):.62f;
    Radar->Show(State,Feet,Yaw,RadiusCm);
}
void ULDWorkshop::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    UpdateRadar();
    ComputeLayout(Geometry);
    const double Now=UiNow();
    const float Delta=LastTick<0.?0.f:float(Now-LastTick);LastTick=Now;
    if(FrozenOpen>=0.f&&IsOpen()){OpenedAt=Now-FrozenOpen;}
    if(FrozenReveal>=0.f&&BatchAt>=0.){BatchAt=Now-FrozenReveal;}
    FHit Next;
    if(FSlateApplication::IsInitialized()&&Ring.bValid)
    {
        const FVector2D Pointer=Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
        if(Pointer.X>=0.&&Pointer.Y>=0.&&Pointer.X<=Layout.Size.X&&Pointer.Y<=Layout.Size.Y){Next=HitAt(Pointer);}
    }
    if(!(Next==Hovered)){Hovered=Next;HoveredAt=Now;}
    // A hand card looked at loses its 新 sticker.
    const FHit Shown=Focus();
    if(Shown.Kind==EHit::Hand&&Ring.Hand.IsValidIndex(Shown.Index)&&Ring.Hand[Shown.Index].bFresh&&Storage.IsValid())
    {Storage->MarkSeen(Ring.Hand[Shown.Index].Key);}
    // The lift springs of hand and offer faces.
    const auto Spring=[&](uint64 Key,bool bUp){float& Value=Lift.FindOrAdd(Key);Value=FMath::Clamp(Value+(bUp?1.f:-1.f)*RingHoverRate*Delta,0.f,1.f);};
    for(const FFace& Face:HandFaces)
    {
        const bool bUp=(Shown.Kind==EHit::Hand&&Shown.Index==Face.Hit.Index)||(Ring.Hand.IsValidIndex(Face.Hit.Index)&&Ring.Hand[Face.Hit.Index].Key==Selected);
        Spring(AnchorKey(EHit::Hand,INDEX_NONE,Face.Hit.Index),bUp);
    }
    for(const FFace& Face:OfferFaces){Spring(AnchorKey(EHit::Offer,INDEX_NONE,Face.Hit.Index),Shown.Kind==EHit::Offer&&Shown.Index==Face.Hit.Index);}
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
void ULDWorkshop::Click(const FHit& Hit,bool bRight)
{
    ULDComponentStorage* Cards=Storage.Get();
    ULDChoicesPanel* Tray=Choices.Get();
    const FCard* Held=SelectedCard();
    const auto Install=[&](int32 HostIndex,FName OnKey)
    {
        if(!Held||!Cards||!Ring.Hosts.IsValidIndex(HostIndex)){return;}
        const FHost& Host=Ring.Hosts[HostIndex];
        if(Held->Form==ELDCardForm::Medal){Tell(TEXT("勋章镶进八角槽，不占构筑格"));return;}
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
        if(Ring.Shop[Hit.Index].Slot==FLDBuildCommand::ShopSkillPointSlot){Tray->BuyShopSkillPoint();}else{Tray->BuyShopSlot(Ring.Shop[Hit.Index].Slot);}
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

float ULDWorkshop::LevelPlateWidth() const
{
    const float U=Layout.U,F=Layout.F;
    const FSlateFontInfo Label=RingFont(LDUIFonts::ERole::Strong,14.f,F),Figure=RingFigure(21.f,F);
    const FString Lv=FString::FromInt(Ring.Level),Used=FString::FromInt(Ring.Installed),Cap=FString::Printf(TEXT(" / %d"),Ring.Capacity);
    const FString Points=FString::FromInt(Ring.Unspent);
    return 16.f*F+float(LDRingPaint::Measure(TEXT("LV "),Label).X+LDRingPaint::Measure(Lv,Figure).X)+12.f*F
        +float(LDRingPaint::Measure(TEXT("构筑 "),Label).X+LDRingPaint::Measure(Used,Figure).X+LDRingPaint::Measure(Cap,Label).X)+12.f*F
        +Ring.Capacity*11.f*U+12.f*F+float(LDRingPaint::Measure(TEXT("属性点 "),Label).X+LDRingPaint::Measure(Points,Figure).X)+16.f*F;
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

// ---- paint ---------------------------------------------------------------------------------------------------------------

void ULDWorkshop::PaintNode(FPainter& P,const FBox2D& Box,const FCard& Card,float Dim,bool bHot,bool bLit,float Flash) const
{
    const float R=Layout.R;
    const float Saved=P.Opacity;P.Opacity*=Dim;
    const FBox2D Face=bHot?Grow(Box,float(Box.GetSize().X)*.07f):Box;
    const float Radius=10.f*R*float(Face.GetSize().X)/(NodeSize*R);
    const FFrameInk Ink=FrameOf(Card.Rarity);
    const TArray<FVector2D> Outline=LDRingPaint::Rounded(Face,Radius,3);
    // The hard drop shadow, a lit (linked) halo, then the metal frame.
    P.Fill(Offset(Outline,FVector2D(3.f,4.f)*R),FLinearColor(0.f,0.f,0.f,.62f));
    if(bLit)
    {
        P.Fill(LDRingPaint::Rounded(Grow(Face,6.f*R),Radius+6.f*R,3),LDBroadcast::Gold.CopyWithNewOpacity(.35f));
        P.Fill(LDRingPaint::Rounded(Grow(Face,2.5f*R),Radius+2.5f*R,3),FLinearColor(1.f,1.f,1.f,.9f));
    }
    if(Card.Rarity==ERarity::Legendary){P.Fill(LDRingPaint::Rounded(Grow(Face,4.f*R),Radius+4.f*R,3),LDBroadcast::Legendary.CopyWithNewOpacity(.35f));}
    P.FillShaded(Outline,[&](FVector2D At){return Metal(Ink,Face,At);});
    if(UTexture2D* Brushed=RingArt(EArt::Metal)){P.Image(Brushed,Face,FLinearColor(1.f,1.f,1.f,.35f),&Outline);}
    const FBox2D Inner=Grow(Face,-3.f*R*float(Face.GetSize().X)/(NodeSize*R));
    const TArray<FVector2D> InnerShape=LDRingPaint::Rounded(Inner,Radius*.7f,3);
    P.Fill(InnerShape,RingFace);
    if(UTexture2D* Icon=RingIcon(Card.IconId)){P.Image(Icon,Inner,FLinearColor::White,&InnerShape);}
    else{P.Text(Card.Name.Left(1),Inner.GetCenter(),RingFont(LDUIFonts::ERole::Title,22.f,Layout.F),RingPale,FVector2D(.5,.5));}
    // Glass: a light sweep at the top edge, a black keyline.
    P.FillShaded(LDRingPaint::ClipConvex({Inner.Min,FVector2D(Inner.Max.X,Inner.Min.Y),FVector2D(Inner.Max.X,Inner.Min.Y+Inner.GetSize().Y*.3),FVector2D(Inner.Min.X,Inner.Min.Y+Inner.GetSize().Y*.42)},InnerShape),
        [&](FVector2D At){return FLinearColor(1.f,1.f,1.f,.2f*(1.f-GradientT(Inner,At,165.f)*1.6f));});
    P.Stroke(InnerShape,1.5f*R,RingDeep,true);
    // Rarity by construction: a rare card carries a corner stripe, a legendary one the stripe and a moving sheen.
    if(Card.Rarity!=ERarity::Common)
    {
        const float S=float(Inner.GetSize().X);
        const TArray<FVector2D> Stripe={FVector2D(Inner.Max.X-S*.34f,Inner.Min.Y),FVector2D(Inner.Max.X-S*.2f,Inner.Min.Y),FVector2D(Inner.Max.X,Inner.Min.Y+S*.2f),FVector2D(Inner.Max.X,Inner.Min.Y+S*.34f)};
        P.Fill(LDRingPaint::ClipConvex(Stripe,InnerShape),Ink.Band);
    }
    if(Card.Rarity==ERarity::Legendary)
    {
        const float Phase=FMath::Fmod(float(UiNow())/3.6f,1.f);
        const float S=float(Inner.GetSize().X);
        const float X=Inner.Min.X+FMath::Lerp(-.6f,1.6f,FMath::Min(1.f,Phase/.55f))*S;
        const TArray<FVector2D> Band={FVector2D(X,Inner.Min.Y),FVector2D(X+S*.22f,Inner.Min.Y),FVector2D(X+S*.02f,Inner.Max.Y),FVector2D(X-S*.2f,Inner.Max.Y)};
        P.Fill(LDRingPaint::ClipConvex(Band,InnerShape),FLinearColor(1.f,1.f,1.f,.38f));
    }
    if(Flash>0.f){P.Fill(LDRingPaint::Rounded(Grow(Face,Flash*8.f*R),Radius,3),FLinearColor(1.f,1.f,1.f,.55f*Flash));}
    if(Card.Copies>1)
    {
        // ×N on a gold slanted tag at the top-right corner.
        const FSlateFontInfo Font=RingFigure(17.f,Layout.F);
        const FString Text=FString::Printf(TEXT("×%d"),Card.Copies);
        const FVector2D Size=LDRingPaint::Measure(Text,Font);
        const float H=21.f*Layout.F,W=float(Size.X)+16.f*Layout.F;
        const FBox2D Tag(FVector2D(Face.Max.X+8.f*Layout.U-W,Face.Min.Y-9.f*Layout.U),FVector2D(Face.Max.X+8.f*Layout.U,Face.Min.Y-9.f*Layout.U+H));
        P.Fill(LDRingPaint::Slant(Tag,8.f*Layout.F),LDBroadcast::Gold);
        P.Text(Text,Tag.GetCenter(),Font,Hex(0x16120A),FVector2D(.5,.5));
    }
    P.Opacity=Saved;
}

void ULDWorkshop::PaintDim(FPainter& P) const
{
    // The world darkens around the ring, deepest at the edges, never over the HUD band or the minimap.
    const FLayout& L=Layout;
    const FGeometry& G=GetCachedGeometry();
    TArray<FBox2D> Holes;
    for(const FSlateRect& Rect:{Latest.Band,Latest.Map})
    {
        if(Rect.Right<=Rect.Left||Rect.Bottom<=Rect.Top){continue;}
        Holes.Add(FBox2D(G.AbsoluteToLocal(FVector2D(Rect.Left,Rect.Top))-FVector2D(2.,2.),G.AbsoluteToLocal(FVector2D(Rect.Right,Rect.Bottom))+FVector2D(2.,2.)));
    }
    TArray<float> Ys={0.f,float(L.Size.Y)};
    for(const FBox2D& Hole:Holes){Ys.Add(float(Hole.Min.Y));Ys.Add(float(Hole.Max.Y));}
    Ys.Sort();
    const float Reach=860.f*L.U;
    const auto Shade=[&](FVector2D At)
    {
        const float T=FMath::Clamp(float(FVector2D::Distance(At,L.Centre))/Reach,0.f,1.f);
        const float A=T<.5f?FMath::Lerp(.34f,.56f,T/.5f):FMath::Lerp(.56f,.8f,(T-.5f)/.5f);
        return Hex(0x05070D,A);
    };
    const int32 Columns=16;
    for(int32 Band=0;Band+1<Ys.Num();++Band)
    {
        const float Y0=FMath::Clamp(Ys[Band],0.f,float(L.Size.Y)),Y1=FMath::Clamp(Ys[Band+1],0.f,float(L.Size.Y));
        if(Y1-Y0<.5f){continue;}
        // The x spans of this band outside every hole.
        TArray<FVector2D> Spans={FVector2D(0.,L.Size.X)};
        for(const FBox2D& Hole:Holes)
        {
            if(Hole.Max.Y<=Y0+.1f||Hole.Min.Y>=Y1-.1f){continue;}
            TArray<FVector2D> Next;
            for(const FVector2D& Span:Spans)
            {
                if(Hole.Max.X<=Span.X||Hole.Min.X>=Span.Y){Next.Add(Span);continue;}
                if(Hole.Min.X>Span.X){Next.Add(FVector2D(Span.X,Hole.Min.X));}
                if(Hole.Max.X<Span.Y){Next.Add(FVector2D(Hole.Max.X,Span.Y));}
            }
            Spans=MoveTemp(Next);
        }
        for(const FVector2D& Span:Spans)
        {
            const int32 N=FMath::Max(1,FMath::CeilToInt(float(Span.Y-Span.X)/float(L.Size.X)*Columns));
            TArray<FVector2D> Top,Bottom;
            for(int32 Step=0;Step<=N;++Step)
            {
                const float X=FMath::Lerp(float(Span.X),float(Span.Y),float(Step)/N);
                Top.Add(FVector2D(X,Y0));Bottom.Add(FVector2D(X,Y1));
            }
            P.Band(Top,Bottom,Shade);
        }
    }
    // The tray behind the hand: a darker floor so the cards read on any ground.
    const float FloorTop=L.HandTop-90.f*L.U;
    if(L.HandBottom>FloorTop)
    {
        TArray<FVector2D> Top={FVector2D(310.f*L.U,FloorTop),FVector2D(1490.f*L.U,FloorTop)},Bottom={FVector2D(310.f*L.U,L.HandBottom),FVector2D(1490.f*L.U,L.HandBottom)};
        P.Band(Top,Bottom,[&](FVector2D At){return Hex(0x05070D,.6f*FMath::Clamp(float((At.Y-FloorTop)/(L.HandBottom-FloorTop)),0.f,1.f));});
    }
}

void ULDWorkshop::PaintSectors(FPainter& P,const TArray<uint64>& Lit,bool bDim) const
{
    const FLayout& L=Layout;
    const float R=L.R;
    const FVector2D C=L.Centre;
    const FCard* Held=SelectedCard();
    const ELDCardForm HeldForm=Held?Held->Form:ELDCardForm::Count;
    const bool bFull=Ring.Capacity>0&&Ring.Installed>=Ring.Capacity;
    const double Now=UiNow();
    // The slow dashed circle inside the sectors and the element ring's own dashed arc.
    {
        const float Spin=float(FMath::Fmod(Now*4.5,360.));
        P.Dashed(LDRingPaint::Arc(C,(SectorInner-4.f)*R,Spin,Spin+360.f,120),2.f*R,2.f*R,11.f*R,Hex(0xFFE9A3,.4f));
        P.Dashed(LDRingPaint::Arc(C,GemRadius*R,-100.f,56.f,80),2.f*R,3.f*R,7.f*R,FLinearColor(1.f,1.f,1.f,.4f));
    }
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num();++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        const float A=HostAngle[HostIndex];
        const FLinearColor Ink=Host.bHasMedal?ElementInk(Host.Medal.Element):RingPale;
        const int32 Slots=Host.Cards.Num()+(bFull?0:1);
        const float Outer=(RowRadius[RowsFor(Slots)-1]+36.f)*R;
        const float Inner=SectorInner*R;
        const bool bTarget=HeldForm==Host.Form&&!(Host.Form==ELDCardForm::Skill&&Host.bEmpty);
        const bool bBlueprintTarget=SelectedBlueprint!=INDEX_NONE&&HostIndex>=1&&HostIndex<=3;
        // The plate: a dark radial fill, the element's tint, the halftone dots.
        const TArray<FVector2D> In=LDRingPaint::Arc(C,Inner,A-SectorHalf,A+SectorHalf,24),Out=LDRingPaint::Arc(C,Outer,A-SectorHalf,A+SectorHalf,24);
        P.Band(In,Out,[&](FVector2D At)
        {
            const float T=FMath::Clamp(float(FVector2D::Distance(At,C))/(470.f*R),0.f,1.f);
            return Hex(0x070A12,T<.4f?.9f:FMath::Lerp(.9f,.42f,(T-.4f)/.6f));
        });
        if(Host.bHasMedal){P.Band(In,Out,[&](FVector2D){return Ink.CopyWithNewOpacity(.13f);});}
        {
            TArray<FVector2D> Dots;
            const float Pitch=9.f*R;
            const FVector2D Min=C+FVector2D(-Outer,-Outer),Max=C+FVector2D(Outer,Outer*.4);
            const float Cs=FMath::Cos(FMath::DegreesToRadians(32.f)),Sn=FMath::Sin(FMath::DegreesToRadians(32.f));
            for(float Y=float(Min.Y);Y<=Max.Y;Y+=Pitch)
            {
                for(float X=float(Min.X);X<=Max.X;X+=Pitch)
                {
                    const FVector2D Grid(X,Y);
                    const FVector2D D=Grid-C;
                    const FVector2D Turned=C+FVector2D(D.X*Cs-D.Y*Sn,D.X*Sn+D.Y*Cs);
                    const FVector2D E=Turned-C;
                    const float Radius=float(E.Size());
                    const float Degrees=FMath::RadiansToDegrees(FMath::Atan2(float(E.X),float(-E.Y)));
                    if(Radius>Inner+4.f*R&&Radius<Outer-4.f*R&&FMath::Abs(Degrees-A)<SectorHalf-1.f){Dots.Add(Turned);}
                }
            }
            P.Dots(Dots,3.8f*R,[&](FVector2D At)
            {
                const float T=FMath::Clamp((float(FVector2D::Distance(At,C))-.42f*470.f*R)/(.58f*470.f*R),0.f,1.f);
                return FLinearColor(1.f,1.f,1.f,.13f*T);
            });
        }
        // The thick inner band, the spray-painted outline and the dashed outer arc.
        P.Stroke(LDRingPaint::Arc(C,(SectorInner+5.f)*R,A-SectorHalf,A+SectorHalf,24),9.f*R,Ink);
        {
            TArray<FVector2D> Edge=LDRingPaint::Arc(C,Inner,A-SectorHalf,A+SectorHalf,24);
            TArray<FVector2D> OuterRev=LDRingPaint::Arc(C,Outer,A+SectorHalf,A-SectorHalf,24);
            Edge.Append(OuterRev);const FVector2D Closing=Edge[0];Edge.Add(Closing);
            const TArray<FVector2D> Spray=LDRingPaint::Jitter(Edge,2.2f*R,uint32(17+HostIndex*31),5.f*R);
            P.Stroke(Spray,8.f*R,RingDeep.CopyWithNewOpacity(.9f));
            P.Stroke(Spray,(bTarget||bBlueprintTarget?7.f:2.5f)*R,bTarget||bBlueprintTarget?LDBroadcast::Gold:Ink.CopyWithNewOpacity(.9f));
        }
        P.Dashed(LDRingPaint::Arc(C,Outer+9.f*R,A-SectorHalf+1.f,A+SectorHalf-1.f,24),4.f*R,2.f*R,9.f*R,Ink.CopyWithNewOpacity(.55f));
        // The spray splat behind the host, then the host: a chamfered gold (or element) frame with its icon.
        const FVector2D HostAt=LDRingPaint::Polar(C,HostRadius*R,A);
        {
            const float Turn=float((HostIndex*37)%50-25);
            const FBox2D Blot=Square(HostAt+FVector2D(0.,10.*R),178.f*R);
            if(UTexture2D* Splat=RingArt(EArt::Splat)){P.Image(Splat,Blot,Ink.CopyWithNewOpacity(Host.bHasMedal?.8f:.28f),nullptr,Turn);}
            else
            {
                for(int32 Blob=0;Blob<5;++Blob)
                {
                    const FVector2D At=HostAt+FVector2D(FMath::Sin(Blob*2.3f+Turn)*34.f*R,10.f*R+FMath::Cos(Blob*1.7f+Turn)*26.f*R);
                    P.Fill(LDRingPaint::Octagon(Square(At,(70.f-Blob*8.f)*R),.29f),Ink.CopyWithNewOpacity(Host.bHasMedal?.45f:.16f));
                }
            }
        }
        const bool bHostLit=Lit.Contains(AnchorKey(EHit::Host,HostIndex,INDEX_NONE));
        const FHit Shown=Focus();
        const bool bHostHot=Shown.Kind==EHit::Host&&Shown.Host==HostIndex;
        const float Size=HostSize*R*(bHostHot?1.07f:1.f);
        const FBox2D Frame=Square(HostAt,Size);
        const TArray<FVector2D> Cut=LDRingPaint::HostCut(Frame,Size*.2f);
        const float HostDim=bDim&&!bHostLit&&!bHostHot?.34f:1.f;
        const float Saved=P.Opacity;P.Opacity*=HostDim;
        P.Fill(Offset(Cut,FVector2D(4.f,5.f)*R),FLinearColor(0.f,0.f,0.f,.7f));
        if(bHostLit){P.Fill(LDRingPaint::HostCut(Grow(Frame,4.f*R),(Size+8.f*R)*.2f),FLinearColor(1.f,1.f,1.f,.95f));}
        const FFrameInk HostInk=Host.bHasMedal?FFrameInk{FLinearColor::White,Ink,Hex(0x2A1C06),Ink}:FFrameInk{Hex(0xFFF6CF),Hex(0xE0A81E),Hex(0x6A4606),LDBroadcast::Gold};
        P.FillShaded(Cut,[&](FVector2D At){return Metal(HostInk,Frame,At);});
        const FBox2D Face=Grow(Frame,-4.f*R);
        const TArray<FVector2D> FaceCut=LDRingPaint::HostCut(Face,Size*.19f);
        P.Fill(FaceCut,RingFace);
        UTexture2D* Icon=Host.Column==LDHUD::EStorageColumn::Body?RingArt(EArt::Body):RingIcon(Host.IconId);
        if(Icon){P.Image(Icon,Face,FLinearColor::White,&FaceCut);}
        else
        {
            const FString Glyph=Host.bEmpty?FString(TEXT("+")):Host.Column==LDHUD::EStorageColumn::Body?FString(TEXT("本体")):Host.Name.Left(1);
            P.Text(Glyph,Face.GetCenter(),RingFont(LDUIFonts::ERole::Title,Host.bEmpty?28.f:19.f,L.F),RingPale.CopyWithNewOpacity(Host.bEmpty?.6f:1.f),FVector2D(.5,.5));
        }
        P.FillShaded(LDRingPaint::ClipConvex({Face.Min,FVector2D(Face.Max.X,Face.Min.Y),FVector2D(Face.Max.X,Face.Min.Y+Face.GetSize().Y*.25),FVector2D(Face.Min.X,Face.Min.Y+Face.GetSize().Y*.5)},FaceCut),
            [&](FVector2D At){return FLinearColor(1.f,1.f,1.f,.28f*(1.f-GradientT(Face,At,160.f)*1.8f));});
        if(!Host.Key.IsEmpty())
        {
            const FSlateFontInfo Font=RingFigure(19.f,L.F);
            const FVector2D TextSize=LDRingPaint::Measure(Host.Key,Font);
            const FBox2D Tag(FVector2D(Frame.Min.X-9.f*L.U,Frame.Min.Y-9.f*L.U),FVector2D(Frame.Min.X-9.f*L.U+FMath::Max(26.f*L.F,float(TextSize.X)+18.f*L.F),Frame.Min.Y-9.f*L.U+23.f*L.F));
            P.Fill(LDRingPaint::Slant(Tag,8.f*L.F),LDBroadcast::Gold);
            P.Text(Host.Key,Tag.GetCenter(),Font,Hex(0x16120A),FVector2D(.5,.5));
        }
        P.Opacity=Saved;
        // The installed cards in their fixed places, then the open place.
        for(int32 Index=0;Index<Slots;++Index)
        {
            const FVector2D Polar=SlotPolar(Index,A);
            const FVector2D At=LDRingPaint::Polar(C,float(Polar.X)*R,float(Polar.Y));
            if(Index<Host.Cards.Num())
            {
                const FCard& Card=Host.Cards[Index];
                const bool bHot=Shown.Kind==EHit::Installed&&Shown.Host==HostIndex&&Shown.Index==Index;
                const bool bLit=Lit.Contains(AnchorKey(EHit::Installed,HostIndex,Index));
                float Flash=0.f;
                for(const FName& Each:Card.Keys){if(const double* At0=Landed.Find(Each)){Flash=FMath::Max(Flash,1.f-float((Now-*At0)/RingLandTime));}}
                const float Scale=Flash>0.f?1.f+.15f*Flash:1.f;
                PaintNode(P,Square(At,NodeSize*R*Scale),Card,bDim&&!bLit&&!bHot?.34f:1.f,bHot,bLit,Flash);
                continue;
            }
            const FBox2D Drop=Square(At,(NodeSize-4.f)*R);
            const TArray<FVector2D> Shape=LDRingPaint::Rounded(Drop,10.f*R,3);
            const float Pulse=bTarget?.5f+.5f*FMath::Abs(FMath::Sin(float(Now)*UE_PI)):0.f;
            P.Fill(Shape,bTarget?LDBroadcast::Gold.CopyWithNewOpacity(.12f+.1f*Pulse):FLinearColor(.02f,.03f,.05f,.35f));
            P.Dashed(Shape,2.f*R,6.f*R,4.f*R,bTarget?LDBroadcast::Gold:FLinearColor(1.f,1.f,1.f,.3f),true);
            P.Text(TEXT("+"),Drop.GetCenter(),RingFont(LDUIFonts::ERole::Title,24.f,R/FMath::Max(.01f,L.F)*L.F),bTarget?LDBroadcast::Gold:FLinearColor(1.f,1.f,1.f,.45f),FVector2D(.5,.55));
        }
    }
}

void ULDWorkshop::PaintGems(FPainter& P) const
{
    const FLayout& L=Layout;
    const float R=L.R;
    const FVector2D C=L.Centre;
    const FCard* Held=SelectedCard();
    const bool bMedalHeld=Held&&Held->Form==ELDCardForm::Medal;
    const FHit Shown=Focus();
    // Reactions first, under the sockets: neighbours meet on the ring, others run on the inner track.
    int32 TrackLane=0;
    for(const FReaction& Reaction:Ring.Reactions)
    {
        const float A0=HostAngle[Reaction.A],A1=HostAngle[Reaction.B];
        const FLinearColor From=ElementInk(Reaction.First),To=ElementInk(Reaction.Second);
        if(Reaction.B-Reaction.A==1)
        {
            const TArray<FVector2D> Arc=LDRingPaint::Arc(C,GemRadius*R,A0+9.f,A1-9.f,24);
            P.Stroke(Arc,10.f*R,RingDeep);
            P.Stroke(Arc,6.f*R,From,false,To);
            continue;
        }
        const float Track=(Reaction.B-Reaction.A==2?TrackRadius:TrackRadius-9.f*(1+TrackLane++))*R;
        TArray<FVector2D> Path={LDRingPaint::Polar(C,(GemRadius-GemSize*.5f+2.f)*R,A0)};
        Path.Append(LDRingPaint::Arc(C,Track,A0,A1,24));
        Path.Add(LDRingPaint::Polar(C,(GemRadius-GemSize*.5f+2.f)*R,A1));
        P.Stroke(Path,3.5f*R,From,false,To);
    }
    UTexture2D* SocketArt=RingArt(EArt::Socket);
    for(int32 HostIndex=0;HostIndex<Ring.Hosts.Num();++HostIndex)
    {
        const FHost& Host=Ring.Hosts[HostIndex];
        if(!Host.bSocket){continue;}
        const float A=HostAngle[HostIndex];
        const FLinearColor Ink=Host.bHasMedal?ElementInk(Host.Medal.Element):Hex(0x59627A);
        // The stem into the sector.
        P.Stroke({LDRingPaint::Polar(C,(GemRadius+GemSize*.5f-2.f)*R,A),LDRingPaint::Polar(C,(SectorInner+2.f)*R,A)},6.f*R,Host.bHasMedal?Ink:FLinearColor(1.f,1.f,1.f,.5f));
        const bool bHot=Shown.Kind==EHit::Gem&&Shown.Host==HostIndex;
        const float Size=GemSize*R*(bHot?1.14f:1.f);
        const FBox2D Box=Square(LDRingPaint::Polar(C,GemRadius*R,A),Size);
        const TArray<FVector2D> Rim=LDRingPaint::Octagon(Box);
        const float Pulse=bMedalHeld?.5f+.5f*FMath::Abs(FMath::Sin(float(UiNow())*UE_PI)):0.f;
        P.Fill(Offset(Rim,FVector2D(2.f,3.f)*R),FLinearColor(0.f,0.f,0.f,.65f));
        if(Host.bHasMedal){P.Fill(LDRingPaint::Octagon(Grow(Box,6.f*R)),Ink.CopyWithNewOpacity(.35f));}
        if(SocketArt)
        {
            const FLinearColor Tint=bMedalHeld?LDRingPaint::Mix(FLinearColor::White,LDBroadcast::Gold,Pulse):Host.bHasMedal?LDRingPaint::Mix(FLinearColor::White,Ink,.45f):FLinearColor::White;
            P.Image(SocketArt,Box,Tint,&Rim);
        }
        else
        {
            // A conic metal rim: light and dark quadrants alternating around the octagon.
            P.FillShaded(Rim,[&](FVector2D At)
            {
                const FVector2D D=At-Box.GetCenter();
                const float T=FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(float(D.X),float(-D.Y)))-22.f+720.f,90.f)/90.f;
                const FLinearColor Light=Host.bHasMedal?FLinearColor::White:Hex(0xF5F7FB),Dark=Host.bHasMedal?Ink:Hex(0x343B4C);
                return bMedalHeld?LDRingPaint::Mix(Light,LDBroadcast::Gold,.5f+.5f*Pulse):LDRingPaint::Mix(Light,Dark,FMath::Abs(T*2.f-1.f));
            });
        }
        const FBox2D Cavity=Grow(Box,-float(Size)*.1f);
        const TArray<FVector2D> Hole=LDRingPaint::Octagon(Cavity);
        if(Host.bHasMedal)
        {
            P.Fill(Hole,Hex(0x0A0E18));
            if(UTexture2D* Icon=RingIcon(Host.Medal.IconId)){P.Image(Icon,Grow(Cavity,-1.f*R),FLinearColor::White,&Hole);}
            P.FillShaded(LDRingPaint::ClipConvex({Cavity.Min,FVector2D(Cavity.Max.X,Cavity.Min.Y),FVector2D(Cavity.Max.X,Cavity.Min.Y+Cavity.GetSize().Y*.2),FVector2D(Cavity.Min.X,Cavity.Min.Y+Cavity.GetSize().Y*.55)},Hole),
                [&](FVector2D At){return FLinearColor(1.f,1.f,1.f,.45f*(1.f-GradientT(Cavity,At,140.f)*1.7f));});
            // The tier: three diamonds under the socket, lit up to the medal's tier.
            const FVector2D Foot(Box.GetCenter().X,Box.Max.Y+4.f*R);
            P.Fill(LDRingPaint::Rounded(FBox2D(Foot-FVector2D(17.f,6.5f)*R,Foot+FVector2D(17.f,6.5f)*R),6.5f*R,3),RingDeep);
            for(int32 Tier=0;Tier<3;++Tier)
            {
                const FVector2D At=Foot+FVector2D((Tier-1)*10.f*R,0.);
                const float H=4.f*R;
                P.Fill({At+FVector2D(0,-H),At+FVector2D(H,0),At+FVector2D(0,H),At+FVector2D(-H,0)},Tier<Host.Medal.Tier?Ink:Hex(0x2A3142));
            }
        }
        else
        {
            P.Fill(Hole,Hex(0x10141F));
            P.Fill(LDRingPaint::Octagon(Square(Box.GetCenter(),float(Size)*.34f)),Hex(0x2A3142));
        }
    }
}

void ULDWorkshop::PaintRadarLabels(FPainter& P) const
{
    const FLayout& L=Layout;
    const float F=L.F;
    const FHit Shown=Focus();
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Stat||!Ring.Stats.IsValidIndex(Box.Hit.Index)){continue;}
        const FStat& Stat=Ring.Stats[Box.Hit.Index];
        const FVector2D* At=L.Anchors.Find(AnchorKey(EHit::Stat,INDEX_NONE,Box.Hit.Index));
        if(!At){continue;}
        const FSlateFontInfo Name=RingFont(LDUIFonts::ERole::Strong,14.f,F),Figure=RingFigure(19.f,F);
        const FString Points=FString::FromInt(Stat.Points);
        const float NameW=float(LDRingPaint::Measure(Stat.Name,Name).X),NumW=float(LDRingPaint::Measure(Points,Figure).X);
        const bool bPlus=Ring.Unspent>0;
        const float W=11.f*F+NameW+5.f*F+NumW+(bPlus?5.f*F+17.f*F:0.f)+9.f*F;
        const float H=24.f*F;
        const FBox2D Chip(*At-FVector2D(W,H)*.5,*At+FVector2D(W,H)*.5);
        const bool bHot=Shown.Kind==EHit::Stat&&Shown.Index==Box.Hit.Index;
        const FLinearColor Plate=Stat.bKeystone?LDBroadcast::Gold:bHot?Hex(0x1C2438):RingInk;
        const FLinearColor Ink=Stat.bKeystone?Hex(0x16120A):RingText;
        P.Fill(LDRingPaint::Slant(Chip,8.f*F),Plate);
        if(!Stat.bKeystone){P.Fill(LDRingPaint::Slant(FBox2D(Chip.Min,FVector2D(Chip.Min.X+13.f*F,Chip.Max.Y)),8.f*F),StatInk(Box.Hit.Index));}
        float X=float(Chip.Min.X)+11.f*F;
        P.Text(Stat.Name,FVector2D(X,Chip.GetCenter().Y),Name,Ink,FVector2D(0.,.5));X+=NameW+5.f*F;
        P.Text(Points,FVector2D(X,Chip.GetCenter().Y),Figure,Stat.bKeystone?Hex(0x16120A):FLinearColor::White,FVector2D(0.,.52),LDBroadcast::SkewDegrees);X+=NumW+5.f*F;
        if(bPlus)
        {
            const FVector2D Dot(X+8.5f*F,Chip.GetCenter().Y);
            P.Fill(LDRingPaint::Rounded(FBox2D(Dot-FVector2D(8.5f,8.5f)*F,Dot+FVector2D(8.5f,8.5f)*F),8.5f*F,4),Stat.bKeystone?Hex(0x16120A):LDBroadcast::Gold);
            P.Text(TEXT("+"),Dot,RingFont(LDUIFonts::ERole::Title,15.f,F),Stat.bKeystone?LDBroadcast::Gold:Hex(0x16120A),FVector2D(.5,.55));
        }
    }
}

void ULDWorkshop::PaintWing(FPainter& P) const
{
    const FLayout& L=Layout;
    const float F=L.F,R=L.R;
    if(Ring.Derived.IsEmpty()){return;}
    // The spray-painted arc the rows hang from, from the first row to the last.
    {
        const FVector2D* First=L.Anchors.Find(AnchorKey(EHit::Wing,INDEX_NONE,0));
        const FVector2D* Last=L.Anchors.Find(AnchorKey(EHit::Wing,INDEX_NONE,Ring.Derived.Num()-1));
        if(First&&Last)
        {
            const auto Degrees=[&](FVector2D At){const FVector2D D=At-L.Centre;return FMath::RadiansToDegrees(FMath::Atan2(float(D.X),float(-D.Y)));};
            P.Stroke(LDRingPaint::Jitter(LDRingPaint::Arc(L.Centre,602.f*R,Degrees(*First)+4.f,Degrees(*Last)-6.f,40),1.6f*R,91u,5.f*R),2.5f*R,Hex(0xFFE9A3,.6f));
        }
    }
    // The hovered stat previews one more point: the rows it raises turn green with the gain.
    const FHit Shown=Focus();
    TMap<FString,FString> Gain;
    if(Shown.Kind==EHit::Stat&&Latest.Content&&Ring.Stats.IsValidIndex(Shown.Index))
    {
        const TArray<const FLDDemoStatDefinition*> Stats=Latest.Content->StatsInDesignOrder();
        if(Stats.IsValidIndex(Shown.Index))
        {
            for(const LDHUD::FStatEffectView& Effect:LDHUD::StatEffects(*Stats[Shown.Index],Latest.Attributes)){Gain.Add(Effect.Attribute,Effect.PerPoint);}
        }
    }
    const FSlateFontInfo Name=RingFont(LDUIFonts::ERole::Strong,14.f,F),Figure=RingFigure(21.f,F),Delta=RingFigure(16.f,F);
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Wing||!Ring.Derived.IsValidIndex(Box.Hit.Index)){continue;}
        const FDerived& Row=Ring.Derived[Box.Hit.Index];
        const FVector2D* At=L.Anchors.Find(AnchorKey(EHit::Wing,INDEX_NONE,Box.Hit.Index));
        if(!At){continue;}
        const FString* Up=Gain.Find(Row.Name);
        const float H=28.f*F;
        const FString DeltaText=Up?(Up->StartsWith(TEXT("+"))?*Up:TEXT("+")+*Up):FString();
        // A compact chip names its row by the attribute's glyph; the hover card spells it out.
        const FString& Label=L.bWingCompact?Row.Glyph:Row.Name;
        const float W=L.bWingCompact?L.WingWidth:FMath::Max(L.WingWidth,18.f*F+float(LDRingPaint::Measure(Label,Name).X)+10.f*F
            +float(LDRingPaint::Measure(Row.Value,Figure).X)+(Up?5.f*F+float(LDRingPaint::Measure(DeltaText,Delta).X):0.f)+14.f*F);
        const FBox2D Plate(FVector2D(At->X-W,At->Y-H*.5f),FVector2D(At->X,At->Y+H*.5f));
        const TArray<FVector2D> Shape=LDRingPaint::Slant(Plate,8.f*F);
        P.FillShaded(Shape,[&](FVector2D Point)
        {
            const float T=float((Point.X-Plate.Min.X)/FMath::Max(1.,Plate.GetSize().X));
            return Up?LDRingPaint::Mix(Hex(0x10301E,.95f),Hex(0x070A11,.8f),T):LDRingPaint::Mix(Hex(0x070A11,.9f),Hex(0x070A11,.72f),T);
        });
        P.Fill(LDRingPaint::Slant(FBox2D(FVector2D(Plate.Max.X-13.f*F,Plate.Min.Y),Plate.Max),8.f*F),StatInk(Row.Source));
        P.Text(Label,FVector2D(Plate.Min.X+(L.bWingCompact?12.f:18.f)*F,At->Y),Name,RingSoft,FVector2D(0.,.5));
        float X=float(Plate.Max.X)-14.f*F;
        if(Up){X-=float(P.Text(DeltaText,FVector2D(X,At->Y),Delta,RingUp,FVector2D(1.,.5),LDBroadcast::SkewDegrees).X)+5.f*F;}
        P.Text(Row.Value,FVector2D(X,At->Y),Figure,FLinearColor::White,FVector2D(1.,.52),LDBroadcast::SkewDegrees);
    }
}

void ULDWorkshop::PaintShop(FPainter& P,const TArray<uint64>& Lit) const
{
    const FLayout& L=Layout;
    if(!Ring.bShop){return;}
    const float F=L.F,R=L.R,U=L.U;
    const FVector2D C=L.Centre;
    {
        // The spray-painted arc the shelf hangs from, from the first row to the last.
        const FVector2D* First=L.Anchors.Find(AnchorKey(EHit::Shop,INDEX_NONE,0));
        const FVector2D* Last=L.Anchors.Find(AnchorKey(EHit::Shop,INDEX_NONE,Ring.Shop.Num()-1));
        if(First&&Last)
        {
            const auto Degrees=[&](FVector2D At){const FVector2D D=At-C;return FMath::RadiansToDegrees(FMath::Atan2(float(D.X),float(-D.Y)));};
            P.Stroke(LDRingPaint::Jitter(LDRingPaint::Arc(C,602.f*R,Degrees(*First)-6.f,Degrees(*Last)+4.f,40),1.6f*R,53u,5.f*R),2.5f*R,Hex(0xFFE9A3,.6f));
        }
    }
    {
        // The labels: 休息商店 and the refresh with its price (free shows as 免费).
        const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Title,14.f,F);
        const FVector2D At=L.ShopLabel;
        const float W=float(LDRingPaint::Measure(TEXT("休息商店"),Font).X)+24.f*F;
        P.Fill(LDRingPaint::Slant(FBox2D(At,At+FVector2D(W,24.f*F)),8.f*F),LDBroadcast::Gold);
        P.Text(TEXT("休息商店"),At+FVector2D(W*.5,12.f*F),Font,Hex(0x16120A),FVector2D(.5,.5));
        const FString Refresh=Ring.RefreshPrice>0?FString::Printf(TEXT("刷新 · %s"),*Diamonds(Ring.RefreshPrice)):FString(TEXT("刷新 · 免费"));
        for(const FShape& Box:L.Boxes)
        {
            if(Box.Hit.Kind!=EHit::ShopRefresh){continue;}
            const FBox2D Button(Box.Shape[0],Box.Shape[2]);
            const bool bHot=Focus().Kind==EHit::ShopRefresh;
            P.Fill(LDRingPaint::Slant(Button,8.f*F),bHot?Hex(0x1C2438):RingInk);
            P.Fill(LDRingPaint::Slant(FBox2D(FVector2D(Button.Min.X,Button.Max.Y-3.f*F),Button.Max),2.f*F),LDBroadcast::Gold);
            P.Text(Refresh,Button.GetCenter(),RingFont(LDUIFonts::ERole::Strong,14.f,F),RingText,FVector2D(.5,.5));
        }
    }
    const float Z=FMath::Max(54.f*U,36.f/L.K);
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Shop||!Ring.Shop.IsValidIndex(Box.Hit.Index)){continue;}
        const FShopItem& Item=Ring.Shop[Box.Hit.Index];
        const FVector2D* At=L.Anchors.Find(AnchorKey(EHit::Shop,INDEX_NONE,Box.Hit.Index));
        if(!At){continue;}
        const float Saved=P.Opacity;P.Opacity*=Item.bSold?.35f:1.f;
        const bool bHot=Focus().Kind==EHit::Shop&&Focus().Index==Box.Hit.Index;
        const FBox2D Tile=Square(*At,Z);
        // The text plate first, the tile over its left end.
        const FBox2D Plate(FVector2D(Tile.Max.X-12.f*F,At->Y-22.f*F),FVector2D(Tile.Max.X-12.f*F+150.f*F,At->Y+22.f*F));
        P.FillShaded(LDRingPaint::Slant(Plate,8.f*F),[&](FVector2D Point){return bHot?Hex(0x1C2438):LDRingPaint::Mix(Hex(0x070A11,.92f),Hex(0x070A11,.7f),float((Point.X-Plate.Min.X)/Plate.GetSize().X));});
        P.Text(Item.Name,FVector2D(Plate.Min.X+18.f*F,Plate.Min.Y+3.f*F),RingFont(LDUIFonts::ERole::Strong,14.f,F),RingText);
        const FString Price=Item.bSold?FString(TEXT("已买")):Diamonds(Item.Price);
        const FVector2D PriceSize=P.Text(Price,FVector2D(Plate.Min.X+18.f*F,Plate.Max.Y-3.f*F),RingFont(LDUIFonts::ERole::Strong,14.f,F),Item.bSold?RingSoft:LDBroadcast::Gold,FVector2D(0.,1.));
        if(!Item.Note.IsEmpty()&&!Item.bSold){P.Text(Item.Note,FVector2D(Plate.Min.X+18.f*F+PriceSize.X+6.f*F,Plate.Max.Y-3.f*F),RingFont(LDUIFonts::ERole::Body,14.f,F),RingSoft,FVector2D(0.,1.));}
        FCard Face;Face.IconId=Item.IconId;Face.Name=Item.Name;Face.Rarity=Item.Rarity;
        if(Item.Slot==FLDBuildCommand::ShopSkillPointSlot)
        {
            const TArray<FVector2D> Outline=LDRingPaint::Rounded(Tile,10.f*R,3);
            P.Fill(Offset(Outline,FVector2D(3.f,4.f)*R),FLinearColor(0.f,0.f,0.f,.62f));
            P.FillShaded(Outline,[&](FVector2D Point){return Metal(FrameOf(ERarity::Common),Tile,Point);});
            P.Fill(LDRingPaint::Rounded(Grow(Tile,-3.f*R),7.f*R,3),RingFace);
            P.Text(TEXT("技能"),Tile.GetCenter()-FVector2D(0.,8.f*F),RingFont(LDUIFonts::ERole::Title,14.f,F),RingPale,FVector2D(.5,.5));
            P.Text(TEXT("点"),Tile.GetCenter()+FVector2D(0.,8.f*F),RingFont(LDUIFonts::ERole::Title,14.f,F),RingPale,FVector2D(.5,.5));
        }
        else{PaintNode(P,Tile,Face,1.f,bHot,Lit.Contains(AnchorKey(EHit::Shop,INDEX_NONE,Box.Hit.Index)),0.f);}
        P.Opacity=Saved;
    }
}

void ULDWorkshop::PaintBottom(FPainter& P,const TArray<uint64>& Lit) const
{
    const FLayout& L=Layout;
    const float U=L.U,F=L.F;
    const FVector2D C=L.Centre;
    const double Now=UiNow();
    const FHit Shown=Focus();
    // The plates over the hand: the level, the capacity and the points on the left, the reactions on the right.
    const float PlateTop=L.HandTop-12.f*U-30.f*F;
    {
        const FSlateFontInfo Label=RingFont(LDUIFonts::ERole::Strong,14.f,F),Figure=RingFigure(21.f,F);
        TArray<TPair<FString,bool>> Parts;
        const float H=30.f*F;
        // Measure first: the plate's right edge stands at 800 design px.
        const FString Lv=FString::FromInt(Ring.Level),Used=FString::FromInt(Ring.Installed),Cap=FString::Printf(TEXT(" / %d"),Ring.Capacity);
        const FString Points=FString::FromInt(Ring.Unspent);
        const float Pip=8.f*U,PipGap=3.f*U;
        const float PipsW=Ring.Capacity*(Pip+PipGap);
        const float W=LevelPlateWidth();
        const FBox2D Plate(FVector2D(800.f*U-W,PlateTop),FVector2D(800.f*U,PlateTop+H));
        P.Fill(LDRingPaint::Slant(Plate,8.f*F),RingInk);
        float X=float(Plate.Min.X)+16.f*F;const float Y=float(Plate.GetCenter().Y);
        X+=float(P.Text(TEXT("LV "),FVector2D(X,Y),Label,RingText,FVector2D(0.,.5)).X);
        X+=float(P.Text(Lv,FVector2D(X,Y),Figure,FLinearColor::White,FVector2D(0.,.52),LDBroadcast::SkewDegrees).X)+12.f*F;
        X+=float(P.Text(TEXT("构筑 "),FVector2D(X,Y),Label,RingText,FVector2D(0.,.5)).X);
        X+=float(P.Text(Used,FVector2D(X,Y),Figure,Ring.Installed>=Ring.Capacity&&Ring.Capacity>0?LDBroadcast::Gold:FLinearColor::White,FVector2D(0.,.52),LDBroadcast::SkewDegrees).X);
        X+=float(P.Text(Cap,FVector2D(X,Y),Label,RingText,FVector2D(0.,.5)).X)+12.f*F;
        for(int32 Index=0;Index<Ring.Capacity;++Index)
        {
            const FBox2D Cell(FVector2D(X,Y-7.f*U),FVector2D(X+Pip,Y+7.f*U));
            P.Fill(LDRingPaint::Slant(Cell,3.f*U),Index<Ring.Installed?LDBroadcast::Gold:Hex(0x39415A));
            X+=Pip+PipGap;
        }
        X+=12.f*F;
        X+=float(P.Text(TEXT("属性点 "),FVector2D(X,Y),Label,LDBroadcast::Gold,FVector2D(0.,.5)).X);
        P.Text(Points,FVector2D(X,Y),Figure,LDBroadcast::Gold,FVector2D(0.,.52),LDBroadcast::SkewDegrees);
        if(!Ring.Reactions.IsEmpty())
        {
            float RW=16.f*F+float(LDRingPaint::Measure(TEXT("反应"),Label).X)+12.f*F;
            for(const FReaction& Reaction:Ring.Reactions){RW+=22.f*F+float(LDRingPaint::Measure(Reaction.Name,Label).X)+12.f*F;}
            const FBox2D Right(FVector2D(1124.f*U,PlateTop),FVector2D(1124.f*U+RW,PlateTop+H));
            P.Fill(LDRingPaint::Slant(Right,8.f*F),RingInk);
            float RX=float(Right.Min.X)+16.f*F;
            RX+=float(P.Text(TEXT("反应"),FVector2D(RX,Y),Label,LDBroadcast::Gold,FVector2D(0.,.5)).X)+12.f*F;
            for(const FReaction& Reaction:Ring.Reactions)
            {
                const float D=4.5f*F;
                for(int32 Side=0;Side<2;++Side)
                {
                    const FVector2D At(RX+D+Side*7.f*F,Y);
                    P.Fill({At+FVector2D(0,-D),At+FVector2D(D,0),At+FVector2D(0,D),At+FVector2D(-D,0)},ElementInk(Side?Reaction.Second:Reaction.First));
                }
                RX+=22.f*F;
                RX+=float(P.Text(Reaction.Name,FVector2D(RX,Y),Label,RingText,FVector2D(0.,.5)).X)+12.f*F;
            }
        }
    }
    const auto Pile=[&](const FBox2D& Box,const FString& Top,const FString& Figure,bool bHot)
    {
        for(int32 Back=2;Back>=1;--Back)
        {
            const FBox2D Behind=Shift(Box,FVector2D(5.f*Back,-5.f*Back)*U);
            P.Fill(LDRingPaint::Rounded(Grow(Behind,2.f),9.f*U,3),Back==2?Hex(0x5C5233):Hex(0x8B7A45));
            P.Fill(LDRingPaint::Rounded(Behind,9.f*U,3),Hex(0x0B0F1A));
        }
        const TArray<FVector2D> Shape=LDRingPaint::Rounded(Box,9.f*U,3);
        P.Fill(Shape,LDBroadcast::Gold);
        P.Fill(LDRingPaint::Rounded(Grow(Box,-3.f*U),7.f*U,3),bHot?Hex(0x232B42):Hex(0x151B2E));
        const float CY=float(Box.GetCenter().Y);
        P.Text(Top,FVector2D(Box.GetCenter().X,CY-14.f*F),RingFont(LDUIFonts::ERole::Title,14.f,F),LDBroadcast::Gold,FVector2D(.5,.5));
        P.Text(Figure,FVector2D(Box.GetCenter().X,CY+10.f*F),RingFigure(24.f,F),FLinearColor::White,FVector2D(.5,.5),LDBroadcast::SkewDegrees);
    };
    for(const FShape& Box:L.Boxes)
    {
        const bool bHot=Shown==Box.Hit;
        if(Box.Hit.Kind==EHit::HandPile){Pile(FBox2D(Box.Shape[0],Box.Shape[2]),TEXT("牌库"),FString::FromInt(Ring.Hand.Num()),bHot);}
        else if(Box.Hit.Kind==EHit::OfferPile)
        {
            const bool bBehind=!bShowHand;
            Pile(FBox2D(Box.Shape[0],Box.Shape[2]),bBehind?TEXT("还有"):TEXT("待选"),FString::Printf(TEXT("%d"),bBehind?Ring.Batch.Queued:Ring.Batch.Queued+1),bHot);
        }
        else if(Box.Hit.Kind==EHit::Reroll||Box.Hit.Kind==EHit::Skip||Box.Hit.Kind==EHit::Respec)
        {
            const FBox2D Button(Box.Shape[0],Box.Shape[2]);
            FString Text;
            if(Box.Hit.Kind==EHit::Reroll){Text=FString::Printf(TEXT("重抽 ×%d"),Ring.Batch.Rerolls);}
            else if(Box.Hit.Kind==EHit::Skip){Text=Ring.Batch.SkipPoints>0?FString::Printf(TEXT("跳过 +%lld"),Ring.Batch.SkipPoints):FString(TEXT("跳过"));}
            else{Text=TEXT("洗点");}
            const bool bOff=Box.Hit.Kind==EHit::Reroll&&Ring.Batch.Rerolls<=0;
            P.Fill(LDRingPaint::Slant(Button,8.f*F),bHot?Hex(0x1C2438):RingInk);
            P.Fill(LDRingPaint::Slant(FBox2D(FVector2D(Button.Min.X,Button.Max.Y-3.f*F),Button.Max),2.f*F),bOff?Hex(0x39415A):LDBroadcast::Gold);
            P.Text(Text,Button.GetCenter(),RingFont(LDUIFonts::ERole::Strong,14.f,F),bOff?RingSoft:RingText,FVector2D(.5,.5));
        }
    }
    if(!OfferFaces.IsEmpty())
    {
        // The batch title over the row, then the big cards dealt in one after another.
        const FBox2D First=OfferFaces[0].Box;
        {
            const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Title,14.f,F);
            const float W=float(LDRingPaint::Measure(Ring.Batch.Title,Font).X)+24.f*F;
            const FBox2D Label(FVector2D(C.X-W*.5f,First.Min.Y-14.f*U-24.f*F),FVector2D(C.X+W*.5f,First.Min.Y-14.f*U));
            P.Fill(LDRingPaint::Slant(Label,8.f*F),LDBroadcast::Gold);
            P.Text(Ring.Batch.Title,Label.GetCenter(),Font,Hex(0x16120A),FVector2D(.5,.5));
        }
        for(const FFace& Face:OfferFaces)
        {
            const FOption& Option=Ring.Batch.Options[Face.Hit.Index];
            const float Age=BatchAt<0.?1.f:FMath::Clamp(float((Now-BatchAt-Face.Hit.Index*RingRevealStagger)/RingRevealTime),0.f,1.f);
            const float Ease=1.f-FMath::Square(1.f-Age);
            const float LiftAmount=Lift.FindRef(AnchorKey(EHit::Offer,INDEX_NONE,Face.Hit.Index));
            const FBox2D Box=Shift(Face.Box,FVector2D(0.,(1.f-Ease)*28.f*U-LiftAmount*9.f*U));
            const float Saved=P.Opacity;P.Opacity*=Ease*(PageAt<0.?1.f:FMath::Clamp(float((Now-PageAt)/RingRevealTime),0.f,1.f));
            const bool bChosen=Ring.Batch.Kind==EBatch::Blueprint&&SelectedBlueprint==Face.Hit.Index;
            const ERarity Rarity=Option.Rarity;
            const FFrameInk Ink=FrameOf(Rarity);
            const TArray<FVector2D> Outline=LDRingPaint::Rounded(Box,11.f*U,3);
            P.Fill(Offset(Outline,FVector2D(4.f,6.f)*U),FLinearColor(0.f,0.f,0.f,.6f));
            if(Rarity==ERarity::Legendary||bChosen||LiftAmount>.01f){P.Fill(LDRingPaint::Rounded(Grow(Box,(bChosen?6.f:4.f)*U),15.f*U,3),(bChosen?LDBroadcast::Gold:Ink.Band).CopyWithNewOpacity(bChosen?.9f:.35f+.3f*LiftAmount));}
            P.FillShaded(Outline,[&](FVector2D At){return Metal(Ink,Box,At);});
            if(UTexture2D* Brushed=RingArt(EArt::Metal)){P.Image(Brushed,Box,FLinearColor(1.f,1.f,1.f,.3f),&Outline);}
            const FBox2D Inner=Grow(Box,-4.f*U);
            const TArray<FVector2D> InnerShape=LDRingPaint::Rounded(Inner,8.f*U,3);
            P.Fill(InnerShape,Hex(0x090C14));
            const float ArtSize=float(Inner.GetSize().Y);
            const FBox2D ArtBox(Inner.Min,Inner.Min+FVector2D(ArtSize,ArtSize));
            const TArray<FVector2D> ArtClip=LDRingPaint::ClipConvex({ArtBox.Min,FVector2D(ArtBox.Max.X,ArtBox.Min.Y),FVector2D(ArtBox.Min.X+ArtSize*.86f,ArtBox.Max.Y),FVector2D(ArtBox.Min.X,ArtBox.Max.Y)},InnerShape);
            if(UTexture2D* Icon=RingIcon(Option.IconId)){P.Image(Icon,ArtBox,FLinearColor::White,&ArtClip);}
            else{P.Fill(ArtClip,Hex(0x222A3D));P.Text(Option.Name.Left(1),ArtBox.GetCenter(),RingFont(LDUIFonts::ERole::Title,40.f,F),RingPale,FVector2D(.5,.5));}
            if(Rarity==ERarity::Legendary)
            {
                const float Phase=FMath::Fmod(float(Now)/3.6f,1.f);
                const float X=float(ArtBox.Min.X)+FMath::Lerp(-.6f,1.6f,FMath::Min(1.f,Phase/.55f))*ArtSize;
                P.Fill(LDRingPaint::ClipConvex({FVector2D(X,ArtBox.Min.Y),FVector2D(X+ArtSize*.22f,ArtBox.Min.Y),FVector2D(X+ArtSize*.02f,ArtBox.Max.Y),FVector2D(X-ArtSize*.2f,ArtBox.Max.Y)},ArtClip),FLinearColor(1.f,1.f,1.f,.35f));
            }
            const float TextX=float(ArtBox.Max.X)-4.f*U+6.f*U;
            const float TextW=float(Inner.Max.X)-TextX-10.f*U;
            const FSlateFontInfo NameFont=RingFont(LDUIFonts::ERole::Title,18.f,F),Body=RingFont(LDUIFonts::ERole::Body,16.f,F);
            P.Text(Option.Name,FVector2D(TextX,Inner.Min.Y+7.f*U),NameFont,RingText);
            const TArray<FString> Lines=LDRingPaint::Wrap(Option.Text,Body,TextW,L.OfferLines);
            float Y=float(Inner.Min.Y)+7.f*U+18.f*F*1.25f+2.f*U;
            for(const FString& Line:Lines){P.Text(Line,FVector2D(TextX,Y),Body,RingSoft);Y+=16.f*F*1.3f;}
            // The pick key: a gold slanted tag on the top-left corner.
            const FSlateFontInfo KeyFont=RingFigure(19.f,F);
            const FBox2D Tag(FVector2D(Box.Min.X-9.f*U,Box.Min.Y-9.f*U),FVector2D(Box.Min.X-9.f*U+26.f*F,Box.Min.Y-9.f*U+23.f*F));
            P.Fill(LDRingPaint::Slant(Tag,8.f*F),LDBroadcast::Gold);
            P.Text(FString::FromInt(Face.Hit.Index+1),Tag.GetCenter(),KeyFont,Hex(0x16120A),FVector2D(.5,.5));
            P.Opacity=Saved;
        }
    }
    // The hand, back to front; the hovered and the selected card rise. A page turn (hand <-> offers) fades it in.
    const float Page=PageAt<0.?1.f:FMath::Clamp(float((Now-PageAt)/RingRevealTime),0.f,1.f);
    const float PageSaved=P.Opacity;P.Opacity*=1.f-FMath::Square(1.f-Page);
    ON_SCOPE_EXIT{P.Opacity=PageSaved;};
    for(const FFace& Face:HandFaces)
    {
        const FCard& Card=Ring.Hand[Face.Hit.Index];
        const float LiftAmount=Lift.FindRef(AnchorKey(EHit::Hand,INDEX_NONE,Face.Hit.Index));
        const bool bSelected=Card.Key==Selected;
        const bool bHot=Shown==Face.Hit;
        const bool bLit=Lit.Contains(AnchorKey(EHit::Hand,INDEX_NONE,Face.Hit.Index));
        const FFrameInk Ink=FrameOf(Card.Rarity);
        if(Card.Form==ELDCardForm::Medal)
        {
            // A medal: an octagon gem with its name under it.
            const FBox2D Box=Shift(Face.Box,FVector2D(0.,-LiftAmount*14.f*U));
            const TArray<FVector2D> Rim=LDRingPaint::Octagon(Box);
            const FLinearColor Gem=ElementInk(Card.Element);
            P.Fill(Offset(Rim,FVector2D(3.f,5.f)*U),FLinearColor(0.f,0.f,0.f,.6f));
            P.Fill(LDRingPaint::Octagon(Grow(Box,7.f*U)),Gem.CopyWithNewOpacity(.35f));
            if(UTexture2D* SocketArt=RingArt(EArt::Socket)){P.Image(SocketArt,Box,bSelected?LDBroadcast::Gold:LDRingPaint::Mix(FLinearColor::White,Gem,.45f),&Rim);}
            else{P.FillShaded(Rim,[&](FVector2D At){return bSelected?LDBroadcast::Gold:LDRingPaint::Mix(FLinearColor::White,Gem,GradientT(Box,At,135.f));});}
            const FBox2D Cavity=Grow(Box,-float(Box.GetSize().X)*.1f);
            const TArray<FVector2D> Hole=LDRingPaint::Octagon(Cavity);
            P.Fill(Hole,Hex(0x0A0E18));
            if(UTexture2D* Icon=RingIcon(Card.IconId)){P.Image(Icon,Cavity,FLinearColor::White,&Hole);}
            P.Text(Card.Name,FVector2D(Box.GetCenter().X,Box.Max.Y+6.f*U),RingFont(LDUIFonts::ERole::Strong,14.f,F),RingText,FVector2D(.5,0.));
            if(Card.Copies>1)
            {
                const FSlateFontInfo Font=RingFigure(17.f,F);const FString Text=FString::Printf(TEXT("×%d"),Card.Copies);
                const float W=float(LDRingPaint::Measure(Text,Font).X)+16.f*F;
                const FBox2D Tag(FVector2D(Box.Max.X+8.f*U-W,Box.Min.Y-9.f*U),FVector2D(Box.Max.X+8.f*U,Box.Min.Y-9.f*U+21.f*F));
                P.Fill(LDRingPaint::Slant(Tag,8.f*F),LDBroadcast::Gold);P.Text(Text,Tag.GetCenter(),Font,Hex(0x16120A),FVector2D(.5,.5));
            }
            if(Card.bFresh)
            {
                const FVector2D At(Box.Max.X+12.f*U-19.f*F,Box.Min.Y-14.f*U+19.f*F);
                if(UTexture2D* Sticker=RingArt(EArt::Fresh)){P.Image(Sticker,Square(At,40.f*F),FLinearColor::White,nullptr,12.f);}
                else{P.Fill(Star(At,19.f*F,12,.78f,12.f),RingFreshRed);P.Text(TEXT("新"),At,RingFont(LDUIFonts::ERole::Title,16.f,F),FLinearColor::White,FVector2D(.5,.5));}
            }
            continue;
        }
        const FVector2D Pivot(Face.Box.GetCenter().X,Face.Box.Min.Y+Face.Box.GetSize().Y*1.3);
        const float Rise=(bSelected?24.f:24.f*LiftAmount)*U;
        const float Grow7=bHot&&!bSelected?1.f+.07f*LiftAmount:1.f;
        P.Xform=FTransform2D(-Pivot).Concatenate(FTransform2D(Grow7)).Concatenate(FTransform2D(FQuat2D(FMath::DegreesToRadians(Face.Degrees)))).Concatenate(FTransform2D(Pivot-FVector2D(0.,Rise)));
        P.bXform=true;
        const FBox2D Box=Face.Box;
        const TArray<FVector2D> Outline=LDRingPaint::Rounded(Box,11.f*U,3);
        // Copies show as a short stack of backs behind the face.
        for(int32 Back=FMath::Min(Card.Copies-1,2);Back>=1;--Back)
        {
            const TArray<FVector2D> Behind=LDRingPaint::Rounded(Shift(Box,FVector2D(5.f*Back,-5.f*Back)*U),11.f*U,3);
            P.FillShaded(Behind,[&](FVector2D At){return LDRingPaint::Mix(Metal(Ink,Box,At),FLinearColor::Black,Back==1?.38f:.6f);});
        }
        P.Fill(Offset(Outline,FVector2D(4.f,6.f)*U),FLinearColor(0.f,0.f,0.f,.6f));
        if(bSelected){P.Fill(LDRingPaint::Rounded(Grow(Box,7.f*U),17.f*U,3),LDBroadcast::Gold.CopyWithNewOpacity(.55f));P.Fill(LDRingPaint::Rounded(Grow(Box,3.f*U),13.f*U,3),FLinearColor::White);}
        else if(bLit){P.Fill(LDRingPaint::Rounded(Grow(Box,3.f*U),13.f*U,3),FLinearColor(1.f,1.f,1.f,.9f));}
        else if(Card.Rarity==ERarity::Legendary){P.Fill(LDRingPaint::Rounded(Grow(Box,5.f*U),15.f*U,3),LDBroadcast::Legendary.CopyWithNewOpacity(.4f));}
        P.FillShaded(Outline,[&](FVector2D At){return Metal(Ink,Box,At);});
        if(UTexture2D* Brushed=RingArt(EArt::Metal)){P.Image(Brushed,Box,FLinearColor(1.f,1.f,1.f,.3f),&Outline);}
        const FBox2D Inner=Grow(Box,-4.f*U);
        const TArray<FVector2D> InnerShape=LDRingPaint::Rounded(Inner,8.f*U,3);
        P.Fill(InnerShape,Hex(0x090C14));
        const float ArtSize=float(Inner.GetSize().X);
        const FBox2D ArtBox(Inner.Min,Inner.Min+FVector2D(ArtSize,ArtSize));
        const TArray<FVector2D> ArtClip=LDRingPaint::ClipConvex({ArtBox.Min,FVector2D(ArtBox.Max.X,ArtBox.Min.Y),FVector2D(ArtBox.Max.X,ArtBox.Min.Y+ArtSize*.9f),FVector2D(ArtBox.Min.X,ArtBox.Max.Y)},InnerShape);
        if(UTexture2D* Icon=RingIcon(Card.IconId)){P.Image(Icon,ArtBox,FLinearColor::White,&ArtClip);}
        else{P.Fill(ArtClip,Hex(0x222A3D));}
        P.FillShaded(ArtClip,[&](FVector2D At){const float T=GradientT(ArtBox,At,165.f);return T<.34f?FLinearColor(1.f,1.f,1.f,.3f*(1.f-T/.34f)):T>.7f?Hex(0x05070C,.55f*(T-.7f)/.3f):FLinearColor::Transparent;});
        if(Card.Rarity==ERarity::Legendary)
        {
            const float Phase=FMath::Fmod(float(Now)/3.6f,1.f);
            const float X=float(ArtBox.Min.X)+FMath::Lerp(-.6f,1.6f,FMath::Min(1.f,Phase/.55f))*ArtSize;
            P.Fill(LDRingPaint::ClipConvex({FVector2D(X,ArtBox.Min.Y),FVector2D(X+ArtSize*.22f,ArtBox.Min.Y),FVector2D(X+ArtSize*.02f,ArtBox.Max.Y),FVector2D(X-ArtSize*.2f,ArtBox.Max.Y)},ArtClip),FLinearColor(1.f,1.f,1.f,.35f));
        }
        // The name (two lines at most) under the art, and the rarity band along the foot.
        {
            const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Strong,14.f,F);
            const TArray<FString> Lines=LDRingPaint::Wrap(Card.Name,Font,float(Inner.GetSize().X)-6.f*U,2);
            const float LineH=14.f*F*1.16f;
            const float Mid=float(ArtBox.Max.Y-5.f*U+Inner.Max.Y)*.5f;
            float Y=Mid-LineH*Lines.Num()*.5f;
            for(const FString& Line:Lines){P.Text(Line,FVector2D(Inner.GetCenter().X,Y),Font,RingText,FVector2D(.5,0.));Y+=LineH;}
        }
        P.Fill(LDRingPaint::ClipConvex({FVector2D(Inner.Min.X,Inner.Max.Y-4.f*U),FVector2D(Inner.Max.X,Inner.Max.Y-4.f*U),Inner.Max,FVector2D(Inner.Min.X,Inner.Max.Y)},InnerShape),Ink.Band);
        P.Stroke(InnerShape,1.5f*U,RingDeep,true);
        // The form tag (技 / 冲 / 体) on the top-left edge.
        {
            const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Title,14.f,F);
            const FString Tag=FormTag(Card.Form);
            const float W=float(LDRingPaint::Measure(Tag,Font).X)+16.f*F;
            const FBox2D Chip(FVector2D(Box.Min.X,Box.Min.Y+8.f*U),FVector2D(Box.Min.X+W,Box.Min.Y+8.f*U+21.f*F));
            P.Fill({Chip.Min,FVector2D(Chip.Max.X,Chip.Min.Y),FVector2D(Chip.Max.X-7.f*F,Chip.Max.Y),FVector2D(Chip.Min.X,Chip.Max.Y)},RingInk);
            P.Text(Tag,FVector2D(Chip.Min.X+7.f*F,Chip.GetCenter().Y),Font,LDBroadcast::Gold,FVector2D(0.,.5));
        }
        if(Card.Copies>1)
        {
            const FSlateFontInfo Font=RingFigure(17.f,F);const FString Text=FString::Printf(TEXT("×%d"),Card.Copies);
            const float W=float(LDRingPaint::Measure(Text,Font).X)+16.f*F;
            const float Y=float(ArtBox.Max.Y)-6.f*U;
            const FBox2D Tag(FVector2D(Box.Max.X+7.f*U-W,Y),FVector2D(Box.Max.X+7.f*U,Y+21.f*F));
            P.Fill(LDRingPaint::Slant(Tag,8.f*F),LDBroadcast::Gold);P.Text(Text,Tag.GetCenter(),Font,Hex(0x16120A),FVector2D(.5,.5));
        }
        if(Card.bFresh)
        {
            const FVector2D At(Box.Max.X+12.f*U-19.f*F,Box.Min.Y-14.f*U+19.f*F);
            if(UTexture2D* Sticker=RingArt(EArt::Fresh)){P.Image(Sticker,Square(At,40.f*F),FLinearColor::White,nullptr,12.f);}
            else{P.Fill(Star(At,19.f*F,12,.78f,12.f),RingFreshRed);P.Text(TEXT("新"),At,RingFont(LDUIFonts::ERole::Title,16.f,F),FLinearColor::White,FVector2D(.5,.5));}
        }
        P.bXform=false;
    }
}

void ULDWorkshop::PaintTop(FPainter& P) const
{
    const FLayout& L=Layout;
    const float F=L.F,U=L.U;
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Close&&Box.Hit.Kind!=EHit::Matrix){continue;}
        const FBox2D Button(Box.Shape[0],Box.Shape[2]);
        const bool bHot=Focus()==Box.Hit||(Box.Hit.Kind==EHit::Matrix&&bMatrix);
        P.Fill(LDRingPaint::Slant(Button,8.f*F),bHot?Hex(0x1C2438):RingInk);
        P.Fill(LDRingPaint::Slant(FBox2D(FVector2D(Button.Min.X,Button.Max.Y-3.f*F),Button.Max),2.f*F),LDBroadcast::Gold);
        const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Strong,14.f,F);
        if(Box.Hit.Kind==EHit::Close)
        {
            const FString Key=Latest.OpenKey.IsEmpty()?FString(TEXT("B")):Latest.OpenKey;
            const float W=float(LDRingPaint::Measure(TEXT("收起"),Font).X+6.f*F+LDRingPaint::Measure(Key,RingFigure(18.f,F)).X);
            float X=float(Button.GetCenter().X)-W*.5f;
            X+=float(P.Text(TEXT("收起"),FVector2D(X,Button.GetCenter().Y),Font,RingText,FVector2D(0.,.5)).X)+6.f*F;
            P.Text(Key,FVector2D(X,Button.GetCenter().Y),RingFigure(18.f,F),LDBroadcast::Gold,FVector2D(0.,.52),LDBroadcast::SkewDegrees);
        }
        else{P.Text(TEXT("反应表"),Button.GetCenter(),Font,RingText,FVector2D(.5,.5));}
    }
    if(!bMatrix){return;}
    // The reaction table: every pair of the five elements, the build's own pairs outlined in gold.
    const ELDDemoElement Elements[5]={ELDDemoElement::Fire,ELDDemoElement::Water,ELDDemoElement::Electric,ELDDemoElement::Chill,ELDDemoElement::Rot};
    TSet<FString> Mine;for(const FReaction& Reaction:Ring.Reactions){Mine.Add(Reaction.Name);}
    const float CellW=48.f*F,CellH=27.f*F,Gap=3.f;
    const FVector2D Origin(L.Size.X-40.f*U-(6*CellW+5*Gap)-20.f*U,96.f*U+40.f*F);
    const FBox2D Sheet(Origin-FVector2D(10.f*U,10.f*U),Origin+FVector2D(6*CellW+5*Gap+10.f*U,6*CellH+5*Gap+10.f*U));
    P.Fill({Sheet.Min,FVector2D(Sheet.Max.X,Sheet.Min.Y),Sheet.Max,FVector2D(Sheet.Min.X,Sheet.Max.Y)},Hex(0x070A11,.96f));
    P.Fill({Sheet.Min,FVector2D(Sheet.Max.X,Sheet.Min.Y),FVector2D(Sheet.Max.X,Sheet.Min.Y+4.f*U),FVector2D(Sheet.Min.X,Sheet.Min.Y+4.f*U)},LDBroadcast::Gold);
    const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Strong,14.f,F);
    for(int32 Row=0;Row<6;++Row)
    {
        for(int32 Column=0;Column<6;++Column)
        {
            const FVector2D At=Origin+FVector2D(Column*(CellW+Gap),Row*(CellH+Gap));
            const FBox2D Cell(At,At+FVector2D(CellW,CellH));
            const TArray<FVector2D> Shape={Cell.Min,FVector2D(Cell.Max.X,Cell.Min.Y),Cell.Max,FVector2D(Cell.Min.X,Cell.Max.Y)};
            if(Row==0&&Column==0){continue;}
            if(Row==0||Column==0)
            {
                const ELDDemoElement E=Elements[(Row==0?Column:Row)-1];
                P.Fill(Shape,ElementInk(E));
                P.Text(LDHUD::ElementName(E),Cell.GetCenter(),Font,Hex(0x0B0F1A),FVector2D(.5,.5));
                continue;
            }
            P.Fill(Shape,Hex(0x141A2A));
            if(Row==Column){P.Text(TEXT("—"),Cell.GetCenter(),Font,FLinearColor(1.f,1.f,1.f,.3f),FVector2D(.5,.5));continue;}
            const FString Name=LDWorkshop::ReactionName(Elements[Row-1],Elements[Column-1]);
            const bool bMine=Mine.Contains(Name);
            if(bMine){P.Stroke(Shape,2.f,LDBroadcast::Gold,true);}
            P.Text(Name,Cell.GetCenter(),Font,bMine?LDBroadcast::Gold:RingText,FVector2D(.5,.5));
        }
    }
}

void ULDWorkshop::PaintLinks(FPainter& P,const TArray<uint64>& Lit) const
{
    const FHit Shown=Focus();
    if(Lit.IsEmpty()){return;}
    uint64 SelfKey=AnchorKey(Shown.Kind,Shown.Kind==EHit::Host||Shown.Kind==EHit::Installed?Shown.Host:INDEX_NONE,Shown.Kind==EHit::Host?INDEX_NONE:Shown.Index);
    const FVector2D* From=Layout.Anchors.Find(SelfKey);
    if(!From){return;}
    const FVector2D C=Layout.Centre;
    const float R=Layout.R;
    for(const uint64 Key:Lit)
    {
        const FVector2D* To=Layout.Anchors.Find(Key);
        if(!To){continue;}
        // A gentle curve bowed towards the centre, dashed gold over a black keyline.
        const FVector2D Mid=(*From+*To)*.5;
        const FVector2D Bow=Mid+(C-Mid)*.3;
        TArray<FVector2D> Curve;
        for(int32 Step=0;Step<=24;++Step)
        {
            const float T=Step/24.f;
            Curve.Add(FMath::Lerp(FMath::Lerp(*From,Bow,T),FMath::Lerp(Bow,*To,T),T));
        }
        P.Stroke(Curve,5.5f*R,RingDeep.CopyWithNewOpacity(.75f));
        P.Dashed(Curve,2.5f*R,10.f*R,5.f*R,Hex(0xFFE27A,.95f));
    }
}

void ULDWorkshop::PaintInspector(FPainter& P,int32 LinkCount) const
{
    const FHit Shown=Focus();
    const FLDDemoContentDefinitions* Content=Latest.Content;
    FString Name,Sub,Body,IconId;
    ERarity Rarity=ERarity::Common;
    bool bBodyIcon=false;
    const auto CardInfo=[&](FName Definition)
    {
        const LDHUD::FCardFaceView Face=LDHUD::CardFace(Definition,Content,Latest.Stats);
        Name=Face.Name;IconId=Face.IconId;Body=Face.SentencePlain;Rarity=RarityOf(Content,Definition);
        Sub=FString::Printf(TEXT("%s · %s · %s"),LDTerms::CardTypeName(uint8(Face.Type)),*FormPlace(Face.Form),*RarityName(Rarity));
        if(Face.Form==ELDCardForm::Medal){Sub=FString::Printf(TEXT("勋章 · %s"),*FormPlace(Face.Form));}
    };
    const auto ActiveInfo=[&](FName Id,ELDSkillBranch Branch,const FString& Where)
    {
        const LDHUD::FActiveCardView Face=LDHUD::ActiveCard(Id,Content,Latest.Stats,Branch);
        Name=Face.Name;IconId=Face.IconId;Body=Face.SentencePlain;Sub=Where;
        if(!Face.Cost.IsEmpty()){Sub+=TEXT(" · ")+Face.Cost;}
    };
    switch(Shown.Kind)
    {
    case EHit::Installed:
        if(Ring.Hosts.IsValidIndex(Shown.Host)&&Ring.Hosts[Shown.Host].Cards.IsValidIndex(Shown.Index)){CardInfo(Ring.Hosts[Shown.Host].Cards[Shown.Index].Definition);}
        break;
    case EHit::Hand:
        if(Ring.Hand.IsValidIndex(Shown.Index)){CardInfo(Ring.Hand[Shown.Index].Definition);}
        break;
    case EHit::Host:
        if(!Ring.Hosts.IsValidIndex(Shown.Host)){break;}
        {
            const FHost& Host=Ring.Hosts[Shown.Host];
            if(!Host.Active.IsNone()){ActiveInfo(Host.Active,Host.Branch,FString::Printf(TEXT("主动技能 · %s"),*Host.Key));}
            else if(Host.Column==LDHUD::EStorageColumn::Body){Name=TEXT("本体");Sub=TEXT("角色本体 · 普攻");Body=TEXT("本体牌装在这里；元素槽让第 2、3 段普攻带上元素。");bBodyIcon=true;}
            else if(Host.Column==LDHUD::EStorageColumn::Dash){Name=Host.Name;IconId=Host.IconId;Sub=FString::Printf(TEXT("冲刺 · %s"),*Host.Key);Body=TEXT("冲刺牌装在这里。");}
            else{Name=TEXT("空技能槽");Sub=Host.Key;Body=TEXT("学会一个主动技能后，技能牌才能装在这里。");}
        }
        break;
    case EHit::Gem:
        if(!Ring.Hosts.IsValidIndex(Shown.Host)){break;}
        if(Ring.Hosts[Shown.Host].bHasMedal)
        {
            CardInfo(Ring.Hosts[Shown.Host].Medal.Definition);
            static const TCHAR* Tiers[]={TEXT(""),TEXT("Ⅰ"),TEXT("Ⅱ"),TEXT("Ⅲ")};
            Sub=FString::Printf(TEXT("%s 阶 · %s 的元素"),Tiers[FMath::Clamp<int32>(Ring.Hosts[Shown.Host].Medal.Tier,0,3)],*Ring.Hosts[Shown.Host].Name);
        }
        else{Name=TEXT("元素槽 · 空");Sub=Ring.Hosts[Shown.Host].Name;Body=TEXT("镶一枚勋章，让这个位置的命中带上元素。不占构筑容量。");}
        break;
    case EHit::Stat:
        if(Ring.Stats.IsValidIndex(Shown.Index)&&Content)
        {
            const FStat& Stat=Ring.Stats[Shown.Index];
            const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
            Name=Stat.Name;IconId=Stat.IconId;
            Sub=Stat.bKeystone?FString::Printf(TEXT("%d 点 · 关键石已激活"),Stat.Points):FString::Printf(TEXT("%d 点 · 再投 %d 点激活关键石"),Stat.Points,FMath::Max(0,Ring.Keystone-Stat.Points));
            if(Stats.IsValidIndex(Shown.Index)){Body=LDHUD::StatRow(*Stats[Shown.Index],Stat.Points,Content).PerPoint;}
        }
        break;
    case EHit::Offer:
        if(!Ring.Batch.Options.IsValidIndex(Shown.Index)){break;}
        {
            const FOption& Option=Ring.Batch.Options[Shown.Index];
            if(Ring.Batch.Kind==EBatch::Card){CardInfo(Option.Id);}
            else if(Ring.Batch.Kind==EBatch::Final){Name=Option.Name;IconId=Option.IconId;Body=Option.Text;Sub=TEXT("终局改装 · 选定后锁定构筑");Rarity=ERarity::Legendary;}
            else{ActiveInfo(Option.Id,ELDSkillBranch::None,Ring.Batch.Kind==EBatch::Blueprint?FString(LDTerms::Blueprint):FString(TEXT("主动技能")));}
        }
        break;
    case EHit::Shop:
        if(!Ring.Shop.IsValidIndex(Shown.Index)){break;}
        {
            const FShopItem& Item=Ring.Shop[Shown.Index];
            if(Item.Kind==ELDShopItemKind::Card){CardInfo(Item.Id);}
            else if(Item.Kind==ELDShopItemKind::Skill){ActiveInfo(Item.Id,ELDSkillBranch::None,TEXT("主动技能 · 商店"));}
            else{Name=TEXT("技能点");Sub=Item.Note;Body=TEXT("给已学的技能升一级。");}
            Sub+=FString::Printf(TEXT(" · %s"),*Diamonds(Item.Price));
        }
        break;
    case EHit::Wing:
        if(Ring.Derived.IsValidIndex(Shown.Index)&&Ring.Stats.IsValidIndex(Ring.Derived[Shown.Index].Source))
        {
            const FDerived& Row=Ring.Derived[Shown.Index];
            Name=Row.Name;Sub=FString::Printf(TEXT("%s · 来自%s"),*Row.Value,*Ring.Stats[Row.Source].Name);
            const FLDCombatAttributeRowView* Source=Latest.Attributes.FindByPredicate([&Row](const FLDCombatAttributeRowView& R){return R.Attribute==Row.Attribute;});
            if(Source){Body=LDHUD::AttributeRow(*Source).Breakdown;}
        }
        break;
    default: break;
    }
    if(Name.IsEmpty()){return;}
    if(LinkCount>0){Sub+=FString::Printf(TEXT(" · 联动 %d"),LinkCount);}
    const FLayout& L=Layout;
    const float U=L.U,F=L.F,R=L.R;
    const float W=FMath::Max(440.f*U,400.f*F);
    const float Z=84.f*R;
    const FVector2D At(40.f*U,96.f*U);
    const FSlateFontInfo NameFont=RingFont(LDUIFonts::ERole::Title,20.f,F),SubFont=RingFont(LDUIFonts::ERole::Strong,14.f,F),BodyFont=RingFont(LDUIFonts::ERole::Body,16.f,F);
    const float TextX=float(At.X)+18.f*U+Z+14.f*U;
    const float TextW=float(At.X)+W-26.f*U-TextX;
    const TArray<FString> Lines=LDRingPaint::Wrap(Body,BodyFont,TextW,4);
    const float H=FMath::Max(128.f*U,14.f*U*2.f+FMath::Max(Z,20.f*F*1.25f+14.f*F*1.4f+5.f*U+Lines.Num()*16.f*F*1.35f));
    const FBox2D Card(At,At+FVector2D(W,H));
    P.FillShaded({Card.Min,FVector2D(Card.Max.X,Card.Min.Y),FVector2D(Card.Max.X-18.f*U,Card.Max.Y),FVector2D(Card.Min.X,Card.Max.Y)},
        [&](FVector2D Point){return LDRingPaint::Mix(Hex(0x070A11,.96f),Hex(0x070A11,.9f),float((Point.X-Card.Min.X)/W));});
    P.Fill({Card.Min,FVector2D(Card.Min.X+6.f*U,Card.Min.Y),FVector2D(Card.Min.X+6.f*U,Card.Max.Y),FVector2D(Card.Min.X,Card.Max.Y)},LDBroadcast::Gold);
    const FBox2D IconBox(FVector2D(At.X+18.f*U,At.Y+14.f*U),FVector2D(At.X+18.f*U+Z,At.Y+14.f*U+Z));
    if(!IconId.IsEmpty()||bBodyIcon)
    {
        FCard Face;Face.IconId=IconId;Face.Name=Name;Face.Rarity=Rarity;
        if(bBodyIcon&&RingArt(EArt::Body))
        {
            const TArray<FVector2D> Outline=LDRingPaint::Rounded(IconBox,10.f*R,3);
            P.FillShaded(Outline,[&](FVector2D Point){return Metal(FFrameInk{Hex(0xFFF6CF),Hex(0xE0A81E),Hex(0x6A4606),LDBroadcast::Gold},IconBox,Point);});
            const FBox2D Inner=Grow(IconBox,-3.f*R);const TArray<FVector2D> InnerShape=LDRingPaint::Rounded(Inner,7.f*R,3);
            P.Fill(InnerShape,RingFace);P.Image(RingArt(EArt::Body),Inner,FLinearColor::White,&InnerShape);
        }
        else{PaintNode(P,IconBox,Face,1.f,false,false,0.f);}
    }
    float Y=float(At.Y)+14.f*U;
    P.Text(Name,FVector2D(TextX,Y),NameFont,FLinearColor::White);Y+=20.f*F*1.25f;
    P.Text(Sub,FVector2D(TextX,Y),SubFont,LDBroadcast::Gold);Y+=14.f*F*1.4f+5.f*U;
    for(const FString& Line:Lines){P.Text(Line,FVector2D(TextX,Y),BodyFont,Hex(0xE3E7EF));Y+=16.f*F*1.35f;}
}

void ULDWorkshop::PaintClosed(FPainter& P) const
{
    const FLayout& L=Layout;
    const float U=L.U,F=L.F;
    const FHit Shown=Focus();
    if(!OfferFaces.IsEmpty())
    {
        const FBox2D First=OfferFaces[0].Box;
        const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Title,14.f,F);
        const float W=float(LDRingPaint::Measure(Ring.Batch.Title,Font).X)+24.f*F;
        const FBox2D Label(FVector2D(First.Min.X,First.Min.Y-12.f*U-24.f*F),FVector2D(First.Min.X+W,First.Min.Y-12.f*U));
        P.Fill(LDRingPaint::Slant(Label,8.f*F),LDBroadcast::Gold);
        P.Text(Ring.Batch.Title,Label.GetCenter(),Font,Hex(0x16120A),FVector2D(.5,.5));
        for(const FFace& Face:OfferFaces)
        {
            const FOption& Option=Ring.Batch.Options[Face.Hit.Index];
            FCard Card;Card.IconId=Option.IconId;Card.Name=Option.Name;Card.Rarity=Option.Rarity;
            const float Age=BatchAt<0.?1.f:FMath::Clamp(float((UiNow()-BatchAt-Face.Hit.Index*RingRevealStagger)/RingRevealTime),0.f,1.f);
            const float Saved=P.Opacity;P.Opacity*=Age;
            PaintNode(P,Shift(Face.Box,FVector2D(0.,(1.f-Age)*16.f*U)),Card,1.f,Shown==Face.Hit,false,0.f);
            const FSlateFontInfo KeyFont=RingFigure(19.f,F);
            const FBox2D Tag(FVector2D(Face.Box.Min.X-9.f*U,Face.Box.Min.Y-9.f*U),FVector2D(Face.Box.Min.X-9.f*U+26.f*F,Face.Box.Min.Y-9.f*U+23.f*F));
            P.Fill(LDRingPaint::Slant(Tag,8.f*F),LDBroadcast::Gold);
            P.Text(FString::FromInt(Face.Hit.Index+1),Tag.GetCenter(),KeyFont,Hex(0x16120A),FVector2D(.5,.5));
            P.Opacity=Saved;
        }
        if(Ring.Batch.Queued>0)
        {
            const FBox2D Last=OfferFaces.Last().Box;
            const FBox2D Pile(FVector2D(Last.Max.X+20.f*U,Last.Min.Y+10.f*U),FVector2D(Last.Max.X+76.f*U,Last.Max.Y));
            for(int32 Back=2;Back>=1;--Back)
            {
                const FBox2D Behind=Shift(Pile,FVector2D(5.f*Back,-5.f*Back)*U);
                P.Fill(LDRingPaint::Rounded(Grow(Behind,2.f),9.f*U,3),Back==2?Hex(0x5C5233):Hex(0x8B7A45));
                P.Fill(LDRingPaint::Rounded(Behind,9.f*U,3),Hex(0x0B0F1A));
            }
            P.Fill(LDRingPaint::Rounded(Pile,9.f*U,3),LDBroadcast::Gold);
            P.Fill(LDRingPaint::Rounded(Grow(Pile,-3.f*U),7.f*U,3),Hex(0x151B2E));
            P.Text(FString::Printf(TEXT("+%d"),Ring.Batch.Queued),Pile.GetCenter(),RingFigure(22.f,F),FLinearColor::White,FVector2D(.5,.5),LDBroadcast::SkewDegrees);
        }
    }
    for(const FShape& Box:L.Boxes)
    {
        if(Box.Hit.Kind!=EHit::Entry){continue;}
        // The entry: 构筑 with its key, the unspent points and the red 新 N.
        const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Strong,14.f,F),Figure=RingFigure(18.f,F);
        const FString Key=Latest.OpenKey.IsEmpty()?FString(TEXT("B")):Latest.OpenKey;
        const FString Points=Ring.Unspent>0?FString::Printf(TEXT("属性点 %lld"),Ring.Unspent):FString();
        const FString Fresh=Ring.Fresh>0?FString::Printf(TEXT("新 %d"),Ring.Fresh):FString();
        const float W=16.f*F+float(LDRingPaint::Measure(TEXT("构筑"),Font).X)+6.f*F+float(LDRingPaint::Measure(Key,Figure).X)
            +(Points.IsEmpty()?0.f:12.f*F+float(LDRingPaint::Measure(Points,Font).X))+(Fresh.IsEmpty()?0.f:10.f*F+float(LDRingPaint::Measure(Fresh,Font).X)+12.f*F)+16.f*F;
        const FBox2D Button(Box.Shape[0],FVector2D(Box.Shape[0].X+W,Box.Shape[2].Y));
        const bool bHot=Shown==Box.Hit;
        P.Fill(LDRingPaint::Slant(Button,8.f*F),bHot?Hex(0x1C2438):RingInk);
        P.Fill(LDRingPaint::Slant(FBox2D(FVector2D(Button.Min.X,Button.Max.Y-3.f*F),Button.Max),2.f*F),LDBroadcast::Gold);
        float X=float(Button.Min.X)+16.f*F;const float Y=float(Button.GetCenter().Y);
        X+=float(P.Text(TEXT("构筑"),FVector2D(X,Y),Font,RingText,FVector2D(0.,.5)).X)+6.f*F;
        X+=float(P.Text(Key,FVector2D(X,Y),Figure,LDBroadcast::Gold,FVector2D(0.,.52),LDBroadcast::SkewDegrees).X);
        if(!Points.IsEmpty()){X+=12.f*F;X+=float(P.Text(Points,FVector2D(X,Y),Font,LDBroadcast::Gold,FVector2D(0.,.5)).X);}
        if(!Fresh.IsEmpty())
        {
            X+=10.f*F;
            const float FW=float(LDRingPaint::Measure(Fresh,Font).X)+12.f*F;
            const FBox2D Pill(FVector2D(X,Y-10.f*F),FVector2D(X+FW,Y+10.f*F));
            P.Fill(LDRingPaint::Rounded(Pill,10.f*F,4),RingFreshRed);
            P.Text(Fresh,Pill.GetCenter(),Font,FLinearColor::White,FVector2D(.5,.5));
        }
    }
}

int32 ULDWorkshop::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    Layer=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!Ring.bValid||!Latest.bShown){return Layer;}
    FPainter P(Elements,Geometry,Layer);
    const float Open=OpenAmount();
    if(Open>0.f)
    {
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
        PaintDim(P);
        PaintSectors(P,Lit,bDim);
        PaintGems(P);
        PaintRadarLabels(P);
        PaintWing(P);
        PaintShop(P,Lit);
        PaintBottom(P,Lit);
        PaintTop(P);
        if(!Shown.IsNone()){PaintLinks(P,Lit);}
        // The element chooser and the rest confirm line, over the ring's top.
        const float U=Layout.U,F=Layout.F;
        if(Storage.IsValid()&&Storage->IsChoosing())
        {
            const TArray<ELDDemoElement> Elements2=Storage->ChooserElements();
            for(const FShape& Box:Layout.Boxes)
            {
                if(Box.Hit.Kind!=EHit::Element||!Elements2.IsValidIndex(Box.Hit.Index)){continue;}
                const FBox2D Button(Box.Shape[0],Box.Shape[2]);
                P.Fill(LDRingPaint::Slant(Button,8.f*F),ElementInk(Elements2[Box.Hit.Index]));
                P.Text(LDHUD::ElementName(Elements2[Box.Hit.Index]),Button.GetCenter(),RingFont(LDUIFonts::ERole::Title,15.f,F),Hex(0x0B0F1A),FVector2D(.5,.5));
            }
            P.Text(TEXT("选一种元素"),FVector2D(Layout.Centre.X,250.f*U-6.f*U),RingFont(LDUIFonts::ERole::Title,15.f,F),FLinearColor::White,FVector2D(.5,1.));
        }
        if(Choices.IsValid()&&Choices->HasChildConfirmation())
        {
            const FString Line=Choices->ConfirmLine();
            const FSlateFontInfo Font=RingFont(LDUIFonts::ERole::Title,15.f,F);
            const float W=float(LDRingPaint::Measure(Line,Font).X)+36.f*F;
            const FBox2D Plate(FVector2D(Layout.Centre.X-W*.5f,250.f*U),FVector2D(Layout.Centre.X+W*.5f,250.f*U+30.f*F));
            P.Fill(LDRingPaint::Slant(Plate,8.f*F),LDBroadcast::LiveRed);
            P.Text(Line,Plate.GetCenter(),Font,FLinearColor::White,FVector2D(.5,.5));
            for(const FShape& Box:Layout.Boxes)
            {
                if(Box.Hit.Kind!=EHit::ConfirmYes&&Box.Hit.Kind!=EHit::ConfirmNo){continue;}
                const FBox2D Button(Box.Shape[0],Box.Shape[2]);
                const bool bYes=Box.Hit.Kind==EHit::ConfirmYes;
                P.Fill(LDRingPaint::Slant(Button,8.f*F),Focus()==Box.Hit?Hex(0x1C2438):RingInk);
                P.Fill(LDRingPaint::Slant(FBox2D(FVector2D(Button.Min.X,Button.Max.Y-3.f*F),Button.Max),2.f*F),bYes?LDBroadcast::LiveRed:LDBroadcast::Gold);
                P.Text(bYes?TEXT("确认"):TEXT("取消"),Button.GetCenter(),RingFont(LDUIFonts::ERole::Strong,14.f,F),RingText,FVector2D(.5,.5));
            }
        }
        PaintInspector(P,Lit.Num());
    }
    if(!IsOpen())
    {
        P.Opacity=1.f-Open;
        PaintClosed(P);
    }
    return P.Layer;
}
