#include "Tests/LDMovementProbe.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

ALDMovementProbe::ALDMovementProbe() { PrimaryActorTick.bCanEverTick = true; }
void ALDMovementProbe::BeginPlay()
{
    Super::BeginPlay();
    StartedAt = FPlatformTime::Seconds();
    for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
    {
        UE_LOG(LogTemp,Display,TEXT("LD_NAV_BOUNDS %s size %s"),*It->GetName(),*It->GetComponentsBoundingBox(true).GetSize().ToString());
    }
    TestBody = GetWorld()->SpawnActor<ALDCharacter>(FVector(-1600,-500,100), FRotator::ZeroRotator);
    if (!TestBody) { Check(false,TEXT("Spawn test character")); Finish(); return; }
    TestBody->SetReplicates(false);
    for (TActorIterator<ALDResident> It(GetWorld()); It; ++It) { ResidentStarts.Add(*It, It->GetActorLocation()); }
    Check(ResidentStarts.Num() == 8, TEXT("Eight authority-spawned residents"));
}
void ALDMovementProbe::Check(bool bPass, const FString& Description)
{
    Results.Add((bPass ? TEXT("PASS ") : TEXT("FAIL ")) + Description);
    if (!bPass) { ++Failures; }
    UE_LOG(LogTemp, Display, TEXT("LD_MOVEMENT_PROBE %s"), *Results.Last());
}
void ALDMovementProbe::StartPhase(int32 NewPhase, FVector Location, bool bSprint)
{
    Phase = NewPhase; PhaseTime = 0;
    TestBody->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    TestBody->GetCharacterMovement()->StopMovementImmediately();
    TestBody->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TestBody->SetSprint(bSprint);
    StartLocation = Location;
}
void ALDMovementProbe::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!TestBody) { return; }
    PhaseTime += Delta;
    if (Phase == 0)
    {
        UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Point;
        const bool bReady = Nav && Nav->GetDefaultNavDataInstance() && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)
            && Nav->GetRandomReachablePointInRadius(TestBody->GetActorLocation(),200,Point);
        if (bReady && FPlatformTime::Seconds() - StartedAt > .3) { StartPhase(1,FVector(-1600,-500,100),false); }
        else if (FPlatformTime::Seconds() - StartedAt > 15) { Check(false,TEXT("Navigation did not initialize within 15 real seconds")); Finish(); }
        return;
    }
    const FVector Direction = Phase == 2 ? FVector(1,1,0).GetSafeNormal() : (Phase == 5 ? FVector(0,-1,0) : FVector(1,0,0));
    if (PhaseTime > .25f) { TestBody->AddMovementInput(Direction); }
    if (Phase == 1 && PhaseTime > 1.5f)
    {
        const float Distance = FVector::Dist2D(StartLocation, TestBody->GetActorLocation());
        Check(FMath::IsNearlyEqual(TestBody->GetVelocity().Size2D(),180.0,2.0) && Distance > 180 && Distance < 240,
            FString::Printf(TEXT("Walk: %.1f cm/s, %.1f cm"), TestBody->GetVelocity().Size2D(), Distance));
        StartPhase(2,FVector(-1600,100,100),true);
    }
    else if (Phase == 2 && PhaseTime > 1.5f)
    {
        const float Distance = FVector::Dist2D(StartLocation, TestBody->GetActorLocation());
        Check(FMath::IsNearlyEqual(TestBody->GetVelocity().Size2D(),300.0,2.0) && Distance > 310 && Distance < 400,
            FString::Printf(TEXT("Diagonal sprint: %.1f cm/s, %.1f cm"), TestBody->GetVelocity().Size2D(), Distance));
        StartPhase(3,FVector(-1300,-450,100),false);
    }
    else if (Phase == 3 && PhaseTime > 2.5f)
    {
        Check(TestBody->GetActorLocation().X <= -1125, TEXT("Wall blocks movement"));
        StartPhase(4,FVector(120,-400,100),false);
    }
    else if (Phase == 4 && PhaseTime > 4.2f)
    {
        Check(TestBody->GetActorLocation().Z > 275 && TestBody->GetActorLocation().X > 650,
            TEXT("Walk up 200 cm ramp onto plateau: ") + TestBody->GetActorLocation().ToString());
        StartPhase(5,FVector(900,200,100),false);
    }
    else if (Phase == 5 && PhaseTime > 2.5f)
    {
        Check(TestBody->GetActorLocation().Y >= 25 && TestBody->GetActorLocation().Z < 120, TEXT("Cliff cannot be stepped up directly"));
        int32 Moved = 0;
        for (const auto& Pair : ResidentStarts) { if (Pair.Key.IsValid() && FVector::Dist2D(Pair.Value,Pair.Key->GetActorLocation()) > 30) { ++Moved; } }
        Check(Moved > 0,FString::Printf(TEXT("Residents navigate: %d moved"),Moved));
        Finish();
    }
}
void ALDMovementProbe::Finish()
{
    FString ReportPath;
    const bool bHasReport = FParse::Value(FCommandLine::Get(), TEXT("LDProbeReport="), ReportPath);
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("failures"), Failures);
    TArray<TSharedPtr<FJsonValue>> Checks;
    for (const FString& Result : Results) { Checks.Add(MakeShared<FJsonValueString>(Result)); }
    Json->SetArrayField(TEXT("checks"),Checks);
    FString Output;
    FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if (!bHasReport || !FFileHelper::SaveStringToFile(Output,*ReportPath)) { ++Failures; }
    UE_LOG(LogTemp,Display,TEXT("LD_MOVEMENT_PROBE_FINISHED failures=%d"),Failures);
    FPlatformMisc::RequestExitWithStatus(false,Failures == 0 ? 0 : 1);
    SetActorTickEnabled(false);
}
