#include "UI/LDMinimap.h"
#include "UI/LDHUDModel.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Core/LDGroundQueries.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

// Public static map from the registered geometry and approved content only; no unit, coverage or remembered mark.
// Fractions of the ground bounds are resolved per world revision; local runs, outlines and site points per widget size.
struct FLDMinimapStatic
{
    // Wall blocks walking and sight, LowWall only walking, SightScreen only sight.
    enum class EArea : uint8 { Wall, LowWall, SightScreen, CaptureZone };
    enum class ESite : uint8 { Shop, Capture, LowCamp, HighCamp };
    struct FArea { EArea Style=EArea::Wall; TArray<FVector2D> Fraction; };
    struct FSite { ESite Kind=ESite::Shop; FVector2D Fraction=FVector2D::ZeroVector; };
    struct FRun { FVector2f Position=FVector2f::ZeroVector; FVector2f Extent=FVector2f::ZeroVector; FLinearColor Color=FLinearColor::White; };
    struct FOutline { TArray<FVector2f> Points; FLinearColor Color=FLinearColor::White; float Thickness=1.f; bool bDashed=false; };
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FString ContentError;
    bool bContentTried=false,bGroundLogged=false,bResolved=false,bValid=false;
    FLDWorldRevision World;
    TArray<FArea> Areas;
    TArray<FSite> Sites;
    FVector2D Size=FVector2D(-1.,-1.);
    TArray<FRun> Fills;
    TArray<FOutline> Outlines;
    TArray<FVector2f> SitePoints;
    // Camp-member triangle around its centre, independent of the widget size.
    TArray<FRun> MemberShadow,MemberBody;
};

