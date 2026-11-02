#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LDScaffoldBootstrapCommandlet.generated.h"

UCLASS()
class ULDScaffoldBootstrapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULDScaffoldBootstrapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
