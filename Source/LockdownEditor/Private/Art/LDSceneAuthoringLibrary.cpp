#include "LDSceneAuthoringLibrary.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Region/LDMapRegistry.h"
#include "World/LDNativeGround.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "UObject/UnrealType.h"

bool ULDSceneAuthoringLibrary::BakeNavigation(UWorld* World, FString& Error)
{
    if (!World || World->IsGameWorld()) { Error = TEXT("Bake navigation in the editor world after stopping Play."); return false; }
    LDNativeGround::RetireLegacyCliffGuards(*World);
    auto* Settings = GetMutableDefault<ULevelEditorMiscSettings>();
    Settings->bNavigationAutoUpdate = true;
    Settings->SaveConfig();
    auto* Nav = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
    if (!Nav) { Error = TEXT("The editor world has no native navigation system."); return false; }
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
    {
        auto* Mode = FindFProperty<FEnumProperty>(ARecastNavMesh::StaticClass(), TEXT("RuntimeGeneration"));
        if (!Mode) { Error = TEXT("Recast runtime generation setting is unavailable."); return false; }
        It->Modify();
        Mode->GetUnderlyingProperty()->SetIntPropertyValue(Mode->ContainerPtrToValuePtr<void>(*It), uint64(ERuntimeGenerationType::Static));
        It->PostEditChange();
    }
    Nav->Build();
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It) { It->EnsureBuildCompletion(); }
    if (!Nav->IsNavigationBuilt(World->GetWorldSettings())) { Error = TEXT("Native navigation build did not complete."); return false; }
    World->MarkPackageDirty();
    Error.Reset();
    return true;
}

bool ULDSceneAuthoringLibrary::ValidateGround(UWorld* World, FString& Error)
{
    auto* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry) { Error = TEXT("The Demo editor world has no map registry."); return false; }
    return Registry->Rebuild(Error);
}

FString ULDSceneAuthoringLibrary::GroundValidationError(UWorld* World)
{
    FString Error;
    if (!ValidateGround(World, Error) && Error.IsEmpty()) { return TEXT("Ground validation failed without a diagnostic."); }
    return Error;
}
