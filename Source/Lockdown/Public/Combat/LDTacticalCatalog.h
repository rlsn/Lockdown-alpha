#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Development/LDCharacterBuild.h"
#include "LDTacticalCatalog.generated.h"

UENUM()
enum class ELDTacticalTarget : uint8 { Self, Unit, SelfOrUnit, Ground, Cone };

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
    UPROPERTY() int32 Circle=1;
    UPROPERTY() ELDTacticalTarget Target=ELDTacticalTarget::Self;
    UPROPERTY() FString RankKey;
    // Alternatives of AND clauses, stored as STR/DEX/CON/INT/WIS/CHA thresholds.
    UPROPERTY() TArray<int32> Gates;
    UPROPERTY() int32 Area=0;
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
    static bool NaturalGate(const FLDTacticalDefinition& D,const FLDCharacterBuild& B);
};
