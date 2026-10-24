#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDAcquisitionCatalog.generated.h"
USTRUCT()
struct FLDAcquisitionItem
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FString Name;
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Fiber;
    UPROPERTY() int32 Price=-1;
    UPROPERTY() int32 Weight=1;
    UPROPERTY() uint8 Tier=1;
    UPROPERTY() bool QualityEligible=false;
};
USTRUCT()
struct FLDAcquisitionRecipe
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() ELDItemKind Output=ELDItemKind::Fiber;
    UPROPERTY() TArray<ELDItemKind> Inputs;
    UPROPERTY() int32 Fee=0;
    UPROPERTY() uint8 Station=0; // 0 town, 1 East advanced, 2 HeartForge
    UPROPERTY() int32 HoldMs=4000;
};
UCLASS()
class LOCKDOWN_API ULDAcquisitionCatalog:public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Checksum;
    UPROPERTY() TArray<FLDAcquisitionItem> Items;
    UPROPERTY() TArray<FLDAcquisitionRecipe> Recipes;
    bool Import(const FString& Text,FString& Why);
    bool Validate(FString& Why) const;
    static ULDAcquisitionCatalog* Load();
    static ELDItemKind Kind(const FString& Id);
    const FLDAcquisitionItem* Find(ELDItemKind K) const{return Items.FindByPredicate([&](const auto& I){return I.Kind==K;});}
    static int32 QualityValue(int32 Base,uint8 Quality){return Base*(Quality==2?114:Quality==1?107:100)/100;}
};
