#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDTradeTypes.generated.h"
UENUM() enum class ELDTradeStage:uint8 { None,Invited,Draft,Review,Committed,Cancelled,Expired };
namespace LDTrade { enum ECommand:uint8 { Invite=1,Accept,Decline,Add,Remove,Gold,Ready,Confirm,Cancel }; }
USTRUCT()
struct FLDTradeItem
{
    GENERATED_BODY()
    UPROPERTY() FGuid Handle;
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Book;
    UPROPERTY() ELDSkill Skill=ELDSkill::None;
    UPROPERTY() uint8 Quality=0;
};
// Own tray handles resolve only own inventory; partner handles are random per session.
// Never serialize the partner's permanent inventory, body, wallet or contender IDs.
USTRUCT()
struct FLDTradeView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Session;
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Serial=0;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() FString Digest;
    UPROPERTY() ELDTradeStage Stage=ELDTradeStage::None;
    UPROPERTY() bool bReceiver=false;
    UPROPERTY() bool bOwnReady=false;
    UPROPERTY() bool bOtherReady=false;
    UPROPERTY() bool bOwnConfirmed=false;
    UPROPERTY() bool bOtherConfirmed=false;
    UPROPERTY() bool bOwnValid=false;
    UPROPERTY() bool bOtherValid=false;
    UPROPERTY() int64 OwnGold=0;
    UPROPERTY() int64 OtherGold=0;
    UPROPERTY() TArray<FLDTradeItem> OwnItems;
    UPROPERTY() TArray<FLDTradeItem> OtherItems;
    UPROPERTY() float ExpiresAt=0;
    UPROPERTY() FString Message;
    bool Active() const{return Stage>=ELDTradeStage::Invited&&Stage<=ELDTradeStage::Review;}
};
