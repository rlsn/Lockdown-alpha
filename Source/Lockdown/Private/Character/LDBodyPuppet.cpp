#include "Character/LDBodyPuppet.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Engine/World.h"
#include "Framework/LDPlayerController.h"
#include "HAL/PlatformTime.h"

ALDBodyPuppet::ALDBodyPuppet()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = true;
    SetActorEnableCollision(false);
    Body = CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("FallingBody"));
    RootComponent = Body;
}

ALDBodyPuppet* ALDBodyPuppet::Spawn(const ULDHumanoidVisualComponent& Source)
{
    UWorld* World = Source.GetWorld();
    if (!World || World->bIsTearingDown || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer) { return nullptr; }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transient;
    ALDBodyPuppet* Puppet = World->SpawnActor<ALDBodyPuppet>(ALDBodyPuppet::StaticClass(), Source.GetComponentTransform(), Params);
    if (Puppet) { Puppet->Body->CopyLook(Source); }
    return Puppet;
}

ALDBodyPuppet* ALDBodyPuppet::SpawnFall(const ULDHumanoidVisualComponent& Source)
{
    ALDBodyPuppet* Puppet = Spawn(Source);
    if (Puppet) { Puppet->Fall(); }
    return Puppet;
}

ALDBodyPuppet* ALDBodyPuppet::SpawnOwnFall(const ULDHumanoidVisualComponent& Source, const ALDPlayerController& Viewer, uint32 Life)
{
    ALDBodyPuppet* Puppet = Spawn(Source);
    if (!Puppet) { return nullptr; }
    Puppet->Watched = &Viewer;
    Puppet->WatchedLife = Life;
    Puppet->ConfirmUntil = FPlatformTime::Seconds() + ConfirmSeconds;
    Puppet->SetActorHiddenInGame(true);
    return Puppet;
}

void ALDBodyPuppet::Fall()
{
    bFalling = true;
    SetActorHiddenInGame(false);
    Body->PlayFall();
}

void ALDBodyPuppet::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!bFalling)
    {
        const ALDPlayerController* Viewer = Watched.Get();
        if (Viewer && Viewer->OwnerState.Life == WatchedLife && Viewer->OwnerState.LifePhase == ELDLifePhase::Dead)
        {
            Fall();
        }
        else
        {
            if (!Viewer || FPlatformTime::Seconds() > ConfirmUntil) { Destroy(); }
            return;
        }
    }
    Body->Animate(FVector::ZeroVector, Delta);
    if (Body->IsFallen()) { Destroy(); }
}
