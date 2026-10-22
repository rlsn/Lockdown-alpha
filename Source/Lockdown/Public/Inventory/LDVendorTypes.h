#pragma once
#include "CoreMinimal.h"
#include "Development/LDCharacterBuild.h"
#include "LDVendorTypes.generated.h"
USTRUCT()
struct FLDVendorOffer
{
    GENERATED_BODY()
    UPROPERTY() ELDSkill Skill=ELDSkill::None;
    UPROPERTY() int32 Price=40;
};
USTRUCT()
struct FLDVendorQuote
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Token=0;
    UPROPERTY() FGuid Session;
    UPROPERTY() uint32 CatalogRevision=0;
    UPROPERTY() TArray<FLDVendorOffer> Offers;
};
