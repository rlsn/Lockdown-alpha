#include "UI/LDMinimap.h"
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
    // The Demo camera arm (pitch -60, yaw -45): UE's own forward and right axes of it, flattened, are drawn up and right.
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
#endif
