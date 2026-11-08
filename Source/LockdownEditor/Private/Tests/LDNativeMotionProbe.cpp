#include "Tests/LDNativeMotionProbe.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonSerializer.h"

ALDNativeMotionProbeCharacter::ALDNativeMotionProbeCharacter(const FObjectInitializer& Initializer)
    : Super(Initializer.SetDefaultSubobjectClass<ULDNativeMotionProbeMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick=false;
    GetCapsuleComponent()->InitCapsuleSize(40,90);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    auto* Move=GetCharacterMovement();
    Move->SetComponentTickEnabled(false);
    Move->bRunPhysicsWithNoController=true;
    Move->MaxWalkSpeed=1200;
    Move->GroundFriction=0;
    Move->BrakingDecelerationWalking=0;
    Move->MaxStepHeight=35;
    Move->MaxSimulationTimeStep=1;
    Move->bCanWalkOffLedges=false;
}

int32 ULDNativeMotionProbeMovement::BeginTrace(FName Kind, double Seconds, double Fraction) const
{
    if (!Tracing || !UpdatedComponent) { return INDEX_NONE; }
    FLDNativeMotionTraceRow Row;
    Row.Kind=Kind;
    Row.From=Row.To=UpdatedComponent->GetComponentLocation();
    Row.NativeSeconds=Seconds;
    Row.NativeFraction=Fraction;
    Row.GroundedBefore=Row.GroundedAfter=IsMovingOnGround() && CurrentFloor.IsWalkableFloor();
    for (const auto* Scope=UpdatedComponent->GetCurrentScopedMovement(); Scope; Scope=Scope->GetOuterDeferredScope())
    { ++Row.ScopeDepth; }
    return Trace.Add(MoveTemp(Row));
}

void ULDNativeMotionProbeMovement::EndTrace(int32 Index, bool Result) const
{
    if (Index==INDEX_NONE) { return; }
    auto& Row=Trace[Index];
    Row.To=UpdatedComponent->GetComponentLocation();
    Row.Result=Result;
    Row.GroundedAfter=IsMovingOnGround() && CurrentFloor.IsWalkableFloor();
}

bool ULDNativeMotionProbeMovement::PrepareGround()
{
    SetMovementMode(MOVE_Walking);
    FindFloor(UpdatedComponent->GetComponentLocation(),CurrentFloor,false,nullptr);
    SetBaseFromFloor(CurrentFloor);
    return HasValidData() && CurrentFloor.IsWalkableFloor();
}

void ULDNativeMotionProbeMovement::TraceWalking(const FVector& InVelocity, float Seconds)
{
    Trace.Reset();
    Velocity=InVelocity;
    Acceleration=FVector::ZeroVector;
    TGuardValue<bool> Guard(Tracing,true);
    const int32 Index=BeginTrace(TEXT("PerformMovement"),Seconds);
    if (!BeginIsolatedNativeMotionProbe(Seconds,CaptureError)) { EndTrace(Index,false); return; }
    PerformMovement(Seconds);
    EndIsolatedNativeMotionProbe(Captured,CaptureError);
    EndTrace(Index,true);
}

void ULDNativeMotionProbeMovement::TraceUnrecordedMove()
{
    Captured.Reset();
    if (!BeginIsolatedNativeMotionProbe(.1f,CaptureError)) { return; }
    UpdatedComponent->SetWorldLocation(UpdatedComponent->GetComponentLocation()+FVector(10,0,0));
    EndIsolatedNativeMotionProbe(Captured,CaptureError);
}

bool ULDNativeMotionProbeMovement::TraceDash(const FVector& Direction, double DistanceCm, double DurationSeconds,
    float StepSeconds, int32 Steps, FString& Error)
{
    Trace.Reset();
    Velocity=FVector::ZeroVector;
    Acceleration=FVector::ZeroVector;
    if (!ApplyDashRootMotion(Direction,DistanceCm,DurationSeconds,Error)) { return false; }
    for (int32 Step=0; Step<Steps; ++Step) { PerformMovement(StepSeconds); }
    return true;
}

void ULDNativeMotionProbeMovement::PerformMovement(float DeltaSeconds)
{
    SkippedStationary=CheckStationary && TryAdvanceStationaryMovement(DeltaSeconds);
    if (!SkippedStationary) { Super::PerformMovement(DeltaSeconds); }
}

bool ULDNativeMotionProbeMovement::TraceStationary(const FVector& Input)
{
    Captured.Reset();
    TGuardValue<bool> Guard(CheckStationary,true);
    if (!BeginIsolatedNativeMotionProbe(.05f,CaptureError)) { return false; }
    ControlledCharacterMove(Input,.05f);
    EndIsolatedNativeMotionProbe(Captured,CaptureError);
    return SkippedStationary;
}

