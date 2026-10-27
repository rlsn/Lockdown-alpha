#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/LDEnvironmentRules.h"
#include "Combat/LDTacticalRules.h"
#include "LDMapSurfaceSubsystem.generated.h"

class ALDCharacter;
class ULDMapDefinition;

struct FLDMapContact
{
    LDTactical::FState State;
    bool Wet=false, Frozen=false;
    int64 SampledAt=-1;
};

// Static substrate/contact slice. Enabled only in the standalone B90 map mode.
// No client RPC or unbound item/casting shortcut is exposed.
UCLASS()
class LOCKDOWN_API ULDMapSurfaceSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return Definition!=nullptr; }
    bool Enabled() const { return Definition!=nullptr; }
    void Refresh(ALDCharacter* Body,int64 At,bool Boundary=false);
    const FLDMapContact* Contact(const ALDCharacter* Body) const;
    bool SeedFrozenForProbe(FVector Surface,FGuid Owner,int64 At,FString& Why);
    void ResetProbe();
    static bool Cell(FVector Point,FIntPoint& Out);
private:
    struct FSurface { LDEnvironment::FCell Conditions; double Z=0; };
    const ULDMapDefinition* Definition=nullptr;
    TMap<FIntPoint,FSurface> Cells;
    TMap<TWeakObjectPtr<ALDCharacter>,FLDMapContact> Contacts;
    int64 BoundaryAt=-1;
};
