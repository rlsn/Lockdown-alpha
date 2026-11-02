#include "Framework/LDGameEngine.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Tests/LDDemoEntryProbe.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

EBrowseReturnVal::Type ULDGameEngine::Browse(FWorldContext& Context, FURL URL, FString& Error)
{
    auto* Instance = Cast<ULDGameInstance>(Context.OwningGameInstance);
#if WITH_DEV_AUTOMATION_TESTS
    ON_SCOPE_EXIT { if (Instance) { LDDemoTests::EndEntryBrowse(*Instance); } };
#endif
    if (URL.IsLocalInternal())
    {
        FString Options;
        for (const FString& Option : URL.Op) { Options += TEXT("?") + Option; }
        FString Rejection;
        if (LDDemoEntry::ValidateSelection(URL.Map, Options, Rejection))
        {
            if (URL.Map == LDDemoEntry::WorldMap
                && (!LDDemoEntry::VerifyRuntimeFiles(Rejection) || !LDDemoEntry::VerifyWorldPackages(Rejection)))
            {
                Error = Rejection;
            }
            else
            {
                UPackage* Package = LoadPackage(nullptr, *URL.Map, LOAD_None);
                const UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
                const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
                if (!Settings) { Rejection = TEXT("The requested Demo entry world could not be loaded."); }
                else if (Settings->DefaultGameMode && Settings->DefaultGameMode != ALDGameMode::StaticClass())
                { Rejection = TEXT("Retired saved GameMode. The Demo world must use LDGameMode."); }
            }
        }
        if (!Rejection.IsEmpty())
        {
            Error = Rejection;
            if (Instance) { Instance->RecordEntryRejectionBeforeWorld(URL.ToString(), Rejection); }
#if WITH_DEV_AUTOMATION_TESTS
            if (Instance) { LDDemoTests::ObserveEntryBeforeWorld(*Instance); }
#endif
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BROWSE_REJECTED %s"), *Rejection);
            return EBrowseReturnVal::Failure;
        }
    }
    return Super::Browse(Context, URL, Error);
}
