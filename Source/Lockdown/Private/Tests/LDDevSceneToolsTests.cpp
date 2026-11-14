#include "Framework/LDGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/LDTestApprovedFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDevSceneToolsTest, "Lockdown.Rules.DemoDevSceneTools",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDevSceneToolsTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("A physical world without navigation"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Combat = World->SpawnActor<ALDCombatDirector>();
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Body = World->SpawnActor<ALDCharacter>(FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
    auto* Roof = World->SpawnActor<AActor>();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Mode || !Public || !Combat || !Body || !Roof || !Cube) { AddError(TEXT("Scene fixture actors unavailable")); return false; }
    auto* Mesh = NewObject<UStaticMeshComponent>(Roof);
    Roof->SetRootComponent(Mesh);
    Mesh->SetStaticMesh(Cube);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    // A static-mesh component is legal even with a dynamic object profile, no terrain tags and no navigation.
    Mesh->SetCollisionObjectType(ECC_WorldDynamic);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    Mesh->RegisterComponent();
    Mesh->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(50000, 50000, 0), FVector(20, 20, 2)));
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {19, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Combat;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Id(0xDE7, 0, 0, 1);
    FLDBodyCombatState State;
    State.Identity = FGuid::NewGuid();
    State.Victim = {ELDVictimKind::Contender, Id, 1};
    State.Resources.HpP2 = State.Resources.MaxHpP2 = 10000;
    Combat->Bodies.Add(Body, State);
    auto& Life = Mode->Lives.Add(Id);
    Life.Kind = ELDVictimKind::Contender;
    Life.Phase = ELDLifePhase::Alive;
    Life.Life = 1;
    Life.Revision = 1;
    Life.Actor = Body;
    Life.Body = {State.Victim, State.Identity};
    Mode->BeginLifeMotion(Life, Life.Body, *Body);
    Life.MotionThroughSequence = 42;
    const FGuid StreamBefore = Life.MotionStreamId;
    World->Tick(LEVELTICK_TimeOnly, .1f);
    FString Error;
    const bool Ran = Mode->BeginAuthorityStep(*Combat, Mode->MatchContext, 50) == ELDCommitReject::None
        && Mode->ExecuteAuthorityPhase(*Combat, ELDCommitPhase::Motion, [&](const auto&)
        { Life.CompletedMotionStep = Mode->AuthorityStep.StepSerial; return true; }, Error)
        && Mode->ExecuteAuthorityPhase(*Combat, ELDCommitPhase::Transactions, [&](const FLDGameplayWritePermit& Permit)
        {
            TestTrue(TEXT("A plain static mesh roof is a destination outside map bounds and without navmesh"),
                Mode->DevTeleport(Id, FVector(50000, 50000, 500), Permit, Error));
            const FVector Foot = Body->GetCharacterMovement()->GetActorFeetLocation();
            TestTrue(TEXT("The capsule stands on the roof, not the original ground"),
                FMath::IsNearlyEqual(Foot.X, 50000., 1.) && FMath::IsNearlyEqual(Foot.Y, 50000., 1.)
                && Foot.Z >= 100 && Foot.Z < 106);
            TestTrue(TEXT("Teleport preserves the life and native sequence instead of resetting a live stream"),
                Life.Life == 1 && Life.MotionThroughSequence == 42 && Life.MotionStreamId == StreamBefore
                && Life.MotionLastFoot.Equals(Foot, .001));
            const FVector Before = Body->GetActorLocation();
            TestFalse(TEXT("A destination without static support is refused"),
                Mode->DevTeleport(Id, FVector(70000, 70000, 500), Permit, Error));
            TestTrue(TEXT("A refused destination leaves the body in place"), Body->GetActorLocation() == Before);
            Life.Phase = ELDLifePhase::Dead;
            TestFalse(TEXT("A dead owner cannot teleport"), Mode->DevTeleport(Id, FVector(50000, 50000, 500), Permit, Error));
            Life.Phase = ELDLifePhase::Alive;
            Error.Reset();
            return true;
        }, Error)
        && Mode->FinishAuthorityStep(*Combat, Error);
    return TestTrue(TEXT("The authoritative scene-tool boundary completes: ") + Error, Ran);
}
#endif
