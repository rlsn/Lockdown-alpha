#include "UI/LDMinimap.h"
#include "UI/LDMinimapRaster.h"
#include "Region/LDMapGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Misc/AutomationTest.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A wide box off the origin in a non-square frame, so a centre, aspect or axis mix-up cannot pass by symmetry.
const FBox2D MinimapFixtureFit(FVector2D(-2600.,-900.),FVector2D(3400.,2100.));
const FVector2D MinimapFixtureFrame(240.,180.);
const ELDMinimapOrientation MinimapFixtureModes[]={ELDMinimapOrientation::Camera,ELDMinimapOrientation::Facing,ELDMinimapOrientation::North};
const double MinimapFixtureYaws[]={-45.,0.,30.,90.,135.,-170.,212.};
constexpr double MinimapTolerance=1e-6;
// Ground direction of a yaw, the way UE turns +X towards +Y.
FVector MinimapFixtureDirection(double YawDeg){return FRotator(0.,YawDeg,0.).Vector();}
FVector MinimapFixtureCentre(const LDMinimap::FView& View){return FVector(View.Centre.X,View.Centre.Y,0.);}
// Frame offset from the frame centre of a world offset from the view centre.
FVector2D MinimapFixtureOffset(const LDMinimap::FView& View,const FVector& WorldOffset)
{return LDMinimap::ToFrame(View,MinimapFixtureCentre(View)+WorldOffset)-View.FrameSize*.5;}
bool MinimapStraightUp(const FVector2D& Offset){return Offset.Y<-MinimapTolerance&&FMath::Abs(Offset.X)<=MinimapTolerance;}
bool MinimapStraightRight(const FVector2D& Offset){return Offset.X>MinimapTolerance&&FMath::Abs(Offset.Y)<=MinimapTolerance;}
bool MinimapSameMapping(const LDMinimap::FView& A,const LDMinimap::FView& B)
{
    for(const FVector& Probe:{FVector(-2600.,-900.,0.),FVector(3400.,2100.,0.),FVector(1234.,-567.,0.),FVector(-8000.,9000.,0.)})
    {if(!LDMinimap::ToFrame(A,Probe).Equals(LDMinimap::ToFrame(B,Probe),MinimapTolerance)){return false;}}
    return A.Mode==B.Mode;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapCameraUpTest,"Lockdown.Rules.Minimap.CameraUp",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapCameraUpTest::RunTest(const FString& Parameters)
{
    // A camera arm (pitch -60, yaw -45; any fixed yaw works the same): UE's own forward and right axes of it, flattened, are drawn up and right.
    const FRotator Arm(-60.,-45.,0.);
    const FVector Ahead=Arm.Vector().GetSafeNormal2D(),Right=Arm.Quaternion().GetRightVector().GetSafeNormal2D();
    const LDMinimap::FView View=LDMinimap::MakeView(ELDMinimapOrientation::Camera,MinimapFixtureFit,MinimapFixtureFrame,Arm.Yaw,true,30.);
    if(!TestTrue(TEXT("A camera view over a valid box and frame is valid"),View.IsValid()&&View.Mode==ELDMinimapOrientation::Camera)){return false;}
    TestTrue(TEXT("The fitted box centre is the frame centre"),LDMinimap::ToFrame(View,MinimapFixtureCentre(View)).Equals(MinimapFixtureFrame*.5,MinimapTolerance));
    TestTrue(TEXT("A point ahead of the centre along the camera's ground forward lands straight above it"),MinimapStraightUp(MinimapFixtureOffset(View,Ahead*500.)));
    TestTrue(TEXT("Its distance follows the uniform scale"),FMath::IsNearlyEqual(MinimapFixtureOffset(View,Ahead*500.).Y,-500.*View.Scale,MinimapTolerance));
    TestTrue(TEXT("The camera's screen-right ground direction lands straight to the right, as on screen"),MinimapStraightRight(MinimapFixtureOffset(View,Right*500.)));
    TestTrue(TEXT("Behind the camera is drawn below the centre"),MinimapFixtureOffset(View,-Ahead*500.).Y>MinimapTolerance);
    TestTrue(TEXT("The body's facing and presence never turn the camera view"),
        MinimapSameMapping(View,LDMinimap::MakeView(ELDMinimapOrientation::Camera,MinimapFixtureFit,MinimapFixtureFrame,Arm.Yaw,false,-120.)));
    for(const double Yaw:MinimapFixtureYaws)
    {
        const LDMinimap::FView Turned=LDMinimap::MakeView(ELDMinimapOrientation::Camera,MinimapFixtureFit,MinimapFixtureFrame,Yaw,true,0.);
        TestTrue(TEXT("Every camera yaw draws its ground forward straight up"),Turned.IsValid()&&MinimapStraightUp(MinimapFixtureOffset(Turned,MinimapFixtureDirection(Yaw)*300.)));
    }
    TestFalse(TEXT("A non-finite camera yaw gives no view rather than a guessed angle"),
        LDMinimap::MakeView(ELDMinimapOrientation::Camera,MinimapFixtureFit,MinimapFixtureFrame,std::numeric_limits<double>::quiet_NaN(),true,0.).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapFacingUpTest,"Lockdown.Rules.Minimap.FacingUp",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapFacingUpTest::RunTest(const FString& Parameters)
{
    const LDMinimap::FView Reference=LDMinimap::MakeView(ELDMinimapOrientation::Facing,MinimapFixtureFit,MinimapFixtureFrame,-45.,true,0.);
    for(const double Yaw:MinimapFixtureYaws)
    {
        const FRotator Body(0.,Yaw,0.);
        const LDMinimap::FView View=LDMinimap::MakeView(ELDMinimapOrientation::Facing,MinimapFixtureFit,MinimapFixtureFrame,-45.,true,Yaw);
        if(!TestTrue(TEXT("A living body gives a facing view"),View.IsValid()&&View.Mode==ELDMinimapOrientation::Facing)){return false;}
        TestTrue(TEXT("The body's facing is drawn straight up"),MinimapStraightUp(MinimapFixtureOffset(View,Body.Vector()*400.)));
        TestTrue(TEXT("The body's right-hand side is drawn to the right"),MinimapStraightRight(MinimapFixtureOffset(View,Body.Quaternion().GetRightVector()*400.)));
        TestTrue(TEXT("Turning never rescales the facing view"),FMath::IsNearlyEqual(View.Scale,Reference.Scale,MinimapTolerance*Reference.Scale));
        TestTrue(TEXT("The camera yaw never turns the facing view"),
            MinimapSameMapping(View,LDMinimap::MakeView(ELDMinimapOrientation::Facing,MinimapFixtureFit,MinimapFixtureFrame,77.,true,Yaw)));
    }
    // Dead or unspawned: there is no body facing, so the view is exactly the camera's.
    for(const double Yaw:MinimapFixtureYaws)
    {
        const LDMinimap::FView Bodiless=LDMinimap::MakeView(ELDMinimapOrientation::Facing,MinimapFixtureFit,MinimapFixtureFrame,Yaw,false,Yaw+60.);
        TestTrue(TEXT("Without a living body the facing view falls back to the camera view"),Bodiless.IsValid()&&Bodiless.Mode==ELDMinimapOrientation::Camera
            &&MinimapSameMapping(Bodiless,LDMinimap::MakeView(ELDMinimapOrientation::Camera,MinimapFixtureFit,MinimapFixtureFrame,Yaw,true,0.)));
        TestTrue(TEXT("The fallback draws the camera's ground forward up"),MinimapStraightUp(MinimapFixtureOffset(Bodiless,MinimapFixtureDirection(Yaw)*400.)));
    }
    TestFalse(TEXT("A non-finite body yaw gives no view rather than a guessed angle"),
        LDMinimap::MakeView(ELDMinimapOrientation::Facing,MinimapFixtureFit,MinimapFixtureFrame,-45.,true,std::numeric_limits<double>::quiet_NaN()).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapNotMirroredTest,"Lockdown.Rules.Minimap.NotMirrored",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapNotMirroredTest::RunTest(const FString& Parameters)
{
    // UE is left-handed: seen from above, the right-hand side of a ground direction is its yaw +90°. A true top view keeps
    // that turn screen-clockwise (y down: a positive 2D cross product); a reflection, like the old +Y-up map, flips it.
    for(const ELDMinimapOrientation Mode:MinimapFixtureModes)
    {
        for(const double Yaw:MinimapFixtureYaws)
        {
            const LDMinimap::FView View=LDMinimap::MakeView(Mode,MinimapFixtureFit,MinimapFixtureFrame,Yaw,true,Yaw+31.);
            if(!TestTrue(TEXT("Every orientation gives a valid view"),View.IsValid())){return false;}
            // The reference direction drawn up: the camera's ground forward, the body's facing or world -Y.
            const double Reference=Mode==ELDMinimapOrientation::North?LDMinimap::NorthYawDeg:Mode==ELDMinimapOrientation::Facing?Yaw+31.:Yaw;
            const FVector2D Up=MinimapFixtureOffset(View,MinimapFixtureDirection(Reference)*400.);
            const FVector2D Side=MinimapFixtureOffset(View,MinimapFixtureDirection(Reference+90.)*400.);
            TestTrue(TEXT("The reference direction is drawn straight up"),MinimapStraightUp(Up));
            TestTrue(TEXT("Its right-hand side (yaw +90) is drawn to the right of it"),MinimapStraightRight(Side));
            TestTrue(TEXT("Up to right is a screen-clockwise turn"),FVector2D::CrossProduct(Up,Side)>0.);
            for(double Angle=-180.;Angle<180.;Angle+=22.5)
            {
                const FVector2D From=MinimapFixtureOffset(View,MinimapFixtureDirection(Angle)*250.),To=MinimapFixtureOffset(View,MinimapFixtureDirection(Angle+90.)*250.);
                TestTrue(TEXT("Every ground direction keeps its right-hand side clockwise of it: no reflection"),FVector2D::CrossProduct(From,To)>0.);
                TestTrue(TEXT("Every direction keeps its length: one uniform scale"),FMath::IsNearlyEqual(From.Size(),250.*View.Scale,MinimapTolerance));
            }
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapNorthFixedTest,"Lockdown.Rules.Minimap.NorthFixed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapNorthFixedTest::RunTest(const FString& Parameters)
{
    const LDMinimap::FView View=LDMinimap::MakeView(ELDMinimapOrientation::North,MinimapFixtureFit,MinimapFixtureFrame,-45.,true,30.);
    if(!TestTrue(TEXT("North gives a valid fixed view"),View.IsValid()&&View.Mode==ELDMinimapOrientation::North)){return false;}
    TestTrue(TEXT("Exactly world -Y up and world +X right"),View.Forward.Equals(FVector2D(0.,-1.),0.)&&View.Right.Equals(FVector2D(1.,0.),0.));
    TestTrue(TEXT("World -Y is drawn straight up"),MinimapStraightUp(MinimapFixtureOffset(View,FVector(0.,-400.,0.))));
    TestTrue(TEXT("World +X is drawn to the right, as in the editor top view"),MinimapStraightRight(MinimapFixtureOffset(View,FVector(400.,0.,0.))));
    TestTrue(TEXT("World +Y is drawn straight down"),MinimapFixtureOffset(View,FVector(0.,400.,0.)).Y>MinimapTolerance
        &&FMath::Abs(MinimapFixtureOffset(View,FVector(0.,400.,0.)).X)<=MinimapTolerance);
    for(const double CameraYaw:MinimapFixtureYaws)
    {
        for(const bool bBody:{true,false})
        {
            TestTrue(TEXT("Neither the camera nor the body turns North"),
                MinimapSameMapping(View,LDMinimap::MakeView(ELDMinimapOrientation::North,MinimapFixtureFit,MinimapFixtureFrame,CameraYaw,bBody,15.-2.*CameraYaw)));
        }
    }
    TestTrue(TEXT("North never reads a yaw, so a non-finite one changes nothing"),
        MinimapSameMapping(View,LDMinimap::MakeView(ELDMinimapOrientation::North,MinimapFixtureFit,MinimapFixtureFrame,std::numeric_limits<double>::quiet_NaN(),true,std::numeric_limits<double>::quiet_NaN())));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapRoundTripTest,"Lockdown.Rules.Minimap.RoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapRoundTripTest::RunTest(const FString& Parameters)
{
    // Painting uses ToFrame and every pan, drag and right-click uses ToWorld, so they must be exact inverses in every mode.
    const FVector Points[]={FVector(-2600.,-900.,0.),FVector(3400.,2100.,0.),FVector(400.,600.,0.),FVector(1234.5,-678.9,120.),FVector(-9000.,7000.,-50.)};
    const FVector2D Locals[]={FVector2D(0.,0.),FVector2D(240.,180.),FVector2D(17.,163.),FVector2D(-30.,500.)};
    for(const ELDMinimapOrientation Mode:MinimapFixtureModes)
    {
        for(const double Yaw:MinimapFixtureYaws)
        {
            for(const bool bBody:{true,false})
            {
                const LDMinimap::FView View=LDMinimap::MakeView(Mode,MinimapFixtureFit,MinimapFixtureFrame,Yaw,bBody,Yaw-77.);
                if(!TestTrue(TEXT("Every orientation gives a valid view"),View.IsValid())){return false;}
                for(const FVector& Point:Points)
                {
                    const FVector Back=LDMinimap::ToWorld(View,LDMinimap::ToFrame(View,Point));
                    TestTrue(TEXT("A world point survives ToFrame then ToWorld, on the ground plane"),FVector::Dist2D(Back,Point)<=MinimapTolerance&&Back.Z==0.);
                }
                for(const FVector2D& Local:Locals)
                {
                    TestTrue(TEXT("A frame point, also one off the frame, survives ToWorld then ToFrame"),
                        LDMinimap::ToFrame(View,LDMinimap::ToWorld(View,Local)).Equals(Local,MinimapTolerance));
                }
            }
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapFitsFrameTest,"Lockdown.Rules.Minimap.FitsFrame",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapFitsFrameTest::RunTest(const FString& Parameters)
{
    const double Margin=LDMinimap::EdgeMargin;const FVector2D Room=MinimapFixtureFrame-FVector2D(2.*Margin);const double Diagonal=MinimapFixtureFit.GetSize().Size();
    const FVector Corners[]={FVector(MinimapFixtureFit.Min.X,MinimapFixtureFit.Min.Y,0.),FVector(MinimapFixtureFit.Max.X,MinimapFixtureFit.Min.Y,0.),
        FVector(MinimapFixtureFit.Max.X,MinimapFixtureFit.Max.Y,0.),FVector(MinimapFixtureFit.Min.X,MinimapFixtureFit.Max.Y,0.)};
    auto Inside=[&Margin](const FVector2D& Local)
    {
        return Local.X>=Margin-MinimapTolerance&&Local.Y>=Margin-MinimapTolerance
            &&Local.X<=MinimapFixtureFrame.X-Margin+MinimapTolerance&&Local.Y<=MinimapFixtureFrame.Y-Margin+MinimapTolerance;
    };
    for(const double Yaw:MinimapFixtureYaws)
    {
        // Camera and North: the turned box's bounds stay inside the margin, centred, and reach it on the tighter axis.
        for(const ELDMinimapOrientation Mode:{ELDMinimapOrientation::Camera,ELDMinimapOrientation::North})
        {
            const LDMinimap::FView View=LDMinimap::MakeView(Mode,MinimapFixtureFit,MinimapFixtureFrame,Yaw,false,0.);
            if(!TestTrue(TEXT("A bounds-fitted view is valid"),View.IsValid())){return false;}
            FBox2D Drawn(ForceInit);
            for(const FVector& Corner:Corners)
            {const FVector2D Local=LDMinimap::ToFrame(View,Corner);TestTrue(TEXT("Every fitted box corner is drawn inside the frame margin"),Inside(Local));Drawn+=Local;}
            TestTrue(TEXT("The fit is as large as the frame allows on its tighter axis"),
                FMath::IsNearlyEqual(Drawn.GetSize().X,Room.X,MinimapTolerance)||FMath::IsNearlyEqual(Drawn.GetSize().Y,Room.Y,MinimapTolerance));
            TestTrue(TEXT("The fitted box is centred in the frame"),Drawn.GetCenter().Equals(MinimapFixtureFrame*.5,MinimapTolerance));
        }
        // Facing: the circumscribed circle fits exactly, so the box stays inside however the body turns.
        const LDMinimap::FView Turning=LDMinimap::MakeView(ELDMinimapOrientation::Facing,MinimapFixtureFit,MinimapFixtureFrame,-45.,true,Yaw);
        if(!TestTrue(TEXT("A facing view is valid"),Turning.IsValid())){return false;}
        TestTrue(TEXT("The circumscribed circle meets the margin on the tighter frame axis"),
            FMath::IsNearlyEqual(Diagonal*.5*Turning.Scale,FMath::Min(Room.X,Room.Y)*.5,MinimapTolerance));
        for(const FVector& Corner:Corners){TestTrue(TEXT("Every fitted box corner stays inside while the body turns"),Inside(LDMinimap::ToFrame(Turning,Corner)));}
        for(double Angle=0.;Angle<360.;Angle+=15.)
        {
            const FVector Edge=MinimapFixtureCentre(Turning)+MinimapFixtureDirection(Angle)*(Diagonal*.5);
            TestTrue(TEXT("Every point of the circumscribed circle is drawn inside the frame margin"),Inside(LDMinimap::ToFrame(Turning,Edge)));
        }
    }
    // Degenerate input gives no view instead of an invented scale.
    TestFalse(TEXT("An empty frame gives no view"),LDMinimap::MakeView(ELDMinimapOrientation::Camera,MinimapFixtureFit,FVector2D::ZeroVector,-45.,true,0.).IsValid());
    TestFalse(TEXT("An unset box gives no view"),LDMinimap::MakeView(ELDMinimapOrientation::North,FBox2D(ForceInit),MinimapFixtureFrame,-45.,true,0.).IsValid());
    TestFalse(TEXT("A box without area gives no view"),
        LDMinimap::MakeView(ELDMinimapOrientation::Facing,FBox2D(FVector2D(0.,0.),FVector2D(500.,0.)),MinimapFixtureFrame,-45.,true,0.).IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapWholeGroundTest,"Lockdown.Rules.Minimap.WholeGround",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapWholeGroundTest::RunTest(const FString& Parameters)
{
    // #97: a square ground turned 45 degrees is drawn whole as a diamond; the frame corners beside it are not ground.
    const FBox2D Ground(FVector2D(-5000.,-5000.),FVector2D(5000.,5000.));const FVector2D Frame(240.,240.);
    const LDMinimap::FView View=LDMinimap::MakeView(ELDMinimapOrientation::Camera,Ground,Frame,-45.,true,0.);
    if(!TestTrue(TEXT("The turned camera view is valid"),View.IsValid())){return false;}
    for(const FVector2D Corner:{Ground.Min,Ground.Max,FVector2D(Ground.Min.X,Ground.Max.Y),FVector2D(Ground.Max.X,Ground.Min.Y)})
    {
        const FVector2D Local=LDMinimap::ToFrame(View,FVector(Corner,0.));
        TestTrue(TEXT("Every ground corner is drawn inside the frame margin"),Local.X>=LDMinimap::EdgeMargin-MinimapTolerance
            &&Local.Y>=LDMinimap::EdgeMargin-MinimapTolerance&&Local.X<=Frame.X-LDMinimap::EdgeMargin+MinimapTolerance&&Local.Y<=Frame.Y-LDMinimap::EdgeMargin+MinimapTolerance);
    }
    TestTrue(TEXT("The frame centre shows ground"),LDMinimap::OnGround(View,Frame*.5,Ground));
    for(const FVector2D Corner:{FVector2D(2.,2.),FVector2D(Frame.X-2.,2.),FVector2D(2.,Frame.Y-2.),FVector2D(Frame.X-2.,Frame.Y-2.)})
    {TestFalse(TEXT("A frame corner beside the diamond is not ground, so a press there reaches the world"),LDMinimap::OnGround(View,Corner,Ground));}
    TestFalse(TEXT("An invalid view shows no ground"),LDMinimap::OnGround(LDMinimap::FView(),Frame*.5,Ground));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapRegionsTest,"Lockdown.Rules.Minimap.Regions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapRegionsTest::RunTest(const FString& Parameters)
{
    // The field, the rest room and the final arena side by side on one ground, as the Demo world lays them out.
    const FBox2D Ground(FVector2D(-5000.,-5000.),FVector2D(16000.,5000.));
    const auto Box=[](double X0,double Y0,double X1,double Y1){return FBox2D(FVector2D(X0,Y0),FVector2D(X1,Y1));};
    TArray<FBox2D> Anchors={Box(-4600.,-4600.,4600.,4600.),Box(6500.,-500.,8500.,700.),Box(11450.,-1342.,14550.,1342.)};
    const FBox2D OuterWall=Box(-5000.,4700.,16000.,5000.),FieldWall=Box(5000.,-5000.,5300.,5000.),RestWall=Box(6100.,-1200.,8900.,1200.);
    const FBox2D RestBoundary=Box(9000.,-5000.,9300.,5000.),Ring=Box(12970.,1925.,13810.,2625.),FarAway=Box(-4900.,-4900.,-4800.,-4800.);
    TArray<FBox2D> Out;FString Error;
    if(!TestTrue(TEXT("Three separate regions fit"),LDMinimap::FitRegions(Anchors,{OuterWall,FieldWall,RestWall,RestBoundary,Ring,FarAway},Ground,Out,Error))){AddError(Error);return false;}
    const FBox2D& Field=Out[LDMinimap::FieldRegion];const FBox2D& Rest=Out[LDMinimap::RestRegion];const FBox2D& Final=Out[LDMinimap::FinalRegion];
    TestTrue(TEXT("Every region is valid"),Field.bIsValid&&Rest.bIsValid&&Final.bIsValid);
    TestFalse(TEXT("A wall along the whole ground does not merge the field and the rest room"),Field.Intersect(Rest));
    TestFalse(TEXT("Nor the rest room and the final arena"),Rest.Intersect(Final));
    TestFalse(TEXT("Nor the field and the final arena"),Field.Intersect(Final));
    TestTrue(TEXT("The field keeps its own boundary wall"),Field.IsInsideOrOn(FieldWall.Min)&&Field.IsInsideOrOn(FieldWall.Max));
    TestTrue(TEXT("The rest room keeps its walls"),Rest.IsInsideOrOn(RestWall.Min)&&Rest.IsInsideOrOn(RestWall.Max));
    TestTrue(TEXT("The rest room takes only the reach of its far boundary"),Rest.Max.X>=RestBoundary.Max.X&&Rest.Max.Y<=Anchors[1].Max.Y+LDMinimap::RegionReachCm);
    TestTrue(TEXT("The final arena keeps its whole ring"),Final.IsInsideOrOn(Ring.Min)&&Final.IsInsideOrOn(Ring.Max));
    TestTrue(TEXT("Every region stays on the ground"),Ground.IsInsideOrOn(Field.Min)&&Ground.IsInsideOrOn(Field.Max)&&Ground.IsInsideOrOn(Final.Max));

    TArray<FBox2D> FieldOnly={Anchors[0],FBox2D(ForceInit),FBox2D(ForceInit)};
    TestTrue(TEXT("A world without a rest room or final arena fits only the field"),LDMinimap::FitRegions(FieldOnly,{OuterWall},Ground,Out,Error)
        &&Out[LDMinimap::FieldRegion].bIsValid&&!Out[LDMinimap::RestRegion].bIsValid&&!Out[LDMinimap::FinalRegion].bIsValid);
    TArray<FBox2D> NoAnchors={FBox2D(ForceInit),FBox2D(ForceInit),FBox2D(ForceInit)};
    TestTrue(TEXT("A field without any anchor fits the whole ground"),LDMinimap::FitRegions(NoAnchors,{},Ground,Out,Error)
        &&Out[LDMinimap::FieldRegion].Min==Ground.Min&&Out[LDMinimap::FieldRegion].Max==Ground.Max);
    TArray<FBox2D> Close=Anchors;Close[1]=Box(4000.,-500.,8500.,700.);
    TestFalse(TEXT("Overlapping region anchors are refused"),LDMinimap::FitRegions(Close,{},Ground,Out,Error));
    TestTrue(TEXT("The refusal names both regions"),Error.Contains(TEXT("field"))&&Error.Contains(TEXT("rest room")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapStaticInvalidationTest,"Lockdown.Rules.Minimap.StaticInvalidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapStaticInvalidationTest::RunTest(const FString& Parameters)
{
    LDMinimapRaster::FStaticKey Key;
    Key.Ground=MinimapFixtureFit;Key.FrameSize=MinimapFixtureFrame;Key.Scale=.025;Key.PixelScale=1.5f;
    Key.World.MapId=TEXT("minimap");Key.World.LayoutRevision=3;Key.World.GeometryRevision=7;
    TestTrue(TEXT("An unchanged world and layout reuse the texture"),Key.Matches(Key));
    TestFalse(TEXT("An unbuilt texture key cannot match"),LDMinimapRaster::FStaticKey().Matches(Key));
    auto Changed=Key;Changed.Ground.Min.X-=1.;TestFalse(TEXT("Ground origin invalidates"),Key.Matches(Changed));
    Changed=Key;Changed.Ground.Max.Y+=1.;TestFalse(TEXT("Ground extent invalidates"),Key.Matches(Changed));
    Changed=Key;Changed.Scale*=1.01;TestFalse(TEXT("View scale invalidates"),Key.Matches(Changed));
    Changed=Key;Changed.FrameSize.X+=1.;TestFalse(TEXT("Widget size invalidates"),Key.Matches(Changed));
    Changed=Key;Changed.PixelScale=2.f;TestFalse(TEXT("Display DPI invalidates"),Key.Matches(Changed));
    Changed=Key;++Changed.World.LayoutRevision;TestFalse(TEXT("World layout revision invalidates"),Key.Matches(Changed));
    Changed=Key;++Changed.World.GeometryRevision;TestFalse(TEXT("World geometry revision invalidates"),Key.Matches(Changed));
    Changed=Key;Changed.World.MapId=TEXT("other");TestFalse(TEXT("World identity invalidates"),Key.Matches(Changed));
    for(const double Yaw:MinimapFixtureYaws)
    {
        Changed=Key;
        Changed.Scale=LDMinimap::MakeView(ELDMinimapOrientation::Facing,Key.Ground,Key.FrameSize,-45.,true,Yaw).Scale;
        auto Reference=Changed;
        Reference.Scale=LDMinimap::MakeView(ELDMinimapOrientation::Facing,Key.Ground,Key.FrameSize,-45.,true,0.).Scale;
        TestTrue(TEXT("Turning at constant scale reuses the north-up raster"),Changed.Matches(Reference));
    }

    // Live scene ramp extraction (#341 review): moved, rotated, scaled and copied terrain ramps resolve at their live
    // world transforms and uphill directions rather than frozen source coordinates.
    {
        // Also verify a 6-vertex BuildPrism wedge transformed (moved + rotated 90 deg + scaled 1.5x).
        const FTransform WedgeXform(FRotator(0.,90.,0.),FVector(-800.,600.,0.),FVector(1.5,1.2,1.));
        const TArray<FVector> LocalWedge={
            FVector(0.,-100.,0.),FVector(0.,100.,0.),FVector(300.,100.,0.),FVector(300.,-100.,0.),
            FVector(300.,100.,150.),FVector(300.,-100.,150.)
        };
        TArray<FVector> WorldWedge;
        for(const FVector& V:LocalWedge){WorldWedge.Add(WedgeXform.TransformPosition(V));}
        LDMinimap::FRampOverlay WedgeOut;
        TestTrue(TEXT("Transformed 6-vertex BuildPrism wedge extracts +Y uphill and transformed footprint"),
            LDMinimap::ExtractRampFromConvexVertices(WorldWedge,WedgeOut)
            &&WedgeOut.FootprintCm.Num()==4
            &&FVector2D::DotProduct((WedgeOut.HighMidCm-WedgeOut.LowMidCm).GetSafeNormal(),FVector2D(0.,1.))>.99
            &&FMath::IsNearlyEqual(WedgeOut.LowMidCm.Y,600.,2.)&&FMath::IsNearlyEqual(WedgeOut.HighMidCm.Y,1050.,2.));
    }
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("Basic Cube static mesh loads for live ramp test"),Cube)){return false;}
    const auto WorldValues=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&WorldValues);
    if(!TestNotNull(TEXT("Minimap scene ramp fixture world"),World)){return false;}
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    auto SpawnTerrainSlab=[&](const FVector& Location,const FRotator& Rotation,const FVector& Scale)
    {
        auto* Actor=World->SpawnActor<AStaticMeshActor>();
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->SetActorLocation(Location);
        Actor->SetActorRotation(Rotation);
        Actor->SetActorScale3D(Scale);
        Actor->Tags={LDDemoWorld::TerrainTag};
        Actor->GetStaticMeshComponent()->RecreatePhysicsState();
        return Actor;
    };
    // Flat plateau (pitch 0) must NOT register as a ramp; moved+rotated+scaled ramp (+Y uphill) and copied ramp (-X uphill) both DO.
    SpawnTerrainSlab(FVector(0.,0.,100.),FRotator::ZeroRotator,FVector(6.,6.,1.));
    SpawnTerrainSlab(FVector(1200.,-500.,120.),FRotator(0.,90.,0.)+FRotator(20.,0.,0.),FVector(5.,3.,.2));
    // Note: FRotator(Pitch=20, Yaw=90, Roll=0) tilts local +X upward by +20 deg along world +Y (uphill = +Y)!
    auto* RotatedRamp=World->SpawnActor<AStaticMeshActor>();
    RotatedRamp->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
    RotatedRamp->GetStaticMeshComponent()->SetStaticMesh(Cube);
    RotatedRamp->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    RotatedRamp->SetActorLocation(FVector(2400.,800.,120.));
    RotatedRamp->SetActorRotation(FRotator(20.,180.,0.)); // uphill = -X (copied & rotated instance)
    RotatedRamp->SetActorScale3D(FVector(6.,4.,.2));
    RotatedRamp->Tags={LDDemoWorld::TerrainTag,TEXT("ramp_copy_instance")};
    RotatedRamp->GetStaticMeshComponent()->RecreatePhysicsState();

    TArray<LDMinimap::FRampOverlay> Ramps;
    LDMinimap::CollectSceneRamps(*World,Ramps);
    TestEqual(TEXT("Flat plateau is ignored while moved/rotated/scaled original and copied ramp both resolve"),Ramps.Num(),2);
    if(Ramps.Num()==2)
    {
        const int32 FirstIdx=FMath::Abs(Ramps[0].LowMidCm.X-1200.)<400.?0:1;
        const LDMinimap::FRampOverlay& MovedRotated=Ramps[FirstIdx];
        const LDMinimap::FRampOverlay& Copied=Ramps[1-FirstIdx];
        const FVector2D UphillA=(MovedRotated.HighMidCm-MovedRotated.LowMidCm).GetSafeNormal();
        const FVector2D UphillB=(Copied.HighMidCm-Copied.LowMidCm).GetSafeNormal();
        TestTrue(TEXT("Moved + 90-deg rotated + scaled ramp resolves at (1200, -500) with +Y uphill and 300cm scaled width"),
            FVector2D::Distance((MovedRotated.LowMidCm+MovedRotated.HighMidCm)*.5,FVector2D(1200.,-500.))<30.
            &&FVector2D::DotProduct(UphillA,FVector2D(0.,1.))>.98
            &&FMath::IsNearlyEqual(MovedRotated.WidthCm,300.,5.));
        TestTrue(TEXT("Copied + 180-deg rotated ramp resolves at (2400, 800) with -X uphill and 400cm scaled width"),
            FVector2D::Distance((Copied.LowMidCm+Copied.HighMidCm)*.5,FVector2D(2400.,800.))<30.
            &&FVector2D::DotProduct(UphillB,FVector2D(-1.,0.))>.98
            &&FMath::IsNearlyEqual(Copied.WidthCm,400.,5.));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMinimapCoverageTextureTest,"Lockdown.Rules.Minimap.CoverageTexture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMinimapCoverageTextureTest::RunTest(const FString& Parameters)
{
    TArray<uint8> Bits;Bits.Init(0,LDObservation::CoverageBytes);
    TArray<FColor> Pixels;
    for(int32 I=0;I<LDObservation::CoverageCells;++I){if((I%LDObservation::CoverageWidth)%2==(I/LDObservation::CoverageWidth)%2){LDObservation::SetCoverageBit(Bits,I);}}
    TestTrue(TEXT("Exact coverage bytes build one pixel per cell"),LDMinimapRaster::CoveragePixels(Bits,Pixels)&&Pixels.Num()==LDObservation::CoverageCells);
    const FColor Visible=FColor::Transparent,Hidden=LDMinimapRaster::FogColor().ToFColorSRGB();
    TestTrue(TEXT("Fog leaves the public terrain readable"),Hidden.A>0&&Hidden.A<255);
    for(int32 I=0;I<Pixels.Num();++I)
    {if(!TestTrue(TEXT("Low-bit-first row-major cells retain the exact fog color and alpha"),Pixels[I]==(LDObservation::CoverageBit(Bits,I)?Visible:Hidden))){return false;}}
    Bits.Init(0,LDObservation::CoverageBytes);
    TestTrue(TEXT("A new empty coverage replaces every old cell"),LDMinimapRaster::CoveragePixels(Bits,Pixels));
    for(const FColor& Pixel:Pixels){if(!TestTrue(TEXT("No remembered coverage"),Pixel==Hidden)){return false;}}
    Bits.SetNum(LDObservation::CoverageBytes-1);
    TestFalse(TEXT("Malformed coverage is rejected, never success-shaped"),LDMinimapRaster::CoveragePixels(Bits,Pixels));
    TestTrue(TEXT("Malformed coverage clears the output"),Pixels.IsEmpty());
    const LDMinimapRaster::FCoverageKey Key{12,4,9};
    TestTrue(TEXT("An unchanged authorized frame does not upload again"),Key.Matches(12,4,9));
    TestFalse(TEXT("A new observation serial uploads"),Key.Matches(13,4,9));
    TestFalse(TEXT("A new match cannot reuse coverage"),Key.Matches(12,5,9));
    TestFalse(TEXT("A new life cannot reuse coverage"),Key.Matches(12,4,10));
    TestFalse(TEXT("Cleared coverage cannot reuse an old upload"),LDMinimapRaster::FCoverageKey().Matches(12,4,9));
    FLDWorldView Frame;Frame.MatchGeneration=4;Frame.ObserverLife=9;
    FLDObservedFrameRead Read;Read.Frame=&Frame;Read.Status=ELDObservedViewStatus::Current;
    TestTrue(TEXT("Only current authorized living coverage is readable"),LDMinimapRaster::AuthorizedFrame(true,4,9,Read)==&Frame);
    TestNull(TEXT("Death hides uploaded coverage immediately"),LDMinimapRaster::AuthorizedFrame(false,4,9,Read));
    TestNull(TEXT("Another match is hidden"),LDMinimapRaster::AuthorizedFrame(true,5,9,Read));
    TestNull(TEXT("Another life is hidden"),LDMinimapRaster::AuthorizedFrame(true,4,10,Read));
    Read.Status=ELDObservedViewStatus::Stale;
    TestNull(TEXT("Expired coverage is hidden"),LDMinimapRaster::AuthorizedFrame(true,4,9,Read));
    return true;
}
#endif