namespace
{
// The minimap shares the registered ground bounds that observation coverage indexes.
bool MinimapGroundBounds(const UWorld* World,FBox2D& Out)
{
    const auto* Registry=World?World->GetSubsystem<ULDMapRegistry>():nullptr;
    const FLDDemoWorldGeometry* Geometry=Registry&&Registry->Enabled()?Registry->GetGeometry():nullptr;
    FVector Min,Max;FString Error;
    if(!Geometry||Geometry->Surfaces.Num()!=1||!Geometry->Surfaces[0].MinCm.ToVector(Min,Error)||!Geometry->Surfaces[0].MaxCm.ToVector(Max,Error)||Max.X<=Min.X||Max.Y<=Min.Y){return false;}
    Out=FBox2D(FVector2D(Min.X,Min.Y),FVector2D(Max.X,Max.Y));
    return true;
}
// Rounded to half its height, so a square element paints an anti-aliased disc.
const FSlateBrush* MinimapDiscBrush(){static const FSlateRoundedBoxBrush Disc(FLinearColor::White);return &Disc;}
// Even-odd fill as runs one local unit high: Slate has no filled-polygon element. Non-finite or far-off input is skipped.
void MinimapScanRuns(const TArray<FVector2f>& Polygon,TFunctionRef<void(float,float,float)> Emit)
{
    if(Polygon.Num()<3){return;}
    float Top=Polygon[0].Y,Bottom=Polygon[0].Y;
    for(const FVector2f& Vertex:Polygon){if(!FMath::IsFinite(Vertex.X)||!FMath::IsFinite(Vertex.Y)){return;}Top=FMath::Min(Top,Vertex.Y);Bottom=FMath::Max(Bottom,Vertex.Y);}
    if(Top<-1e5f||Bottom>1e5f){return;}
    TArray<float,TInlineAllocator<8>> Cross;
    for(int32 Row=FMath::FloorToInt(Top);Row<Bottom;++Row)
    {
        const float Mid=Row+.5f;Cross.Reset();
        for(int32 I=0,J=Polygon.Num()-1;I<Polygon.Num();J=I++)
        {const FVector2f& From=Polygon[J];const FVector2f& To=Polygon[I];if((From.Y>Mid)!=(To.Y>Mid)){Cross.Add(From.X+(Mid-From.Y)*(To.X-From.X)/(To.Y-From.Y));}}
        Cross.Sort();
        for(int32 K=0;K+1<Cross.Num();K+=2){if(Cross[K+1]>Cross[K]){Emit(Cross[K],float(Row),Cross[K+1]-Cross[K]);}}
    }
}
// Sutherland-Hodgman against the unit square, so geometry beyond the ground bounds never paints outside the map.
TArray<FVector2D> MinimapClipUnit(TArray<FVector2D> Polygon)
{
    for(int32 Edge=0;Edge<4&&Polygon.Num()>=3;++Edge)
    {
        const int32 Component=Edge/2;const double Toward=Edge%2?-1.:1.,Limit=Edge%2?1.:0.;
        const TArray<FVector2D> Before=MoveTemp(Polygon);Polygon.Reset();
        for(int32 I=0;I<Before.Num();++I)
        {
            const FVector2D& From=Before[I];const FVector2D& To=Before[(I+1)%Before.Num()];
            const double FromSide=Toward*(From[Component]-Limit),ToSide=Toward*(To[Component]-Limit);
            if(FromSide>=0){Polygon.Add(From);}
            if((FromSide>=0)!=(ToSide>=0)){Polygon.Add(From+(To-From)*(FromSide/(FromSide-ToSide)));}
        }
    }
    return Polygon;
}
double MinimapArea(const TArray<FVector2D>& Polygon)
{double Twice=0;for(int32 I=0;I<Polygon.Num();++I){const FVector2D& From=Polygon[I];const FVector2D& To=Polygon[(I+1)%Polygon.Num()];Twice+=From.X*To.Y-To.X*From.Y;}return FMath::Abs(Twice)*.5;}
// One registered world revision. Any unresolved item fails the whole layer, because a partial map could pass for the
// approved one. Spawn anchors are never drawn.
bool MinimapResolveStatic(FLDMinimapStatic& Cache,const FLDDemoWorldGeometry& Geometry,const FLDDemoContentDefinitions& Approved,const FBox2D& Ground,FString& Error)
{
    Cache.Areas.Reset();Cache.Sites.Reset();
    if(!Approved.World.MapId.Equals(Geometry.MapId,ESearchCase::CaseSensitive))
    {Error=FString::Printf(TEXT("Approved content names map %s but the registered world is %s."),*Approved.World.MapId,*Geometry.MapId);return false;}
    auto ToMap=[&Ground,&Error](const FString& Id,const FLDDemoWorldPoint& Point,FVector2D& Result)
    {FVector Cm;if(!Point.ToVector(Cm,Error)){Error=Id+TEXT(": ")+Error;return false;}Result=LDHUD::MapPoint(Cm,Ground);return true;};
    for(const FLDDemoWorldBlocker& Blocker:Geometry.Blockers)
    {
        if(!Blocker.BlocksMovement&&!Blocker.BlocksSight){continue;}
        FLDMinimapStatic::FArea Area;Area.Style=!Blocker.BlocksSight?FLDMinimapStatic::EArea::LowWall:!Blocker.BlocksMovement?FLDMinimapStatic::EArea::SightScreen:FLDMinimapStatic::EArea::Wall;
        for(const FLDDemoWorldPoint& Corner:Blocker.PolygonCm){FVector2D Mapped;if(!ToMap(Blocker.Id,Corner,Mapped)){return false;}Area.Fraction.Add(Mapped);}
        if(Area.Fraction.Num()<3){Error=Blocker.Id+TEXT(": a blocker polygon needs at least three corners.");return false;}
        Area.Fraction=MinimapClipUnit(MoveTemp(Area.Fraction));
        if(MinimapArea(Area.Fraction)>1e-9){Cache.Areas.Add(MoveTemp(Area));}
    }
    const FLDDemoRational& Radius=Approved.Parameters.CaptureRadiusCm;
    const double RadiusCm=Radius.Denominator>0?double(Radius.Numerator)/double(Radius.Denominator):0.;const FVector2D Span=Ground.GetSize();
    for(const FLDDemoWorldAnchor& Anchor:Geometry.Anchors)
    {
        if(Anchor.Kind==ELDDemoWorldAnchorKind::Spawn){continue;}
        FLDMinimapStatic::FSite Site;if(!ToMap(Anchor.Id,Anchor.PositionCm,Site.Fraction)){return false;}
        if(Anchor.Kind==ELDDemoWorldAnchorKind::Shop){Site.Kind=FLDMinimapStatic::ESite::Shop;}
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::Capture)
        {
            if(!FMath::IsFinite(RadiusCm)||RadiusCm<=0){Error=TEXT("CAPTURE.RadiusCm is not a positive approved length.");return false;}
            Site.Kind=FLDMinimapStatic::ESite::Capture;FLDMinimapStatic::FArea Zone;Zone.Style=FLDMinimapStatic::EArea::CaptureZone;
            for(int32 Step=0;Step<48;++Step){const double Angle=UE_DOUBLE_TWO_PI*Step/48.;Zone.Fraction.Add(Site.Fraction+FVector2D(FMath::Cos(Angle)*RadiusCm/Span.X,FMath::Sin(Angle)*RadiusCm/Span.Y));}
            Zone.Fraction=MinimapClipUnit(MoveTemp(Zone.Fraction));if(MinimapArea(Zone.Fraction)>1e-9){Cache.Areas.Insert(MoveTemp(Zone),0);}
        }
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::Camp)
        {
            // Tier comes from the approved monster placed on this camp anchor.
            const FLDDemoCampPlacement* Camp=Approved.Camps.FindByPredicate([&Anchor](const FLDDemoCampPlacement& Placement){return Placement.AnchorId.Equals(Anchor.Id,ESearchCase::CaseSensitive);});
            const FLDDemoMonsterDefinition* Monster=Camp?Approved.Monsters.FindByPredicate([Camp](const FLDDemoMonsterDefinition& Definition){return Definition.Id.Equals(Camp->MonsterId,ESearchCase::CaseSensitive);}):nullptr;
            if(!Monster){Error=Anchor.Id+TEXT(": camp has no approved placement or monster tier.");return false;}
            Site.Kind=Monster->Tier==ELDDemoCampTier::High?FLDMinimapStatic::ESite::HighCamp:FLDMinimapStatic::ESite::LowCamp;
        }
        else{Error=Anchor.Id+TEXT(": unsupported anchor kind.");return false;}
        Cache.Sites.Add(Site);
    }
    return true;
}
void MinimapLayoutStatic(FLDMinimapStatic& Cache,const FVector2D& Size)
{
    Cache.Size=Size;Cache.Fills.Reset();Cache.Outlines.Reset();Cache.SitePoints.Reset();
    auto Local=[&Size](const FVector2D& Point){return FVector2f(float(Point.X*Size.X),float(Point.Y*Size.Y));};
    for(const FLDMinimapStatic::FArea& Area:Cache.Areas)
    {
        // Opaque walls, translucent low walls with a solid rim, sight screens with a dashed rim: not colour alone.
        FLinearColor Fill(.50f,.53f,.58f),Rim(.82f,.85f,.90f);float Thickness=1.f;bool bDashed=false;
        switch(Area.Style)
        {
        case FLDMinimapStatic::EArea::Wall:break;
        case FLDMinimapStatic::EArea::LowWall:Fill=FLinearColor(.48f,.34f,.18f,.40f);Rim=FLinearColor(.90f,.68f,.40f);Thickness=1.5f;break;
        case FLDMinimapStatic::EArea::SightScreen:Fill=FLinearColor(.20f,.46f,.26f,.35f);Rim=FLinearColor(.52f,.90f,.56f);Thickness=1.5f;bDashed=true;break;
        case FLDMinimapStatic::EArea::CaptureZone:Fill=FLinearColor(1.f,.84f,.30f,.16f);Rim=FLinearColor(1.f,.84f,.34f,.95f);Thickness=1.5f;break;
        }
        TArray<FVector2f> Points;for(const FVector2D& Point:Area.Fraction){Points.Add(Local(Point));}
        MinimapScanRuns(Points,[&Cache,&Fill](float X,float Y,float Width)
        {
            // Rows with the same span merge, so an axis-aligned wall stays one element.
            FLDMinimapStatic::FRun* Last=Cache.Fills.Num()?&Cache.Fills.Last():nullptr;
            if(Last&&Last->Color==Fill&&FMath::IsNearlyEqual(Last->Position.X,X,.01f)&&FMath::IsNearlyEqual(Last->Extent.X,Width,.01f)&&FMath::IsNearlyEqual(Last->Position.Y+Last->Extent.Y,Y,.01f)){Last->Extent.Y+=1.f;}
            else{Cache.Fills.Add({FVector2f(X,Y),FVector2f(Width,1.f),Fill});}
        });
        const FVector2f First=Points[0];Points.Add(First);Cache.Outlines.Add({MoveTemp(Points),Rim,Thickness,bDashed});
    }
    for(const FLDMinimapStatic::FSite& Site:Cache.Sites){Cache.SitePoints.Add(Local(Site.Fraction));}
}
void MinimapGlyphTemplates(FLDMinimapStatic& Cache)
{
    const auto Rows=[](const TArray<FVector2f>& Polygon,TArray<FLDMinimapStatic::FRun>& Out)
    {MinimapScanRuns(Polygon,[&Out](float X,float Y,float Width){Out.Add({FVector2f(X,Y),FVector2f(Width,1.f),FLinearColor::White});});};
    Rows({FVector2f(0.f,-6.5f),FVector2f(6.f,4.5f),FVector2f(-6.f,4.5f)},Cache.MemberShadow);
    Rows({FVector2f(0.f,-4.5f),FVector2f(4.f,3.f),FVector2f(-4.f,3.f)},Cache.MemberBody);
}
FLDMinimapStatic& MinimapCache(TSharedPtr<FLDMinimapStatic>& Holder)
{if(!Holder){Holder=MakeShared<FLDMinimapStatic>();MinimapGlyphTemplates(*Holder);}return *Holder;}
// Once per registered world revision and outside paint. Approved content loads once per widget like the HUD's labels;
// a failure is logged once and never replaced by guessed sites.
void MinimapResolveRevision(FLDMinimapStatic& Cache,const UWorld* World,const FBox2D& Ground)
{
    const auto* Registry=World->GetSubsystem<ULDMapRegistry>();const FLDDemoWorldGeometry* Geometry=Registry?Registry->GetGeometry():nullptr;
    if(!Geometry||(Cache.bResolved&&Cache.World==Registry->Revision())){return;}
    Cache.bResolved=true;Cache.World=Registry->Revision();Cache.Size=FVector2D(-1.,-1.);FString Error;
    if(!Cache.bContentTried){Cache.bContentTried=true;if(!LDDemoEntry::LoadRuntimeContent(Cache.Content,Error)||!Cache.Content){Cache.Content.Reset();Cache.ContentError=Error;}}
    Cache.bValid=Cache.Content&&MinimapResolveStatic(Cache,*Geometry,Cache.Content->Get(),Ground,Error);
    if(!Cache.bValid)
    {
        Cache.Areas.Reset();Cache.Sites.Reset();
        const FString Reason=Cache.Content?Error:TEXT("Approved Demo content unavailable: ")+Cache.ContentError;
        UE_LOG(LogTemp,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE %s %s"),*Cache.World.MapId.ToString(),*Reason);
    }
}
// Paint only lays a resolved layer out for its own size, and never draws one resolved for another world revision.
bool MinimapStaticReady(FLDMinimapStatic& Cache,const UWorld* World,const FVector2D& Size)
{
    const auto* Registry=World->GetSubsystem<ULDMapRegistry>();
    if(!Cache.bValid||!Registry||Cache.World!=Registry->Revision()){return false;}
    if(!Cache.Size.Equals(Size,.01)){MinimapLayoutStatic(Cache,Size);}
    return true;
}
}
TSharedRef<SWidget> ULDMinimap::RebuildWidget()
{if(!WidgetTree->RootWidget){auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor(.02f,.03f,.05f));WidgetTree->RootWidget=Background;}return Super::RebuildWidget();}

