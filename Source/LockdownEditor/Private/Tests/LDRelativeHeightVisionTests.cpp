#include "Vision/LDVisionDirector.h"
#include "Region/LDMapGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformTime.h"
#include "Art/LDArtPlacement.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRelativeHeightVision, "Lockdown.Rules.StaticScene.RelativeHeightVision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRelativeHeightVision::RunTest(const FString& Parameters)
{
    auto* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("Native cube collision fixture"), Cube)) { return false; }
    const auto Place = [&](FVector At, FVector Scale, bool bTerrain)
    {
        auto* Actor = World->SpawnActor<AStaticMeshActor>();
        auto* Part = Actor->GetStaticMeshComponent();
        Part->SetStaticMesh(Cube); Part->SetCollisionProfileName(TEXT("BlockAll"));
        if (bTerrain)
        {
            Actor->Tags.Add(LDDemoWorld::TerrainTag);
            Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
        }
        else { Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block); }
        Actor->SetActorScale3D(Scale); Actor->SetActorLocation(At);
        return Actor;
    };
    Place(FVector(0,0,-50), FVector(100,100,1), true);
    auto* Platform = Place(FVector(850,0,40), FVector(4,8,.8), true);
    FLDDemoWorldGeometry Geometry;
    Geometry.LevelStepCm = {50,1}; Geometry.SightSampleHeightCm = {120,1};
    auto& Surface = Geometry.Surfaces.AddDefaulted_GetRef(); Surface.Id = TEXT("test_floor");
    Surface.MinCm.X = {-5000,1}; Surface.MinCm.Y = {-5000,1};
    Surface.MaxCm.X = {5000,1}; Surface.MaxCm.Y = {5000,1};
    FLDDemoVisionRangeParameters Range;
    Range.NeutralHeightCm={35,1}; Range.FullHeightCm={120,1};
    Range.UphillRadiusBp=7500; Range.DownhillRadiusBp=12500;
    FString Error;
    FLDSightGeometry Sight;
    const auto Bind = [&](int32 Radius = 1400)
    {
        LDDemoWorld::FTerrain Terrain;
        if (!Terrain.Build(Geometry, Error)) { return false; }
        Terrain.BindScene(*World,FBox(FVector(-5000,-5000,-100),FVector(5000,5000,1500)));
        return Sight.Build(Geometry,{TEXT("height_sight"),1,1},{Radius,1},Range,Error,World,&Terrain);
    };
    if (!TestTrue(TEXT("Current physical scene binds: ")+Error,Bind())) { return false; }
    TestEqual(TEXT("Flat boundary included"),Sight.Trace(FVector(0,1500,0),FVector(1400,1500,0)),ELDSightResult::Visible);
    TestEqual(TEXT("Flat range does not expand"),Sight.Trace(FVector(0,1500,0),FVector(1400.01,1500,0)),ELDSightResult::OutOfRange);
    TestEqual(TEXT("An airborne point cannot gain range from its visual altitude"),Sight.Trace(FVector(0,1500,500),FVector(1600,1500,0)),ELDSightResult::OutOfRange);
    TestEqual(TEXT("A low platform below eye height is visible"),Sight.Trace(FVector(0,0,0),FVector(900,0,80)),ELDSightResult::Visible);
    auto* LateFence = Place(FVector(700,1500,100), FVector(.2,8,2), false);
    LateFence->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    TestEqual(TEXT("A physics-only fence spawned after binding cannot become terrain occlusion"),
        Sight.Trace(FVector(0,1500,0),FVector(1400,1500,0)),ELDSightResult::Visible);
    LateFence->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    TestEqual(TEXT("A late ordinary obstacle still blocks through its LDVision response"),
        Sight.Trace(FVector(0,1500,0),FVector(1400,1500,0)),ELDSightResult::Blocked);
    LateFence->Destroy();
    Platform->SetActorScale3D(FVector(4,8,3)); Platform->SetActorLocation(FVector(850,0,150));
    if (!TestTrue(TEXT("Raised physical platform binds"),Bind())) { return false; }
    TestEqual(TEXT("A tall cliff blocks its top without a tier prohibition"),Sight.Trace(FVector(0,0,0),FVector(900,0,300)),ELDSightResult::Blocked);
    TestEqual(TEXT("High platform expands lower range without a foot blind spot"),Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Visible);
    TestEqual(TEXT("The actual near-foot ledge blind spot is exempted"),Sight.Trace(FVector(900,0,300),FVector(600,0,0)),ELDSightResult::Visible);
    TestEqual(TEXT("Downhill boundary is inclusive"),Sight.Trace(FVector(900,0,300),FVector(-850,0,0)),ELDSightResult::Visible);
    TestEqual(TEXT("Downhill range remains bounded"),Sight.Trace(FVector(900,0,300),FVector(-850.01,0,0)),ELDSightResult::OutOfRange);
    TestEqual(TEXT("Reverse uphill range is smaller"),Sight.Trace(FVector(-700,0,0),FVector(900,0,300)),ELDSightResult::OutOfRange);
    Platform->Tags.Reset();
    if (!TestTrue(TEXT("Replacement without semantic id binds"),Bind())) { return false; }
    TestEqual(TEXT("Actual supporting geometry supplies the same downhill rule"),Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Visible);
    Platform->Tags.Add(LDDemoWorld::TerrainTag);
    auto* Ridge=Place(FVector(0,0,300),FVector(1,8,6),true);
    if (!TestTrue(TEXT("Intermediate ridge binds"),Bind())) { return false; }
    TestEqual(TEXT("High ground cannot see through another mountain"),Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Blocked);
    Ridge->Destroy();
    auto* LowRidge=Place(FVector(-500,0,75),FVector(1,8,1.5),true);
    if(!TestTrue(TEXT("A lower intervening mountain binds"),Bind())){return false;}
    TestEqual(TEXT("Another lower mountain still occludes its far side"),Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Blocked);
    LowRidge->Destroy();
    auto* ComplexCube=DuplicateObject<UStaticMesh>(Cube,GetTransientPackage());
    auto* ComplexBody=DuplicateObject<UBodySetup>(Cube->GetBodySetup(),ComplexCube,
        MakeUniqueObjectName(ComplexCube,UBodySetup::StaticClass(),TEXT("SightComplexBody")));
    ComplexCube->SetBodySetup(ComplexBody);ComplexBody->CollisionTraceFlag=CTF_UseComplexAsSimple;
    ComplexBody->CreatePhysicsMeshes();Platform->GetStaticMeshComponent()->SetStaticMesh(ComplexCube);
    Platform->GetStaticMeshComponent()->RecreatePhysicsState();
    if(!TestTrue(TEXT("Cooked triangle collision binds"),Bind())){return false;}
    TestEqual(TEXT("Triangle collision preserves the departure-only exemption"),Sight.Trace(FVector(900,0,300),FVector(600,0,0)),ELDSightResult::Visible);
    auto* MergedCube=DuplicateObject<UStaticMesh>(Cube,GetTransientPackage());
    auto* MergedBody=DuplicateObject<UBodySetup>(Cube->GetBodySetup(),MergedCube,
        MakeUniqueObjectName(MergedCube,UBodySetup::StaticClass(),TEXT("SightMergedBody")));
    MergedCube->SetBodySetup(MergedBody);MergedBody->CollisionTraceFlag=CTF_UseSimpleAsComplex;
    auto& ExtraBox=MergedBody->AggGeom.BoxElems.AddDefaulted_GetRef();
    ExtraBox.Center=FVector(-337.5,0,-25);ExtraBox.X=25;ExtraBox.Y=100;ExtraBox.Z=50;
    MergedBody->InvalidatePhysicsData();MergedBody->CreatePhysicsMeshes();
    Platform->GetStaticMeshComponent()->SetStaticMesh(MergedCube);Platform->GetStaticMeshComponent()->RecreatePhysicsState();
    if(!TestTrue(TEXT("Merged source platform and lower ridge bind"),Bind())){return false;}
    TestEqual(TEXT("Merging another mountain into the source model cannot reveal its far side"),
        Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Blocked);
    Platform->GetStaticMeshComponent()->SetStaticMesh(ComplexCube);Platform->GetStaticMeshComponent()->RecreatePhysicsState();
    auto* Wall=Place(FVector(0,0,125),FVector(1,8,2.5),false);
    if (!TestTrue(TEXT("Actual lower wall binds"),Bind())) { return false; }
    TestEqual(TEXT("A lower rooted wall still blocks the eye-to-ground ray"),Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Blocked);
    Wall->Tags.Add(TEXT("NoViewBlock"));
    if (!TestTrue(TEXT("Transparent wall binds"),Bind())) { return false; }
    TestEqual(TEXT("NoViewBlock removes only the view obstruction"),Sight.Trace(FVector(900,0,300),FVector(-700,0,0)),ELDSightResult::Visible);
    Wall->Destroy();
    Platform->SetActorLocation(FVector(850,0,275)); Platform->SetActorScale3D(FVector(4,8,.5));
    if (!TestTrue(TEXT("Solid bridge with a lower floor binds"),Bind())) { return false; }
    TestEqual(TEXT("Known feet below a roof keep their actual lower support"),Sight.Trace(FVector(900,0,0),FVector(0,0,0)),ELDSightResult::Visible);
    TestEqual(TEXT("Downhill advantage never reveals a unit directly underneath the deck"),Sight.Trace(FVector(900,0,300),FVector(900,0,0)),ELDSightResult::Blocked);
    TestEqual(TEXT("A lower body cannot see through the ceiling"),Sight.Trace(FVector(900,0,0),FVector(900,0,300)),ELDSightResult::Blocked);

    TArray<uint8> Bits;
    const FVector Eye(900,0,300);
    Sight.Coverage(Eye,Bits);
    int32 Compared=0;
    for(int32 I=0;I<LDObservation::CoverageCells;++I)
    {
        const FVector2D P=Sight.CellCentre(I);
        // This open ground strip has one support layer and no obstacle ambiguity.
        if(P.X>-900&&P.X<-300&&FMath::Abs(P.Y)<350)
        {
            TestEqual(TEXT("Coverage and exact ground authorization agree"),LDObservation::CoverageBit(Bits,I),
                Sight.Trace(Eye,FVector(P,0))==ELDSightResult::Visible); ++Compared;
        }
    }
    TestTrue(TEXT("Coverage comparison contains an actual strip"),Compared>20);
    TArray<double> Times;
    for(int32 I=0;I<32;++I)
    {
        const double Start=FPlatformTime::Seconds();
        Sight.Coverage(Eye+FVector(0,I*.5,0),Bits);
        Times.Add((FPlatformTime::Seconds()-Start)*1000.);
    }
    Times.Sort(); double Total=0; for(double T:Times){Total+=T;}
    AddInfo(FString::Printf(TEXT("HEIGHT_VISION_PERF coverage_ms mean=%.3f p95=%.3f max=%.3f samples=%d"),
        Total/Times.Num(),Times[30],Times.Last(),Times.Num()));

    constexpr double LaneY = 2400, Run = 1200, Rise = 360;
    const FVector Along(Run, 0, Rise);
    const FQuat RampRotation = FRotationMatrix::MakeFromXZ(Along, FVector::UpVector).ToQuat();
    auto* Ramp = Place(FVector(0, LaneY, Rise * .5) - RampRotation.GetAxisZ() * 50,
        FVector(Along.Size() / 100., 8, 1), true);
    Ramp->SetActorRotation(RampRotation);
    Place(FVector(1400, LaneY, Rise * .5), FVector(16, 8, Rise / 100.), true);
    if (!TestTrue(TEXT("A walkable ramp and flush landing bind at the playtest radius"), Bind(1120))) { return false; }

    const auto RampFoot = [&](double X) { return FVector(X, LaneY, (X + Run * .5) * Rise / Run); };
    const FVector LandingTargets[] = {{650,LaneY,Rise}, {850,LaneY,Rise}, {1025,LaneY,Rise}};
    int32 FirstVisible[] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
    for (int32 X = -450; X <= 400; X += 5)
    {
        for (int32 I = 0; I < UE_ARRAY_COUNT(LandingTargets); ++I)
        {
            const bool bVisible = Sight.Trace(RampFoot(X), LandingTargets[I]) == ELDSightResult::Visible;
            if (bVisible && FirstVisible[I] == INDEX_NONE) { FirstVisible[I] = X; }
            TestTrue(TEXT("An open ramp keeps already revealed landing positions visible as the player advances"),
                bVisible || FirstVisible[I] == INDEX_NONE);
        }
    }
    AddInfo(FString::Printf(TEXT("RAMP_REVEAL source_x_cm near=%d middle=%d far=%d"), FirstVisible[0], FirstVisible[1], FirstVisible[2]));
    TestTrue(TEXT("The actual visibility of the landing advances from the lip toward the far side over several steps"),
        FirstVisible[0] != INDEX_NONE && FirstVisible[1] > FirstVisible[0] && FirstVisible[2] > FirstVisible[1]
        && FirstVisible[2] - FirstVisible[0] >= 35);
    TestEqual(TEXT("The lower landing is already visible near its edge from the top of the ramp"),
        Sight.Trace(RampFoot(575), FVector(-650,LaneY,0)), ELDSightResult::Visible);
    TestEqual(TEXT("Distance still hides deeper downhill ground when farther up the ramp"),
        Sight.Trace(RampFoot(575), FVector(-1000,LaneY,0)), ELDSightResult::OutOfRange);
    TestEqual(TEXT("Descending brings that deeper downhill ground into view"),
        Sight.Trace(RampFoot(300), FVector(-1000,LaneY,0)), ELDSightResult::Visible);
    Place(FVector(400,LaneY,400),FVector(.2,8,2),false);
    if (!TestTrue(TEXT("An ordinary wall across the ramp binds"),Bind(1120))) { return false; }
    TestEqual(TEXT("A wall still blocks a landing reached by the higher terrain ray"),
        Sight.Trace(RampFoot(100),LandingTargets[0]),ELDSightResult::Blocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRelativeHeightMap, "Lockdown.Rules.StaticScene.RelativeHeightMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDRelativeHeightMap::RunTest(const FString& Parameters)
{
    auto* World=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(
        LDDemoEntry::WorldMap,FPackageName::GetMapPackageExtension()));
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FString Error;
    if(!TestNotNull(TEXT("Saved formal map"),World)||!TestTrue(TEXT("Approved data: ")+Error,LDArt::LoadDemoWorld(Content,Geometry,Error))){return false;}
    auto* Registry=World->GetSubsystem<ULDMapRegistry>();
    if(!TestTrue(TEXT("Current scene binds: ")+Error,Registry->Rebuild(Error))){return false;}
    FLDSightGeometry Sight,Flat;
    auto Neutral=Content->Get().Parameters.VisionRange;Neutral.UphillRadiusBp=Neutral.DownhillRadiusBp=10000;
    const double Start=FPlatformTime::Seconds();
    if(!TestTrue(TEXT("Height-aware formal map builds: ")+Error,Sight.Build(*Registry->GetGeometry(),Registry->Revision(),
        Content->Get().Parameters.VisionRadiusCm,Content->Get().Parameters.VisionRange,Error,World))
        ||!TestTrue(TEXT("Neutral comparison builds: ")+Error,Flat.Build(*Registry->GetGeometry(),Registry->Revision(),
        Content->Get().Parameters.VisionRadiusCm,Neutral,Error,World))){return false;}
    const double BuildMs=(FPlatformTime::Seconds()-Start)*1000.;
    TArray<FVector> Feet;
    for(const auto& Anchor:Geometry->Anchors)
    {
        const auto* Point=Registry->Resolve(FName(*Anchor.Id),Registry->Generation);
        if(!Point){continue;}
        double Z;int32 Tier;
        if(Registry->GetTerrain().GroundNear(Point->Position,Z,Tier,true))
        {Feet.Add(FVector(Point->Position.X,Point->Position.Y,Z));}
        if(Feet.Num()==24){break;}
    }
    if(!TestTrue(TEXT("Formal map contains supported sampling points"),Feet.Num()>=12)){return false;}
    TArray<double> ActiveTimes,FlatTimes,PointTotals;TArray<uint8> Bits;
    PointTotals.Init(0,Feet.Num());
    for(int32 Pass=0;Pass<3;++Pass){for(int32 P=0;P<Feet.Num();++P)
    {
        const auto& Foot=Feet[P];
        double Before=FPlatformTime::Seconds();Flat.Coverage(Foot,Bits);FlatTimes.Add((FPlatformTime::Seconds()-Before)*1000.);
        Before=FPlatformTime::Seconds();Sight.Coverage(Foot,Bits);ActiveTimes.Add((FPlatformTime::Seconds()-Before)*1000.);
        PointTotals[P]+=ActiveTimes.Last();
    }}
    const auto Summarize=[](TArray<double>& Values,double& Mean,double& P95)
    {Values.Sort();Mean=0;for(double V:Values){Mean+=V;}Mean/=Values.Num();P95=Values[FMath::FloorToInt32((Values.Num()-1)*.95)];};
    double ActiveMean,ActiveP95,FlatMean,FlatP95;
    Summarize(ActiveTimes,ActiveMean,ActiveP95);Summarize(FlatTimes,FlatMean,FlatP95);
    for(int32 P=0;P<Feet.Num();++P)
    {AddInfo(FString::Printf(TEXT("HEIGHT_MAP_POINT foot=%s mean_ms=%.3f"),*Feet[P].ToString(),PointTotals[P]/3.));}
    int32 Traces=0;
    for(const auto& From:Feet){for(const auto& To:Feet)
    {
        const auto Result=Sight.Trace(From,To);
        TestTrue(TEXT("Supported formal points return an ordinary visibility classification"),Result==ELDSightResult::Visible
            ||Result==ELDSightResult::Blocked||Result==ELDSightResult::OutOfRange);
        if(Result==ELDSightResult::Visible)
        {TestTrue(TEXT("Authorized formal target is inside its relative-height radius"),FVector::Dist2D(From,To)<=Sight.RadiusForHeights(From.Z,To.Z)+.001);}
        ++Traces;
    }}
    AddInfo(FString::Printf(TEXT("HEIGHT_MAP_PERF points=%d traces=%d samples=%d build_both_ms=%.2f neutral_mean_ms=%.3f neutral_p95_ms=%.3f height_mean_ms=%.3f height_p95_ms=%.3f"),
        Feet.Num(),Traces,ActiveTimes.Num(),BuildMs,FlatMean,FlatP95,ActiveMean,ActiveP95));
    const auto& D=Sight.QueryDiagnostics();
    AddInfo(FString::Printf(TEXT("HEIGHT_MAP_QUERY terrain_preparations=%llu prop_preparations=%llu departure_calls=%llu scopes=%llu terrain_prep_ms=%.2f prop_prep_ms=%.2f departure_ms=%.2f scope_ms=%.2f"),
        D.TerrainBuilds,D.PropBuilds,D.DepartureCalls,D.Scopes,D.TerrainBuildSeconds*1000.,D.PropBuildSeconds*1000.,D.DepartureSeconds*1000.,D.ScopeSeconds*1000.));

    if(!FParse::Param(FCommandLine::Get(),TEXT("LDReplayPlaytestRamp"))){return true;}
    const auto ReadGround = [&](FVector& Foot)
    {
        int32 Tier=0;
        double Z=0;
        if(!Registry->GetTerrain().GroundNear(Foot,Z,Tier,true)){return false;}
        Foot.Z=Z;
        return true;
    };
    // These sample positions come from the recorded playtest on the saved map.
    constexpr double LandingLaneY=2371;
    const auto VisibleLandingCount = [&](double X) -> int32
    {
        FVector From(X,LandingLaneY,1150);
        if(!ReadGround(From)){return INDEX_NONE;}
        int32 Visible=0;
        for(int32 TargetX:{1760,1900,2050,2250})
        {
            FVector To(TargetX,LandingLaneY,1150);
            if(!ReadGround(To)){return INDEX_NONE;}
            if(Sight.Trace(From,To)==ELDSightResult::Visible){++Visible;}
        }
        return Visible;
    };
    const int32 LandingNear=VisibleLandingCount(1300), LandingMid=VisibleLandingCount(1436), LandingFar=VisibleLandingCount(1550);
    AddInfo(FString::Printf(TEXT("RAMP_PLAYTEST_LANDING visible_targets near=%d mid=%d far=%d of 4"),
        LandingNear,LandingMid,LandingFar));
    TestTrue(TEXT("The recorded ramp reveals one part of the upper landing at a time"),
        LandingNear>0&&LandingNear<LandingMid&&LandingMid<LandingFar&&LandingFar==4);

    FVector ShoulderViewer(2250,2244,1500);
    if(!TestTrue(TEXT("The recorded shoulder viewpoint has authored walkable support"),ReadGround(ShoulderViewer))){return false;}
    Sight.Coverage(ShoulderViewer,Bits);
    const FVector2D Span=Sight.MaxCm()-Sight.MinCm();
    int32 CoveredShoulders=0;
    for(FVector Shoulder: {FVector(2450,2030,1500),FVector(2450,1880,1500),FVector(2530,1880,1500)})
    {
        if(!TestTrue(TEXT("The recorded shoulder point lies on the same elevation"),
            ReadGround(Shoulder)&&FMath::IsNearlyEqual(Shoulder.Z,ShoulderViewer.Z,3.))){return false;}
        TestEqual(TEXT("An open shoulder beside the ramp is visible to gameplay"),
            Sight.Trace(ShoulderViewer,Shoulder),ELDSightResult::Visible);
        const FVector2D Cell=(FVector2D(Shoulder)-Sight.MinCm())/Span;
        const int32 X=FMath::FloorToInt(Cell.X*LDObservation::CoverageWidth);
        const int32 Y=FMath::FloorToInt(Cell.Y*LDObservation::CoverageHeight);
        CoveredShoulders+=LDObservation::CoverageBit(Bits,Y*LDObservation::CoverageWidth+X)?1:0;
    }
    TestEqual(TEXT("The rendered coverage includes the recorded shoulder cells"),CoveredShoulders,3);
    return true;
}
