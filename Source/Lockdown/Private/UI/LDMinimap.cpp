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
#include "Camera/PlayerCameraManager.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

// Public static map from the registered geometry and approved content only; no unit, coverage or remembered mark.
// World shapes are resolved per world revision; their north-up layout per view scale, which turning never changes.
struct FLDMinimapStatic
{
    // Wall blocks walking and sight, LowWall only walking, SightScreen only sight.
    enum class EArea : uint8 { Wall, LowWall, SightScreen, CaptureZone };
    enum class ESite : uint8 { Shop, Capture, LowCamp, HighCamp };
    struct FArea { EArea Style=EArea::Wall; TArray<FVector2D> Cm; };
    struct FSite { ESite Kind=ESite::Shop; FVector2D Cm=FVector2D::ZeroVector; };
    struct FRun { FVector2f Position=FVector2f::ZeroVector; FVector2f Extent=FVector2f::ZeroVector; FLinearColor Color=FLinearColor::White; };
    struct FOutline { TArray<FVector2f> Points; FLinearColor Color=FLinearColor::White; float Thickness=1.f; bool bDashed=false; };
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FString ContentError;
    bool bContentTried=false,bGroundLogged=false,bResolved=false,bValid=false;
    FLDWorldRevision World;
    // Areas are clipped to the registered ground. Bounds is the box the view fits: every registered blocker, site, capture
    // zone and spawn point as registered, kept to the ground (outer walls may lie wholly beyond it). FitRadius is the
    // turning views' circle about its centre: the farthest site, spawn, capture-zone or inner blocker point plus a margin,
    // never more than the box's half diagonal; 0 when there is none.
    TArray<FArea> Areas;
    TArray<FSite> Sites;
    FBox2D Bounds=FBox2D(ForceInit);
    double FitRadius=0.;
    // North-up plane (+X right, +Y down) at Scale frame units per cm from Origin; a view only rotates it.
    FVector2D Origin=FVector2D::ZeroVector;
    double Scale=-1.;
    TArray<FRun> Fills;
    TArray<FOutline> Outlines;
    // Camp-member triangle around its centre, independent of the widget size.
    TArray<FRun> MemberShadow,MemberBody;
};

