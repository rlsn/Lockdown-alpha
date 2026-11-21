#include "UI/LDMinimap.h"
#include "World/LDFieldPacing.h"
#include "UI/LDMinimapRaster.h"
#include "UI/LDHUD.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDUIFonts.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameState.h"
#include "Development/LDDemoContentCatalog.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "World/LDNativeGround.h"
#include "Core/LDGroundQueries.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/OrthoMatrix.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SLeafWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// Registered public geometry and its raster, plus the owner's current coverage upload; no units or remembered marks.
// World shapes resolve per revision; the north-up raster rebuilds only for world/layout changes, not a turn at fixed scale.
struct FLDMinimapStatic
{
    enum class EArea : uint8 { SightOnlyBlocker, FullBlocker, LowBlocker, Ramp, CaptureZone, DormZone };
    enum class ESite : uint8 { Capture, LowCamp, HighCamp, Dorm, BossWaypoint, EliteCamp, AncientCamp };
    // D27: the approved leader circle radius, read with the static map.
    double LeaderRadiusCm=0;
    struct FArea { EArea Style=EArea::FullBlocker; TArray<FVector2D> Cm; };
    struct FRampChevron { FVector2D LowMidCm=FVector2D::ZeroVector, HighMidCm=FVector2D::ZeroVector; double WidthCm=0.; };
    struct FSite { ESite Kind=ESite::Capture; FVector2D Cm=FVector2D::ZeroVector; };
    struct FRun { FVector2f Position=FVector2f::ZeroVector; FVector2f Extent=FVector2f::ZeroVector; FLinearColor Color=FLinearColor::White; };
    struct FOutline { TArray<FVector2f> Points; FLinearColor Color=FLinearColor::White; bool bDashed=false; };
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FString ContentError;
    bool bContentTried=false,bGroundLogged=false,bResolved=false,bValid=false;
    FLDWorldRevision World;
    // Areas are clipped to the registered ground. The map shows one region at a time, the one the owner is in: the field
    // (Field), the #198 rest room (Rest) or the final arena (Final); Rest and Final are invalid when the world has none.
    // Each is the box of its anchors grown by the drawn areas nearest them (see MinimapResolveStatic), kept to the ground.
    TArray<FArea> Areas;
    TArray<FRampChevron> RampChevrons;
    TArray<FSite> Sites;
    FBox2D Field=FBox2D(ForceInit);
    FBox2D Rest=FBox2D(ForceInit);
    FBox2D Final=FBox2D(ForceInit);
    struct FMapImage
    {
        FBox2D Region;
        TStrongObjectPtr<UTextureRenderTarget2D> Texture;
        FSlateBrush Brush;
    };
    TArray<FMapImage> MapImages;
    // Transparent public zone overlays above the level artwork.
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
int32 LDMinimap::HitSpawnCandidate(const FView& View,const FVector2D& Local,TConstArrayView<FVector> CandidatesCm)
{
    if(!View.IsValid()||Local.ContainsNaN()){return INDEX_NONE;}
    int32 Best=INDEX_NONE;
    double BestDistSq=SpawnBadgeHitRadius*SpawnBadgeHitRadius;
    for(int32 Index=0;Index<CandidatesCm.Num();++Index)
    {
        const FVector2D Spot=ToFrame(View,CandidatesCm[Index]);
        const double DistSq=FVector2D::DistSquared(Local,Spot);
        if(DistSq<=BestDistSq){BestDistSq=DistSq;Best=Index;}
    }
    return Best;
}
bool LDMinimap::IntersectCameraPlane(const FVector& Origin,const FVector& Direction,double PlaneZ,FVector& OutHit)
{
    if(Origin.ContainsNaN()||Direction.ContainsNaN()||!FMath::IsFinite(PlaneZ)||!(Direction.Z<-.001)){return false;}
    const double T=(PlaneZ-Origin.Z)/Direction.Z;
    if(!(T>0.)||!FMath::IsFinite(T)){return false;}
    OutHit=Origin+Direction*T;
    return true;
}
LDMinimap::EBlockerOverlay LDMinimap::ClassifyBlocker(bool bBlocksMovement,bool bBlocksSight)
{
    if(!bBlocksMovement){return EBlockerOverlay::None;}
    return bBlocksSight?EBlockerOverlay::FullBlocker:EBlockerOverlay::LowBlocker;
}
namespace
{
TArray<FVector2D> MinimapConvexHull2D(TArray<FVector2D> Pts)
{
    Pts.Sort([](const FVector2D& A,const FVector2D& B){return A.X!=B.X?A.X<B.X:A.Y<B.Y;});
    TArray<FVector2D> Unique;Unique.Reserve(Pts.Num());
    for(const FVector2D& P:Pts)
    {
        if(Unique.IsEmpty()||FVector2D::DistSquared(Unique.Last(),P)>.25){Unique.Add(P);}
    }
    if(Unique.Num()<3){return {};}
    auto Cross=[](const FVector2D& O,const FVector2D& A,const FVector2D& B)
    {return (A.X-O.X)*(B.Y-O.Y)-(A.Y-O.Y)*(B.X-O.X);};
    TArray<FVector2D> Lower,Upper;
    for(const FVector2D& P:Unique)
    {
        while(Lower.Num()>=2&&Cross(Lower[Lower.Num()-2],Lower.Last(),P)<=1e-3){Lower.Pop(EAllowShrinking::No);}
        Lower.Add(P);
    }
    for(int32 I=Unique.Num()-1;I>=0;--I)
    {
        const FVector2D& P=Unique[I];
        while(Upper.Num()>=2&&Cross(Upper[Upper.Num()-2],Upper.Last(),P)<=1e-3){Upper.Pop(EAllowShrinking::No);}
        Upper.Add(P);
    }
    Lower.Pop(EAllowShrinking::No);Upper.Pop(EAllowShrinking::No);
    Lower.Append(Upper);
    return Lower;
}
}
bool LDMinimap::ExtractRampFromConvexVertices(TConstArrayView<FVector> WorldVertices,FRampOverlay& OutRamp)
{
    OutRamp=FRampOverlay();
    TArray<FVector> Pts;Pts.Reserve(WorldVertices.Num());
    for(const FVector& V:WorldVertices)
    {
        if(V.ContainsNaN()){return false;}
        if(!Pts.ContainsByPredicate([&V](const FVector& Existing){return FVector::DistSquared(Existing,V)<=.25;}))
        {
            Pts.Add(V);
        }
    }
    if(Pts.Num()<4){return false;}
    if(Pts.Num()>32)
    {
        TArray<FVector> Reduced;Reduced.Reserve(32);
        for(int32 Az=0;Az<16;++Az)
        {
            const double Yaw=UE_DOUBLE_TWO_PI*double(Az)/16.;
            for(const double ZSign:{.65,-.65})
            {
                const FVector Dir=FVector(FMath::Cos(Yaw),FMath::Sin(Yaw),ZSign).GetSafeNormal();
                int32 BestIdx=0;double BestDot=FVector::DotProduct(Pts[0],Dir);
                for(int32 I=1;I<Pts.Num();++I)
                {
                    const double D=FVector::DotProduct(Pts[I],Dir);
                    if(D>BestDot){BestDot=D;BestIdx=I;}
                }
                if(!Reduced.ContainsByPredicate([&](const FVector& E){return FVector::DistSquared(E,Pts[BestIdx])<=.25;}))
                {
                    Reduced.Add(Pts[BestIdx]);
                }
            }
        }
        Pts=MoveTemp(Reduced);
        if(Pts.Num()<4){return false;}
    }
    FVector Inside=FVector::ZeroVector;
    for(const FVector& P:Pts){Inside+=P;}
    Inside/=double(Pts.Num());

    double BestArea=0.;
    FVector BestNormal=FVector::ZeroVector;
    TArray<FVector2D> BestHull;
    for(int32 I=0;I<Pts.Num();++I)
    {
        for(int32 J=I+1;J<Pts.Num();++J)
        {
            for(int32 K=J+1;K<Pts.Num();++K)
            {
                const FVector RawCross=FVector::CrossProduct(Pts[J]-Pts[I],Pts[K]-Pts[I]);
                if(RawCross.SizeSquared()<=1.){continue;}
                FVector N=RawCross.GetSafeNormal();
                if(FVector::DotProduct(N,Pts[I]-Inside)<0.){N=-N;}
                // Walkable sloped face: steeper than flat floors/plateaus (<= ~5 deg) and shallower than cliffs (<= ~47 deg).
                if(N.Z<.68||N.Z>.996){continue;}
                bool bOuterFace=true;
                double MinZ=Pts[I].Z,MaxZ=Pts[I].Z;
                TArray<FVector2D> FaceXY;
                for(const FVector& P:Pts)
                {
                    const double SignedDist=FVector::DotProduct(N,P-Pts[I]);
                    if(SignedDist>1.5){bOuterFace=false;break;}
                    if(SignedDist>=-1.5)
                    {
                        FaceXY.Add(FVector2D(P.X,P.Y));
                        MinZ=FMath::Min(MinZ,P.Z);
                        MaxZ=FMath::Max(MaxZ,P.Z);
                    }
                }
                if(!bOuterFace||FaceXY.Num()<3||(MaxZ-MinZ)<15.){continue;}
                TArray<FVector2D> Hull=MinimapConvexHull2D(MoveTemp(FaceXY));
                if(Hull.Num()<3){continue;}
                double Twice=0.;
                for(int32 H=0;H<Hull.Num();++H)
                {
                    const FVector2D& A=Hull[H];const FVector2D& B=Hull[(H+1)%Hull.Num()];
                    Twice+=A.X*B.Y-B.X*A.Y;
                }
                const double Area=FMath::Abs(Twice)*.5;
                if(Area>BestArea&&Area>100.)
                {
                    BestArea=Area;
                    BestNormal=N;
                    BestHull=MoveTemp(Hull);
                }
            }
        }
    }
    if(BestHull.Num()<3){return false;}
    const FVector2D Uphill=FVector2D(-BestNormal.X,-BestNormal.Y).GetSafeNormal();
    if(Uphill.IsNearlyZero()){return false;}
    const FVector2D Side(-Uphill.Y,Uphill.X);
    double MinAlong=TNumericLimits<double>::Max(),MaxAlong=-TNumericLimits<double>::Max();
    double MinSide=TNumericLimits<double>::Max(),MaxSide=-TNumericLimits<double>::Max();
    for(const FVector2D& P:BestHull)
    {
        const double Along=FVector2D::DotProduct(P,Uphill);
        const double Across=FVector2D::DotProduct(P,Side);
        MinAlong=FMath::Min(MinAlong,Along);MaxAlong=FMath::Max(MaxAlong,Along);
        MinSide=FMath::Min(MinSide,Across);MaxSide=FMath::Max(MaxSide,Across);
    }
    if(MaxAlong-MinAlong<10.){return false;}
    const double Band=FMath::Max(5.,(MaxAlong-MinAlong)*.15);
    FVector2D LowSum=FVector2D::ZeroVector,HighSum=FVector2D::ZeroVector;
    int32 LowCount=0,HighCount=0;
    for(const FVector2D& P:BestHull)
    {
        const double Along=FVector2D::DotProduct(P,Uphill);
        if(Along<=MinAlong+Band){LowSum+=P;++LowCount;}
        if(Along>=MaxAlong-Band){HighSum+=P;++HighCount;}
    }
    OutRamp.FootprintCm=MoveTemp(BestHull);
    OutRamp.LowMidCm=LowCount>0?LowSum/double(LowCount):FVector2D::ZeroVector;
    OutRamp.HighMidCm=HighCount>0?HighSum/double(HighCount):FVector2D::ZeroVector;
    OutRamp.WidthCm=FMath::Max(20.,MaxSide-MinSide);
    return FVector2D::DistSquared(OutRamp.LowMidCm,OutRamp.HighMidCm)>=100.;
}
void LDMinimap::CollectSceneRamps(const UWorld& World,TArray<FRampOverlay>& OutRamps)
{
    OutRamps.Reset();
    TArray<const UStaticMeshComponent*> PartsToInspect;
    for(const ULevel* Level:World.GetLevels())
    {
        if(!Level||!Level->bIsVisible){continue;}
        for(const AActor* Actor:Level->Actors)
        {
            if(!IsValid(Actor)||Actor->IsEditorOnly()||Actor==World.GetDefaultBrush()
                ||!Actor->ActorHasTag(LDDemoWorld::TerrainTag)){continue;}
            TInlineComponentArray<UStaticMeshComponent*> MeshParts(Actor);
            for(const UStaticMeshComponent* Part:MeshParts)
            {
                if(Part&&LDNativeGround::IsSupport(*Part)){PartsToInspect.Add(Part);}
            }
        }
    }
    PartsToInspect.Sort([](const UStaticMeshComponent& A,const UStaticMeshComponent& B)
    {return A.GetPathName()<B.GetPathName();});
    for(const UStaticMeshComponent* Part:PartsToInspect)
    {
        const UBodySetup* Body=Part->GetStaticMesh()?Part->GetStaticMesh()->GetBodySetup():nullptr;
        if(!Body){continue;}
        const FTransform& CompTransform=Part->GetComponentTransform();
        for(const FKConvexElem& Convex:Body->AggGeom.ConvexElems)
        {
            const FTransform ElemWorld=Convex.GetTransform()*CompTransform;
            TArray<FVector> WorldPts;WorldPts.Reserve(Convex.VertexData.Num());
            for(const FVector& V:Convex.VertexData){WorldPts.Add(ElemWorld.TransformPosition(V));}
            FRampOverlay Ramp;
            if(ExtractRampFromConvexVertices(WorldPts,Ramp)){OutRamps.Add(MoveTemp(Ramp));}
        }
        for(const FKBoxElem& Box:Body->AggGeom.BoxElems)
        {
            const FTransform ElemWorld=Box.GetTransform()*CompTransform;
            const FVector Half(Box.X*.5,Box.Y*.5,Box.Z*.5);
            TArray<FVector> WorldPts;WorldPts.Reserve(8);
            for(double SX:{-1.,1.})
            {
                for(double SY:{-1.,1.})
                {
                    for(double SZ:{-1.,1.})
                    {
                        WorldPts.Add(ElemWorld.TransformPosition(FVector(Half.X*SX,Half.Y*SY,Half.Z*SZ)));
                    }
                }
            }
            FRampOverlay Ramp;
            if(ExtractRampFromConvexVertices(WorldPts,Ramp)){OutRamps.Add(MoveTemp(Ramp));}
        }
    }
}
bool LDMinimap::FitRegions(TConstArrayView<FBox2D> Anchors,TConstArrayView<FBox2D> Footprints,const FBox2D& Ground,
    TArray<FBox2D>& Out,FString& Error)
{
    Out.Init(FBox2D(ForceInit),RegionCount);
    if(Anchors.Num()!=RegionCount||!Ground.bIsValid){Error=TEXT("Minimap regions need one anchor box per region and a ground.");return false;}
    // The part of A inside B; invalid when they share no area.
    const auto Overlap=[](const FBox2D& A,const FBox2D& B)
    {
        const FVector2D Min(FMath::Max(A.Min.X,B.Min.X),FMath::Max(A.Min.Y,B.Min.Y)),Max(FMath::Min(A.Max.X,B.Max.X),FMath::Min(A.Max.Y,B.Max.Y));
        return A.bIsValid&&B.bIsValid&&Max.X>Min.X&&Max.Y>Min.Y?FBox2D(Min,Max):FBox2D(ForceInit);
    };
    static const TCHAR* const Names[RegionCount]={TEXT("field"),TEXT("rest room"),TEXT("final arena")};
    FBox2D Cells[RegionCount],Boxes[RegionCount];
    for(int32 I=0;I<RegionCount;++I)
    {
        Cells[I]=Ground;Boxes[I]=Anchors[I];
        if(!Anchors[I].bIsValid){continue;}
        for(int32 J=0;J<RegionCount;++J)
        {
            if(J==I||!Anchors[J].bIsValid){continue;}
            const FBox2D& Own=Anchors[I];const FBox2D& Other=Anchors[J];
            const double GapX=FMath::Max(Other.Min.X-Own.Max.X,Own.Min.X-Other.Max.X),GapY=FMath::Max(Other.Min.Y-Own.Max.Y,Own.Min.Y-Other.Max.Y);
            if(GapX<=0.&&GapY<=0.)
            {Error=FString::Printf(TEXT("The %s and the %s overlap on the minimap; their anchors are too close to tell apart."),Names[I],Names[J]);return false;}
            const int32 Axis=GapX>=GapY?0:1;
            if(Other.Min[Axis]>Own.Max[Axis]){Cells[I].Max[Axis]=FMath::Min(Cells[I].Max[Axis],(Own.Max[Axis]+Other.Min[Axis])*.5);}
            else{Cells[I].Min[Axis]=FMath::Max(Cells[I].Min[Axis],(Other.Max[Axis]+Own.Min[Axis])*.5);}
        }
    }
    for(const FBox2D& Footprint:Footprints)
    {
        if(!Footprint.bIsValid){continue;}
        int32 Nearest=INDEX_NONE;double NearestGap=0.;
        for(int32 I=0;I<RegionCount;++I)
        {
            if(!Anchors[I].bIsValid){continue;}
            const FBox2D& Own=Anchors[I];
            const double Gap=FMath::Max3(0.,FMath::Max(Own.Min.X-Footprint.Max.X,Footprint.Min.X-Own.Max.X),FMath::Max(Own.Min.Y-Footprint.Max.Y,Footprint.Min.Y-Own.Max.Y));
            if(Gap<=RegionReachCm&&(Nearest==INDEX_NONE||Gap<NearestGap)){Nearest=I;NearestGap=Gap;}
        }
        if(Nearest==INDEX_NONE){continue;}
        const FBox2D Part=Overlap(Overlap(Footprint,Anchors[Nearest].ExpandBy(RegionReachCm)),Cells[Nearest]);
        if(Part.bIsValid){Boxes[Nearest]+=Part;}
    }
    for(int32 I=0;I<RegionCount;++I){Out[I]=Anchors[I].bIsValid?Overlap(Boxes[I],Cells[I]):FBox2D(ForceInit);}
    if(!Out[FieldRegion].bIsValid){Out[FieldRegion]=Ground;}
    Error.Reset();
    return true;
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
// The rest room's and the final arena's anchors: everything else belongs to the field.
LDMinimap::ERegion MinimapRegionOf(ELDDemoWorldAnchorKind Kind)
{
    if(Kind==ELDDemoWorldAnchorKind::RestSpawn||Kind==ELDDemoWorldAnchorKind::RestService||Kind==ELDDemoWorldAnchorKind::TrainingDummy
        ||Kind==ELDDemoWorldAnchorKind::PKSignup){return LDMinimap::RestRegion;}
    if(Kind==ELDDemoWorldAnchorKind::FinalSpawn||Kind==ELDDemoWorldAnchorKind::FinalCapture){return LDMinimap::FinalRegion;}
    return LDMinimap::FieldRegion;
}
bool MinimapResolveStatic(FLDMinimapStatic& Cache,const UWorld* World,const FLDDemoWorldGeometry& Geometry,const FLDDemoContentDefinitions& Approved,const FBox2D& Ground,FString& Error)
{
    Cache.Areas.Reset();Cache.RampChevrons.Reset();Cache.Sites.Reset();Cache.Field=FBox2D(ForceInit);Cache.Rest=FBox2D(ForceInit);Cache.Final=FBox2D(ForceInit);
    if(!Approved.World.MapId.Equals(Geometry.MapId,ESearchCase::CaseSensitive))
    {Error=FString::Printf(TEXT("Approved content names map %s but the registered world is %s."),*Approved.World.MapId,*Geometry.MapId);return false;}
    auto ToCm=[&Error](const FString& Id,const FLDDemoWorldPoint& Point,FVector2D& Result)
    {FVector Cm;if(!Point.ToVector(Cm,Error)){Error=Id+TEXT(": ")+Error;return false;}Result=FVector2D(Cm.X,Cm.Y);return true;};
    // An area keeps only its part on the ground; one left without area is not drawn.
    auto Keep=[&Ground](FLDMinimapStatic::FArea& Area)
    {
        Area.Cm=MinimapClipBox(MoveTemp(Area.Cm),Ground);
        return MinimapArea(Area.Cm)>1.;
    };
    bool bHasSceneTerrain=false;
    if(World)
    {
        for(const ULevel* Level:World->GetLevels())
        {
            if(!Level||!Level->bIsVisible){continue;}
            for(const AActor* Actor:Level->Actors)
            {
                if(IsValid(Actor)&&!Actor->IsEditorOnly()&&Actor!=World->GetDefaultBrush()
                    &&Actor->ActorHasTag(LDDemoWorld::TerrainTag))
                {
                    bHasSceneTerrain=true;
                    break;
                }
            }
            if(bHasSceneTerrain){break;}
        }
    }
    if(bHasSceneTerrain&&World)
    {
        TArray<LDMinimap::FRampOverlay> SceneRamps;
        LDMinimap::CollectSceneRamps(*World,SceneRamps);
        for(LDMinimap::FRampOverlay& SceneRamp:SceneRamps)
        {
            FLDMinimapStatic::FArea Area;
            Area.Style=FLDMinimapStatic::EArea::Ramp;
            Area.Cm=MoveTemp(SceneRamp.FootprintCm);
            if(Ground.IsInsideOrOn(SceneRamp.LowMidCm)&&Ground.IsInsideOrOn(SceneRamp.HighMidCm))
            {
                Cache.RampChevrons.Add({SceneRamp.LowMidCm,SceneRamp.HighMidCm,SceneRamp.WidthCm});
            }
            if(Keep(Area)){Cache.Areas.Add(MoveTemp(Area));}
        }
    }
    else
    {
        for(const FLDDemoWorldRamp& Ramp:Geometry.Ramps)
        {
            if(Ramp.PolygonCm.Num()<3){continue;}
            FLDMinimapStatic::FArea Area;
            Area.Style=FLDMinimapStatic::EArea::Ramp;
            Area.Cm.Reserve(Ramp.PolygonCm.Num());
            for(const FLDDemoWorldPoint& Vertex:Ramp.PolygonCm)
            {
                FVector2D Pt=FVector2D::ZeroVector;
                if(!ToCm(Ramp.Id,Vertex,Pt)){return false;}
                Area.Cm.Add(Pt);
            }
            if(Ramp.PolygonCm.Num()==4)
            {
                const FVector2D LowMid=(Area.Cm[0]+Area.Cm[1])*.5;
                const FVector2D HighMid=(Area.Cm[2]+Area.Cm[3])*.5;
                if(Ground.IsInsideOrOn(LowMid)&&Ground.IsInsideOrOn(HighMid))
                {
                    Cache.RampChevrons.Add({LowMid,HighMid,FVector2D::Distance(Area.Cm[0],Area.Cm[1])});
                }
            }
            if(Keep(Area)){Cache.Areas.Add(MoveTemp(Area));}
        }
    }
    TArray<FBox2D> Anchors;Anchors.Init(FBox2D(ForceInit),LDMinimap::RegionCount);
    for(const FLDDemoWorldBlocker& Blocker:Geometry.Blockers)
    {
        if(!Blocker.BlocksMovement&&!Blocker.BlocksSight){continue;}
        FLDMinimapStatic::FArea Area;
        const LDMinimap::EBlockerOverlay Overlay=LDMinimap::ClassifyBlocker(Blocker.BlocksMovement,Blocker.BlocksSight);
        Area.Style=Overlay==LDMinimap::EBlockerOverlay::FullBlocker?FLDMinimapStatic::EArea::FullBlocker
            :Overlay==LDMinimap::EBlockerOverlay::LowBlocker?FLDMinimapStatic::EArea::LowBlocker
            :FLDMinimapStatic::EArea::SightOnlyBlocker;
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
        Anchors[MinimapRegionOf(Anchor.Kind)]+=Site.Cm;
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
            const FString& DefinitionId=Anchor.PrototypeId.IsEmpty()?Anchor.Id:Anchor.PrototypeId;
            const FLDDemoCampPlacement* Camp=Approved.Camps.FindByPredicate([&DefinitionId](const FLDDemoCampPlacement& Placement){return Placement.AnchorId.Equals(DefinitionId,ESearchCase::CaseSensitive);});
            const FLDDemoMonsterDefinition* Monster=Camp?Approved.Monsters.FindByPredicate([Camp](const FLDDemoMonsterDefinition& Definition){return Definition.Id.Equals(Camp->MonsterId,ESearchCase::CaseSensitive);}):nullptr;
            if(!Monster){Error=Anchor.Id+TEXT(": camp has no approved placement or monster tier.");return false;}
            Site.Kind=Monster->Tier==ELDDemoCampTier::Ancient?FLDMinimapStatic::ESite::AncientCamp:Monster->Tier==ELDDemoCampTier::Elite?FLDMinimapStatic::ESite::EliteCamp
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
        // P6 spec §11.3: lockers show only while out and mini flags only while live (public match view).
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::Locker||Anchor.Kind==ELDDemoWorldAnchorKind::MiniFlag){continue;}
        // #198: PK and rest-space anchors are not field sites.
        else if(Anchor.Kind==ELDDemoWorldAnchorKind::PKSpawn||Anchor.Kind==ELDDemoWorldAnchorKind::PKThrow
            ||Anchor.Kind==ELDDemoWorldAnchorKind::PKSignup||Anchor.Kind==ELDDemoWorldAnchorKind::RestSpawn
            ||Anchor.Kind==ELDDemoWorldAnchorKind::RestService||Anchor.Kind==ELDDemoWorldAnchorKind::TrainingDummy
            ||Anchor.Kind==ELDDemoWorldAnchorKind::FinalSpawn||Anchor.Kind==ELDDemoWorldAnchorKind::FinalCapture){continue;}
        else{Error=Anchor.Id+TEXT(": unsupported anchor kind.");return false;}
        Cache.Sites.Add(Site);
    }
    TArray<FBox2D> Footprints,Regions;
    for(const FLDMinimapStatic::FArea& Area:Cache.Areas)
    {
        if(Area.Style!=FLDMinimapStatic::EArea::Ramp){Footprints.Add(FBox2D(Area.Cm));}
    }
    if(!LDMinimap::FitRegions(Anchors,Footprints,Ground,Regions,Error)){return false;}
    Cache.Field=Regions[LDMinimap::FieldRegion];Cache.Rest=Regions[LDMinimap::RestRegion];Cache.Final=Regions[LDMinimap::FinalRegion];
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
            if(Outline.bDashed)
            {
                TArray<FVector2f> Copy=Outline.Points;
                FSlateDrawElement::MakeDashedLines(E,L+1,G.ToPaintGeometry(),MoveTemp(Copy),ESlateDrawEffect::None,Outline.Color,1.5f,4.f);
            }
            else
            {
                FSlateDrawElement::MakeLines(E,L+1,G.ToPaintGeometry(),Outline.Points,ESlateDrawEffect::None,Outline.Color,true,1.5f);
            }
        }
        return L+1;
    }
};
void MinimapLayoutStatic(const FLDMinimapStatic& Cache,const FVector2D& Origin,double Scale,SLDMinimapRaster& Raster)
{
    const FLinearColor LowBlockerFill=FLinearColor(FColor(0x31,0x48,0x52)).CopyWithNewOpacity(.50f);
    const FLinearColor LowBlockerRim=FLinearColor(FColor(0x6F,0xA3,0xB8)).CopyWithNewOpacity(.92f);
    const FLinearColor FullBlockerRim=FLinearColor(FColor(0x22,0x25,0x2A)).CopyWithNewOpacity(.85f);
    const FLinearColor RampFill=FLinearColor(FColor(0x7D,0x6E,0x49)).CopyWithNewOpacity(.32f);
    const FLinearColor RampChevronInk=FLinearColor(FColor(0xE2,0xC9,0x86)).CopyWithNewOpacity(.92f);
    auto EmitFill=[&Raster](const TArray<FVector2f>& Points,const FLinearColor& Fill)
    {
        MinimapScanRuns(Points,[&Raster,&Fill](float X,float Y,float Width)
        {
            FLDMinimapStatic::FRun* Last=Raster.Fills.Num()?&Raster.Fills.Last():nullptr;
            if(Last&&Last->Color==Fill&&FMath::IsNearlyEqual(Last->Position.X,X,.01f)&&FMath::IsNearlyEqual(Last->Extent.X,Width,.01f)&&FMath::IsNearlyEqual(Last->Position.Y+Last->Extent.Y,Y,.01f)){Last->Extent.Y+=1.f;}
            else{Raster.Fills.Add({FVector2f(X,Y),FVector2f(Width,1.f),Fill});}
        });
    };
    for(const FLDMinimapStatic::FArea& Area:Cache.Areas)
    {
        if(Area.Style==FLDMinimapStatic::EArea::SightOnlyBlocker||Area.Cm.Num()<3){continue;}
        TArray<FVector2f> Points;Points.Reserve(Area.Cm.Num()+1);
        for(const FVector2D& Cm:Area.Cm){Points.Add(FVector2f(float((Cm.X-Origin.X)*Scale),float((Cm.Y-Origin.Y)*Scale)));}
        if(Area.Style==FLDMinimapStatic::EArea::Ramp)
        {
            EmitFill(Points,RampFill);
            continue;
        }
        if(Area.Style==FLDMinimapStatic::EArea::LowBlocker)
        {
            EmitFill(Points,LowBlockerFill);
            const FVector2f First=Points[0];Points.Add(First);
            Raster.Outlines.Add({MoveTemp(Points),LowBlockerRim,true});
            continue;
        }
        if(Area.Style==FLDMinimapStatic::EArea::FullBlocker)
        {
            const FVector2f First=Points[0];Points.Add(First);
            Raster.Outlines.Add({MoveTemp(Points),FullBlockerRim,false});
            continue;
        }
        const bool bCaptureZone=Area.Style==FLDMinimapStatic::EArea::CaptureZone;
        const FLinearColor Fill=bCaptureZone?FLinearColor(1.f,.84f,.30f,.16f):FLinearColor(.25f,.62f,1.f,.18f);
        const FLinearColor Rim=bCaptureZone?FLinearColor(1.f,.84f,.34f,.95f):FLinearColor(.45f,.75f,1.f,.95f);
        EmitFill(Points,Fill);
        const FVector2f First=Points[0];Points.Add(First);Raster.Outlines.Add({MoveTemp(Points),Rim,false});
    }
    for(const FLDMinimapStatic::FRampChevron& Ramp:Cache.RampChevrons)
    {
        const FVector2f Low(float((Ramp.LowMidCm.X-Origin.X)*Scale),float((Ramp.LowMidCm.Y-Origin.Y)*Scale));
        const FVector2f High(float((Ramp.HighMidCm.X-Origin.X)*Scale),float((Ramp.HighMidCm.Y-Origin.Y)*Scale));
        const FVector2f Delta=High-Low;
        const float Len=Delta.Size();
        if(Len<3.f){continue;}
        const FVector2f Dir=Delta/Len;
        const FVector2f Side(-Dir.Y,Dir.X);
        const float Wing=FMath::Clamp(float(Ramp.WidthCm*Scale)*.28f,2.2f,5.f);
        const FVector2f Tip=Low+Dir*(Len*.66f);
        const FVector2f BasePt=Tip-Dir*FMath::Min(Wing*1.1f,Len*.42f);
        Raster.Outlines.Add({{BasePt-Side*Wing,Tip,BasePt+Side*Wing},RampChevronInk,false});
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
    Cache.MapImages.Reset();
    if(!Cache.bContentTried){Cache.bContentTried=true;if(!LDDemoEntry::LoadRuntimeContent(Cache.Content,Error)||!Cache.Content){Cache.Content.Reset();Cache.ContentError=Error;}}
    Cache.bValid=Cache.Content&&MinimapResolveStatic(Cache,World,*Geometry,Cache.Content->Get(),Ground,Error);
    if(!Cache.bValid)
    {
        Cache.Areas.Reset();Cache.Sites.Reset();Cache.Field=FBox2D(ForceInit);Cache.Rest=FBox2D(ForceInit);Cache.Final=FBox2D(ForceInit);
        const FString Reason=Cache.Content?Error:TEXT("Approved Demo content unavailable: ")+Cache.ContentError;
        UE_LOG(LogLDUI,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE %s %s"),*Cache.World.MapId.ToString(),*Reason);
    }
}
// A layer resolved for another world revision is never drawn or fitted.
bool MinimapStaticCurrent(const FLDMinimapStatic& Cache,const UWorld* World)
{const auto* Registry=World->GetSubsystem<ULDMapRegistry>();return Cache.bValid&&Registry&&Cache.World==Registry->Revision();}

const FLDMinimapStatic::FMapImage* MinimapFindImage(const FLDMinimapStatic& Cache, const FBox2D& Region)
{
    return Cache.MapImages.FindByPredicate([&](const auto& Image)
    {
        return Image.Region.Min == Region.Min && Image.Region.Max == Region.Max;
    });
}

void MinimapUpdateImage(FLDMinimapStatic& Cache, UWorld* World, const FBox2D& Region)
{
    if (!FApp::CanEverRender() || !MinimapStaticCurrent(Cache, World) || MinimapFindImage(Cache, Region)) { return; }
    TRACE_CPUPROFILER_EVENT_SCOPE(LDMinimap_CaptureArtwork);
    const FVector2D Span = Region.GetSize();
    constexpr double LongSide = 512.;
    const double Scale = LongSide / FMath::Max(Span.X, Span.Y);
    const int32 Width = FMath::Max(1, FMath::RoundToInt32(Span.X * Scale));
    const int32 Height = FMath::Max(1, FMath::RoundToInt32(Span.Y * Scale));
    auto& Image = Cache.MapImages.AddDefaulted_GetRef();
    Image.Region = Region;
    auto* Capture = NewObject<USceneCaptureComponent2D>(World, NAME_None, RF_Transient);
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    FBox SceneBounds(ForceInit);
    // Startup components are saved level artwork. Even listen hosts must exclude runtime actors,
    // including bodies the server owns but the local player has no permission to observe.
    for (const ULevel* Level : World->GetLevels())
    {
        if (!Level || !Level->bIsVisible) { continue; }
        for (const AActor* Actor : Level->Actors)
        {
            if (!IsValid(Actor) || Actor->IsHidden() || Actor->IsEditorOnly() || Actor == World->GetDefaultBrush()) { continue; }
            TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
            for (UPrimitiveComponent* Part : Parts)
            {
                if (!Part->IsNetStartupComponent() || !Part->IsRegistered() || !Part->IsVisible()
                    || Part->bHiddenInGame || Part->IsEditorOnly() || Part->Mobility == EComponentMobility::Movable) { continue; }
                const FBox Bounds = Part->Bounds.GetBox();
                if (!Bounds.IsValid || !Region.Intersect(FBox2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max)))) { continue; }
                Capture->ShowOnlyComponent(Part);
                SceneBounds += Bounds;
            }
        }
    }
    if (!SceneBounds.IsValid)
    {
        UE_LOG(LogLDUI, Warning, TEXT("LD_MINIMAP_ART_UNAVAILABLE No saved visible static artwork in the displayed region."));
        return;
    }

    Image.Texture.Reset(NewObject<UTextureRenderTarget2D>(World, NAME_None, RF_Transient));
    Image.Texture->ClearColor = LDMinimapRaster::GroundColor();
    Image.Texture->Filter = TF_Bilinear;
    Image.Texture->InitCustomFormat(Width, Height, PF_B8G8R8A8, true);
    Capture->TextureTarget = Image.Texture.Get();
    Capture->CaptureSource = SCS_BaseColor;
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = Span.X;
    Capture->bUseCustomProjectionMatrix = true;
    Capture->bAutoCalculateOrthoPlanes = false;
    Capture->bUpdateOrthoPlanes = false;
    constexpr double DepthPaddingCm = 100.;
    Capture->CustomProjectionMatrix = FReversedZOrthoMatrix(Span.X * .5, Span.Y * .5,
        1. / (SceneBounds.GetSize().Z + 2. * DepthPaddingCm), 0.);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetVolumetricFog(false);
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetDynamicShadows(false);
    Capture->SetWorldLocationAndRotation(FVector(Region.GetCenter(), SceneBounds.Max.Z + DepthPaddingCm), FRotator(-90., -90., 0.));
    Capture->RegisterComponentWithWorld(World);
    Capture->CaptureScene();
    Capture->DestroyComponent();
    Image.Brush.SetResourceObject(Image.Texture.Get());
    Image.Brush.DrawAs = ESlateBrushDrawType::Image;
}

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
    constexpr int32 Width=LDObservation::CoverageWidth,Height=LDObservation::CoverageHeight;
    if(!Cache.Coverage.IsValid())
    {
        Cache.Coverage.Reset(UTexture2D::CreateTransient(Width,Height,PF_B8G8R8A8));
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
        Cache.Coverage->UpdateTextureRegions(0,1,new FUpdateTextureRegion2D(0,0,0,0,Width,Height),Width*sizeof(FColor),sizeof(FColor),Data,
            [](uint8* Bytes,const FUpdateTextureRegion2D* Regions){delete[] Bytes;delete Regions;});
    }
    Cache.CoverageKey={Read.LocalSerial,PC.OwnerState.Match,PC.OwnerState.Life};
}
// The region the map shows: the rest room or the final arena while the owner's living body stands in it (without a body,
// while the camera looks at it), else the field; the whole ground until the layer resolves. It is the map's own ground: the view fits it,
// paint clips to it and gestures clamp into it.
double MinimapOwnFootPlaneZ(const ALDPlayerController& PC)
{
    if(PC.OwnerState.LifePhase!=ELDLifePhase::Alive){return 0.;}
    if(const ACharacter* Character=Cast<ACharacter>(PC.GetPawn());Character&&Character->GetCapsuleComponent())
    {
        return Character->GetActorLocation().Z-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
    if(const APawn* Body=PC.GetPawn()){return Body->GetActorLocation().Z;}
    return 0.;
}
FBox2D MinimapRegion(const ALDPlayerController& PC,const FLDMinimapStatic& Cache,const UWorld* World,const FBox2D& Ground)
{
    if(!MinimapStaticCurrent(Cache,World)){return Ground;}
    if(!Cache.Rest.bIsValid&&!Cache.Final.bIsValid){return Cache.Field;}
    FVector2D Where=FVector2D::ZeroVector;bool bWhere=false;
    if(const APawn* Body=PC.OwnerState.LifePhase==ELDLifePhase::Alive?PC.GetPawn():nullptr){Where=FVector2D(Body->GetActorLocation());bWhere=true;}
    else
    {
        int32 W=0,H=0;PC.GetViewportSize(W,H);FVector Origin,Direction,Hit;
        if(W>0&&H>0&&PC.DeprojectScreenPositionToWorld(W*.5f,H*.5f,Origin,Direction)
            &&LDMinimap::IntersectCameraPlane(Origin,Direction,MinimapOwnFootPlaneZ(PC),Hit))
        {Where=FVector2D(Hit);bWhere=true;}
    }
    if(bWhere&&Cache.Rest.bIsValid&&Cache.Rest.IsInsideOrOn(Where)){return Cache.Rest;}
    return bWhere&&Cache.Final.bIsValid&&Cache.Final.IsInsideOrOn(Where)?Cache.Final:Cache.Field;
}
// The owner's live view: the applied orientation over the shown region. Painting and gestures both use it, so a pointer
// lands where the map shows.
LDMinimap::FView MinimapLiveView(const ALDPlayerController& PC,const FBox2D& Region,const FVector2D& Size)
{
    const ELDMinimapOrientation Mode=PC.Controls?PC.Controls->GetMinimapOrientation():GetDefault<ULDControlSettings>()->GetMinimapOrientation();
    // Without a camera manager the camera yaw is unknown; the Camera view then keeps the north yaw instead of a guess.
    const double CameraYaw=PC.PlayerCameraManager?double(PC.PlayerCameraManager->GetCameraRotation().Yaw):LDMinimap::NorthYawDeg;
    const APawn* Body=PC.OwnerState.LifePhase==ELDLifePhase::Alive?PC.GetPawn():nullptr;
    return LDMinimap::MakeView(Mode,Region,Size,CameraYaw,Body!=nullptr,Body?double(Body->GetActorRotation().Yaw):0.);
}
}
TSharedRef<SWidget> ULDMinimap::RebuildWidget()
{if(!WidgetTree->RootWidget){auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor::Transparent);WidgetTree->RootWidget=Background;}return Super::RebuildWidget();}

