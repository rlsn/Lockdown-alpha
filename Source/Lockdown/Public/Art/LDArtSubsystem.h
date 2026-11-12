#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Art/LDArtCatalog.h"
#include "LDArtSubsystem.generated.h"



// Resolves the Demo art catalog once when the game instance starts. Bodies read their look and clips from here; a
// missing or broken entry is logged once as LD_ART_CATALOG_INVALID and Get returns null, never a placeholder.
UCLASS()
class LOCKDOWN_API ULDArtSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    // The resolved catalog of this game instance, or null when it is unavailable (see GetError).
    const FLDArtResolved* GetResolved() const { return Catalog ? &Resolved : nullptr; }
    const ULDArtCatalog* GetCatalog() const { return Catalog; }
    const FString& GetError() const { return Error; }
    // Convenience lookup from any object in a game world; null outside a game instance or without a valid catalog.
    static const FLDArtResolved* Get(const UObject* WorldContext);
private:
    UPROPERTY() TObjectPtr<const ULDArtCatalog> Catalog;
    FLDArtResolved Resolved;
    FString Error;
};

// Validates the saved scene on rendering clients and starts the explicit DEV art probes.
UCLASS()
class LOCKDOWN_API ULDArtWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

};
