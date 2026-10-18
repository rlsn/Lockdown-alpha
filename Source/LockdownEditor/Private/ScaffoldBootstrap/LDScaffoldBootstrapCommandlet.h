#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LDScaffoldBootstrapCommandlet.generated.h"

// B31 foundation generator. Future stage assets are added only when implemented.
UCLASS()
class ULDScaffoldBootstrapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULDScaffoldBootstrapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
