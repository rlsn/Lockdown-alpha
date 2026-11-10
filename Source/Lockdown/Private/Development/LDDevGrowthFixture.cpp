#include "Development/LDDevGrowthFixture.h"
#include "Core/LDLog.h"

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

bool LDDevGrowthFixture::ResolvePreset(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
    const TArray<FName>& DefinitionIds, FString& Error)
{
    Error.Reset();
    if (DefinitionIds.IsEmpty() || DefinitionIds.Num() > 3)
    {
        Error = FString::Printf(TEXT("A preset offer names one to three actives, not %d."), DefinitionIds.Num());
        return false;
    }
    for (int32 Index = 0; Index < DefinitionIds.Num(); ++Index)
    {
        const FName Id = DefinitionIds[Index];
        const bool bActive = !Id.IsNone() && Content.Abilities.ContainsByPredicate(
            [Id](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == Id; });
        if (!bActive || Build.FindActive(Id) || Build.HasBlueprint(Id))
        {
            Error = FString::Printf(TEXT("'%s' is not an active this build may be offered (unknown, learned or a pending blueprint)."),
                *Id.ToString());
            return false;
        }
        for (int32 Earlier = 0; Earlier < Index; ++Earlier)
        {
            if (DefinitionIds[Earlier] == Id)
            {
                Error = FString::Printf(TEXT("'%s' appears twice; the actives of an offer are distinct."), *Id.ToString());
                return false;
            }
        }
    }
    return true;
}

ELDGrowthReason LDDevGrowthFixture::PickHead(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint8 Ordinal,
    uint64 Seed, TArray<TArray<FName>>& Queue, TArray<FName>& Out, bool& bPreset, FString& PresetError)
{
    Out.Reset();
    bPreset = false;
    PresetError.Reset();
    if (!Queue.IsEmpty())
    {
        const TArray<FName> Preset = MoveTemp(Queue[0]);
        Queue.RemoveAt(0);
        if (ResolvePreset(Content, Build, Preset, PresetError))
        {
            Out = Preset;
            bPreset = true;
            return ELDGrowthReason::None;
        }
    }
    return LDProgression::DrawActiveOffer(Content, Build, Ordinal, Seed, Out);
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
    TArray<TArray<FName>>& Staged = DevGrowthStagedOffers().FindOrAdd(Contender);
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    if (!ResolvePreset(Content, LDProgression::StartBuild(Content), DefinitionIds, Error))
    {
        return false;
    }
    // One offer per active opportunity: the opening one and one per active_offer_levels entry.
    const int32 Opportunities = 1 + Content.Progression.ActiveOfferLevels.Num();
    if (Staged.Num() >= Opportunities)
    {
        Error = FString::Printf(TEXT("No offer is left to stage: %d staged of %d."), Staged.Num(), Opportunities);
        return false;
    }
    const int32 Position = Staged.Add(DefinitionIds) + 1;
    UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE staged offer for the next match contender=%s position=%d cards=%s"),
        *Contender.ToString(), Position, *FString::JoinBy(DefinitionIds, TEXT(","), [](const FName& Id) { return Id.ToString(); }));
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
            UE_LOG(LogLDGrowth, Error, TEXT("LD_DEV_FIXTURE_PRESET_REFUSED contender=%s staged in the Lobby is not in the locked roster"),
                *Entry.Key.ToString());
        }
    }
    Staged.Reset();
}
#endif
