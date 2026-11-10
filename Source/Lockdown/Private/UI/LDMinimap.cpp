#include "UI/LDMinimap.h"
#include "LDMinimapRaster.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDemoContentCatalog.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Core/LDGroundQueries.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SLeafWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// Registered public geometry and its raster, plus the owner's current coverage upload; no units or remembered marks.
// World shapes resolve per revision; the north-up raster rebuilds only for world/layout changes, not a turn at fixed scale.
struct FLDMinimapStatic
{
    // Wall blocks walking and sight, LowWall only walking, SightScreen only sight. #97: Level1/Level2 plateaus and the
    // ramps between levels lie under every other area.
    enum class EArea : uint8 { Wall, LowWall, SightScreen, CaptureZone, DormZone, Level1, Level2, Ramp };
    enum class ESite : uint8 { Capture, LowCamp, HighCamp, Dorm, BossWaypoint, EliteCamp };
    // D27: the approved leader circle radius, read with the static map.
    double LeaderRadiusCm=0;
    struct FArea { EArea Style=EArea::Wall; TArray<FVector2D> Cm; };
    struct FSite { ESite Kind=ESite::Capture; FVector2D Cm=FVector2D::ZeroVector; };
    struct FRun { FVector2f Position=FVector2f::ZeroVector; FVector2f Extent=FVector2f::ZeroVector; FLinearColor Color=FLinearColor::White; };
    struct FOutline { TArray<FVector2f> Points; FLinearColor Color=FLinearColor::White; float Thickness=1.f; bool bDashed=false; };
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FString ContentError;
    bool bContentTried=false,bGroundLogged=false,bResolved=false,bValid=false;
    FLDWorldRevision World;
    // Areas are clipped to the registered ground. Bounds is the box the view fits: every registered blocker, site, capture
    // zone and spawn point as registered, kept to the ground (outer walls may lie wholly beyond it).
    TArray<FArea> Areas;
    TArray<FSite> Sites;
    FBox2D Bounds=FBox2D(ForceInit);
    // Transparent terrain raster above the separate opaque ground/coverage plane.
    FBox2D ResolvedGround=FBox2D(ForceInit);
    LDMinimapRaster::FStaticKey RasterKey;
    TStrongObjectPtr<UTextureRenderTarget2D> Raster;
    FSlateBrush RasterBrush;
    TStrongObjectPtr<UTexture2D> Coverage;
    FSlateBrush CoverageBrush;
    LDMinimapRaster::FCoverageKey CoverageKey;
    // Camp-member triangle around its centre, independent of the widget size.
    TArray<FRun> MemberShadow,MemberBody;
};