void ULDMinimap::FreezeForWorldTeardown()
{
    bFrozenForTeardown=true;
    bDragging=false;
    if(StaticMap&&StaticMap->Coverage.IsValid()&&StaticMap->Coverage->GetResource())
    {
        FlushRenderingCommands();
    }
}
void ULDMinimap::NativeDestruct()
{
    FreezeForWorldTeardown();
    StaticMap.Reset();
    Super::NativeDestruct();
}
FVector2D ULDMinimap::Fraction(const FGeometry& G,const FPointerEvent& E) const
{return G.AbsoluteToLocal(E.GetScreenSpacePosition())/G.GetLocalSize();}
bool ULDMinimap::Gesture(FVector2D P,bool Move)
{
    UWorld* World=GetWorld();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bFrozenForTeardown||!World||World->bIsTearingDown||!PC||PC->IsFrontend()||!PC->Navigation){return false;}
    // Death keeps camera operations (MOVEMENT_INPUT_AND_CAMERA input ownership); a ground order still needs the controller's world-command gate.
    if(Move&&!PC->CanWorldCommand()){if(PC->IsAwaitingRespawn()){PC->PostIntentNotice(FString(LDTerms::Dead)+TEXT("，只能移动镜头，不能下达移动"));}return false;}
    FBox2D Ground(ForceInit);if(!MinimapGroundBounds(World,Ground)){PC->PostIntentNotice(TEXT("小地图不可用：未加载 Demo 世界"));return false;}
    const FBox2D Region=MinimapRegion(*PC,MinimapCache(StaticMap),World,Ground);
    const LDMinimap::FView View=MinimapLiveView(*PC,Region,FrameSize);
    if(!View.IsValid()){PC->PostIntentNotice(TEXT("小地图不可用：尚未完成布局"));return false;}
    // A point off the ground (a turned corner, the fitted margin) is clamped onto it like any edge point; a move is then
    // still validated against the public ground below.
    const FVector XY=LDMinimap::ClampToGround(LDMinimap::ToWorld(View,P*View.FrameSize),Region);
    if(!Move){PC->Navigation->PanTo(XY);return true;}
    FVector Surface;if(!LDGround::MapSurface(World,XY,Surface)){PC->PostIntentNotice(TEXT("无法到达该位置"));return false;}
    // Shift queues the minimap move like a Shift right click in the world.
    PC->Navigation->Navigate(Surface,PC->IsQueueModifierDown());return true;
}
FReply ULDMinimap::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    FrameSize=G.GetLocalSize();
    UWorld* World=GetWorld();
    auto* Owner=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bFrozenForTeardown||!World||World->bIsTearingDown||!Owner){return FReply::Unhandled();}
    const FVector2D LocalClick=Fraction(G,E)*FrameSize;
    const FKey Button=E.GetEffectingButton();
    {
        FBox2D Ground(ForceInit);
        if(!Owner->IsFrontend()&&MinimapGroundBounds(World,Ground))
        {
            const FBox2D Region=MinimapRegion(*Owner,MinimapCache(StaticMap),World,Ground);
            const LDMinimap::FView View=MinimapLiveView(*Owner,Region,FrameSize);
            // Spawn-choice candidate badges take priority within 22 px of any badge centre (left or right click),
            // even if a high-ground edge badge reaches slightly into the minimap margin.
            if(Button==EKeys::LeftMouseButton||Button==EKeys::RightMouseButton)
            {
                int64 NowMs=0;const bool bClock=Owner->ReadDemoAuthorityTimeMs(NowMs);
                const LDHUD::FSpawnChoiceView Choice=LDHUD::SpawnChoice(Owner->OwnerState,bClock,NowMs);
                if(Choice.bVisible)
                {
                    const int32 Count=FMath::Min(Choice.Count,Owner->OwnerState.Pacing.SpawnCandidateCm.Num());
                    const int32 Hit=LDMinimap::HitSpawnCandidate(View,LocalClick,
                        TConstArrayView<FVector>(Owner->OwnerState.Pacing.SpawnCandidateCm.GetData(),Count));
                    if(Hit!=INDEX_NONE)
                    {
                        bDragging=false;
                        if(Owner->HUDUI){Owner->HUDUI->SelectSpawnCandidate(Hit);}
                        else
                        {
                            Owner->SubmitSpawnChoice(uint8(Hit));
                            if(Owner->Navigation){Owner->Navigation->PanTo(Owner->OwnerState.Pacing.SpawnCandidateCm[Hit]);}
                        }
                        return FReply::Handled();
                    }
                }
            }
            if(!LDMinimap::OnGround(View,LocalClick,Region)){return FReply::Unhandled();}
        }
    }
    if(Button==EKeys::LeftMouseButton)
    {
        // The press keeps the pointer until release even when the pan is refused, so the release never reaches the world.
        DragMatch=Owner->OwnerState.Match;DragLife=Owner->OwnerState.Life;
        bDragging=Gesture(Fraction(G,E),false);return FReply::Handled().CaptureMouse(TakeWidget());
    }
    if(Button==EKeys::RightMouseButton){Gesture(Fraction(G,E),true);return FReply::Handled();}return FReply::Handled();
}
FReply ULDMinimap::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{FrameSize=G.GetLocalSize();if(bDragging&&!Gesture(Fraction(G,E),false)){bDragging=false;}return FReply::Handled();}
FReply ULDMinimap::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{if(E.GetEffectingButton()==EKeys::LeftMouseButton){bDragging=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Handled();}
void ULDMinimap::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){bDragging=false;Super::NativeOnMouseCaptureLost(E);}
void ULDMinimap::NativeTick(const FGeometry& G,float D)
{
    Super::NativeTick(G,D);FrameSize=G.GetLocalSize();
    UWorld* World=GetWorld();
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bFrozenForTeardown||!World||World->bIsTearingDown||!PC){bDragging=false;return;}
    FLDMinimapStatic& Cache=MinimapCache(StaticMap);FBox2D Ground(ForceInit);
    const bool bGround=MinimapGroundBounds(World,Ground);
    if(bGround)
    {
        MinimapResolveRevision(Cache,World,Ground);
        // Capture the region once. Only the zone overlays depend on the current display scale.
        const FBox2D Region=MinimapRegion(*PC,Cache,World,Ground);
        const LDMinimap::FView View=MinimapLiveView(*PC,Region,FrameSize);
        if(View.IsValid())
        {
            MinimapUpdateImage(Cache, World, Region);
            MinimapUpdateStatic(Cache,World,{Region,FrameSize,View.Scale,G.GetAccumulatedLayoutTransform().GetScale(),Cache.World});
        }
        MinimapUpdateCoverage(Cache,*PC);
    }
    else if(!Cache.bGroundLogged){Cache.bGroundLogged=true;UE_LOG(LogLDUI,Warning,TEXT("LD_MINIMAP_STATIC_UNAVAILABLE No single registered ground surface; only the background is drawn."));}
    // Death alone keeps a drag; it ends when panning is invalid or the owner moved to another match or life.
    if(bDragging&&(PC->IsFrontend()||!bGround||PC->OwnerState.Match!=DragMatch||PC->OwnerState.Life!=DragLife)){bDragging=false;}
}
int32 ULDMinimap::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    const UWorld* World=GetWorld();
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bFrozenForTeardown||!World||World->bIsTearingDown||!PC){return Base;}
    const FVector2D Size=G.GetLocalSize();const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const FLinearColor Ink(.02f,.03f,.03f,.92f);
    auto Box=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint){FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
    auto Diamond=[&](int32 Layer,const FVector2f& At,float Edge,const FLinearColor& Tint){FSlateDrawElement::MakeRotatedBox(E,Base+Layer,G.ToPaintGeometry(FVector2f(Edge,Edge),FSlateLayoutTransform(At-FVector2f(Edge*.5f,Edge*.5f))),White,ESlateDrawEffect::None,UE_PI*.25f,TOptional<FVector2f>(),FSlateDrawElement::RelativeToElement,Tint);};
    auto Disc=[&](int32 Layer,const FVector2f& At,float Diameter,const FLinearColor& Tint){FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(FVector2f(Diameter,Diameter),FSlateLayoutTransform(At-FVector2f(Diameter*.5f,Diameter*.5f))),MinimapDiscBrush(),ESlateDrawEffect::None,Tint);};
    auto Lines=[&](int32 Layer,const TArray<FVector2f>& Points,const FLinearColor& Tint,float Thickness){FSlateDrawElement::MakeLines(E,Base+Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Tint,true,Thickness);};
    E.PushClip(FSlateClippingZone(G));
    FBox2D Ground(ForceInit);FLDMinimapStatic& Cache=MinimapCache(StaticMap);
    const bool bGround=MinimapGroundBounds(GetWorld(),Ground);
    const FBox2D Region=bGround?MinimapRegion(*PC,Cache,GetWorld(),Ground):FBox2D(ForceInit);
    const LDMinimap::FView View=bGround?MinimapLiveView(*PC,Region,Size):LDMinimap::FView();
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
        // Everything is clipped to the shown region, turned with the view: the other region never shows.
        const FVector2f RegionAt(float((Region.Min.X-Ground.Min.X)*View.Scale),float((Region.Min.Y-Ground.Min.Y)*View.Scale));
        const FVector2D RegionSpan=Region.GetSize();const FVector2f RegionSize(float(RegionSpan.X*View.Scale),float(RegionSpan.Y*View.Scale));
        auto Corner=[&](double X,double Y){return FVector2f(G.LocalToAbsolute(FVector2D(Spot(FVector(X,Y,0.)))));};
        E.PushClip(FSlateClippingZone(Corner(Region.Min.X,Region.Min.Y),Corner(Region.Max.X,Region.Min.Y),Corner(Region.Min.X,Region.Max.Y),Corner(Region.Max.X,Region.Max.Y)));
        PlaneBox(1,RegionAt,RegionSize,LDMinimapRaster::GroundColor());
        if (const auto* Image = MinimapFindImage(Cache, Region); MinimapStaticCurrent(Cache, GetWorld()) && Image && Image->Texture.IsValid())
        {
            FSlateDrawElement::MakeBox(E, Base+2, Plane.ToPaintGeometry(RegionSize, FSlateLayoutTransform(RegionAt)),
                &Image->Brush, ESlateDrawEffect::IgnoreTextureAlpha);
        }
        const FLDOwnerState& Own=PC->OwnerState;const bool bAlive=Own.LifePhase==ELDLifePhase::Alive;
        const ALDWorldViewRelay* Relay=bAlive?PC->LocalView():nullptr;const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
        // Dynamic marks only come from the current authorized frame of this match and life; dead, stale or foreign frames draw none.
        const FLDWorldView* Frame=MinimapAuthorizedFrame(*PC,Read);
        if(Frame&&Frame->Footprint.SourceStatus==ELDObservationSource::ActiveBody&&Cache.Coverage.IsValid()
            &&Cache.CoverageKey.Matches(Read.LocalSerial,Own.Match,Own.Life))
        {
            FSlateDrawElement::MakeBox(E,Base+4,Plane.ToPaintGeometry(),&Cache.CoverageBrush,ESlateDrawEffect::NoGamma);
        }
        else { PlaneBox(4, RegionAt, RegionSize, LDMinimapRaster::FogColor()); }
        if(MinimapStaticReady(Cache,GetWorld(),{Region,Size,View.Scale,G.GetAccumulatedLayoutTransform().GetScale(),Cache.World}))
        {
            FSlateDrawElement::MakeBox(E,Base+3,Plane.ToPaintGeometry(Cache.RasterBrush.ImageSize,FSlateLayoutTransform(RegionAt-FVector2f(2.f))),
                &Cache.RasterBrush,ESlateDrawEffect::PreMultipliedAlpha|ESlateDrawEffect::NoGamma);
            // Capture: flag on the zone centre. Camps: small diamond or large bordered one.
            for(const FLDMinimapStatic::FSite& Site:Cache.Sites)
            {
                const FVector2f At=Spot(FVector(Site.Cm.X,Site.Cm.Y,0.));
                switch(Site.Kind)
                {
                case FLDMinimapStatic::ESite::Capture:Box(5,At+FVector2f(-.75f,-9.f),FVector2f(1.5f,9.5f),FLinearColor(.94f,.94f,.90f));Box(6,At+FVector2f(.75f,-9.f),FVector2f(6.f,4.f),FLinearColor(.92f,.20f,.16f));break;
                // Camps grow with their tier and wear its colour (white, blue, violet, gold), the same as their world labels.
                case FLDMinimapStatic::ESite::LowCamp:Diamond(5,At,6.5f,Ink);Diamond(6,At,4.5f,LDHUDStyle::CampTierInk(uint8(ELDDemoCampTier::Low)));break;
                case FLDMinimapStatic::ESite::HighCamp:Diamond(5,At,11.f,Ink);Diamond(6,At,8.5f,LDHUDStyle::CampTierInk(uint8(ELDDemoCampTier::High)));Diamond(7,At,3.f,Ink);break;
                case FLDMinimapStatic::ESite::EliteCamp:Diamond(5,At,14.f,Ink);Diamond(6,At,11.f,LDHUDStyle::CampTierInk(uint8(ELDDemoCampTier::Elite)));Diamond(7,At,4.5f,FLinearColor(1.f,.93f,.76f));break;
                case FLDMinimapStatic::ESite::AncientCamp:Diamond(5,At,17.f,Ink);Diamond(6,At,14.f,LDHUDStyle::CampTierInk(uint8(ELDDemoCampTier::Ancient)));Diamond(7,At,8.f,Ink);Diamond(8,At,4.f,LDHUDStyle::CampTierInk(uint8(ELDDemoCampTier::Ancient)));break;
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
            // A rumor day's runner-up gets the same ring around its own fuzzy centre.
            if(Public->PublicMatch.Generation==Own.Match&&Cache.LeaderRadiusCm>0)
            {
                const TPair<FGuid,FVector> Trending[]={{Notice.Leader,Notice.CircleCentreCm},{Notice.RunnerUp,Notice.RunnerUpCentreCm}};
                for(const TPair<FGuid,FVector>& One:Trending)
                {
                    if(!One.Key.IsValid()){continue;}
                    TArray<FVector2f> Ring;
                    for(int32 Step=0;Step<=40;++Step){const double Angle=UE_DOUBLE_TWO_PI*Step/40.;Ring.Add(Spot(One.Value+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0.)*Cache.LeaderRadiusCm));}
                    FSlateDrawElement::MakeDashedLines(E,Base+5,G.ToPaintGeometry(),MoveTemp(Ring),ESlateDrawEffect::None,FLinearColor(1.f,.32f,.28f,.95f),2.f,5.f);
                }
            }
            if(Public->PublicMatch.Generation==Own.Match&&(Drop.Phase==ELDAirdropPhase::Announced||Drop.Phase==ELDAirdropPhase::Landed))
            {
                const FVector2f At=Spot(Drop.PositionCm);const FLinearColor Orange=ALDAirdropMarker::Color();
                Box(5,At-FVector2f(7.f,7.f),FVector2f(14.f,14.f),Ink);Box(6,At-FVector2f(6.f,6.f),FVector2f(12.f,12.f),Orange);
                if(Drop.Phase==ELDAirdropPhase::Landed){Box(7,At-FVector2f(4.f,4.f),FVector2f(8.f,8.f),FLinearColor(.55f,.36f,.16f));Box(7,At-FVector2f(4.f,.75f),FVector2f(8.f,1.5f),Ink);}
                else{Box(7,At-FVector2f(4.f,4.f),FVector2f(8.f,8.f),Ink);}
            }
            if(Public->PublicMatch.Generation==Own.Match)
            {
                // P6 spec §10.3: the mid-field objective's circle, dashed while announced and solid while open, with a flag
                // on a mini flag's centre.
                const FLDObjectiveView& Objective=Public->PublicMatch.Objective;
                if((Objective.Phase==ELDObjectivePhase::Announced||Objective.Phase==ELDObjectivePhase::Open)&&Objective.RadiusCm>0.f)
                {
                    TArray<FVector2f> Ring;
                    for(int32 Step=0;Step<=32;++Step){const double Angle=UE_DOUBLE_TWO_PI*Step/32.;Ring.Add(Spot(Objective.CentreCm+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0.)*double(Objective.RadiusCm)));}
                    const FLinearColor Gold(1.f,.80f,.20f,.95f);
                    if(Objective.Phase==ELDObjectivePhase::Announced){FSlateDrawElement::MakeDashedLines(E,Base+5,G.ToPaintGeometry(),MoveTemp(Ring),ESlateDrawEffect::None,Gold,2.f,4.f);}
                    else{Lines(5,Ring,Gold,2.f);}
                    if(Objective.Kind==ELDObjectiveKind::MiniFlag)
                    {
                        const FVector2f At=Spot(Objective.CentreCm);
                        Box(6,At+FVector2f(-.75f,-8.f),FVector2f(1.5f,8.5f),FLinearColor(.94f,.94f,.90f));Box(7,At+FVector2f(.75f,-8.f),FVector2f(5.f,3.5f),Gold);
                    }
                }
                // P6 spec §11.3: the delivery lockers out now (public), a brown box with a pale lid while someone holds it.
                for(const FLDLockerView& Locker:Public->PublicMatch.Lockers)
                {
                    const FVector2f At=Spot(Locker.PositionCm);
                    Box(5,At-FVector2f(6.f,6.f),FVector2f(12.f,12.f),Ink);
                    Box(6,At-FVector2f(5.f,5.f),FVector2f(10.f,10.f),FLinearColor(.86f,.56f,.24f));
                    if(Locker.bChanneling){Box(7,At-FVector2f(5.f,5.f),FVector2f(10.f,3.f),FLinearColor(.98f,.92f,.78f));}
                }
                // #198 outer §5: an open camp marker, a marker-violet ring around its camp diamond.
                for(const FLDFieldMarkerView& Marker:Public->PublicMatch.Markers)
                {
                    if(Marker.Phase!=ELDFieldMarkerPhase::Revealed){continue;}
                    const FVector2f At=Spot(Marker.PositionCm);TArray<FVector2f> Ring;
                    for(int32 Step=0;Step<=24;++Step){const float Angle=float(UE_DOUBLE_TWO_PI*Step/24.);Ring.Add(At+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*18.f);}
                    Lines(7,Ring,LDHUD::MarkerColor(),2.5f);
                }
            }
        }
        if(Frame)
        {
            // Currently visible units only, where their world proxy is presented (the frame sample until it is placed), so
            // they glide with the world instead of jumping per frame: contenders in their identity tint, camp members amber.
            const ALDGameState* PublicState=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
            for(const FLDObservedUnitView& Unit:Frame->Units)
            {
                FVector Presented=FVector::ZeroVector;const FVector2f At=Spot(Relay->ReadPresentedLocation(Unit.Handle,Unit.ObservationEpoch,Presented)?Presented:FVector(Unit.FeetCm));
                if(Unit.Kind==ELDObservedUnitKind::Contender)
                {
                    const FString Relation=PublicState?LDHUD::PKRelationText(PublicState->PublicMatch,Own.ContenderId,Unit.ContenderId):FString();
                    if(Relation==LDTerms::PKTeammate||Relation==LDTerms::PKOwnSide)
                    {
                        Diamond(8,At,10.f,Ink);Diamond(9,At,7.f,LDHUD::PaletteColor(Unit.Presentation));
                    }
                    else
                    {
                        Disc(8,At,9.f,Ink);Disc(9,At,6.5f,LDHUD::PaletteColor(Unit.Presentation));
                        if(Relation==LDTerms::PKEnemy)
                        {
                            Lines(10,{At+FVector2f(-2.f,-2.f),At+FVector2f(2.f,2.f)},FLinearColor::White,1.5f);
                            Lines(10,{At+FVector2f(-2.f,2.f),At+FVector2f(2.f,-2.f)},FLinearColor::White,1.5f);
                        }
                    }
                }
                // P6 spec §11.3: the second-hand dealer, a green diamond with a pale core.
                else if(Unit.Kind==ELDObservedUnitKind::CampMember&&Unit.bPeddler)
                {Diamond(8,At,12.f,Ink);Diamond(9,At,9.f,FLinearColor(.32f,.82f,.36f));Diamond(10,At,3.5f,FLinearColor(.95f,.96f,.98f));}
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
        // Actual camera's footprint on Z = OwnFootZ (0 when dead/bodiless) so elevated terrain adds zero parallax offset.
        int32 W=0,H=0;PC->GetViewportSize(W,H);TArray<FVector2f> Footprint;
        const double PlaneZ=MinimapOwnFootPlaneZ(*PC);
        for(FVector2D Pixel:{FVector2D(0,0),FVector2D(W,0),FVector2D(W,H),FVector2D(0,H)})
        {
            FVector Origin,Direction,Hit;
            if(PC->DeprojectScreenPositionToWorld(float(Pixel.X),float(Pixel.Y),Origin,Direction)
                &&LDMinimap::IntersectCameraPlane(Origin,Direction,PlaneZ,Hit))
            {Footprint.Add(Spot(FVector(FMath::Clamp(Hit.X,Region.Min.X,Region.Max.X),FMath::Clamp(Hit.Y,Region.Min.Y,Region.Max.Y),0.)));}
        }
        if(Footprint.Num()==4){const FVector2f First=Footprint[0];Footprint.Add(First);Lines(11,Footprint,FLinearColor(1.f,1.f,1.f,.8f),1.5f);}
        E.PopClip();
        // #97: the map is the shown region itself, turned with the view (a diamond in 跟随镜头) and drawn whole; its rim is
        // the minimap's edge and the frame beyond it stays see-through.
        TArray<FVector2f> Rim={Spot(FVector(Region.Min.X,Region.Min.Y,0.)),Spot(FVector(Region.Max.X,Region.Min.Y,0.)),
            Spot(FVector(Region.Max.X,Region.Max.Y,0.)),Spot(FVector(Region.Min.X,Region.Max.Y,0.))};
        const FVector2f Closing=Rim[0];Rim.Add(Closing);Lines(13,Rim,FLinearColor(.42f,.55f,.62f,.9f),1.5f);
        // Spawn-choice candidate badges (14 px radius) sit above the region rim so edge spawn anchors are never clipped.
        if(const auto* Public=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
            Public&&Public->PublicMatch.Generation==Own.Match&&Own.LifePhase==ELDLifePhase::Dead&&Own.Pacing.SpawnLife==Own.Life)
        {
            int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
            const LDHUD::FSpawnChoiceView Choice=LDHUD::SpawnChoice(Own,bClock,NowMs);
            if(Choice.bVisible)
            {
                const double UiSec=FPlatformTime::Seconds();
                const float Breath=float(.5+.5*FMath::Sin(UiSec*6.));
                const float HaloPhase=float(FMath::Fmod(UiSec*1.25,1.));
                const FLinearColor Gold(FColor(0xFA,0xCC,0x15));
                const FLinearColor Emerald(FColor(0x10,0xB9,0x81));
                const FLinearColor DarkFill(FColor(0x0F,0x17,0x2A));
                const FSlateFontInfo BadgeFont=LDUIFonts::Font(LDUIFonts::ERole::Number,14);
                FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
                const TSharedPtr<FSlateFontMeasure> FontMeasure=Renderer?TSharedPtr<FSlateFontMeasure>(Renderer->GetFontMeasureService()):nullptr;
                const int32 Count=FMath::Min(Choice.Count,Own.Pacing.SpawnCandidateCm.Num());
                const float Diameter=float(LDMinimap::SpawnBadgeRadius*2.);
                for(int32 Index=0;Index<Count;++Index)
                {
                    const FVector2f At=Spot(Own.Pacing.SpawnCandidateCm[Index]);
                    const bool bChosen=Index==Choice.Chosen;
                    if(bChosen)
                    {
                        const float HaloRadius=float(LDMinimap::SpawnBadgeRadius)+10.f*HaloPhase;
                        TArray<FVector2f> Halo;Halo.Reserve(25);
                        for(int32 Step=0;Step<=24;++Step)
                        {
                            const float Angle=float(UE_DOUBLE_TWO_PI*Step/24.);
                            Halo.Add(At+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*HaloRadius);
                        }
                        Lines(14,Halo,Gold.CopyWithNewOpacity((1.f-HaloPhase)*.85f),2.f);
                        Disc(14,At,Diameter+3.f,Ink);
                        Disc(15,At,Diameter,Gold);
                    }
                    else
                    {
                        const float PulseRim=Diameter+2.f*Breath;
                        Disc(14,At,PulseRim+2.f,Ink);
                        Disc(15,At,PulseRim,Emerald.CopyWithNewOpacity(.82f+.18f*Breath));
                        Disc(16,At,Diameter-4.f,DarkFill.CopyWithNewOpacity(.96f));
                    }
                    const FString Label=FString::FromInt(Index+1);
                    const FVector2f Extent=FontMeasure.IsValid()?FontMeasure->Measure(Label,BadgeFont):FVector2f(9.f,16.f);
                    const FLinearColor TextInk=bChosen?FLinearColor(.01f,.015f,.02f,1.f):FLinearColor::White;
                    FSlateDrawElement::MakeText(E,Base+17,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At-Extent*.5f)),
                        Label,BadgeFont,ESlateDrawEffect::None,TextInk);
                }
            }
            else if(!Own.Pacing.SpawnCandidateCm.IsEmpty())
            {
                const int32 Locked=Own.Pacing.SpawnCandidateCm.IsValidIndex(Own.Pacing.SpawnChosen)?Own.Pacing.SpawnChosen:0;
                const FVector2f At=Spot(Own.Pacing.SpawnCandidateCm[Locked]);
                Disc(14,At,14.f,Ink);Disc(15,At,11.f,FLinearColor(FColor(0xFA,0xCC,0x15)));
            }
        }
    }
    E.PopClip();return Base+18;
}
