#include "Development/LDDevGrowthFixture.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Development/LDDemoContentCatalog.h"
#include "Evaluation/LDEvaluationSeeds.h"
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
    const ALDGameMode* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!IsSwitchPresent() && !(Mode && Mode->IsRuntimeDevTestEnabled()))
    {
        Error = TEXT("Testing is off: use -LDDevGrowthFixture or let the Host enable the in-game test menu.");
        return false;
    }
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

bool StageValidatedOffer(const FGuid& Contender, const TArray<FName>& DefinitionIds, bool bEvaluationOpening, FString& Error)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDDemoEntry::LoadRuntimeContent(Catalog, Error) || !Catalog)
    {
        if (Error.IsEmpty()) { Error = TEXT("The approved Demo catalog is unavailable for a staged preset."); }
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    if (bEvaluationOpening ? !LDDevGrowthFixture::ResolveEvaluationOpening(Content, DefinitionIds, Error)
                           : !LDDevGrowthFixture::ResolvePreset(Content, LDProgression::StartBuild(Content), DefinitionIds, Error))
    {
        return false;
    }
    TArray<TArray<FName>>& Staged = DevGrowthStagedOffers().FindOrAdd(Contender);
    if (bEvaluationOpening && !Staged.IsEmpty())
    {
        Error = TEXT("The evaluation AI already has a staged offer.");
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
}

bool LDDevGrowthFixture::StageOffer(const UWorld* World, const FGuid& Contender, const TArray<FName>& DefinitionIds, FString& Error)
{
    if (!Admit(World, Error)) { return false; }
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
    return StageValidatedOffer(Contender, DefinitionIds, false, Error);
}

bool LDDevGrowthFixture::ResolveEvaluationOpening(const FLDDemoContentDefinitions& Content, const TArray<FName>& DefinitionIds,
    FString& Error)
{
    const FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    if (!ResolvePreset(Content, Build, DefinitionIds, Error)) { return false; }
    TArray<FName> Eligible;
    if (LDProgression::EligibleActives(Content, Build, 1, Eligible) != ELDGrowthReason::None)
    {
        Error = TEXT("The normal opening active pool could not be resolved.");
        return false;
    }
    for (const FName Id : DefinitionIds)
    {
        if (!Eligible.Contains(Id))
        {
            Error = FString::Printf(TEXT("'%s' is not in the normal opening active pool."), *Id.ToString());
            return false;
        }
    }
    return true;
}

bool LDDevGrowthFixture::PrepareEvaluationLevelGrants(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Current,
    uint8 LevelBefore, uint8 LevelAfter, const TArray<FName>& DefinitionIds, FLDGrowthBuild& Out,
    TArray<FLDComponentGrant>& OutGrants, FString& Error)
{
    Out = Current;
    OutGrants.Reset();
    Error.Reset();
    TArray<uint8> DrawLevels;
    for (int32 Level = int32(LevelBefore) + 1; Level <= int32(LevelAfter); ++Level)
    {
        for (uint8 Draw = 0; Draw < LDProgression::LevelComponentDraws(Content.Progression, uint8(Level)); ++Draw)
        {
            DrawLevels.Add(uint8(Level));
        }
    }
    if (LevelBefore < 1 || LevelAfter <= LevelBefore || LevelAfter > Content.Progression.MaxLevel
        || Current.Level != LevelAfter || DefinitionIds.Num() != DrawLevels.Num()
        || Current.Components.ContainsByPredicate([LevelBefore](const FLDComponentInstance& Instance)
            { return Instance.Source == ELDComponentSource::LevelUp && Instance.SourceLevel > LevelBefore; }))
    {
        Error = TEXT("A component preset must name exactly the new normal level-up draws of the crossed levels.");
        return false;
    }

    FLDGrowthBuild Prepared = Current;
    TArray<FLDComponentGrant> Grants;
    for (int32 Index = 0; Index < DefinitionIds.Num(); ++Index)
    {
        const uint8 Level = DrawLevels[Index];
        const FName Id = DefinitionIds[Index];
        bool bEligible = false;
        const ELDGrowthReason Reason = LDProgression::LevelComponentEligible(Content, Prepared, Level, Id, bEligible);
        if (Reason != ELDGrowthReason::None || !bEligible)
        {
            Error = FString::Printf(TEXT("'%s' is not eligible for the level-%u component draw (%s)."),
                *Id.ToString(), uint32(Level), LDGrowthWire::ReasonName(Reason));
            return false;
        }
        FLDComponentInstance& Instance = Prepared.Components.AddDefaulted_GetRef();
        Instance.Id = Id;
        Instance.Source = ELDComponentSource::LevelUp;
        Instance.SourceLevel = Level;
        Prepared.Components.Sort([](const FLDComponentInstance& A, const FLDComponentInstance& B)
            { return A.Id.ToString().Compare(B.Id.ToString(), ESearchCase::CaseSensitive) < 0; });
        Grants.Add({Level, Id});
    }
    if (LDProgression::ValidateBuild(Content, Prepared) != ELDGrowthReason::None)
    {
        Error = TEXT("The preset would leave an invalid growth build.");
        return false;
    }
    Out = MoveTemp(Prepared);
    OutGrants = MoveTemp(Grants);
    return true;
}

bool LDDevGrowthFixture::StageEvaluationOpening(const UWorld* World, const FGuid& Contender, const TArray<FName>& DefinitionIds,
    FString& Error)
{
    if (!Admit(World, Error)) { return false; }
    const FLDEvaluationSeeds* Seeds = LDEvaluationSeeds::Armed();
    if (!Seeds)
    {
        Error = TEXT("An evaluation opening needs -LDBuildEvaluation and an armed case seed.");
        return false;
    }
    const FLDLobbyView Lobby = World->GetAuthGameMode<ALDGameMode>()->ReadLobby();
    bool bSeededSlot = false;
    for (int32 Slot = 0; Slot < int32(Lobby.AICount) && Slot < FLDEvaluationSeeds::MaxAISlots; ++Slot)
    {
        bSeededSlot |= Seeds->AIContenders[Slot] == Contender;
    }
    if (Lobby.Phase != ELDMatchPhase::Lobby || !Contender.IsValid() || !bSeededSlot)
    {
        Error = TEXT("An evaluation opening needs an armed AI slot in the current Lobby.");
        return false;
    }
    return StageValidatedOffer(Contender, DefinitionIds, true, Error);
}

void LDDevGrowthFixture::DiscardEvaluationOpening(const FGuid& Contender)
{
    const FLDEvaluationSeeds* Seeds = LDEvaluationSeeds::Armed();
    if (!Seeds) { return; }
    for (const FGuid& Id : Seeds->AIContenders)
    {
        if (Id == Contender) { DevGrowthStagedOffers().Remove(Contender); return; }
    }
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
