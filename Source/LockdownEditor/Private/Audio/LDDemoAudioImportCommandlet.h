#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LDDemoAudioImportCommandlet.generated.h"

// -run=LDDemoAudioImport [-Validate]: imports or validates the Demo audio assets (see LDDemoAudioImport.h).
UCLASS()
class ULDDemoAudioImportCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    ULDDemoAudioImportCommandlet();
    virtual int32 Main(const FString& Params) override;
};
