#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Development/LDCharacterBuild.h"
#include "LDTacticalCatalog.generated.h"

USTRUCT()
struct FLDTacticalDefinition
{
    GENERATED_BODY()
    UPROPERTY() ELDSkill Skill=ELDSkill::None;
    UPROPERTY() FString Id;
    UPROPERTY() int32 Windup=0;
    UPROPERTY() int32 Mana=0;
    UPROPERTY() int32 Cooldown=0;
    UPROPERTY() int32 Range=0;
    UPROPERTY() TArray<int32> Magnitudes;
};
UCLASS()
class LOCKDOWN_API ULDTacticalCatalog:public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Checksum;
    UPROPERTY() TArray<FLDTacticalDefinition> Abilities;
    UPROPERTY() int32 NullBellPrice=8;
    UPROPERTY() int32 NullBellCooldown=12000;
    bool Import(const FString& Text,FString& Error);
    bool Validate(FString& Error) const;
    const FLDTacticalDefinition* Find(ELDSkill S) const{return Abilities.FindByPredicate([&](const auto& A){return A.Skill==S;});}
    static ULDTacticalCatalog* Load();
};
