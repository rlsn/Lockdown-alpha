#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDEquipmentCatalog.generated.h"
USTRUCT()
struct FLDEquipmentDefinition
{
    GENERATED_BODY()
    UPROPERTY() ELDItemKind Kind=ELDItemKind::IronHalberd;
    UPROPERTY() FString Id;
    UPROPERTY() FString Name;
    UPROPERTY() FString ModifierHelp;
    UPROPERTY() uint8 Binding=0;
    UPROPERTY() bool TwoHanded=false;
    UPROPERTY() int32 Weight=0;
    UPROPERTY() int32 Price=-1;
    UPROPERTY() FLDGearStats Stats;
    UPROPERTY() FString Operation;
    UPROPERTY() FString Family;
    UPROPERTY() FString ActiveJson;
    UPROPERTY() int32 Cooldown=0;
    UPROPERTY() bool Consumable=false;
    double Param(const TCHAR* Name,double Default=0) const;
    // Major protections share the explicit eight-second activation lock;
    // their longer reuse endpoints belong to the individual item definition.
    FName CooldownKey() const {return FName(*(Family==TEXT("major_protection")?Id:Family));}
};
USTRUCT()
struct FLDAbilityGate
{
    GENERATED_BODY()
    UPROPERTY() ELDSkill Skill=ELDSkill::None;
    UPROPERTY() uint8 Circle=1;
    UPROPERTY() FString School;
    UPROPERTY() TArray<int32> Alternatives;
    UPROPERTY() TArray<FString> Potency;
};
UCLASS()
class LOCKDOWN_API ULDEquipmentCatalog:public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Checksum;
    UPROPERTY() TArray<FLDEquipmentDefinition> Items;
    UPROPERTY() TArray<FLDAbilityGate> Gates;
    UPROPERTY() uint8 RulesVersion=7;
    bool Import(const FString& Text,FString& Why);
    bool Validate(FString& Why) const;
    static ULDEquipmentCatalog* Load(uint8 Rules=7);
    const FLDEquipmentDefinition* Find(ELDItemKind K) const {return Items.FindByPredicate([&](const auto& D){return D.Kind==K;});}
    const FLDAbilityGate* Gate(ELDSkill S) const {return Gates.FindByPredicate([&](const auto& D){return D.Skill==S;});}
};
namespace LDGear
{
    LOCKDOWN_API bool NaturalGate(const FLDCharacterBuild& B,ELDSkill S);
    LOCKDOWN_API FLDGearStats Derive(const FLDInventory& I);
    LOCKDOWN_API TArray<int32> Allocate(const TArray<int32>& Requested,TArray<int32>& Granted);
    LOCKDOWN_API bool Propose(const FLDInventory& I,const FLDCharacterBuild& B,uint8 Command,FGuid Item,uint8 Slot,FLDInventory& Out,FString& Why);
    LOCKDOWN_API bool OnRoad(const UWorld* World,const FVector& P);
    LOCKDOWN_API FString Preview(const FLDInventory& I,const FLDCharacterBuild& B);
    LOCKDOWN_API int32 Magnitude(const FLDCharacterBuild& B,ELDSkill S,const FString& Key,const TArray<int32>& Values);
}
