#pragma once

#include "CoreMinimal.h"
#include "Engine/GameEngine.h"
#include "LDGameEngine.generated.h"

UCLASS()
class LOCKDOWN_API ULDGameEngine : public UGameEngine
{
    GENERATED_BODY()
public:
    virtual EBrowseReturnVal::Type Browse(FWorldContext& Context, FURL URL, FString& Error) override;
};