void ULDNativeMotionProbeMovement::MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
    const int32 Index=BeginTrace(TEXT("MoveAlongFloor"),DeltaSeconds);
    Super::MoveAlongFloor(InVelocity,DeltaSeconds,OutStepDownResult);
    EndTrace(Index,true);
}

bool ULDNativeMotionProbeMovement::StepUp(const FVector& GravDir, const FVector& Delta,
    const FHitResult& InHit, FStepDownResult* OutStepDownResult)
{
    const int32 Index=BeginTrace(TEXT("StepUp"));
    const int32 FirstChild=Trace.Num();
    const bool Result=Super::StepUp(GravDir,Delta,InHit,OutStepDownResult);
    EndTrace(Index,Result);
    if (Index!=INDEX_NONE && !Result)
    {
        for (int32 Child=FirstChild; Child<Trace.Num(); ++Child) { Trace[Child].DiscardedStepUp=true; }
    }
    return Result;
}

float ULDNativeMotionProbeMovement::SlideAlongSurface(const FVector& Delta, float Time,
    const FVector& Normal, FHitResult& Hit, bool HandleImpact)
{
    const int32 Index=BeginTrace(TEXT("SlideAlongSurface"),0,Time);
    const float Result=Super::SlideAlongSurface(Delta,Time,Normal,Hit,HandleImpact);
    EndTrace(Index,Result>0);
    return Result;
}

bool ULDNativeMotionProbeMovement::SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& Rotation,
    bool Sweep, FHitResult& Hit, ETeleportType Teleport)
{
    const int32 Index=BeginTrace(TEXT("SafeMove"));
    const bool Result=Super::SafeMoveUpdatedComponent(Delta,Rotation,Sweep,Hit,Teleport);
    EndTrace(Index,Result);
    if (Index!=INDEX_NONE) { Trace[Index].NativeFraction=Hit.Time; }
    return Result;
}

bool ULDNativeMotionProbeMovement::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation,
    bool Sweep, FHitResult* Hit, ETeleportType Teleport)
{
    const int32 Index=BeginTrace(TEXT("Move"));
    const bool Result=Super::MoveUpdatedComponentImpl(Delta,Rotation,Sweep,Hit,Teleport);
    EndTrace(Index,Result);
    if (Index!=INDEX_NONE && Hit) { Trace[Index].NativeFraction=Hit->Time; }
    return Result;
}

bool ULDNativeMotionProbeMovement::ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation)
{
    const int32 Index=BeginTrace(TEXT("ResolvePenetration"));
    const bool Result=Super::ResolvePenetrationImpl(Adjustment,Hit,Rotation);
    EndTrace(Index,Result);
    return Result;
}

FVector ULDNativeMotionProbeMovement::GetLedgeMove(const FVector& OldLocation, const FVector& Delta, const FFindFloorResult& OldFloor) const
{
    const int32 Index=BeginTrace(TEXT("GetLedgeMove"));
    const FVector Result=Super::GetLedgeMove(OldLocation,Delta,OldFloor);
    EndTrace(Index,!Result.IsZero());
    return Result;
}

bool ULDNativeMotionProbeMovement::CheckFall(const FFindFloorResult& OldFloor, const FHitResult& Hit,
    const FVector& Delta, const FVector& OldLocation, float RemainingTime, float TimeTick, int32 Iterations, bool MustJump)
{
    const int32 Index=BeginTrace(TEXT("CheckFall"),TimeTick);
    const bool Result=Super::CheckFall(OldFloor,Hit,Delta,OldLocation,RemainingTime,TimeTick,Iterations,MustJump);
    EndTrace(Index,Result);
    return Result;
}

void ULDNativeMotionProbeMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    const int32 Index=BeginTrace(TEXT("MovementMode"));
    Super::OnMovementModeChanged(PreviousMovementMode,PreviousCustomMode);
    EndTrace(Index,true);
}

