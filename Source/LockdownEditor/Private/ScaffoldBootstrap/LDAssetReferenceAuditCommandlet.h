#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LDAssetReferenceAuditCommandlet.generated.h"

UCLASS()
class ULDAssetReferenceAuditCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULDAssetReferenceAuditCommandlet();
    virtual int32 Main(const FString& Params) override;
};
