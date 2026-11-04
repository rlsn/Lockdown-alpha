#include "Development/LDDevGrowthFixture.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool LDDevGrowthFixture::IsSwitchPresent()
{
    static const bool bSwitch = FParse::Param(FCommandLine::Get(), TEXT("LDDevGrowthFixture"));
    return bSwitch;
}

bool LDDevGrowthFixture::Admit(const UWorld* World, FString& Error)
{
    Error.Reset();
    if (!IsSwitchPresent())
    {
        Error = TEXT("The DEV growth fixture is off: the authority was not started with -LDDevGrowthFixture.");
        return false;
    }
    const ALDGameMode* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!World || World->GetNetMode() == NM_Client || !Mode || !Mode->IsDemoRoom())
    {
        Error = TEXT("The DEV growth fixture acts only on the authority of a Demo room.");
        return false;
    }
    return true;
}

bool LDDevGrowthFixture::ResolvePreset(const TArray<LDProgression::FLegalOption>& Pool, const TArray<FName>& DefinitionIds,
    TArray<LDProgression::FLegalOption>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (DefinitionIds.Num() != 3)
    {
        Error = FString::Printf(TEXT("A preset offer names exactly three definitions, not %d."), DefinitionIds.Num());
        return false;
    }
    for (int32 Index = 0; Index < DefinitionIds.Num(); ++Index)
    {
        const FName Id = DefinitionIds[Index];
        const LDProgression::FLegalOption* Legal = Id.IsNone() ? nullptr
            : Pool.FindByPredicate([Id](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == Id; });
        if (!Legal)
        {
            Error = FString::Printf(TEXT("'%s' is not a legal choice of this build (unknown, at its rank cap or otherwise excluded)."),
                *Id.ToString());
        }
        else if (Out.ContainsByPredicate([Id](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == Id; }))
        {
            Error = FString::Printf(TEXT("'%s' appears twice; the three cards of an offer are distinct."), *Id.ToString());
        }
        if (!Error.IsEmpty())
        {
            Out.Reset();
            return false;
        }
        Out.Add(*Legal);
    }
    return true;
}

bool LDDevGrowthFixture::ResolvePreset(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
    const TArray<FName>& DefinitionIds, TArray<LDProgression::FLegalOption>& Out, FString& Error)
{
    Out.Reset();
    TArray<LDProgression::FLegalOption> Pool;
    const ELDGrowthReason PoolReason = LDProgression::LegalPool(Catalog, Build, Pool);
    if (PoolReason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The build has no legal pool: %s"), LDGrowthWire::ReasonName(PoolReason));
        return false;
    }
    return ResolvePreset(Pool, DefinitionIds, Out, Error);
}

ELDGrowthReason LDDevGrowthFixture::PickHead(const FLDDemoProgressionDefinition& Progression,
    const TArray<LDProgression::FLegalOption>& Pool, uint8 Ordinal, int64 Weight, uint64 Seed,
    TArray<TArray<FName>>& Queue, TArray<LDProgression::FLegalOption>& Out, bool& bPreset, FString& PresetError)
{
    Out.Reset();
    bPreset = false;
    PresetError.Reset();
    if (!Queue.IsEmpty())
    {
        const TArray<FName> Preset = MoveTemp(Queue[0]);
        Queue.RemoveAt(0);
        if (ResolvePreset(Pool, Preset, Out, PresetError))
        {
            bPreset = true;
            return ELDGrowthReason::None;
        }
    }
    return LDProgression::DrawOffer(Progression, Pool, Ordinal, Weight, Seed, Out);
}

namespace
{
// Offers staged in the Lobby for the next match, per contender, oldest first.
TMap<FGuid, TArray<TArray<FName>>>& DevGrowthStagedOffers()
{
    static TMap<FGuid, TArray<TArray<FName>>> Staged;
    return Staged;
}
}

bool LDDevGrowthFixture::StageOffer(const UWorld* World, const FGuid& Contender, const TArray<FName>& DefinitionIds, FString& Error)
{
    if (!Admit(World, Error))
    {
        return false;
    }
    const FLDLobbyView Lobby = World->GetAuthGameMode<ALDGameMode>()->ReadLobby();
    if (Lobby.Phase != ELDMatchPhase::Lobby)
    {
        Error = TEXT("Offers are staged only in the Lobby; in an open match use ALDDevelopmentDirector::DevQueueOffer.");
        return false;
    }
    if (!Contender.IsValid() || !Lobby.Members.ContainsByPredicate([&Contender](const FLDLobbyMember& Member)
        { return Member.ContenderId == Contender; }))
    {
        Error = TEXT("The preset recipient is not a member of this Lobby.");
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDDemoEntry::LoadRuntimeContent(Catalog, Error) || !Catalog)
    {
        if (Error.IsEmpty()) { Error = TEXT("The approved Demo catalog is unavailable for a staged preset."); }
        return false;
    }
    TArray<LDProgression::FLegalOption> Resolved;
    if (!ResolvePreset(*Catalog, FLDGrowthBuild(), DefinitionIds, Resolved, Error))
    {
        return false;
    }
    TArray<TArray<FName>>& Staged = DevGrowthStagedOffers().FindOrAdd(Contender);
    if (Staged.Num() >= Catalog->Get().Progression.MaxChoices)
    {
        Error = FString::Printf(TEXT("No offer is left to stage: %d staged of %d."), Staged.Num(), Catalog->Get().Progression.MaxChoices);
        return false;
    }
    const int32 Position = Staged.Add(DefinitionIds) + 1;
    UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE staged offer for the next match contender=%s position=%d cards=%s,%s,%s"),
        *Contender.ToString(), Position, *DefinitionIds[0].ToString(), *DefinitionIds[1].ToString(), *DefinitionIds[2].ToString());
    return true;
}

void LDDevGrowthFixture::TakeStaged(const TArray<FGuid>& Roster, TMap<FGuid, TArray<TArray<FName>>>& Out)
{
    Out.Reset();
    TMap<FGuid, TArray<TArray<FName>>>& Staged = DevGrowthStagedOffers();
    for (TPair<FGuid, TArray<TArray<FName>>>& Entry : Staged)
    {
        if (Roster.Contains(Entry.Key))
        {
            Out.Add(Entry.Key, MoveTemp(Entry.Value));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEV_FIXTURE_PRESET_REFUSED contender=%s staged in the Lobby is not in the locked roster"),
                *Entry.Key.ToString());
        }
    }
    Staged.Reset();
}
#endif