FVector2D ULDMinimap::Fraction(const FGeometry& G,const FPointerEvent& E) const
{return G.AbsoluteToLocal(E.GetScreenSpacePosition())/G.GetLocalSize();}
bool ULDMinimap::Gesture(FVector2D P,bool Move)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->IsFrontend()){return false;}
    // Death keeps camera operations (UI 4); a ground order still needs the controller's world-command gate.
    if(Move&&!PC->CanWorldCommand()){if(PC->IsAwaitingRespawn()){PC->IntentFeedback=TEXT("社死中只能移动镜头，不能下达移动");}return false;}
    FBox2D Ground;if(!MinimapGroundBounds(GetWorld(),Ground)){PC->IntentFeedback=TEXT("小地图不可用：未加载 Demo 世界");return false;}
    const FVector XY=LDHUD::MapWorld(P,Ground);
    if(!Move){PC->Navigation->PanTo(XY);return true;}
    FVector Surface;if(!LDGround::MapSurface(GetWorld(),XY,Surface)){PC->IntentFeedback=TEXT("无法到达该位置");return false;}
    PC->Navigation->Navigate(Surface);return true;
}
FReply ULDMinimap::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        // The press keeps the pointer until release even when the pan is refused, so the release never reaches the world.
        const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());DragMatch=PC->OwnerState.Match;DragLife=PC->OwnerState.Life;
        bDragging=Gesture(Fraction(G,E),false);return FReply::Handled().CaptureMouse(TakeWidget());
    }
    if(E.GetEffectingButton()==EKeys::RightMouseButton){Gesture(Fraction(G,E),true);return FReply::Handled();}return FReply::Handled();
}
FReply ULDMinimap::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{if(bDragging&&!Gesture(Fraction(G,E),false)){bDragging=false;}return FReply::Handled();}
FReply ULDMinimap::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{if(E.GetEffectingButton()==EKeys::LeftMouseButton){bDragging=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Handled();}
void ULDMinimap::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){bDragging=false;Super::NativeOnMouseCaptureLost(E);}
void ULDMinimap::NativeTick(const FGeometry& G,float D)
{
    Super::NativeTick(G,D);
    const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());FLDMinimapStatic& Cache=MinimapCache(StaticMap);FBox2D Ground;
    const bool bGround=MinimapGroundBounds(GetWorld(),Ground);
    if(bGround){MinimapResolveRevision(Cache,GetWorld(),Ground);}
    else if(!Cache.bGroundLogged){Cache.bGroundLogged=true;UE_LOG(LogTemp,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE No single registered ground surface; only the background is drawn."));}
    // Death alone keeps a drag; it ends when panning is invalid or the owner moved to another match or life.
    if(bDragging&&(PC->IsFrontend()||!bGround||PC->OwnerState.Match!=DragMatch||PC->OwnerState.Life!=DragLife)){bDragging=false;}
}
int32 ULDMinimap::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    const FVector2D Size=G.GetLocalSize();const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const FLinearColor Ink(.02f,.03f,.03f,.92f);
    auto Box=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint){FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
    auto Diamond=[&](int32 Layer,const FVector2f& At,float Edge,const FLinearColor& Tint){FSlateDrawElement::MakeRotatedBox(E,Base+Layer,G.ToPaintGeometry(FVector2f(Edge,Edge),FSlateLayoutTransform(At-FVector2f(Edge*.5f,Edge*.5f))),White,ESlateDrawEffect::None,UE_PI*.25f,TOptional<FVector2f>(),FSlateDrawElement::RelativeToElement,Tint);};
    auto Disc=[&](int32 Layer,const FVector2f& At,float Diameter,const FLinearColor& Tint){FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(FVector2f(Diameter,Diameter),FSlateLayoutTransform(At-FVector2f(Diameter*.5f,Diameter*.5f))),MinimapDiscBrush(),ESlateDrawEffect::None,Tint);};
    auto Lines=[&](int32 Layer,const TArray<FVector2f>& Points,const FLinearColor& Tint,float Thickness){FSlateDrawElement::MakeLines(E,Base+Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Tint,true,Thickness);};
    E.PushClip(FSlateClippingZone(G));
    Box(1,FVector2f::ZeroVector,FVector2f(float(Size.X),float(Size.Y)),FLinearColor(.035f,.05f,.045f));
    FBox2D Ground;
    if(MinimapGroundBounds(GetWorld(),Ground))
    {
        FLDMinimapStatic& Cache=MinimapCache(StaticMap);
        const FLDOwnerState& Own=PC->OwnerState;const bool bAlive=Own.LifePhase==ELDLifePhase::Alive;
        const ALDWorldViewRelay* Relay=bAlive?PC->LocalView():nullptr;const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
        // Dynamic marks only come from the current authorized frame of this match and life; dead, stale or foreign frames draw none.
        const FLDWorldView* Frame=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life?Read.Frame:nullptr;
        if(Frame&&Frame->Footprint.SourceStatus==ELDObservationSource::ActiveBody)
        {
            // Current authorized coverage only: no explored or remembered layer exists. Rows merge into runs.
            const int32 Cells=LDObservation::CoverageSide;const FVector2f Cell(float(Size.X)/Cells,float(Size.Y)/Cells);const TArray<uint8>& Bits=Frame->Footprint.CurrentBits;
            for(int32 Y=0;Y<Cells;++Y)
            {
                for(int32 X=0;X<Cells;)
                {
                    if(!LDObservation::CoverageBit(Bits,Y*Cells+X)){++X;continue;}
                    const int32 Start=X;while(X<Cells&&LDObservation::CoverageBit(Bits,Y*Cells+X)){++X;}
                    Box(2,FVector2f(Start*Cell.X,(Cells-1-Y)*Cell.Y),FVector2f((X-Start)*Cell.X,Cell.Y),FLinearColor(.17f,.33f,.28f));
                }
            }
        }
        if(MinimapStaticReady(Cache,GetWorld(),Size))
        {
            for(const FLDMinimapStatic::FRun& Run:Cache.Fills){Box(3,Run.Position,Run.Extent,Run.Color);}
            for(const FLDMinimapStatic::FOutline& Outline:Cache.Outlines)
            {
                if(Outline.bDashed){FSlateDrawElement::MakeDashedLines(E,Base+4,G.ToPaintGeometry(),TArray<FVector2f>(Outline.Points),ESlateDrawEffect::None,Outline.Color,Outline.Thickness,4.f);}
                else{Lines(4,Outline.Points,Outline.Color,Outline.Thickness);}
            }
            // Shop: stall square with an awning band. Capture: flag on the zone centre. Camps: small diamond or large bordered one.
            for(int32 Index=0;Index<Cache.Sites.Num();++Index)
            {
                const FVector2f Spot=Cache.SitePoints[Index];
                switch(Cache.Sites[Index].Kind)
                {
                case FLDMinimapStatic::ESite::Shop:Box(5,Spot-FVector2f(5.f,5.f),FVector2f(10.f,10.f),Ink);Box(6,Spot-FVector2f(4.f,4.f),FVector2f(8.f,8.f),FLinearColor(.98f,.72f,.22f));Box(7,Spot-FVector2f(4.f,4.f),FVector2f(8.f,2.5f),FLinearColor(.42f,.15f,.06f));break;
                case FLDMinimapStatic::ESite::Capture:Box(5,Spot+FVector2f(-.75f,-9.f),FVector2f(1.5f,9.5f),FLinearColor(.94f,.94f,.90f));Box(6,Spot+FVector2f(.75f,-9.f),FVector2f(6.f,4.f),FLinearColor(.92f,.20f,.16f));break;
                case FLDMinimapStatic::ESite::LowCamp:Diamond(5,Spot,6.5f,Ink);Diamond(6,Spot,4.5f,FLinearColor(.96f,.66f,.30f));break;
                case FLDMinimapStatic::ESite::HighCamp:Diamond(5,Spot,12.f,FLinearColor(1.f,.93f,.76f));Diamond(6,Spot,9.f,FLinearColor(.80f,.22f,.17f));Diamond(7,Spot,3.5f,Ink);break;
                }
            }
        }
        if(Frame)
        {
            // Currently visible units only: contenders are discs, camp members triangles.
            for(const FLDObservedUnitView& Unit:Frame->Units)
            {
                const FVector2D UnitMap=LDHUD::MapPoint(FVector(Unit.FeetCm),Ground);const FVector2f Spot(float(UnitMap.X*Size.X),float(UnitMap.Y*Size.Y));
                if(Unit.Kind==ELDObservedUnitKind::Contender){Disc(8,Spot,9.f,Ink);Disc(9,Spot,6.5f,FLinearColor(1.f,.28f,.36f));}
                else if(Unit.Kind==ELDObservedUnitKind::CampMember)
                {for(const FLDMinimapStatic::FRun& Run:Cache.MemberShadow){Box(8,Spot+Run.Position,Run.Extent,Ink);}for(const FLDMinimapStatic::FRun& Run:Cache.MemberBody){Box(9,Spot+Run.Position,Run.Extent,FLinearColor(.98f,.84f,.30f));}}
            }
        }
        if(const APawn* Body=bAlive?PC->GetPawn():nullptr)
        {
            // Own living body as an arrow along its facing; world +Y is up and the map axes scale independently.
            const FVector2D BodyMap=LDHUD::MapPoint(Body->GetActorLocation(),Ground);const FVector2f Spot(float(BodyMap.X*Size.X),float(BodyMap.Y*Size.Y));
            const FVector Facing=Body->GetActorForwardVector();const FVector2D Span=Ground.GetSize();
            FVector2f Ahead=FVector2f(float(Facing.X*Size.X/Span.X),float(-Facing.Y*Size.Y/Span.Y)).GetSafeNormal();if(Ahead.IsNearlyZero()){Ahead=FVector2f(1.f,0.f);}
            const FVector2f Across(-Ahead.Y,Ahead.X);
            const TArray<FVector2f> Arrow={Spot+Ahead*7.f,Spot-Ahead*5.f+Across*5.f,Spot-Ahead*2.f,Spot-Ahead*5.f-Across*5.f};
            MinimapScanRuns(Arrow,[&Box](float X,float Y,float Width){Box(10,FVector2f(X,Y),FVector2f(Width,1.f),FLinearColor(.30f,1.f,.86f));});
            TArray<FVector2f> Rim=Arrow;Rim.Add(Arrow[0]);Lines(11,Rim,Ink,1.f);
        }
        // Actual camera's ground footprint, without querying any world actor.
        int32 W=0,H=0;PC->GetViewportSize(W,H);TArray<FVector2f> Footprint;
        for(FVector2D Pixel:{FVector2D(0,0),FVector2D(W,0),FVector2D(W,H),FVector2D(0,H)})
        {FVector Origin,Direction;if(PC->DeprojectScreenPositionToWorld(float(Pixel.X),float(Pixel.Y),Origin,Direction)&&Direction.Z<-.001){const FVector2D Corner=LDHUD::MapPoint(Origin+Direction*(-Origin.Z/Direction.Z),Ground);Footprint.Add(FVector2f(float(FMath::Clamp(Corner.X,0.,1.)*Size.X),float(FMath::Clamp(Corner.Y,0.,1.)*Size.Y)));}}
        if(Footprint.Num()==4){const FVector2f First=Footprint[0];Footprint.Add(First);Lines(11,Footprint,FLinearColor(1.f,1.f,1.f,.8f),1.5f);}
    }
    E.PopClip();return Base+11;
}
