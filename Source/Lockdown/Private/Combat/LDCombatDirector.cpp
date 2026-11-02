#include "Combat/LDCombatDirector.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

ALDCombatDirector::ALDCombatDirector()
{
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    CritRandom.GenerateNewSeed();
}

void ALDCombatDirector::Tick(float Delta)
{
    Super::Tick(Delta);
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode || Mode->Combat!=this || !Mode->IsGameplayWriteOpen(Mode->GetMatchContext())) { return; }
    int64 Through=0;
    FString Error;
    if (!Mode->AvailableAuthorityTime(Through,Error) || !AdvanceAuthority(Mode->GetMatchContext(),Through,Error))
    { Mode->FaultAuthority(Error); }
}