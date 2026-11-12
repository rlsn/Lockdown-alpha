#pragma once
#include "CoreMinimal.h"
#include "Art/LDArtCatalog.h"

class AStaticMeshActor;
class UStaticMeshComponent;
class UWorld;
struct FLDDemoWorldGeometry;

// Saved standard actors are the authoring source. The art catalog carries a derived, read-only scene index.
namespace LDStaticScene
{
    inline const FName VisualTag(TEXT("LDSceneVisual"));
    inline const FName CollisionTag(TEXT("LDSceneCollision"));
    inline const FName HiddenTag(TEXT("LDSceneHiddenGeometry"));
    LOCKDOWN_API FString Value(const AActor& Actor, const TCHAR* Prefix);
    LOCKDOWN_API UStaticMeshComponent* Collision(AStaticMeshActor& Actor);
    LOCKDOWN_API UStaticMeshComponent* Collision(const AStaticMeshActor& Actor);
    LOCKDOWN_API TArray<UStaticMeshComponent*> Visuals(const UWorld& World);
    LOCKDOWN_API bool Read(const UWorld& World, const ULDArtCatalog& Assets,
        TArray<FLDArtDecoration>& Placements, TArray<FString>& Hidden, FString& Error);
    LOCKDOWN_API bool Validate(const UWorld& World, const ULDArtCatalog& Index, FString& Error);
    // Applies saved standard object transforms to the typed base. Does not approve or commit edited geometry.
    LOCKDOWN_API bool ProjectGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base,
        FLDDemoWorldGeometry& Out, FString& Error);
    LOCKDOWN_API bool ValidateGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base, FString& Error);
}