void ULDNativeMotionProbeMovement::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult,
    bool UseCachedLocation, const FHitResult* DownwardSweepResult) const
{
    const int32 Index=&OutFloorResult==&CurrentFloor?BeginTrace(TEXT("CurrentFloor")):INDEX_NONE;
    Super::FindFloor(CapsuleLocation,OutFloorResult,UseCachedLocation,DownwardSweepResult);
    EndTrace(Index,OutFloorResult.IsWalkableFloor());
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FNativeWorld
    {
        UWorld* World=nullptr;
        FNativeWorld()
        {
            const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true).CreateFXSystem(false);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
            if (World)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                World->InitializeActorsForPlay(FURL(),false);
                World->BeginPlay();
                World->GetWorldSettings()->NotifyBeginPlay();
                for (int32 Tick=0; Tick<8; ++Tick) { World->Tick(LEVELTICK_TimeOnly,.25f); }
            }
        }
        ~FNativeWorld()
        {
            if (World)
            {
                World->EndPlay(EEndPlayReason::Destroyed);
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
        }
        UBoxComponent* Box(FVector Centre, FVector Extent, bool Walkable=true)
        {
            auto* Actor=World->SpawnActor<AActor>();
            if (!Actor) { return nullptr; }
            auto* Shape=NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Shape);
            Actor->AddInstanceComponent(Shape);
            Shape->SetWorldLocation(Centre);
            Shape->SetBoxExtent(Extent);
            Shape->SetMobility(EComponentMobility::Static);
            Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Shape->SetCollisionObjectType(ECC_WorldStatic);
            Shape->SetCollisionResponseToAllChannels(ECR_Block);
            Shape->CanCharacterStepUpOn=ECB_Yes;
            Shape->SetCanEverAffectNavigation(false);
            if (!Walkable) { Shape->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable,0)); }
            Shape->RegisterComponentWithWorld(World);
            return Shape;
        }
        ALDNativeMotionProbeCharacter* Body(FVector Foot)
        {
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Result=World->SpawnActor<ALDNativeMotionProbeCharacter>(Foot+FVector(0,0,92.15),FRotator::ZeroRotator,Spawn);
            if (Result) { Result->GetCharacterMovement()->SetComponentTickEnabled(false); }
            return Result;
        }
        UStaticMeshComponent* ConvexFloor(bool Ramp=false)
        {
            auto* Actor=World->SpawnActor<AActor>();
            if (!Actor) { return nullptr; }
            auto* Mesh=NewObject<UStaticMesh>();
            Mesh->CreateBodySetup();
            auto* Setup=Mesh->GetBodySetup();
            FKConvexElem Hull;
            for (double X : {Ramp?0.:-500.,Ramp?220.:500.})
            {
                for (double Y : {-500.,500.})
                {
                    for (double Z : {-20.,Ramp?X*50./220.:0.}) { Hull.VertexData.Add(FVector(X,Y,Z)); }
                }
            }
            Hull.UpdateElemBox();
            Setup->AggGeom.ConvexElems.Add(MoveTemp(Hull));
            Setup->CollisionTraceFlag=CTF_UseSimpleAsComplex;
            Setup->CreatePhysicsMeshes();
            auto* Shape=NewObject<UStaticMeshComponent>(Actor);
            Actor->SetRootComponent(Shape);
            Actor->AddInstanceComponent(Shape);
            Shape->SetStaticMesh(Mesh);
            Shape->SetMobility(EComponentMobility::Static);
            Shape->SetCollisionProfileName(TEXT("BlockAll"));
            Shape->CanCharacterStepUpOn=ECB_No;
            Shape->SetCanEverAffectNavigation(false);
            Shape->RegisterComponentWithWorld(World);
            return Shape;
        }
    };

    bool SaveTrace(FAutomationTestBase& Test, const TCHAR* Case, const ULDNativeMotionProbeMovement& Move, bool Passed)
    {
        Passed&=Test.TestTrue(TEXT("Production native recorder accepted the real scope and rollback"),
            Move.Captured.IsValid() && Move.CaptureError.IsEmpty());
        if (!Move.CaptureError.IsEmpty()) { Test.AddError(Move.CaptureError); }
        auto Report=MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("scope"),TEXT("Isolated native engine hook/rollback probe, not production CT06 segments or provider fences."));
        Report->SetStringField(TEXT("case"),Case);
        Report->SetBoolField(TEXT("passed"),Passed);
        Report->SetNumberField(TEXT("native_world_seconds"),Move.GetWorld()->GetTimeSeconds());
        Report->SetNumberField(TEXT("movement_mode"),uint8(Move.MovementMode));
        Report->SetStringField(TEXT("capture_error"),Move.CaptureError);
        if (Move.Captured)
        {
            Report->SetNumberField(TEXT("committed_records"),Move.Captured->Records.Num());
            Report->SetNumberField(TEXT("reverted_trials"),Move.Captured->RevertedTrials);
            TArray<TSharedPtr<FJsonValue>> Records;
            for (const auto& Record : Move.Captured->Records)
            {
                auto Row=MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("kind"),uint8(Record.Kind));
                Row->SetStringField(TEXT("from"),Record.FromFootCm.ToString());
                Row->SetStringField(TEXT("to"),Record.ToFootCm.ToString());
                Row->SetNumberField(TEXT("start_seconds"),Record.StartServerSeconds);
                Row->SetNumberField(TEXT("end_seconds"),Record.EndServerSeconds);
                Row->SetBoolField(TEXT("grounded"),Record.Support.Grounded);
                Records.Add(MakeShared<FJsonValueObject>(Row));
            }
            Report->SetArrayField(TEXT("committed"),Records);
        }
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (const auto& Event : Move.Trace)
        {
            auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("kind"),Event.Kind.ToString());
            Row->SetStringField(TEXT("from"),Event.From.ToString());
            Row->SetStringField(TEXT("to"),Event.To.ToString());
            Row->SetNumberField(TEXT("native_seconds"),Event.NativeSeconds);
            Row->SetNumberField(TEXT("native_fraction"),Event.NativeFraction);
            Row->SetNumberField(TEXT("scope_depth"),Event.ScopeDepth);
            Row->SetBoolField(TEXT("result"),Event.Result);
            Row->SetBoolField(TEXT("discarded_step_up"),Event.DiscardedStepUp);
            Row->SetBoolField(TEXT("grounded_before"),Event.GroundedBefore);
            Row->SetBoolField(TEXT("grounded_after"),Event.GroundedAfter);
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Report->SetArrayField(TEXT("trace"),Rows);
        const FString Directory=FPaths::ProjectDir()/TEXT("Artifacts")/TEXT("CT06NativeProbe")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FString Json;
        return Test.TestTrue(TEXT("Persist isolated native trace"),
            IFileManager::Get().MakeDirectory(*Directory,true)
            && FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json,*(Directory/FString(Case).Append(TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) && Passed;
    }

    ULDNativeMotionProbeMovement* Prepare(FAutomationTestBase& Test, ALDNativeMotionProbeCharacter* Body)
    {
        if (!Test.TestNotNull(TEXT("Native transient character"),Body)) { return nullptr; }
        auto* Movement=CastChecked<ULDNativeMotionProbeMovement>(Body->GetCharacterMovement());
        if (!Test.TestTrue(TEXT("Native CMC has real walkable support"),Movement->PrepareGround())) { return nullptr; }
        return Movement;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeStationaryProbe, "Lockdown.Rules.DemoMotionProbe.Stationary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeStationaryProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)
        || !TestNotNull(TEXT("Static floor"),Fixture.ConvexFloor())) { return false; }
    auto* Move=Prepare(*this,Fixture.Body(FVector::ZeroVector));
    if (!Move) { return false; }
    TestFalse(TEXT("Newly grounded body must first run native CMC"),Move->TraceStationary());
    Move->TraceWalking(FVector::ZeroVector,.05f);
    const auto Full=Move->Captured;
    if (!TestNotNull(TEXT("Full idle batch"),Full.Get())) { return false; }
    TestTrue(TEXT("Settled body skips native physics"),Move->TraceStationary());
    const auto Idle=Move->Captured;
    if (!TestNotNull(TEXT("Skipped idle batch"),Idle.Get())) { return false; }
    TestTrue(TEXT("Full and skipped batches have identical stationary positions and support"),
        Full->StartFootCm==Idle->StartFootCm && Full->EndFootCm==Idle->EndFootCm
        && Full->InitialSupport.Grounded==Idle->InitialSupport.Grounded
        && Full->FinalSupport.Grounded==Idle->FinalSupport.Grounded
        && Full->FinalSupport.Floor==Idle->FinalSupport.Floor);
    TestTrue(TEXT("Both paths submit no fabricated movement, adjustment or support transition"),
        Full->Records.IsEmpty() && Idle->Records.IsEmpty() && Idle->PreviousSequence==Idle->ThroughSequence
        && Idle->Step.EndServerSeconds>Idle->Step.StartServerSeconds && Move->CaptureError.IsEmpty());
    TestFalse(TEXT("New steering wakes the body in the same step"),Move->TraceStationary({1,0,0}));
    TestTrue(TEXT("Steering moved through native CMC"),Move->Velocity.X>0);
    Move->StopMovementImmediately();
    TestFalse(TEXT("Just stopped velocity still requires a native update"),Move->TraceStationary());
    TestTrue(TEXT("Following settled step skips again"),Move->TraceStationary());
    Move->bForceNextFloorCheck=true;
    TestFalse(TEXT("Explicit floor invalidation wakes the body"),Move->TraceStationary());
    TestTrue(TEXT("Revalidated floor sleeps again"),Move->TraceStationary());
    Move->AddImpulse({100,0,0},true);
    TestFalse(TEXT("Pending impulse cannot be skipped"),Move->TraceStationary());
    Move->StopMovementImmediately();
    Move->TraceWalking(FVector::ZeroVector,.05f);
    Move->AddForce({100,0,0});
    TestFalse(TEXT("Pending force cannot be skipped"),Move->TraceStationary());
    Move->StopMovementImmediately();
    Move->TraceWalking(FVector::ZeroVector,.05f);
    FString Error;
    TestTrue(TEXT("Committed displacement source"),Move->TraceDash({1,0,0},100,.2,0,0,Error));
    TestFalse(TEXT("Pending root motion/knockback cannot be skipped"),Move->TraceStationary());
    TestTrue(TEXT("Root motion produced a witnessed displacement"),
        Move->Captured && Move->Captured->Records.ContainsByPredicate([](const auto& Record) { return Record.RootMotion; }));
    Move->Launch({0,0,200});
    TestFalse(TEXT("Pending launch cannot be skipped"),Move->TraceStationary());
    Move->SetMovementMode(MOVE_None);
    TestFalse(TEXT("Disabled/dead movement cannot be skipped"),Move->TraceStationary());
    Move->StopMovementImmediately();
    Move->UpdatedComponent->SetWorldLocation({0,0,92.15},false,nullptr,ETeleportType::TeleportPhysics);
    TestTrue(TEXT("Respawn support prepared"),Move->PrepareGround());
    TestFalse(TEXT("Repositioned respawn must run full CMC"),Move->TraceStationary());
    TestTrue(TEXT("Respawn settles normally"),Move->TraceStationary());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeStationarySupportProbe, "Lockdown.Rules.DemoMotionProbe.StationarySupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeStationarySupportProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)) { return false; }
    auto* Floor=Fixture.ConvexFloor(true);
    if (!TestNotNull(TEXT("Ramp floor"),Floor)) { return false; }
    auto* Move=Prepare(*this,Fixture.Body({100,0,100.*50./220.}));
    if (!Move) { return false; }
    Move->TraceWalking(FVector::ZeroVector,.05f);
    Move->TraceWalking(FVector::ZeroVector,.05f);
    TestTrue(TEXT("Static walkable slope can sleep"),Move->TraceStationary());
    Floor->SetMobility(EComponentMobility::Movable);
    TestFalse(TEXT("Moving base requires native based movement"),Move->TraceStationary());
    Floor->SetMobility(EComponentMobility::Static);
    Move->TraceWalking(FVector::ZeroVector,.05f);
    Move->CurrentFloor.HitResult.bStartPenetrating=true;
    TestFalse(TEXT("Pending floor penetration requires native resolution"),Move->TraceStationary());
    Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Disabled support cannot keep a stationary floor alive"),Move->TraceStationary());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeSlideProbe, "Lockdown.Rules.DemoMotionProbe.Slide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeSlideProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)) { return false; }
    if (!TestNotNull(TEXT("Ground box"),Fixture.Box({0,0,-10},{500,500,10}))
        || !TestNotNull(TEXT("Blocking wall"),Fixture.Box({110,0,200},{10,400,200}))) { return false; }
    auto* Move=Prepare(*this,Fixture.Body(FVector::ZeroVector));
    if (!Move) { return false; }
    Move->TraceWalking({600,300,0},.25f);
    const auto* Floor=Move->Trace.FindByPredicate([](const auto& Row) { return Row.Kind==TEXT("MoveAlongFloor"); });
    const auto* Slide=Move->Trace.FindByPredicate([](const auto& Row) { return Row.Kind==TEXT("SlideAlongSurface") && Row.Result; });
    TArray<const FLDNativeMotionTraceRow*> Horizontal;
    for (const auto& Row : Move->Trace)
    {
        if (Row.Kind==TEXT("Move") && !Row.DiscardedStepUp && FVector::DistSquared2D(Row.From,Row.To)>1)
        { Horizontal.Add(&Row); }
    }
    bool Bent=false;
    for (int32 Index=1; Index<Horizontal.Num(); ++Index)
    {
        const auto A=(Horizontal[Index-1]->To-Horizontal[Index-1]->From).GetSafeNormal2D();
        const auto B=(Horizontal[Index]->To-Horizontal[Index]->From).GetSafeNormal2D();
        Bent|=FVector::DotProduct(A,B)<.99;
    }
    bool Passed=TestTrue(TEXT("Real wall contact creates a committed bent native path, not one start/end chord"),Bent);
    Passed&=TestTrue(TEXT("Native walking supplies its actual time slice"),Floor && Floor->NativeSeconds==.25);
    Passed&=TestTrue(TEXT("Native slide supplies a remaining-time fraction"),Slide && Slide->NativeFraction>0 && Slide->NativeFraction<=1);
    Passed&=TestTrue(TEXT("Final body still has actual walkable support"),Move->CurrentFloor.IsWalkableFloor());
    if (Move->Captured)
    {
        const auto* NativeFirst=Move->Trace.FindByPredicate([](const auto& Row) { return Row.Kind==TEXT("Move") && Row.Result; });
        const auto* RecordedFirst=Move->Captured->Records.FindByPredicate([](const auto& Row) { return Row.Kind==ELDNativeMotionRecordKind::Movement; });
        Passed&=TestTrue(TEXT("First impact retains its native hit-time fraction, not the frame endpoint"),
            NativeFirst && RecordedFirst && FMath::IsNearlyEqual(RecordedFirst->EndServerSeconds,.25*NativeFirst->NativeFraction,.000000001));
        int32 Movements=0;
        for (const auto& Record : Move->Captured->Records) { Movements+=Record.Kind==ELDNativeMotionRecordKind::Movement?1:0; }
        Passed&=TestTrue(TEXT("Production batch retains multiple committed movement pieces"),Movements>=2);
    }
    return SaveTrace(*this,TEXT("slide"),*Move,Passed) && Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeFloorGapProbe, "Lockdown.Rules.DemoMotionProbe.RampSeamSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeFloorGapProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)
        || !TestNotNull(TEXT("Registered-height convex floor"),Fixture.ConvexFloor())) { return false; }
    auto* Body=Fixture.Body({0,0,100});
    if (!TestNotNull(TEXT("Falling native body"),Body)) { return false; }
    auto* Move=CastChecked<ULDNativeMotionProbeMovement>(Body->GetCharacterMovement());
    Move->SetMovementMode(MOVE_Falling);
    for (int32 Step=0; Step<40; ++Step) { Move->PerformMovement(.05f); }
    AddInfo(FString::Printf(TEXT("Settled foot=%.6f native distance=%.6f impactZ=%.6f"),
        Move->GetActorFeetLocation().Z,Move->CurrentFloor.FloorDist,Move->CurrentFloor.HitResult.ImpactPoint.Z));
    bool Passed=TestTrue(TEXT("Landing retains native walkable support"),Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor())
        && TestTrue(TEXT("A settled body fits the strict registered ground gap"),
            Move->GetActorFeetLocation().Z>=0 && Move->GetActorFeetLocation().Z<=UCharacterMovementComponent::MAX_FLOOR_DIST);
    auto* Ramp=Fixture.ConvexFloor(true);
    if (!TestNotNull(TEXT("Adjacent registered-height ramp"),Ramp)) { return false; }
    Body->SetActorLocation(FVector(-1,0,190));
    Move->SetMovementMode(MOVE_Falling);
    for (int32 Step=0; Step<40; ++Step) { Move->PerformMovement(.05f); }
    AddInfo(FString::Printf(TEXT("Ramp seam foot=%.6f native distance=%.6f impactZ=%.6f"),
        Move->GetActorFeetLocation().Z,Move->CurrentFloor.FloorDist,Move->CurrentFloor.HitResult.ImpactPoint.Z));
    Passed&=TestTrue(TEXT("The capsule rests on the ramp while its foot centre remains above flat ground"),
        Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor()
        && Move->CurrentFloor.HitResult.GetComponent()==Ramp && Move->GetActorFeetLocation().X<0
        && Move->GetActorFeetLocation().Z>UCharacterMovementComponent::MAX_FLOOR_DIST
        && Move->CurrentFloor.FloorDist<=UCharacterMovementComponent::MAX_FLOOR_DIST);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeStepUpProbe, "Lockdown.Rules.DemoMotionProbe.StepUpRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeStepUpProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)) { return false; }
    if (!TestNotNull(TEXT("Ground box"),Fixture.Box({0,0,-10},{500,500,10}))
        || !TestNotNull(TEXT("Step with native unwalkable top"),Fixture.Box({135,0,10},{65,100,10},false))) { return false; }
    auto* Move=Prepare(*this,Fixture.Body(FVector::ZeroVector));
    if (!Move) { return false; }
    Move->TraceWalking({600,0,0},.25f);
    const auto* Rejected=Move->Trace.FindByPredicate([](const auto& Row)
    { return Row.Kind==TEXT("StepUp") && !Row.Result; });
    int32 TrialMoves=0;
    for (const auto& Row : Move->Trace)
    { TrialMoves+=Row.Kind==TEXT("Move") && Row.DiscardedStepUp && FVector::DistSquared(Row.From,Row.To)>.01 ? 1 : 0; }
    bool Passed=TestTrue(TEXT("Rejected native StepUp performed nonempty trial movement"),Rejected && TrialMoves>=2);
    Passed&=TestTrue(TEXT("Native deferred rollback restores the exact step-up starting transform"),
        Rejected && Rejected->From.Equals(Rejected->To,0.));
    Passed&=TestTrue(TEXT("Rolled-back trials do not become a successful step"),
        Move->GetActorFeetLocation().X<40 && Move->CurrentFloor.IsWalkableFloor());
    Passed&=TestTrue(TEXT("Production records discard the successful trial sweeps after native StepUp rejection"),
        Move->Captured && Move->Captured->RevertedTrials>0
        && !Move->Captured->Records.ContainsByPredicate([&](const auto& Row)
        { return Row.ToFootCm.X>Move->GetActorFeetLocation().X+.001 || Row.ToFootCm.Z>3; }));
    return SaveTrace(*this,TEXT("step_up_rollback"),*Move,Passed) && Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeLedgeProbe, "Lockdown.Rules.DemoMotionProbe.LedgeRevert",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeLedgeProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)) { return false; }
    if (!TestNotNull(TEXT("Finite narrow ground"),Fixture.Box({-100,0,-10},{100,45,10}))) { return false; }
    auto* Move=Prepare(*this,Fixture.Body({-100,0,0}));
    if (!Move) { return false; }
    Move->TraceWalking({600,0,0},.25f);
    const auto* Whole=Move->Trace.FindByPredicate([](const auto& Row) { return Row.Kind==TEXT("PerformMovement"); });
    const auto* Fall=Move->Trace.FindByPredicate([](const auto& Row) { return Row.Kind==TEXT("CheckFall") && !Row.Result; });
    const auto* Ledge=Move->Trace.FindByPredicate([](const auto& Row) { return Row.Kind==TEXT("GetLedgeMove") && !Row.Result; });
    bool SawTrial=false;
    bool SawReverseHook=false;
    for (const auto& Row : Move->Trace)
    {
        if (Row.Kind!=TEXT("Move")) { continue; }
        SawTrial|=Row.To.X>Row.From.X+10;
        SawReverseHook|=Row.To.X<Row.From.X-10;
    }
    bool Passed=TestTrue(TEXT("Native ground-loss checks explicitly reject walking off the ledge"),Fall && Ledge);
    Passed&=TestTrue(TEXT("The real walking trial is reverted to its original pose"),
        SawTrial && Whole && Whole->From.Equals(Whole->To,.001));
    Passed&=TestFalse(TEXT("CMC MoveUpdatedComponentImpl alone does not observe nonvirtual RevertMove"),SawReverseHook);
    Passed&=TestTrue(TEXT("Reverted support returns to the native old floor"),Move->CurrentFloor.IsWalkableFloor());
    Passed&=TestTrue(TEXT("Production rollback witness removes the forward trial instead of inventing a return segment"),
        Move->Captured && Move->Captured->RevertedTrials>0
        && Move->Captured->StartFootCm.Equals(Move->Captured->EndFootCm,.001)
        && Move->Captured->Records.IsEmpty());
    return SaveTrace(*this,TEXT("ledge_revert"),*Move,Passed) && Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeSupportProbe, "Lockdown.Rules.DemoMotionProbe.SupportTransitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeSupportProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)) { return false; }
    if (!TestNotNull(TEXT("Ground with an actual edge"),Fixture.Box({-100,0,-10},{100,45,10}))) { return false; }
    auto* Move=Prepare(*this,Fixture.Body({-100,0,0}));
    if (!Move) { return false; }
    Move->bCanWalkOffLedges=true;
    Move->TraceWalking({600,0,0},.25f);
    bool Passed=TestTrue(TEXT("Native walking off the edge confirms loss of support"),
        Move->IsFalling() && !Move->CurrentFloor.IsWalkableFloor());
    Passed&=TestTrue(TEXT("Native floor or mode update exposes the support transition"),
        Move->Trace.ContainsByPredicate([](const auto& Row) { return Row.GroundedBefore && !Row.GroundedAfter; }));
    Passed&=TestTrue(TEXT("Production record preserves confirmed support loss"),
        Move->Captured && Move->Captured->Records.ContainsByPredicate([](const auto& Row)
        { return Row.Kind==ELDNativeMotionRecordKind::Support && !Row.Support.Grounded; }));
    Passed&=SaveTrace(*this,TEXT("support_lost"),*Move,Passed);
    const FVector Before=Move->GetActorFeetLocation();
    if (!TestNotNull(TEXT("New ground below the falling body"),Fixture.Box({100,0,-10},{100,120,10}))) { return false; }
    Move->TraceWalking(FVector::ZeroVector,.2f);
    Passed&=TestTrue(TEXT("Real landing restores walkable native support"),
        Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor());
    Passed&=TestTrue(TEXT("A support transition does not require an XY position change"),
        FVector::DistSquared2D(Before,Move->GetActorFeetLocation())<.000001);
    Passed&=TestTrue(TEXT("Landing has a native support/mode witness"),
        Move->Trace.ContainsByPredicate([](const auto& Row) { return !Row.GroundedBefore && Row.GroundedAfter; }));
    Passed&=TestTrue(TEXT("Production record preserves support restoration with no XY movement"),
        Move->Captured && Move->Captured->Records.ContainsByPredicate([](const auto& Row)
        { return Row.Kind==ELDNativeMotionRecordKind::Support && Row.Support.Grounded; }));
    return SaveTrace(*this,TEXT("support_regained"),*Move,Passed) && Passed;
}

