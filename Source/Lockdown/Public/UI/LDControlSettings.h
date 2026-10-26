#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "LDControlSettings.generated.h"

struct FLDControlBinding
{
    FName Action;
    FString Label;
    FKey Key;
    bool Axis=false;
};
UCLASS()
class LOCKDOWN_API ULDControlSettings : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Schema=1;
    UPROPERTY() uint8 Profile=6;
    UPROPERTY() TMap<FName,FKey> Bindings;
    UPROPERTY() bool bEdgePan=true;
    UPROPERTY() bool bStartFollowing=true;
    UPROPERTY() float TextScale=1;
    UPROPERTY() float Zoom=1400;
    static const TArray<FLDControlBinding>& Defaults(bool Tactical=false);
    const TArray<FLDControlBinding>& Definitions() const {return Profile==8?HybridDefaults():Defaults(Profile==7);}
    static const TArray<FLDControlBinding>& HybridDefaults();
    bool MigrateHybrid();
    bool MigrateTactical();
    static ULDControlSettings* Load(const FString& Slot=TEXT("LockdownLocalControlsV1"),uint8 RequestedProfile=6);
    FKey Key(FName Action) const;
    FString Label(FName Action) const;
    bool Rebind(FName Action,FKey NewKey,FString& Error);
    bool Valid() const;
    void ResetBindings();
    bool Save() const;
private:
    FString StorageSlot=TEXT("LockdownLocalControlsV1");
};