LDMinimap::FView LDMinimap::MakeView(ELDMinimapOrientation Mode,const FBox2D& Fit,const FVector2D& FrameSize,double CameraYawDeg,bool bHasBody,double BodyYawDeg,double FitRadius)
{
    FView View;
    const FVector2D Span=Fit.GetSize();
    if(!Fit.bIsValid||Span.ContainsNaN()||FrameSize.ContainsNaN()||!(Span.X>0.)||!(Span.Y>0.)||!(FrameSize.X>0.)||!(FrameSize.Y>0.)){return View;}
    View.Mode=Mode==ELDMinimapOrientation::North?ELDMinimapOrientation::North
        :Mode==ELDMinimapOrientation::Facing&&bHasBody?ELDMinimapOrientation::Facing:ELDMinimapOrientation::Camera;
    if(View.Mode!=ELDMinimapOrientation::North)
    {
        const double Yaw=View.Mode==ELDMinimapOrientation::Facing?BodyYawDeg:CameraYawDeg;
        if(!FMath::IsFinite(Yaw)){return FView();}
        const double Radians=FMath::DegreesToRadians(Yaw);
        View.Forward=FVector2D(FMath::Cos(Radians),FMath::Sin(Radians));View.Right=FVector2D(-View.Forward.Y,View.Forward.X);
    }
    View.Centre=Fit.GetCenter();View.FrameSize=FrameSize;
    const double Margin=FMath::Min(EdgeMargin,.25*FMath::Min(FrameSize.X,FrameSize.Y));const FVector2D Room=FrameSize-FVector2D(2.*Margin);
    // Facing fits the circumscribed circle; the others fit the rotated box's bounds, whose extent along a frame axis is
    // the box span projected onto that axis's ground direction. A content radius replaces both for the turning views.
    const bool bTurning=View.Mode!=ELDMinimapOrientation::North;
    View.Scale=bTurning&&FMath::IsFinite(FitRadius)&&FitRadius>0.?FMath::Min(Room.X,Room.Y)/(2.*FitRadius)
        :View.Mode==ELDMinimapOrientation::Facing?FMath::Min(Room.X,Room.Y)/Span.Size()
        :FMath::Min(Room.X/(Span.X*FMath::Abs(View.Right.X)+Span.Y*FMath::Abs(View.Right.Y)),Room.Y/(Span.X*FMath::Abs(View.Forward.X)+Span.Y*FMath::Abs(View.Forward.Y)));
    if(!FMath::IsFinite(View.Scale)||View.Scale<=0.){return FView();}
    return View;
}
FVector2D LDMinimap::ToFrame(const FView& View,const FVector& World)
{
    const FVector2D Offset(World.X-View.Centre.X,World.Y-View.Centre.Y);
    return View.FrameSize*.5+FVector2D(FVector2D::DotProduct(Offset,View.Right),-FVector2D::DotProduct(Offset,View.Forward))*View.Scale;
}
FVector LDMinimap::ToWorld(const FView& View,const FVector2D& Local)
{
    if(!View.IsValid()){return FVector(View.Centre.X,View.Centre.Y,0.);}
    const FVector2D Along=(Local-View.FrameSize*.5)/View.Scale;const FVector2D Offset=View.Right*Along.X-View.Forward*Along.Y;
    return FVector(View.Centre.X+Offset.X,View.Centre.Y+Offset.Y,0.);
}
FVector LDMinimap::ClampToGround(const FVector& World,const FBox2D& Ground)
{
    const FVector2D Span=Ground.GetSize();
    return FVector(FMath::Clamp(World.X,Ground.Min.X,Ground.Min.X+.999999*Span.X),FMath::Clamp(World.Y,Ground.Min.Y,Ground.Min.Y+.999999*Span.Y),0.);
}

