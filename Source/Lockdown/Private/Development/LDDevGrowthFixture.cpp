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

bool LDDevGrowthFixture::ResolvePreset(const FLDDemoContentDefinitions& Content, const TArray<LDProgression::FLegalOption>& Pool,
    uint8 Ordinal, const TArray<FName>& DefinitionIds, TArray<LDProgression::FLegalOption>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (Ordinal == 1 && DefinitionIds.Num() == 6)
    {
        // #153: three (opening card, hidden active) pairs.
        for (int32 Index = 0; Index < 6; Index += 2)
        {
            const FName Card = DefinitionIds[Index];
            const FName Active = DefinitionIds[Index + 1];
            const bool bCard = Content.OpeningCards.ContainsByPredicate([Card](const FLDDemoOpeningCardDefinition& Row) { return FName(*Row.Id) == Card; });
            const LDProgression::FLegalOption* Legal = Pool.FindByPredicate([Active](const LDProgression::FLegalOption& Row)
                { return Row.DefinitionId == Active && Row.Kind == ELDChoiceOptionKind::LearnActive; });
            const FLDDemoAbilityDefinition* Ability = Content.Abilities.FindByPredicate([Active](const FLDDemoAbilityDefinition& Row)
                { return FName(*Row.Id) == Active; });
            if (!bCard || !Legal || !Ability || !Ability->Draftable)
            {
                Error = FString::Printf(TEXT("'%s' hiding '%s' is not an opening card hiding a new draftable active."), *Card.ToString(),
                    *Active.ToString());
            }
            else if (Out.ContainsByPredicate([Card, Active](const LDProgression::FLegalOption& Row)
                { return Row.DefinitionId == Card || Row.HiddenActive == Active; }))
            {
                Error = FString::Printf(TEXT("'%s' or '%s' appears twice; the opening cards and their actives are distinct."),
                    *Card.ToString(), *Active.ToString());
            }
            if (!Error.IsEmpty())
            {
                Out.Reset();
                return false;
            }
            Out.Add({ELDChoiceOptionKind::Opening, Card, 0, 1, Active});
        }
        return true;
    }
    if (DefinitionIds.Num() != 3)
    {
        Error = FString::Printf(TEXT("A preset offer names exactly three definitions (or, for the opening, three card and hidden active pairs), not %d."),
            DefinitionIds.Num());
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

bool LDDevGrowthFixture::ResolvePreset(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build, uint8 Ordinal,
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
    return ResolvePreset(Catalog.Get(), Pool, Ordinal, DefinitionIds, Out, Error);
}

ELDGrowthReason LDDevGrowthFixture::PickHead(const LDProgression::FDrawInput& Input,
    const TArray<LDProgression::FLegalOption>& Pool, TArray<TArray<FName>>& Queue, TArray<LDProgression::FLegalOption>& Out,
    bool& bPreset, FString& PresetError)
{
    Out.Reset();
    bPreset = false;
    PresetError.Reset();
    if (!Queue.IsEmpty())
    {
        const TArray<FName> Preset = MoveTemp(Queue[0]);
        Queue.RemoveAt(0);
        if (ResolvePreset(*Input.Content, Pool, Input.Ordinal, Preset, Out, PresetError))
        {
            bPreset = true;
            return ELDGrowthReason::None;
        }
    }
    return LDProgression::DrawOffer(Input, Pool, Out);
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
    TArray<LDProgression::FLegalOption> Resolved;
    if (!ResolvePreset(*Catalog, FLDGrowthBuild(), uint8(Staged.Num() + 1), DefinitionIds, Resolved, Error))
    {
        return false;
    }
    if (Staged.Num() >= Catalog->Get().Progression.MaxChoices)
    {
        Error = FString::Printf(TEXT("No offer is left to stage: %d staged of %d."), Staged.Num(), Catalog->Get().Progression.MaxChoices);
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
