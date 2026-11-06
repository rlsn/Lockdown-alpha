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
    // A fault after Close cannot overwrite the locked result; the core reports it instead.
    if (!Mode->AvailableAuthorityTime(Through,Error) || !AdvanceAuthority(Mode->GetMatchContext(),Through,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("The authority driver failed without a diagnostic."); }
        Mode->ReportAuthorityFault(Mode->GetMatchContext(),Error);
        return;
    }
    // Read-only playtest sample after the advance; it skips itself unless an authority second just began.
    Mode->LogPlaytestHeartbeat();
}