namespace
{
// Room around the farthest fitted point in the turning views, so a body walking just past a spawn stays on the map.
constexpr double MinimapFitMarginCm=600.;
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
// Sutherland-Hodgman against the ground, so geometry beyond it never paints outside the map.
TArray<FVector2D> MinimapClipBox(TArray<FVector2D> Polygon,const FBox2D& Box)
{
    for(int32 Edge=0;Edge<4&&Polygon.Num()>=3;++Edge)
    {
        const int32 Component=Edge/2;const double Toward=Edge%2?-1.:1.,Limit=Edge%2?Box.Max[Component]:Box.Min[Component];
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
// One registered world revision, in world cm. Any unresolved item fails the whole layer, because a partial map could pass
// for the approved one. Spawn anchors are never drawn; they only keep the playable area inside the fit.
bool MinimapResolveStatic(FLDMinimapStatic& Cache,const FLDDemoWorldGeometry& Geometry,const FLDDemoContentDefinitions& Approved,const FBox2D& Ground,FString& Error)
{
    Cache.Areas.Reset();Cache.Sites.Reset();Cache.Bounds=FBox2D(ForceInit);Cache.FitRadius=0.;
    if(!Approved.World.MapId.Equals(Geometry.MapId,ESearchCase::CaseSensitive))
    {Error=FString::Printf(TEXT("Approved content names map %s but the registered world is %s."),*Approved.World.MapId,*Geometry.MapId);return false;}
    auto ToCm=[&Error](const FString& Id,const FLDDemoWorldPoint& Point,FVector2D& Result)
    {FVector Cm;if(!Point.ToVector(Cm,Error)){Error=Id+TEXT(": ")+Error;return false;}Result=FVector2D(Cm.X,Cm.Y);return true;};
    FBox2D Content(ForceInit);
    // Points the turning views keep inside their circle: sites, spawns, the capture zone and blocker corners strictly
    // inside the ground (the outer fence lies on or beyond its edge and only bounds the empty field corners).
    TArray<FVector2D> FitPoints;
    auto Inside=[&Ground](const FVector2D& Point){return Point.X>Ground.Min.X&&Point.X<Ground.Max.X&&Point.Y>Ground.Min.Y&&Point.Y<Ground.Max.Y;};
    // An area is fitted as registered but keeps only its part on the ground; one left without area is not drawn.
    auto Keep=[&Content,&Ground,&FitPoints,&Inside](FLDMinimapStatic::FArea& Area)
    {
        for(const FVector2D& Point:Area.Cm){Content+=Point;if(Inside(Point)){FitPoints.Add(Point);}}
        Area.Cm=MinimapClipBox(MoveTemp(Area.Cm),Ground);
        return MinimapArea(Area.Cm)>1.;
    };
    for(const FLDDemoWorldBlocker& Blocker:Geometry.Blockers)
    {
        if(!Blocker.BlocksMovement&&!Blocker.BlocksSight){continue;}
        FLDMinimapStatic::FArea Area;Area.Style=!Blocker.BlocksSight?FLDMinimapStatic::EArea::LowWall:!Blocker.BlocksMovement?FLDMinimapStatic::EArea::SightScreen:FLDMinimapStatic::EArea::Wall;
        for(const FLDDemoWorldPoint& Corner:Blocker.PolygonCm){FVector2D Point;if(!ToCm(Blocker.Id,Corner,Point)){return false;}Area.Cm.Add(Point);}
        if(Area.Cm.Num()<3){Error=Blocker.Id+TEXT(": a blocker polygon needs at least three corners.");return false;}
        if(Keep(Area)){Cache.Areas.Add(MoveTemp(Area));}
    }
    const FLDDemoRational& Radius=Approved.Parameters.CaptureRadiusCm;
    const double RadiusCm=Radius.Denominator>0?double(Radius.Numerator)/double(Radius.Denominator):0.;
    for(const FLDDemoWorldAnchor& Anchor:Geometry.Anchors)
    {
        FLDMinimapStatic::FSite Site;if(!ToCm(Anchor.Id,Anchor.PositionCm,Site.Cm)){return false;}
        Content+=Site.Cm;FitPoints.Add(Site.Cm);
        if(Anchor.Kind==ELDDemoWorldAnchorKind::Spawn){continue;}
        if(Anchor.Kind==ELDDemoWorldAnchorKind::Shop){Site.Kind=FLDMinimapStatic::ESite::Shop;}
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::Capture)
        {
            if(!FMath::IsFinite(RadiusCm)||RadiusCm<=0){Error=TEXT("CAPTURE.RadiusCm is not a positive approved length.");return false;}
            Site.Kind=FLDMinimapStatic::ESite::Capture;FLDMinimapStatic::FArea Zone;Zone.Style=FLDMinimapStatic::EArea::CaptureZone;
            for(int32 Step=0;Step<48;++Step){const double Angle=UE_DOUBLE_TWO_PI*Step/48.;Zone.Cm.Add(Site.Cm+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*RadiusCm);}
            if(Keep(Zone)){Cache.Areas.Insert(MoveTemp(Zone),0);}
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
    // The fit never reaches beyond the ground, where nothing is drawn; content spanning no area there fits the whole ground.
    if(Content.bIsValid)
    {Cache.Bounds=FBox2D(FVector2D(FMath::Max(Content.Min.X,Ground.Min.X),FMath::Max(Content.Min.Y,Ground.Min.Y)),FVector2D(FMath::Min(Content.Max.X,Ground.Max.X),FMath::Min(Content.Max.Y,Ground.Max.Y)));}
    const FVector2D Span=Cache.Bounds.GetSize();
    if(!Cache.Bounds.bIsValid||!(Span.X>0.)||!(Span.Y>0.)){Cache.Bounds=Ground;}
    const FVector2D Centre=Cache.Bounds.GetCenter();double Farthest=-1.;
    for(const FVector2D& Point:FitPoints){Farthest=FMath::Max(Farthest,FVector2D::Distance(Point,Centre));}
    if(Farthest>=0.){Cache.FitRadius=FMath::Min(Farthest+MinimapFitMarginCm,.5*Cache.Bounds.GetSize().Size());}
    return true;
}
void MinimapLayoutStatic(FLDMinimapStatic& Cache,const FVector2D& Origin,double Scale)
{
    Cache.Origin=Origin;Cache.Scale=Scale;Cache.Fills.Reset();Cache.Outlines.Reset();
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
        TArray<FVector2f> Points;for(const FVector2D& Cm:Area.Cm){Points.Add(FVector2f(float((Cm.X-Origin.X)*Scale),float((Cm.Y-Origin.Y)*Scale)));}
        MinimapScanRuns(Points,[&Cache,&Fill](float X,float Y,float Width)
        {
            // Rows with the same span merge, so an axis-aligned wall stays one element.
            FLDMinimapStatic::FRun* Last=Cache.Fills.Num()?&Cache.Fills.Last():nullptr;
            if(Last&&Last->Color==Fill&&FMath::IsNearlyEqual(Last->Position.X,X,.01f)&&FMath::IsNearlyEqual(Last->Extent.X,Width,.01f)&&FMath::IsNearlyEqual(Last->Position.Y+Last->Extent.Y,Y,.01f)){Last->Extent.Y+=1.f;}
            else{Cache.Fills.Add({FVector2f(X,Y),FVector2f(Width,1.f),Fill});}
        });
        const FVector2f First=Points[0];Points.Add(First);Cache.Outlines.Add({MoveTemp(Points),Rim,Thickness,bDashed});
    }
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
    Cache.bResolved=true;Cache.World=Registry->Revision();Cache.Scale=-1.;FString Error;
    if(!Cache.bContentTried){Cache.bContentTried=true;if(!LDDemoEntry::LoadRuntimeContent(Cache.Content,Error)||!Cache.Content){Cache.Content.Reset();Cache.ContentError=Error;}}
    Cache.bValid=Cache.Content&&MinimapResolveStatic(Cache,*Geometry,Cache.Content->Get(),Ground,Error);
    if(!Cache.bValid)
    {
        Cache.Areas.Reset();Cache.Sites.Reset();Cache.Bounds=FBox2D(ForceInit);
        const FString Reason=Cache.Content?Error:TEXT("Approved Demo content unavailable: ")+Cache.ContentError;
        UE_LOG(LogTemp,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE %s %s"),*Cache.World.MapId.ToString(),*Reason);
    }
}
// A layer resolved for another world revision is never drawn or fitted.
bool MinimapStaticCurrent(const FLDMinimapStatic& Cache,const UWorld* World)
{const auto* Registry=World->GetSubsystem<ULDMapRegistry>();return Cache.bValid&&Registry&&Cache.World==Registry->Revision();}
// Paint lays the current layer out again only when the plane's origin or scale changed, never for a new angle.
bool MinimapStaticReady(FLDMinimapStatic& Cache,const UWorld* World,const FVector2D& Origin,double Scale)
{
    if(!MinimapStaticCurrent(Cache,World)){return false;}
    if(!Cache.Origin.Equals(Origin,1e-6)||!FMath::IsNearlyEqual(Cache.Scale,Scale,Scale*1e-4)){MinimapLayoutStatic(Cache,Origin,Scale);}
    return true;
}
// The owner's live view: the applied orientation over the drawn content, or over the ground until that resolves.
// Painting and gestures both use it, so a pointer lands where the map shows.
LDMinimap::FView MinimapLiveView(const ALDPlayerController& PC,const FLDMinimapStatic& Cache,const UWorld* World,const FBox2D& Ground,const FVector2D& Size)
{
    const ELDMinimapOrientation Mode=PC.Controls?PC.Controls->GetMinimapOrientation():GetDefault<ULDControlSettings>()->GetMinimapOrientation();
    // Without a camera manager the camera yaw is unknown; the Camera view then keeps the north yaw instead of a guess.
    const double CameraYaw=PC.PlayerCameraManager?double(PC.PlayerCameraManager->GetCameraRotation().Yaw):LDMinimap::NorthYawDeg;
    const APawn* Body=PC.OwnerState.LifePhase==ELDLifePhase::Alive?PC.GetPawn():nullptr;
    return LDMinimap::MakeView(Mode,MinimapStaticCurrent(Cache,World)?Cache.Bounds:Ground,Size,CameraYaw,Body!=nullptr,Body?double(Body->GetActorRotation().Yaw):0.,
        MinimapStaticCurrent(Cache,World)?Cache.FitRadius:0.);
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
    if(Move&&!PC->CanWorldCommand()){if(PC->IsAwaitingRespawn()){PC->PostIntentNotice(TEXT("社死中只能移动镜头，不能下达移动"));}return false;}
    FBox2D Ground(ForceInit);if(!MinimapGroundBounds(GetWorld(),Ground)){PC->PostIntentNotice(TEXT("小地图不可用：未加载 Demo 世界"));return false;}
    const LDMinimap::FView View=MinimapLiveView(*PC,MinimapCache(StaticMap),GetWorld(),Ground,FrameSize);
    if(!View.IsValid()){PC->PostIntentNotice(TEXT("小地图不可用：尚未完成布局"));return false;}
    // A point off the ground (a turned corner, the fitted margin) is clamped onto it like any edge point; a move is then
    // still validated against the public ground below.
    const FVector XY=LDMinimap::ClampToGround(LDMinimap::ToWorld(View,P*View.FrameSize),Ground);
    if(!Move){PC->Navigation->PanTo(XY);return true;}
    FVector Surface;if(!LDGround::MapSurface(GetWorld(),XY,Surface)){PC->PostIntentNotice(TEXT("无法到达该位置"));return false;}
    PC->Navigation->Navigate(Surface);return true;
}
FReply ULDMinimap::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    FrameSize=G.GetLocalSize();
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        // The press keeps the pointer until release even when the pan is refused, so the release never reaches the world.
        const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());DragMatch=PC->OwnerState.Match;DragLife=PC->OwnerState.Life;
        bDragging=Gesture(Fraction(G,E),false);return FReply::Handled().CaptureMouse(TakeWidget());
    }
    if(E.GetEffectingButton()==EKeys::RightMouseButton){Gesture(Fraction(G,E),true);return FReply::Handled();}return FReply::Handled();
}
FReply ULDMinimap::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{FrameSize=G.GetLocalSize();if(bDragging&&!Gesture(Fraction(G,E),false)){bDragging=false;}return FReply::Handled();}
FReply ULDMinimap::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{if(E.GetEffectingButton()==EKeys::LeftMouseButton){bDragging=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Handled();}
void ULDMinimap::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){bDragging=false;Super::NativeOnMouseCaptureLost(E);}
void ULDMinimap::NativeTick(const FGeometry& G,float D)
{
    Super::NativeTick(G,D);FrameSize=G.GetLocalSize();
    const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());FLDMinimapStatic& Cache=MinimapCache(StaticMap);FBox2D Ground(ForceInit);
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
    FBox2D Ground(ForceInit);FLDMinimapStatic& Cache=MinimapCache(StaticMap);
    const LDMinimap::FView View=MinimapGroundBounds(GetWorld(),Ground)?MinimapLiveView(*PC,Cache,GetWorld(),Ground,Size):LDMinimap::FView();
    if(View.IsValid())
    {
        // Every mark goes through the same view as the gestures; point glyphs stay upright at their mapped spot.
        auto Spot=[&View](const FVector& World){const FVector2D Local=LDMinimap::ToFrame(View,World);return FVector2f(float(Local.X),float(Local.Y));};
        // The north-up plane drawn through the view: a pure rotation about the view centre (none in 固定正北), so the area
        // layers keep one layout however the map turns.
        const FVector2D Span=Ground.GetSize();
        const FVector2f PlaneSize(float(Span.X*View.Scale),float(Span.Y*View.Scale));
        const FVector2f Pivot(float((View.Centre.X-Ground.Min.X)*View.Scale),float((View.Centre.Y-Ground.Min.Y)*View.Scale));
        const FGeometry Plane=G.MakeChild(PlaneSize,FSlateLayoutTransform(FVector2f(float(Size.X*.5),float(Size.Y*.5))-Pivot),
            FSlateRenderTransform(FMatrix2x2(float(View.Right.X),float(-View.Forward.X),float(View.Right.Y),float(-View.Forward.Y))),Pivot/PlaneSize);
        auto PlaneBox=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint){FSlateDrawElement::MakeBox(E,Base+Layer,Plane.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
        PlaneBox(1,FVector2f::ZeroVector,PlaneSize,FLinearColor(.035f,.05f,.045f));
        const FLDOwnerState& Own=PC->OwnerState;const bool bAlive=Own.LifePhase==ELDLifePhase::Alive;
        const ALDWorldViewRelay* Relay=bAlive?PC->LocalView():nullptr;const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
        // Dynamic marks only come from the current authorized frame of this match and life; dead, stale or foreign frames draw none.
        const FLDWorldView* Frame=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life?Read.Frame:nullptr;
        if(Frame&&Frame->Footprint.SourceStatus==ELDObservationSource::ActiveBody)
        {
            // Current authorized coverage only: no explored or remembered layer exists. Cell (X,Y) covers the ground from
            // Min + (X,Y) cells, its true place in the plane; row runs merge.
            const int32 Cells=LDObservation::CoverageSide;const FVector2f Cell(PlaneSize.X/Cells,PlaneSize.Y/Cells);const TArray<uint8>& Bits=Frame->Footprint.CurrentBits;
            for(int32 Y=0;Y<Cells;++Y)
            {
                for(int32 X=0;X<Cells;)
                {
                    if(!LDObservation::CoverageBit(Bits,Y*Cells+X)){++X;continue;}
                    const int32 Start=X;while(X<Cells&&LDObservation::CoverageBit(Bits,Y*Cells+X)){++X;}
                    PlaneBox(2,FVector2f(Start*Cell.X,Y*Cell.Y),FVector2f((X-Start)*Cell.X,Cell.Y),FLinearColor(.17f,.33f,.28f));
                }
            }
        }
        if(MinimapStaticReady(Cache,GetWorld(),Ground.Min,View.Scale))
        {
            for(const FLDMinimapStatic::FRun& Run:Cache.Fills){PlaneBox(3,Run.Position,Run.Extent,Run.Color);}
            for(const FLDMinimapStatic::FOutline& Outline:Cache.Outlines)
            {
                if(Outline.bDashed){FSlateDrawElement::MakeDashedLines(E,Base+4,Plane.ToPaintGeometry(),TArray<FVector2f>(Outline.Points),ESlateDrawEffect::None,Outline.Color,Outline.Thickness,4.f);}
                else{FSlateDrawElement::MakeLines(E,Base+4,Plane.ToPaintGeometry(),Outline.Points,ESlateDrawEffect::None,Outline.Color,true,Outline.Thickness);}
            }
            // Shop: stall square with an awning band. Capture: flag on the zone centre. Camps: small diamond or large bordered one.
            for(const FLDMinimapStatic::FSite& Site:Cache.Sites)
            {
                const FVector2f At=Spot(FVector(Site.Cm.X,Site.Cm.Y,0.));
                switch(Site.Kind)
                {
                case FLDMinimapStatic::ESite::Shop:Box(5,At-FVector2f(5.f,5.f),FVector2f(10.f,10.f),Ink);Box(6,At-FVector2f(4.f,4.f),FVector2f(8.f,8.f),FLinearColor(.98f,.72f,.22f));Box(7,At-FVector2f(4.f,4.f),FVector2f(8.f,2.5f),FLinearColor(.42f,.15f,.06f));break;
                case FLDMinimapStatic::ESite::Capture:Box(5,At+FVector2f(-.75f,-9.f),FVector2f(1.5f,9.5f),FLinearColor(.94f,.94f,.90f));Box(6,At+FVector2f(.75f,-9.f),FVector2f(6.f,4.f),FLinearColor(.92f,.20f,.16f));break;
                case FLDMinimapStatic::ESite::LowCamp:Diamond(5,At,6.5f,Ink);Diamond(6,At,4.5f,FLinearColor(.96f,.66f,.30f));break;
                case FLDMinimapStatic::ESite::HighCamp:Diamond(5,At,12.f,FLinearColor(1.f,.93f,.76f));Diamond(6,At,9.f,FLinearColor(.80f,.22f,.17f));Diamond(7,At,3.5f,Ink);break;
                }
            }
        }
        if(Frame)
        {
            // Currently visible units only, where their world proxy is presented (the frame sample until it is placed), so
            // they glide with the world instead of jumping per frame: contenders in their identity tint, camp members amber.
            for(const FLDObservedUnitView& Unit:Frame->Units)
            {
                FVector Presented=FVector::ZeroVector;const FVector2f At=Spot(Relay->ReadPresentedLocation(Unit.Handle,Unit.ObservationEpoch,Presented)?Presented:FVector(Unit.FeetCm));
                if(Unit.Kind==ELDObservedUnitKind::Contender){Disc(8,At,9.f,Ink);Disc(9,At,6.5f,LDHUD::PaletteColor(Unit.Presentation));}
                else if(Unit.Kind==ELDObservedUnitKind::CampMember)
                {for(const FLDMinimapStatic::FRun& Run:Cache.MemberShadow){Box(8,At+Run.Position,Run.Extent,Ink);}for(const FLDMinimapStatic::FRun& Run:Cache.MemberBody){Box(9,At+Run.Position,Run.Extent,FLinearColor(.98f,.84f,.30f));}}
            }
        }
        if(const APawn* Body=bAlive?PC->GetPawn():nullptr)
        {
            // Own living body as an arrow along its facing (straight up in 跟随角色朝向).
            const FVector2f At=Spot(Body->GetActorLocation());const FVector Facing=Body->GetActorForwardVector();
            FVector2f Ahead=FVector2f(float(Facing.X*View.Right.X+Facing.Y*View.Right.Y),float(-(Facing.X*View.Forward.X+Facing.Y*View.Forward.Y))).GetSafeNormal();
            if(Ahead.IsNearlyZero()){Ahead=FVector2f(0.f,-1.f);}
            const FVector2f Across(-Ahead.Y,Ahead.X);
            const TArray<FVector2f> Arrow={At+Ahead*7.f,At-Ahead*5.f+Across*5.f,At-Ahead*2.f,At-Ahead*5.f-Across*5.f};
            MinimapScanRuns(Arrow,[&Box](float X,float Y,float Width){Box(10,FVector2f(X,Y),FVector2f(Width,1.f),FLinearColor(.30f,1.f,.86f));});
            TArray<FVector2f> Rim=Arrow;Rim.Add(Arrow[0]);Lines(11,Rim,Ink,1.f);
        }
        // Actual camera's ground footprint, without querying any world actor; a corner beyond the ground is clamped onto it.
        int32 W=0,H=0;PC->GetViewportSize(W,H);TArray<FVector2f> Footprint;
        for(FVector2D Pixel:{FVector2D(0,0),FVector2D(W,0),FVector2D(W,H),FVector2D(0,H)})
        {
            FVector Origin,Direction;
            if(PC->DeprojectScreenPositionToWorld(float(Pixel.X),float(Pixel.Y),Origin,Direction)&&Direction.Z<-.001)
            {const FVector Hit=Origin+Direction*(-Origin.Z/Direction.Z);Footprint.Add(Spot(FVector(FMath::Clamp(Hit.X,Ground.Min.X,Ground.Max.X),FMath::Clamp(Hit.Y,Ground.Min.Y,Ground.Max.Y),0.)));}
        }
        if(Footprint.Num()==4){const FVector2f First=Footprint[0];Footprint.Add(First);Lines(11,Footprint,FLinearColor(1.f,1.f,1.f,.8f),1.5f);}
        FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
        if(View.Mode!=ELDMinimapOrientation::North)
        {
            // A turned square ground would leave beyond-ground corners in the frame: the turning views show a round window
            // no larger than the circle the ground fills around the view centre, and cover the rest in the frame's colour.
            const FVector2D Middle=Size*.5;
            const double Inscribed=FMath::Min(FMath::Min(View.Centre.X-Ground.Min.X,Ground.Max.X-View.Centre.X),FMath::Min(View.Centre.Y-Ground.Min.Y,Ground.Max.Y-View.Centre.Y))*View.Scale;
            const float Radius=float(FMath::Max(4.,FMath::Min(FMath::Min(Size.X,Size.Y)*.5-1.,Inscribed)));
            const FLinearColor Outside(.02f,.03f,.05f);
            for(int32 Row=0;Row<FMath::CeilToInt(Size.Y);++Row)
            {
                const float Dy=float(Row)+.5f-float(Middle.Y);
                if(FMath::Abs(Dy)>=Radius){Box(12,FVector2f(0.f,float(Row)),FVector2f(float(Size.X),1.f),Outside);continue;}
                const float Dx=FMath::Sqrt(Radius*Radius-Dy*Dy);
                Box(12,FVector2f(0.f,float(Row)),FVector2f(FMath::Max(0.f,float(Middle.X)-Dx),1.f),Outside);
                Box(12,FVector2f(float(Middle.X)+Dx,float(Row)),FVector2f(FMath::Max(0.f,float(Size.X-Middle.X)-Dx),1.f),Outside);
            }
            TArray<FVector2f> Rim;
            for(int32 Step=0;Step<=64;++Step){const float Angle=2.f*PI*Step/64.f;Rim.Add(FVector2f(float(Middle.X)+FMath::Cos(Angle)*Radius,float(Middle.Y)+FMath::Sin(Angle)*Radius));}
            Lines(13,Rim,FLinearColor(.42f,.55f,.62f,.8f),1.5f);
            if(Renderer)
            {
                // North (world -Y, up in 固定正北) is marked just inside the window's rim, outlined over any layer.
                FFontOutlineSettings Outline;Outline.OutlineSize=2;Outline.OutlineColor=FLinearColor(0.f,0.f,0.f,.85f);
                const float TextScale=PC->Controls?PC->Controls->TextScale:1.f;
                const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),float(FMath::Max(1,FMath::RoundToInt(12*TextScale))),Outline);
                const FString North=TEXT("北");const FVector2f Extent=Renderer->GetFontMeasureService()->Measure(North,Font);
                const FVector2D Toward(-View.Right.Y,View.Forward.Y);
                const FVector2D Mark=Middle+Toward.GetSafeNormal()*FMath::Max(0.,double(Radius)-.5*FMath::Max(Extent.X,Extent.Y)-3.);
                FSlateDrawElement::MakeText(E,Base+14,G.ToPaintGeometry(Extent,FSlateLayoutTransform(FVector2f(float(Mark.X-Extent.X*.5),float(Mark.Y-Extent.Y*.5)))),North,Font,ESlateDrawEffect::None,FLinearColor(1.f,.86f,.42f));
            }
        }
    }
    E.PopClip();return Base+14;
}
