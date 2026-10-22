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
    TFunction<void()> PressCallback,ReleaseCallback;
    UFUNCTION() void Execute(){if(Callback){Callback();}}
    UFUNCTION() void Press(){if(PressCallback){PressCallback();}}
    UFUNCTION() void Release(){if(ReleaseCallback){ReleaseCallback();}}
};
UCLASS()
class LOCKDOWN_API ULDWorldButton : public ULDCommandButton
{
    GENERATED_BODY()
public:
    ULDWorldButton(){InitIsFocusable(false);}
};
namespace LDWidgets
{
    LOCKDOWN_API UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size=16);
    LOCKDOWN_API ULDCommandButton* Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World=false);
}
