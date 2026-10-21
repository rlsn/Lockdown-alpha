#pragma once
#include "CoreMinimal.h"
#include "Components/Button.h"
#include "LDWidgets.generated.h"
class UWidgetTree;
class UTextBlock;
UCLASS()
class LOCKDOWN_API ULDCommandButton : public UButton
{
    GENERATED_BODY()
public:
    TFunction<void()> Callback;
    UFUNCTION() void Execute(){if(Callback){Callback();}}
};
namespace LDWidgets
{
    LOCKDOWN_API UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size=16);
    LOCKDOWN_API ULDCommandButton* Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback);
}
