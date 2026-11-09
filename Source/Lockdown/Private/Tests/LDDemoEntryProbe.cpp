#include "Tests/LDDemoEntryProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDGameInstance.h"
#include "Framework/LDDemoEntry.h"
#include "Dom/JsonObject.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ObjectKey.h"

namespace
{
    bool IsGameplayService(const AActor& Actor)
    {
        static const TSet<FName> Names = {TEXT("LDCombatDirector"),TEXT("LDVisionDirector"),
            TEXT("LDWorldViewRelay"),TEXT("LDGrowthDirector"),TEXT("LDDevelopmentDirector")};
        for (const UClass* Class = Actor.GetClass(); Class; Class = Class->GetSuperClass())
        {
            if (Names.Contains(Class->GetFName())) { return true; }
        }
        return false;
    }

    struct FObservedWorld
    {
        LDDemoTests::FEntryObservation Counts;
        TSet<FObjectKey> Actors;
        FDelegateHandle SpawnHandle;
    };

    class FEntryObserver : public TSharedFromThis<FEntryObserver>
    {
    public:
        TMap<TWeakObjectPtr<UWorld>, FObservedWorld> Worlds;
        TOptional<TWeakObjectPtr<UWorld>> RejectedWorld;
        bool RejectedBeforeWorld = false;
        bool ActiveRejectedBrowse = false;
        FDelegateHandle BeforeHandle, CleanupHandle;

        ~FEntryObserver()
        {
            FWorldDelegates::OnPreWorldInitialization.Remove(BeforeHandle);
            FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
            for (const auto& Pair : Worlds)
            {
                if (auto* World = Pair.Key.Get()) { World->RemoveOnActorSpawnedHandler(Pair.Value.SpawnHandle); }
            }
        }

        void ActorSpawned(AActor* Actor)
        {
            if (!Actor || Actor->HasAnyFlags(RF_ClassDefaultObject) || !Actor->GetWorld() || !Actor->GetWorld()->IsGameWorld()) { return; }
            auto& State = Worlds.FindOrAdd(Actor->GetWorld());
            const FObjectKey Key(Actor);
            if (State.Actors.Contains(Key)) { return; }
            State.Actors.Add(Key);
            State.Counts.Pawns += Actor->IsA<APawn>() ? 1 : 0;
            State.Counts.GameModes += Actor->IsA<AGameModeBase>() ? 1 : 0;
            State.Counts.Services += IsGameplayService(*Actor) ? 1 : 0;
        }

        void Snapshot(UWorld* World)
        {
            if (!World || !World->IsGameWorld() || !World->PersistentLevel) { return; }
            for (AActor* Actor : World->PersistentLevel->Actors) { ActorSpawned(Actor); }
        }

        void BeforeWorld(UWorld* World, const UWorld::InitializationValues Values)
        {
            if (!World || !World->IsGameWorld()) { return; }
            auto& State = Worlds.FindOrAdd(World);
            if (ActiveRejectedBrowse)
            {
                RejectedWorld = TWeakObjectPtr<UWorld>(World);
                RejectedBeforeWorld = false;
            }
            State.Counts.Observed = true;
            State.SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateSP(AsShared(), &FEntryObserver::ActorSpawned));
            Snapshot(World);
        }

        void WorldCleanup(UWorld* World, bool SessionEnded, bool CleanupResources) { Snapshot(World); }
    };

    TMap<TWeakObjectPtr<const ULDGameInstance>, TSharedPtr<FEntryObserver>> Observers;
}

void LDDemoTests::StartEntryObservation(ULDGameInstance& Instance, bool IsolatedTest)
{
    if (!IsolatedTest && !FParse::Param(FCommandLine::Get(), TEXT("LDDemoEntryProbe"))) { return; }
    auto Observer = MakeShared<FEntryObserver>();
    Observer->BeforeHandle = FWorldDelegates::OnPreWorldInitialization.AddSP(Observer, &FEntryObserver::BeforeWorld);
    Observer->CleanupHandle = FWorldDelegates::OnWorldCleanup.AddSP(Observer, &FEntryObserver::WorldCleanup);
    Observers.Add(&Instance, Observer);
}

void LDDemoTests::StopEntryObservation(ULDGameInstance& Instance) { Observers.Remove(&Instance); }

void LDDemoTests::ObserveRejectedEntry(ULDGameInstance& Instance, UWorld& World)
{
    if (auto* Observer = Observers.Find(&Instance))
    {
        (*Observer)->RejectedWorld = TWeakObjectPtr<UWorld>(&World);
        (*Observer)->RejectedBeforeWorld = false;
        (*Observer)->Snapshot(&World);
    }
}

void LDDemoTests::ObserveEntryBeforeWorld(ULDGameInstance& Instance)
{
    if (auto* Observer = Observers.Find(&Instance))
    {
        (*Observer)->RejectedWorld.Reset();
        (*Observer)->RejectedBeforeWorld = true;
        (*Observer)->ActiveRejectedBrowse = true;
    }
}

