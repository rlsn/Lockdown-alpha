#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Art/LDArtCatalog.h"
#include "LDArtSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;



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
    // One dynamic instance of Material with its "Tint" vector set to Tint, shared by every body of this game instance
    // that draws the same fixed marker colour (no body may change it afterwards). Outside a game instance each caller
    // gets its own instance.
    static UMaterialInstanceDynamic* SharedTint(UObject* Outer, UMaterialInterface* Material, const FLinearColor& Tint);
private:
    UPROPERTY() TObjectPtr<const ULDArtCatalog> Catalog;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Tints;
    TArray<FLinearColor> TintColours;
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
