#include "Art/LDArtSubsystem.h"
#include "Art/LDDecorationSet.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/LDDemoEntry.h"
#include "Tests/LDDemoArtCaptureProbe.h"

bool ULDArtSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // A dedicated server draws nothing; every other game instance presents bodies and decorations.
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULDArtSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Resolve the whole catalog once; a missing asset or entry is an explicit error, never a placeholder body.
    Catalog = nullptr;
    Resolved = FLDArtResolved();
    if (const auto* Loaded = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath, nullptr, LOAD_NoWarn))
    {
        if (Loaded->Resolve(Resolved, Error)) { Catalog = Loaded; }
    }
    else { Error = FString::Printf(TEXT("Art catalog %s is missing."), ULDArtCatalog::AssetPath); }
    if (Catalog)
    {
        UE_LOG(LogTemp, Display, TEXT("LD_ART_CATALOG_READY %d characters, %d props and %d decorations resolved."),
            Catalog->Characters.Num(), Catalog->Props.Num(), Catalog->Decorations.Num());
        return;
    }
    Resolved = FLDArtResolved();
    UE_LOG(LogTemp, Error, TEXT("LD_ART_CATALOG_INVALID %s Run Scripts\\ImportDemoArt.ps1 -Mode Import or restore the LFS assets."), *Error);
}

const FLDArtResolved* ULDArtSubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
    const auto* Art = Instance ? Instance->GetSubsystem<ULDArtSubsystem>() : nullptr;
    return Art ? Art->GetResolved() : nullptr;
}

bool ULDArtWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool ULDArtWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULDArtWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    // Decorations dress the Demo map only; the frontend and fixture worlds show none.
    if (World.GetNetMode() == NM_DedicatedServer || UWorld::RemovePIEPrefix(World.GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        return;
    }
    const UGameInstance* Instance = World.GetGameInstance();
    const auto* Art = Instance ? Instance->GetSubsystem<ULDArtSubsystem>() : nullptr;
    FString Error;
    if (!Art || !Art->GetResolved()) { Error = Art ? Art->GetError() : FString(TEXT("The art subsystem is not running.")); }
    else
    {
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags |= RF_Transient;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ALDDecorationSet* Set = World.SpawnActor<ALDDecorationSet>(Spawn);
        if (!Set) { Error = TEXT("The decoration set could not be spawned."); }
        else if (!Set->Build(*Art->GetResolved(), Error) || !Set->IsPresentationOnly())
        {
            if (Error.IsEmpty()) { Error = TEXT("A decoration part could collide, overlap or affect navigation."); }
            Set->Destroy();
        }
        else { Decorations = Set; }
    }
    if (Decorations.IsValid())
    {
        UE_LOG(LogTemp, Display, TEXT("LD_ART_DECORATIONS_READY %d placements, display only."), Decorations->GetInstanceCount());
    }
    else { UE_LOG(LogTemp, Error, TEXT("LD_ART_DECORATIONS_UNAVAILABLE %s"), *Error); }
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::StartArtCaptureProbe(World);
#endif
}