LDMinimap::FView LDMinimap::MakeView(ELDMinimapOrientation Mode,const FBox2D& Fit,const FVector2D& FrameSize,double CameraYawDeg,bool bHasBody,double BodyYawDeg)
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
    // the box span projected onto that axis's ground direction.
    View.Scale=View.Mode==ELDMinimapOrientation::Facing?FMath::Min(Room.X,Room.Y)/Span.Size()
        :FMath::Min(Room.X/(Span.X*FMath::Abs(View.Right.X)+Span.Y*FMath::Abs(View.Right.Y)),Room.Y/(Span.X*FMath::Abs(View.Forward.X)+Span.Y*FMath::Abs(View.Forward.Y)));
    if(!FMath::IsFinite(View.Scale)||View.Scale<=0.){return FView();}
    return View;
}
bool LDMinimap::OnGround(const FView& View,const FVector2D& Local,const FBox2D& Ground)
{
    if(!View.IsValid()||!Ground.bIsValid){return false;}
    const FVector World=ToWorld(View,Local);
    return World.X>=Ground.Min.X&&World.X<=Ground.Max.X&&World.Y>=Ground.Min.Y&&World.Y<=Ground.Max.Y;
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
    Cache.Areas.Reset();Cache.Sites.Reset();Cache.Bounds=FBox2D(ForceInit);
    if(!Approved.World.MapId.Equals(Geometry.MapId,ESearchCase::CaseSensitive))
    {Error=FString::Printf(TEXT("Approved content names map %s but the registered world is %s."),*Approved.World.MapId,*Geometry.MapId);return false;}
    auto ToCm=[&Error](const FString& Id,const FLDDemoWorldPoint& Point,FVector2D& Result)
    {FVector Cm;if(!Point.ToVector(Cm,Error)){Error=Id+TEXT(": ")+Error;return false;}Result=FVector2D(Cm.X,Cm.Y);return true;};
    FBox2D Content(ForceInit);
    // An area is fitted as registered but keeps only its part on the ground; one left without area is not drawn.
    auto Keep=[&Content,&Ground](FLDMinimapStatic::FArea& Area)
    {
        for(const FVector2D& Point:Area.Cm){Content+=Point;}
        Area.Cm=MinimapClipBox(MoveTemp(Area.Cm),Ground);
        return MinimapArea(Area.Cm)>1.;
    };
    auto Terrain=[&](const FString& Id,const TArray<FLDDemoWorldPoint>& Polygon,FLDMinimapStatic::EArea Style)
    {
        FLDMinimapStatic::FArea Area;Area.Style=Style;
        for(const FLDDemoWorldPoint& Point:Polygon){FVector2D Cm;if(!ToCm(Id,Point,Cm)){return false;}Area.Cm.Add(Cm);}
        if(Keep(Area)){Cache.Areas.Add(MoveTemp(Area));}
        return true;
    };
    for(int32 Level=1;Level<=LDDemoWorld::MaxLevel;++Level){for(const FLDDemoWorldPlateau& Plateau:Geometry.Plateaus)
    {if(Plateau.Level==Level&&!Terrain(Plateau.Id,Plateau.PolygonCm,Level==1?FLDMinimapStatic::EArea::Level1:FLDMinimapStatic::EArea::Level2)){return false;}}}
    for(const FLDDemoWorldRamp& Ramp:Geometry.Ramps){if(!Terrain(Ramp.Id,Ramp.PolygonCm,FLDMinimapStatic::EArea::Ramp)){return false;}}
    for(const FLDDemoWorldBlocker& Blocker:Geometry.Blockers)
    {
        if(!Blocker.BlocksMovement&&!Blocker.BlocksSight){continue;}
        FLDMinimapStatic::FArea Area;Area.Style=!Blocker.BlocksSight?FLDMinimapStatic::EArea::LowWall:!Blocker.BlocksMovement?FLDMinimapStatic::EArea::SightScreen:FLDMinimapStatic::EArea::Wall;
        // A tree is drawn as its circumscribing polygon, the same footprint every other consumer uses.
        if(!LDDemoWorld::Footprint(Blocker,Area.Cm,Error)){Error=Blocker.Id+TEXT(": ")+Error;return false;}
        if(Keep(Area)){Cache.Areas.Add(MoveTemp(Area));}
    }
    const FLDDemoRational& Radius=Approved.Parameters.CaptureRadiusCm;
    const double RadiusCm=Radius.Denominator>0?double(Radius.Numerator)/double(Radius.Denominator):0.;
    const FLDDemoRational& DormRadius=Approved.Parameters.DormRadiusCm;
    const FLDDemoRational& LeaderRadius=Approved.Parameters.LeaderCircleRadiusCm;
    Cache.LeaderRadiusCm=LeaderRadius.Denominator>0?double(LeaderRadius.Numerator)/double(LeaderRadius.Denominator):0.;
    const double DormRadiusCm=DormRadius.Denominator>0?double(DormRadius.Numerator)/double(DormRadius.Denominator):0.;
    for(const FLDDemoWorldAnchor& Anchor:Geometry.Anchors)
    {
        FLDMinimapStatic::FSite Site;if(!ToCm(Anchor.Id,Anchor.PositionCm,Site.Cm)){return false;}
        Content+=Site.Cm;
        if(Anchor.Kind==ELDDemoWorldAnchorKind::Spawn){continue;}
        if(Anchor.Kind==ELDDemoWorldAnchorKind::Capture)
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
            Site.Kind=Monster->Tier==ELDDemoCampTier::Elite?FLDMinimapStatic::ESite::EliteCamp
                :Monster->Tier==ELDDemoCampTier::High?FLDMinimapStatic::ESite::HighCamp:FLDMinimapStatic::ESite::LowCamp;
        }
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::Dorm)
        {
            // D21 (#79): the public dorm disk, drawn like the capture zone in its own colour.
            if(!FMath::IsFinite(DormRadiusCm)||DormRadiusCm<=0){Error=TEXT("DORM.RadiusCm is not a positive approved length.");return false;}
            Site.Kind=FLDMinimapStatic::ESite::Dorm;FLDMinimapStatic::FArea Zone;Zone.Style=FLDMinimapStatic::EArea::DormZone;
            for(int32 Step=0;Step<32;++Step){const double Angle=UE_DOUBLE_TWO_PI*Step/32.;Zone.Cm.Add(Site.Cm+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*DormRadiusCm);}
            if(Keep(Zone)){Cache.Areas.Insert(MoveTemp(Zone),0);}
        }
        // D26 (#79): an airdrop point is drawn only while it holds the current public drop (see NativePaint).
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::Airdrop){continue;}
        // D26 (#79): a public Boss patrol point, a small gold ring.
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::BossWaypoint){Site.Kind=FLDMinimapStatic::ESite::BossWaypoint;}
        else{Error=Anchor.Id+TEXT(": unsupported anchor kind.");return false;}
        Cache.Sites.Add(Site);
    }
    // The fit never reaches beyond the ground, where nothing is drawn; content spanning no area there fits the whole ground.
    if(Content.bIsValid)
    {Cache.Bounds=FBox2D(FVector2D(FMath::Max(Content.Min.X,Ground.Min.X),FMath::Max(Content.Min.Y,Ground.Min.Y)),FVector2D(FMath::Min(Content.Max.X,Ground.Max.X),FMath::Min(Content.Max.Y,Ground.Max.Y)));}
    const FVector2D Span=Cache.Bounds.GetSize();
    if(!Cache.Bounds.bIsValid||!(Span.X>0.)||!(Span.Y>0.)){Cache.Bounds=Ground;}
    return true;
}
class SLDMinimapRaster : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLDMinimapRaster){} SLATE_END_ARGS()
    void Construct(const FArguments&){}
    TArray<FLDMinimapStatic::FRun> Fills;
    TArray<FLDMinimapStatic::FOutline> Outlines;
    FVector2D Size=FVector2D::ZeroVector;
    virtual FVector2D ComputeDesiredSize(float) const override {return Size;}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool) const override
    {
        const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        for(const auto& Run:Fills){FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(Run.Extent,FSlateLayoutTransform(Run.Position)),White,ESlateDrawEffect::None,Run.Color);}
        for(const auto& Outline:Outlines)
        {
            if(Outline.bDashed){FSlateDrawElement::MakeDashedLines(E,L+1,G.ToPaintGeometry(),TArray<FVector2f>(Outline.Points),ESlateDrawEffect::None,Outline.Color,Outline.Thickness,4.f);}
            else{FSlateDrawElement::MakeLines(E,L+1,G.ToPaintGeometry(),Outline.Points,ESlateDrawEffect::None,Outline.Color,true,Outline.Thickness);}
        }
        return L+1;
    }
};
void MinimapLayoutStatic(const FLDMinimapStatic& Cache,const FVector2D& Origin,double Scale,SLDMinimapRaster& Raster)
{
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
        case FLDMinimapStatic::EArea::DormZone:Fill=FLinearColor(.25f,.62f,1.f,.18f);Rim=FLinearColor(.45f,.75f,1.f,.95f);Thickness=1.5f;break;
        // High ground lightens by level over the vision footprint; a cliff rim is solid, a ramp's dashed.
        case FLDMinimapStatic::EArea::Level1:Fill=FLinearColor(.62f,.74f,.52f,.16f);Rim=FLinearColor(.36f,.30f,.22f);Thickness=2.f;break;
        case FLDMinimapStatic::EArea::Level2:Fill=FLinearColor(.70f,.82f,.58f,.24f);Rim=FLinearColor(.36f,.30f,.22f);Thickness=2.f;break;
        case FLDMinimapStatic::EArea::Ramp:Fill=FLinearColor(.78f,.66f,.44f,.22f);Rim=FLinearColor(.78f,.66f,.44f,.8f);Thickness=1.f;bDashed=true;break;
        }
        TArray<FVector2f> Points;for(const FVector2D& Cm:Area.Cm){Points.Add(FVector2f(float((Cm.X-Origin.X)*Scale),float((Cm.Y-Origin.Y)*Scale)));}
        MinimapScanRuns(Points,[&Raster,&Fill](float X,float Y,float Width)
        {
            // Rows with the same span merge, so an axis-aligned wall stays one element.
            FLDMinimapStatic::FRun* Last=Raster.Fills.Num()?&Raster.Fills.Last():nullptr;
            if(Last&&Last->Color==Fill&&FMath::IsNearlyEqual(Last->Position.X,X,.01f)&&FMath::IsNearlyEqual(Last->Extent.X,Width,.01f)&&FMath::IsNearlyEqual(Last->Position.Y+Last->Extent.Y,Y,.01f)){Last->Extent.Y+=1.f;}
            else{Raster.Fills.Add({FVector2f(X,Y),FVector2f(Width,1.f),Fill});}
        });
        const FVector2f First=Points[0];Points.Add(First);Raster.Outlines.Add({MoveTemp(Points),Rim,Thickness,bDashed});
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
    if(!Geometry||(Cache.bResolved&&Cache.World==Registry->Revision()&&Cache.ResolvedGround.Min==Ground.Min&&Cache.ResolvedGround.Max==Ground.Max)){return;}
    Cache.bResolved=true;Cache.World=Registry->Revision();Cache.ResolvedGround=Ground;Cache.RasterKey=LDMinimapRaster::FStaticKey();FString Error;
    if(!Cache.bContentTried){Cache.bContentTried=true;if(!LDDemoEntry::LoadRuntimeContent(Cache.Content,Error)||!Cache.Content){Cache.Content.Reset();Cache.ContentError=Error;}}
    Cache.bValid=Cache.Content&&MinimapResolveStatic(Cache,*Geometry,Cache.Content->Get(),Ground,Error);
    if(!Cache.bValid)
    {
        Cache.Areas.Reset();Cache.Sites.Reset();Cache.Bounds=FBox2D(ForceInit);
        const FString Reason=Cache.Content?Error:TEXT("Approved Demo content unavailable: ")+Cache.ContentError;
        UE_LOG(LogLDUI,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE %s %s"),*Cache.World.MapId.ToString(),*Reason);
    }
}
// A layer resolved for another world revision is never drawn or fitted.
bool MinimapStaticCurrent(const FLDMinimapStatic& Cache,const UWorld* World)
{const auto* Registry=World->GetSubsystem<ULDMapRegistry>();return Cache.bValid&&Registry&&Cache.World==Registry->Revision();}
// Paint only consumes the texture built in tick for this exact world and layout.
bool MinimapStaticReady(FLDMinimapStatic& Cache,const UWorld* World,const LDMinimapRaster::FStaticKey& Key)
{
    if(!MinimapStaticCurrent(Cache,World)){return false;}
    return Cache.Raster.IsValid()&&Cache.RasterKey.Matches(Key);
}
void MinimapUpdateStatic(FLDMinimapStatic& Cache,const UWorld* World,const LDMinimapRaster::FStaticKey& Key)
{
    if(!MinimapStaticCurrent(Cache,World)||MinimapStaticReady(Cache,World,Key)||!FApp::CanEverRender()||Key.PixelScale<=0.f){return;}
    TRACE_CPUPROFILER_EVENT_SCOPE(LDMinimap_StaticRebuild);
    const FVector2D PlaneSize=Key.Ground.GetSize()*Key.Scale;
    // Padding keeps the antialiased rims at the ground edge; the same Slate primitives are rasterized at display DPI.
    constexpr double Padding=2.;
    const FVector2D DrawSize=PlaneSize+FVector2D(2.*Padding);
    const FVector2D Pixels(FMath::CeilToDouble(DrawSize.X*Key.PixelScale),FMath::CeilToDouble(DrawSize.Y*Key.PixelScale));
    TSharedRef<SLDMinimapRaster> Layer=SNew(SLDMinimapRaster);
    Layer->Size=Pixels/Key.PixelScale;
    MinimapLayoutStatic(Cache,Key.Ground.Min-FVector2D(Padding/Key.Scale),Key.Scale,*Layer);
    if(!Cache.Raster.IsValid()||Cache.Raster->SizeX!=int32(Pixels.X)||Cache.Raster->SizeY!=int32(Pixels.Y))
    {Cache.Raster.Reset(FWidgetRenderer::CreateTargetFor(Pixels,TF_Bilinear,true));}
    if(!ensureMsgf(Cache.Raster.IsValid(),TEXT("Cannot create minimap static render target"))){return;}
    FWidgetRenderer* Renderer=new FWidgetRenderer(true);
    Renderer->SetApplyColorDeficiencyCorrection(false);
    Renderer->DrawWidget(Cache.Raster.Get(),Layer,Key.PixelScale,Pixels,0.f);
    BeginCleanup(Renderer);
    Cache.RasterBrush.SetResourceObject(Cache.Raster.Get());
    Cache.RasterBrush.DrawAs=ESlateBrushDrawType::Image;
    Cache.RasterBrush.ImageSize=FVector2f(Layer->Size);
    Cache.RasterKey=Key;
}
const FLDWorldView* MinimapAuthorizedFrame(const ALDPlayerController& PC,const FLDObservedFrameRead& Read)
{
    const auto& Own=PC.OwnerState;
    return LDMinimapRaster::AuthorizedFrame(Own.LifePhase==ELDLifePhase::Alive,Own.Match,Own.Life,Read);
}
void MinimapUpdateCoverage(FLDMinimapStatic& Cache,const ALDPlayerController& PC)
{
    const auto* Relay=PC.LocalView();
    const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const FLDWorldView* Frame=MinimapAuthorizedFrame(PC,Read);
    if(!Frame||Frame->Footprint.SourceStatus!=ELDObservationSource::ActiveBody)
    {Cache.CoverageKey=LDMinimapRaster::FCoverageKey();return;}
    if(Cache.CoverageKey.Matches(Read.LocalSerial,PC.OwnerState.Match,PC.OwnerState.Life)){return;}
    if(!FApp::CanEverRender()){return;}
    TRACE_CPUPROFILER_EVENT_SCOPE(LDMinimap_CoverageUpload);
    TArray<FColor> Pixels;
    if(!ensureMsgf(LDMinimapRaster::CoveragePixels(Frame->Footprint.CurrentBits,Pixels),TEXT("Invalid authorized minimap coverage length"))){Cache.CoverageKey=LDMinimapRaster::FCoverageKey();return;}
    constexpr int32 Side=LDObservation::CoverageSide;
    if(!Cache.Coverage.IsValid())
    {
        Cache.Coverage.Reset(UTexture2D::CreateTransient(Side,Side,PF_B8G8R8A8));
        if(!ensureMsgf(Cache.Coverage.IsValid(),TEXT("Cannot create minimap coverage texture"))){return;}
        Cache.Coverage->SRGB=false;Cache.Coverage->Filter=TF_Nearest;
        Cache.Coverage->AddressX=TA_Clamp;Cache.Coverage->AddressY=TA_Clamp;Cache.Coverage->NeverStream=true;
        void* Data=Cache.Coverage->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data,Pixels.GetData(),Pixels.Num()*sizeof(FColor));
        Cache.Coverage->GetPlatformData()->Mips[0].BulkData.Unlock();
        Cache.Coverage->UpdateResource();
        Cache.CoverageBrush.SetResourceObject(Cache.Coverage.Get());Cache.CoverageBrush.DrawAs=ESlateBrushDrawType::Image;
    }
    else
    {
        uint8* Data=new uint8[Pixels.Num()*sizeof(FColor)];
        FMemory::Memcpy(Data,Pixels.GetData(),Pixels.Num()*sizeof(FColor));
        Cache.Coverage->UpdateTextureRegions(0,1,new FUpdateTextureRegion2D(0,0,0,0,Side,Side),Side*sizeof(FColor),sizeof(FColor),Data,
            [](uint8* Bytes,const FUpdateTextureRegion2D* Regions){delete[] Bytes;delete Regions;});
    }
    Cache.CoverageKey={Read.LocalSerial,PC.OwnerState.Match,PC.OwnerState.Life};
}
// The owner's live view: the applied orientation over the drawn content, or over the ground until that resolves.
// Painting and gestures both use it, so a pointer lands where the map shows.
LDMinimap::FView MinimapLiveView(const ALDPlayerController& PC,const FLDMinimapStatic& Cache,const UWorld* World,const FBox2D& Ground,const FVector2D& Size)
{
    const ELDMinimapOrientation Mode=PC.Controls?PC.Controls->GetMinimapOrientation():GetDefault<ULDControlSettings>()->GetMinimapOrientation();
    // Without a camera manager the camera yaw is unknown; the Camera view then keeps the north yaw instead of a guess.
    const double CameraYaw=PC.PlayerCameraManager?double(PC.PlayerCameraManager->GetCameraRotation().Yaw):LDMinimap::NorthYawDeg;
    const APawn* Body=PC.OwnerState.LifePhase==ELDLifePhase::Alive?PC.GetPawn():nullptr;
    return LDMinimap::MakeView(Mode,MinimapStaticCurrent(Cache,World)?Cache.Bounds:Ground,Size,CameraYaw,Body!=nullptr,Body?double(Body->GetActorRotation().Yaw):0.);
}
}
TSharedRef<SWidget> ULDMinimap::RebuildWidget()
{if(!WidgetTree->RootWidget){auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor::Transparent);WidgetTree->RootWidget=Background;}return Super::RebuildWidget();}