void LDDemoTests::EndEntryBrowse(ULDGameInstance& Instance)
{
    if (auto* Observer = Observers.Find(&Instance)) { (*Observer)->ActiveRejectedBrowse = false; }
}

void LDDemoTests::ObserveMapRegistry(UWorld& World, bool Activation)
{
    if (!World.IsGameWorld()) { return; }
    for (const auto& Pair : Observers)
    {
        if (Pair.Value->ActiveRejectedBrowse)
        {
            Pair.Value->RejectedWorld = TWeakObjectPtr<UWorld>(&World);
            Pair.Value->RejectedBeforeWorld = false;
        }
        auto& Counts = Pair.Value->Worlds.FindOrAdd(&World).Counts;
        (Activation ? Counts.RegistryActivations : Counts.RegistryCreations)++;
    }
}

LDDemoTests::FEntryObservation LDDemoTests::ReadEntryObservation(const ULDGameInstance& Instance)
{
    const auto* Observer = Observers.Find(&Instance);
    if (Observer && (*Observer)->RejectedBeforeWorld)
    {
        FEntryObservation Result;
        Result.RejectedBeforeWorldCreation = true;
        return Result;
    }
    if (Observer && (*Observer)->RejectedWorld.IsSet())
    {
        if (const auto* State = (*Observer)->Worlds.Find((*Observer)->RejectedWorld.GetValue())) { return State->Counts; }
    }
    return {};
}

void LDDemoTests::ProbeRejectedEntry(UWorld& World)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoEntryProbe"))) { return; }
    FString Output;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoEntryProbeOutput="), Output)
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_ENTRY_PROBE requires an output file under Artifacts."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    const auto* Instance = World.GetGameInstance<ULDGameInstance>();
    const FEntryObservation Attempt = Instance ? ReadEntryObservation(*Instance) : FEntryObservation();
    int32 Pawns = 0, LegacyModes = 0, Services = 0;
    TArray<TSharedPtr<FJsonValue>> PawnDetails;
    for (TObjectIterator<AActor> It; It; ++It)
    {
        const AActor* Actor = *It;
        if (Actor->HasAnyFlags(RF_ClassDefaultObject) || Actor->IsActorBeingDestroyed() || !Actor->GetWorld()
            || !Actor->GetWorld()->IsGameWorld()) { continue; }
        Pawns += Actor->IsA<APawn>() ? 1 : 0;
        if (Actor->IsA<APawn>())
        {
            PawnDetails.Add(MakeShared<FJsonValueString>(Actor->GetPathName() + TEXT(" class=") + Actor->GetClass()->GetPathName()));
        }
        LegacyModes += Actor->IsA<AGameModeBase>() && Actor->GetClass()->GetFName() != FName(TEXT("LDGameMode")) ? 1 : 0;
        Services += IsGameplayService(*Actor) ? 1 : 0;
    }
    const bool Passed = Instance && !Instance->RejectedEntryURL().IsEmpty() && !Instance->ConnectionMessage.IsEmpty()
        && UWorld::RemovePIEPrefix(World.GetOutermost()->GetName()) == LDDemoEntry::FrontendMap
        && Pawns == 0 && LegacyModes == 0 && Services == 0 && (Attempt.Observed || Attempt.RejectedBeforeWorldCreation)
        && Attempt.Pawns == 0 && Attempt.GameModes == 0 && Attempt.Services == 0
        && Attempt.RegistryCreations == 0 && Attempt.RegistryActivations == 0;
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("rejected_url"), Instance ? Instance->RejectedEntryURL() : TEXT(""));
    Report->SetStringField(TEXT("failure"), Instance ? Instance->ConnectionMessage : TEXT("Missing GameInstance"));
    Report->SetStringField(TEXT("arrived_world"), World.GetOutermost()->GetName());
    Report->SetNumberField(TEXT("live_pawns"), Pawns);
    Report->SetArrayField(TEXT("live_pawn_details"), PawnDetails);
    Report->SetNumberField(TEXT("legacy_game_modes"), LegacyModes);
    Report->SetNumberField(TEXT("gameplay_services"), Services);
    Report->SetBoolField(TEXT("observed_before_initialization"), Attempt.Observed);
    Report->SetBoolField(TEXT("rejected_before_world_creation"), Attempt.RejectedBeforeWorldCreation);
    Report->SetNumberField(TEXT("rejected_attempt_pawns"), Attempt.Pawns);
    Report->SetNumberField(TEXT("rejected_attempt_game_modes"), Attempt.GameModes);
    Report->SetNumberField(TEXT("rejected_attempt_services"), Attempt.Services);
    Report->SetNumberField(TEXT("registry_creations"), Attempt.RegistryCreations);
    Report->SetNumberField(TEXT("registry_activations"), Attempt.RegistryActivations);
    FString Json;
    const bool Written = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
        && FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_ENTRY_PROBE passed=%d pawns=%d legacy_modes=%d services=%d"), Passed && Written, Pawns, LegacyModes, Services);
    FPlatformMisc::RequestExitWithStatus(false, Passed && Written ? 0 : 1);
}
#endif
