#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LDDemoArtImportCommandlet.generated.h"

// -run=LDDemoArtImport imports the Demo art; with -Validate it only checks the saved assets against their sources.
UCLASS()
class ULDDemoArtImportCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULDDemoArtImportCommandlet();
    virtual int32 Main(const FString& Params) override;
};