// D19: the dash is native root motion, so blocking geometry stops it at the farthest legal capsule position instead of
// passing through or teleporting; an immediately blocked heading is refused before anything is paid.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeDashProbe, "Lockdown.Rules.DemoMotionProbe.DashStopsAtWall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeDashProbe::RunTest(const FString& Parameters)
{
    // Probe geometry only, not product values: the dash parameters come from the approved content in Combat.
    constexpr double DistanceCm=300, DurationSeconds=.2, WallFaceX=140, CapsuleRadius=40;
    constexpr float StepSeconds=.05f;
    constexpr int32 Steps=6;
    FString Error;
    {
        FNativeWorld Open;
        if (!TestNotNull(TEXT("Native physics world"),Open.World)
            || !TestNotNull(TEXT("Open ground"),Open.Box({0,0,-10},{800,800,10}))) { return false; }
        auto* Move=Prepare(*this,Open.Body(FVector::ZeroVector));
        if (!Move) { return false; }
        const FVector Start=Move->GetActorFeetLocation();
        TestTrue(TEXT("Open ground lets the dash start"),Move->CanStartDash(FVector(1,0,0),20));
        TestTrue(TEXT("Production dash root motion integrates: ")+Error,
            Move->TraceDash(FVector(1,0,0),DistanceCm,DurationSeconds,StepSeconds,Steps,Error));
        const FVector End=Move->GetActorFeetLocation();
        TestTrue(FString::Printf(TEXT("An unblocked dash covers its distance on the ground (moved %.1fcm)"),End.X-Start.X),
            FMath::IsNearlyEqual(End.X-Start.X,DistanceCm,15.) && FMath::Abs(End.Y-Start.Y)<1. && Move->IsMovingOnGround());
        TestTrue(TEXT("The dash ends with zero velocity"),Move->Velocity.Size2D()<1.);
    }
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)
        || !TestNotNull(TEXT("Ground box"),Fixture.Box({0,0,-10},{800,800,10}))
        || !TestNotNull(TEXT("Blocking wall"),Fixture.Box({WallFaceX+10,0,200},{10,400,200}))) { return false; }
    auto* Move=Prepare(*this,Fixture.Body(FVector::ZeroVector));
    if (!Move) { return false; }
    TestTrue(TEXT("Production dash root motion integrates into the wall: ")+Error,
        Move->TraceDash(FVector(1,0,0),DistanceCm,DurationSeconds,StepSeconds,Steps,Error));
    const FVector Stopped=Move->GetActorFeetLocation();
    TestTrue(FString::Printf(TEXT("A blocked dash stops at the farthest legal position before the wall (x=%.1f)"),Stopped.X),
        Stopped.X<=WallFaceX-CapsuleRadius+.5 && Stopped.X>=WallFaceX-CapsuleRadius-5. && FMath::Abs(Stopped.Y)<1.);
    TestTrue(TEXT("The stopped dash keeps walkable support and no velocity"),
        Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor() && Move->Velocity.Size2D()<1.);
    TestFalse(TEXT("A heading straight into touching geometry is refused before release"),Move->CanStartDash(FVector(1,0,0),20));
    TestTrue(TEXT("Turning away from the wall the dash may start"),Move->CanStartDash(FVector(-1,0,0),20));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeUnrecordedMoveProbe, "Lockdown.Rules.DemoMotionProbe.UnrecordedMoveRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeUnrecordedMoveProbe::RunTest(const FString& Parameters)
{
    FNativeWorld Fixture;
    if (!TestNotNull(TEXT("Native physics world"),Fixture.World)
        || !TestNotNull(TEXT("Ground box"),Fixture.Box({0,0,-10},{500,500,10}))) { return false; }
    auto* Move=Prepare(*this,Fixture.Body(FVector::ZeroVector));
    if (!Move) { return false; }
    Move->TraceUnrecordedMove();
    TestFalse(TEXT("An unobserved position write cannot produce a completed motion batch"),Move->Captured.IsValid());
    TestTrue(TEXT("A missing native segment fails explicitly instead of filling a chord"),
        Move->CaptureError.Contains(TEXT("unrecorded"),ESearchCase::IgnoreCase)
        || Move->CaptureError.Contains(TEXT("final pose"),ESearchCase::IgnoreCase));
    return true;
}
#endif