FVector2D ULDMinimap::Fraction(const FGeometry& G,const FPointerEvent& E) const
{return G.AbsoluteToLocal(E.GetScreenSpacePosition())/G.GetLocalSize();}
bool ULDMinimap::Gesture(FVector2D P,bool Move)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->IsFrontend()){return false;}
    // Death keeps camera operations (UI 4); a ground order still needs the controller's world-command gate.
    if(Move&&!PC->CanWorldCommand()){if(PC->IsAwaitingRespawn()){PC->PostIntentNotice(FString(LDTerms::Dead)+TEXT("，只能移动镜头，不能下达移动"));}return false;}
    FBox2D Ground(ForceInit);if(!MinimapGroundBounds(GetWorld(),Ground)){PC->PostIntentNotice(TEXT("小地图不可用：未加载 Demo 世界"));return false;}
    const LDMinimap::FView View=MinimapLiveView(*PC,MinimapCache(StaticMap),GetWorld(),Ground,FrameSize);
    if(!View.IsValid()){PC->PostIntentNotice(TEXT("小地图不可用：尚未完成布局"));return false;}
    // A point off the ground (a turned corner, the fitted margin) is clamped onto it like any edge point; a move is then
    // still validated against the public ground below.
    const FVector XY=LDMinimap::ClampToGround(LDMinimap::ToWorld(View,P*View.FrameSize),Ground);
    if(!Move){PC->Navigation->PanTo(XY);return true;}
    FVector Surface;if(!LDGround::MapSurface(GetWorld(),XY,Surface)){PC->PostIntentNotice(TEXT("无法到达该位置"));return false;}
    // Shift queues the minimap move like a Shift right click in the world.
    PC->Navigation->Navigate(Surface,PC->IsQueueModifierDown());return true;
}
FReply ULDMinimap::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    FrameSize=G.GetLocalSize();
    {
        const auto* Owner=CastChecked<ALDPlayerController>(GetOwningPlayer());FBox2D Ground(ForceInit);
        if(!Owner->IsFrontend()&&MinimapGroundBounds(GetWorld(),Ground)
            &&!LDMinimap::OnGround(MinimapLiveView(*Owner,MinimapCache(StaticMap),GetWorld(),Ground,FrameSize),Fraction(G,E)*FrameSize,Ground))
        {return FReply::Unhandled();}
    }
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
    if(bGround)
    {
        MinimapResolveRevision(Cache,GetWorld(),Ground);
        const LDMinimap::FView View=MinimapLiveView(*PC,Cache,GetWorld(),Ground,FrameSize);
        if(View.IsValid()){MinimapUpdateStatic(Cache,GetWorld(),{Ground,FrameSize,View.Scale,G.GetAccumulatedLayoutTransform().GetScale(),Cache.World});}
        MinimapUpdateCoverage(Cache,*PC);
    }
    else if(!Cache.bGroundLogged){Cache.bGroundLogged=true;UE_LOG(LogLDUI,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE No single registered ground surface; only the background is drawn."));}
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
        PlaneBox(1,FVector2f::ZeroVector,PlaneSize,LDMinimapRaster::GroundColor());
        const FLDOwnerState& Own=PC->OwnerState;const bool bAlive=Own.LifePhase==ELDLifePhase::Alive;
        const ALDWorldViewRelay* Relay=bAlive?PC->LocalView():nullptr;const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
        // Dynamic marks only come from the current authorized frame of this match and life; dead, stale or foreign frames draw none.
        const FLDWorldView* Frame=MinimapAuthorizedFrame(*PC,Read);
        if(Frame&&Frame->Footprint.SourceStatus==ELDObservationSource::ActiveBody&&Cache.Coverage.IsValid()
            &&Cache.CoverageKey.Matches(Read.LocalSerial,Own.Match,Own.Life))
        {
            FSlateDrawElement::MakeBox(E,Base+2,Plane.ToPaintGeometry(),&Cache.CoverageBrush,ESlateDrawEffect::NoGamma);
        }
        if(MinimapStaticReady(Cache,GetWorld(),{Ground,Size,View.Scale,G.GetAccumulatedLayoutTransform().GetScale(),Cache.World}))
        {
            FSlateDrawElement::MakeBox(E,Base+3,Plane.ToPaintGeometry(Cache.RasterBrush.ImageSize,FSlateLayoutTransform(FVector2f(-2.f))),
                &Cache.RasterBrush,ESlateDrawEffect::PreMultipliedAlpha|ESlateDrawEffect::NoGamma);
            // Capture: flag on the zone centre. Camps: small diamond or large bordered one.
            for(const FLDMinimapStatic::FSite& Site:Cache.Sites)
            {
                const FVector2f At=Spot(FVector(Site.Cm.X,Site.Cm.Y,0.));
                switch(Site.Kind)
                {
                case FLDMinimapStatic::ESite::Capture:Box(5,At+FVector2f(-.75f,-9.f),FVector2f(1.5f,9.5f),FLinearColor(.94f,.94f,.90f));Box(6,At+FVector2f(.75f,-9.f),FVector2f(6.f,4.f),FLinearColor(.92f,.20f,.16f));break;
                case FLDMinimapStatic::ESite::LowCamp:Diamond(5,At,6.5f,Ink);Diamond(6,At,4.5f,FLinearColor(.96f,.66f,.30f));break;
                case FLDMinimapStatic::ESite::HighCamp:Diamond(5,At,12.f,FLinearColor(1.f,.93f,.76f));Diamond(6,At,9.f,FLinearColor(.80f,.22f,.17f));Diamond(7,At,3.5f,Ink);break;
                // #97 Elite camp: the largest diamond, violet with a pale core, so it reads apart from the red High camp.
                case FLDMinimapStatic::ESite::EliteCamp:Diamond(5,At,14.f,Ink);Diamond(6,At,11.f,FLinearColor(.55f,.20f,.85f));Diamond(7,At,4.5f,FLinearColor(1.f,.93f,.76f));break;
                // Dorm: a small bed, a white pillow on a blue frame.
                case FLDMinimapStatic::ESite::Dorm:Box(5,At-FVector2f(6.f,4.f),FVector2f(12.f,8.f),Ink);Box(6,At-FVector2f(5.f,3.f),FVector2f(10.f,6.f),FLinearColor(.25f,.62f,1.f));Box(7,At-FVector2f(5.f,3.f),FVector2f(3.f,6.f),FLinearColor(.95f,.96f,.98f));break;
                // Boss patrol point: a gold dot on an ink disc.
                case FLDMinimapStatic::ESite::BossWaypoint:Disc(5,At,8.f,Ink);Disc(6,At,5.f,FLinearColor(.95f,.72f,.12f));break;
                }
            }
        }
        // D26: the current public airdrop, a crate on an orange plate (outlined while it is only announced).
        if(const auto* Public=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr)
        {
            const FLDAirdropView& Drop=Public->PublicMatch.Airdrop;
            // D27: the public 上热搜 circle, a dashed red ring of the approved radius around the published fuzzy centre.
            const FLDLeaderView& Notice=Public->PublicMatch.Leader;
            if(Public->PublicMatch.Generation==Own.Match&&Notice.Leader.IsValid()&&Cache.LeaderRadiusCm>0)
            {
                TArray<FVector2f> Ring;
                for(int32 Step=0;Step<=40;++Step){const double Angle=UE_DOUBLE_TWO_PI*Step/40.;Ring.Add(Spot(Notice.CircleCentreCm+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0.)*Cache.LeaderRadiusCm));}
                FSlateDrawElement::MakeDashedLines(E,Base+5,G.ToPaintGeometry(),MoveTemp(Ring),ESlateDrawEffect::None,FLinearColor(1.f,.32f,.28f,.95f),2.f,5.f);
            }
            if(Public->PublicMatch.Generation==Own.Match&&(Drop.Phase==ELDAirdropPhase::Announced||Drop.Phase==ELDAirdropPhase::Landed))
            {
                const FVector2f At=Spot(Drop.PositionCm);const FLinearColor Orange=ALDAirdropMarker::Color();
                Box(5,At-FVector2f(7.f,7.f),FVector2f(14.f,14.f),Ink);Box(6,At-FVector2f(6.f,6.f),FVector2f(12.f,12.f),Orange);
                if(Drop.Phase==ELDAirdropPhase::Landed){Box(7,At-FVector2f(4.f,4.f),FVector2f(8.f,8.f),FLinearColor(.55f,.36f,.16f));Box(7,At-FVector2f(4.f,.75f),FVector2f(8.f,1.5f),Ink);}
                else{Box(7,At-FVector2f(4.f,4.f),FVector2f(8.f,8.f),Ink);}
            }
            // #198: lying charms, a disc in the kind's colour (heal green, mana blue, move speed gold) on an ink disc.
            if(Public->PublicMatch.Generation==Own.Match)
            {
                for(const FLDCharmView& Charm:Public->PublicMatch.Charms)
                {
                    const FVector2f At=Spot(Charm.PositionCm);
                    Disc(5,At,7.5f,Ink);Disc(6,At,5.5f,LDHUD::CharmColor(Charm.Kind));Disc(7,At,2.f,FLinearColor(.97f,.97f,.94f));
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
        // #97: the map is the ground itself, turned with the view (a diamond in 跟随镜头) and drawn whole; its rim is the
        // minimap's edge and the frame beyond it stays see-through.
        TArray<FVector2f> Rim={Spot(FVector(Ground.Min.X,Ground.Min.Y,0.)),Spot(FVector(Ground.Max.X,Ground.Min.Y,0.)),
            Spot(FVector(Ground.Max.X,Ground.Max.Y,0.)),Spot(FVector(Ground.Min.X,Ground.Max.Y,0.))};
        const FVector2f Closing=Rim[0];Rim.Add(Closing);Lines(13,Rim,FLinearColor(.42f,.55f,.62f,.9f),1.5f);
    }
    E.PopClip();return Base+14;
}
