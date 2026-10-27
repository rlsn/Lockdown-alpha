#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LDB90BootstrapCommandlet.generated.h"

UCLASS()
class ULDB90BootstrapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULDB90BootstrapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
